
#include <atomic>
#include <condition_variable>
#include <mutex>

#include <fmt/color.h>
#include <fmt/format.h>

#include <interpreter.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/code_buffer.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <recompiler/target/x86_64.h>
#include <r4300/state.h>

#include "core.h"
#include "debugger.h"
#include "trace.h"

#define RECOMPILER_REQUEST_QUEUE_LEN 128
#define CACHE_PAGE_SHIFT  (14)
#define CACHE_PAGE_SIZE   (UINT32_C(1) << CACHE_PAGE_SHIFT)
#define CACHE_PAGE_MASK   (CACHE_PAGE_SIZE - 1)
#define CACHE_PAGE_COUNT  (0x100)

using namespace R4300;

struct recompiler_request {
    uint64_t virt_address;
    uint64_t phys_address;

    recompiler_request() : virt_address(0), phys_address(0) {}
    recompiler_request(uint64_t virt_address, uint64_t phys_address) :
        virt_address(virt_address), phys_address(phys_address) {}
};

struct recompiler_request_queue {
    std::mutex mutex;
    std::condition_variable semaphore;
    std::atomic_bool notified;
    std::atomic_uint32_t head;
    std::atomic_uint32_t tail;
    uint32_t capacity;
    struct recompiler_request *buffer;

    /** Capacity must be a power of two. */
    recompiler_request_queue(size_t capacity)
        : notified(false), head(0), tail(0), capacity(capacity) {
        buffer = new recompiler_request[capacity];
        memset(buffer, 0xab, capacity * sizeof(*buffer));
    }
    ~recompiler_request_queue() {
        delete buffer;
    }

    bool is_full(void);
    bool is_empty(void);
    bool enqueue(struct recompiler_request const &request);
    void dequeue(struct recompiler_request &request);
    void flush(void);
};

bool recompiler_request_queue::is_full(void) {
    return (head - tail) == capacity;
}

bool recompiler_request_queue::is_empty(void) {
    return head == tail;
}

bool recompiler_request_queue::enqueue(struct recompiler_request const &request) {
    if (is_full()) {
        return false;
    }

    uint32_t head = this->head.load(std::memory_order_relaxed);
    buffer[head % capacity] = request;
    this->head.store(head + 1, std::memory_order_release);

    if (!notified.exchange(true, std::memory_order_release)) {
        semaphore.notify_one();
    }

    return true;
}

void recompiler_request_queue::dequeue(struct recompiler_request &request) {
    while (is_empty()) {
        std::unique_lock<std::mutex> lock(mutex);
        notified.store(false, std::memory_order_release);
        semaphore.wait(lock, [this] { return true; });
    }

    uint32_t tail = this->tail.load(std::memory_order_acquire);
    request = buffer[tail % capacity];
    this->tail.store(tail + 1, std::memory_order_relaxed);
}

void recompiler_request_queue::flush(void) {
    uint32_t head = this->head.load(std::memory_order_acquire);
    this->tail.store(head, std::memory_order_release);
    notified = false;
}

/**
 * The n64 dram is small enough (4MB) that a direct mapping can be used to
 * associate addresses to recompiled binary code.
 * Each cache entry has the following format:
 *
 *   31             2 1 0
 *  +----------------+-+-+
 *  |    offset      |P|V|
 *  +----------------+-+-+
 *
 * + *offset*: offset from the code buffer start to the
 *   binary code entrypoint.
 * + *P* pending bit, set when a cache query misses, and a
 *   recompiler request is emitted.
 * + *V* valid bit, set if the entry contains a valid offset.
 *
 * The status bits are updated according to the following table.
 * `upd offset P V` here means to update the cache entry.
 * The compare and swap may need to be atomic depending on the
 * circumstances.
 *
 *
 *  P V | Query               | Update               | Invalidate
 *  ----+---------------------+----------------------+-----------
 *  0 0 | send request        | NA                   | nop
 *      | upd 0 1 1           |                      |
 *      | miss                |                      |
 *  0 1 | hit ptr             | NA                   | upd 0 0
 *      |                     |                      |
 *  1 0 | miss                | upd 0 0 0            | nop
 *      |                     |                      |
 *  1 1 | miss                | upd ptr 0 1          | upd 0 1 0
 *
 * The cache is further organized in pages. The code recompiled from addresses
 * in the same page is stored in a common code buffer, all entries in the page
 * are invalidated when memory needs to be reclaimed.
 */

struct recompiler_cache {
    std::atomic_uint32_t map[0x100000];
    code_buffer_t *buffers;
};

namespace core {

unsigned long recompiler_cycles;
unsigned long recompiler_requests;
unsigned long instruction_blocks;

static struct recompiler_request_queue recompiler_request_queue(
    RECOMPILER_REQUEST_QUEUE_LEN);
static recompiler_backend_t   *recompiler_backend;
static struct recompiler_cache recompiler_cache;
static std::thread            *recompiler_thread;
static std::thread            *interpreter_thread;
static std::mutex              interpreter_mutex;
static std::condition_variable interpreter_semaphore;
static std::atomic_bool        interpreter_halted;
static std::atomic_bool        interpreter_stopped;
static std::string             interpreter_halted_reason;

/**
 * @brief Invalidate the recompiler cache entry for the provided address range.
 *  Called from the interpreter thread only.
 * @param start_phys_address    Start address of the range to invalidate.
 * @param end_phys_address      Exclusive end address of the range to invalidate.
 */
void invalidate_recompiler_cache(uint64_t start_phys_address,
                                 uint64_t end_phys_address) {
#if ENABLE_RECOMPILER
    if (start_phys_address > 0x400000) {
        return;
    }
    if (end_phys_address > 0x400000) {
        end_phys_address = 0x400000;
    }

    start_phys_address = start_phys_address >> 2;
    end_phys_address = (end_phys_address + 3) >> 2;

    for (uint32_t index = start_phys_address; index < end_phys_address; index++) {
        // Not setting P,V = 0,0 because P needs to remain up
        // to prevent concurrency issues with the recompiler thread.
        recompiler_cache.map[index].fetch_and(UINT32_C(0xfffffffe));
    }
#endif /* ENABLE_RECOMPILER */
}

/**
 * @brief Clear a full recompiler cache page.
 *  All cache entries are invalidated, the code buffer is emptied.
 *  Called from the recompiled thread only.
 * @param phys_address          Any physical address inside the page to clear.
 */
static inline
void clear_recompiler_cache_page(uint32_t phys_address) {
    uint32_t page_nr = phys_address >> CACHE_PAGE_SHIFT;
    uint32_t page_start = page_nr << CACHE_PAGE_SHIFT;

    recompiler_cache.buffers[page_nr].length = 0;
    for (uint32_t index = 0; index < CACHE_PAGE_SIZE; index+=4) {
        recompiler_cache.map[(page_start + index) >> 2] = 0x0;
    }
}

static
void exec_recompiler_request(struct recompiler_backend *backend,
                             struct recompiler_request *request) {
    // Update request count.
    recompiler_requests++;

    // Get the pointer to the buffer containing the MIPS assembly to
    // recompiler. The length is computed so as to not cross a cache page
    // boundary: the code must fit inside a cache range.
    uint64_t phys_address = request->phys_address;
    uint64_t phys_address_end =
        (phys_address + CACHE_PAGE_SIZE) & ~CACHE_PAGE_MASK;
    uint64_t phys_len = phys_address_end - phys_address;
    uint8_t *phys_ptr = state.dram + phys_address;
    uint32_t buffer_index = phys_address >> CACHE_PAGE_SHIFT;
    code_buffer_t *buffer = recompiler_cache.buffers + buffer_index;
    ir_graph_t *graph;
    code_entry_t binary;
    size_t binary_len;

    clear_recompiler_backend(backend);
    graph = ir_mips_disassemble(
        backend, request->virt_address, phys_ptr, phys_len);

    if (graph == NULL) {
        return;
    }

    // Optimize generated graph.
    ir_optimize(backend, graph);

#if 0
    // Sanity checks on the optimized intermediate representation.
    if (!ir_typecheck(backend, graph)) {
        return;
    }
#endif

    // Re-compile to x86_64.
    binary = ir_x86_64_assemble(backend, buffer, graph, &binary_len);
    if (binary == NULL) {
        clear_recompiler_cache_page(phys_address);
        recompiler_request_queue.flush();
        return;
    }

    // Update the recompiler cache.
    // Check that the entry was not invalidated while the recompiler
    // was busy completing the request. The recompiled binary is dropped
    // in this case, and the code buffer rolled back.
    // TODO only checking the first address at the moment.
    uint32_t index = phys_address >> 2;
    uint32_t offset = (unsigned char *)binary - buffer->ptr;
    uint32_t entry = (offset << 2) | 0x1;
    uint32_t entry_expected = 0x3;

    if (!recompiler_cache.map[index].compare_exchange_weak(
            entry_expected, entry, std::memory_order_release)) {
        buffer->length -= binary_len;
        recompiler_cache.map[index] = 0x0;
    }
}

/**
 * Run the RSP interpreter for the given number of cycles.
 */
static
void exec_rsp_interpreter(unsigned long cycles) {
    for (unsigned long nr = 0; nr < cycles; nr++) {
        R4300::RSP::step();
    }
}

/**
 * Handle scheduled events (counter timeout, VI interrupt).
 * Called only at block endings.
 */
static
void check_cpu_events(void) {
    if (state.cycles >= state.cpu.nextEvent) {
        state.handleEvent();
    }
}

/**
 * Run the interpreter until the n-th branching instruction.
 * The loop is also brokn by setting the halted flag.
 * The state is left with action Jump.
 * @return true exiting because of a branch instruction, false if because
 *  of a breakpoint.
 */
static
bool exec_cpu_interpreter(int nr_jumps) {
    while (!interpreter_halted.load(std::memory_order_acquire)) {
        switch (state.cpu.nextAction) {
        case State::Action::Continue:
            state.reg.pc += 4;
            state.cpu.delaySlot = false;
            interpreter::cpu::eval();
            break;

        case State::Action::Delay:
            state.reg.pc += 4;
            state.cpu.nextAction = State::Action::Jump;
            state.cpu.delaySlot = true;
            interpreter::cpu::eval();
            break;

        case State::Action::Jump:
            if (nr_jumps-- <= 0) return true;
            state.reg.pc = state.cpu.nextPc;
            state.cpu.nextAction = State::Action::Continue;
            state.cpu.delaySlot = false;
            interpreter::cpu::eval();
        }
    }
    return false;
}

static
void exec_interpreter(struct recompiler_request_queue *queue,
                      struct recompiler_backend *backend) {
    uint64_t virt_address = state.cpu.nextPc;
    uint64_t phys_address;
    unsigned long cycles = state.cycles;

    // First translate the virtual address.
    // The next action must be jump, the recompilation is triggered
    // at block starts only.
    Exception exn = translateAddress(
        state.cpu.nextPc, &phys_address, false);

    // Query the recompiler cache.
    code_entry_t binary = NULL;
    uint32_t entry;

    if (exn == Exception::None && phys_address < 0x400000) {
        uint32_t index = phys_address >> 2;
        uint32_t buffer_index = phys_address >> CACHE_PAGE_SHIFT;
        entry = recompiler_cache.map[index];
        switch (entry & 0x3) {
        case 0x0:
            recompiler_cache.map[index] = 0x3;
            queue->enqueue(recompiler_request(virt_address, phys_address));
            break;
        case 0x1:
            binary = (code_entry_t)(
                recompiler_cache.buffers[buffer_index].ptr + (entry >> 2));
            break;
        case 0x2:
        case 0x3:
            break;
        }
    }

    // The recompiler cache did not contain the requested entry point,
    // run the recompiler until the next branching instruction.
    if (binary == NULL) {
        // Run the interpreter until the next branching instruction.
        (void)exec_cpu_interpreter(1);
    }
    // The recompiler cache did contain the entry point.
    // Jump to the recompiled code, then patch the state.
    else {
        // Set default state.
        state.cpu.delaySlot = false;
        state.cpu.nextAction = State::Continue;
        state.cpu.nextPc = 0;

        // Run generated assembly.
        binary();

        // Post-binary state rectification.
        // The nextPc, nextAction need to be corrected after exiting from an
        // exception or interrupt to correctly follow up with interpreter
        // execution.
        if (state.cpu.nextAction != State::Jump) {
            state.cpu.nextAction = State::Jump;
            state.cpu.nextPc = state.reg.pc;
        }

        // Check for interrupts.
        // Counter interrupts will be caught at the end of the block.
        // The ERET instruction can also activate pending interrupts.
        checkInterrupt();

        // Update analytics.
        recompiler_cycles += state.cycles - cycles;
    }
}

/** Return the cache usage statistics. */
void get_recompiler_cache_stats(float *cache_usage,
                                float *buffer_usage) {
#if ENABLE_RECOMPILER
    size_t map_taken = 0;
    for (size_t nr = 0; nr < 0x100000; nr++) {
        map_taken += (recompiler_cache.map[nr] & 1);
    }

    size_t buffer_taken = 0;
    size_t buffer_capacity = 0;
    for (size_t nr = 0; nr < CACHE_PAGE_COUNT; nr++) {
        buffer_taken += recompiler_cache.buffers[nr].length;
        buffer_capacity += recompiler_cache.buffers[nr].capacity;
    }

    *cache_usage = (float)map_taken / 0x100000;
    *buffer_usage = (float)buffer_taken / buffer_capacity;
#else
    *cache_usage = 0;
    *buffer_usage = 0;
#endif /* ENABLE_RECOMPILER */
}

/**
 * @brief Recompiler thead routine.
 * Loops waiting for recompilation requests issued by the interpreter thread.
 * Requests are added to the cache when completed.
 */
static
void recompiler_routine(void) {
    fmt::print(fmt::fg(fmt::color::dark_orange),
        "recompiler thread starting\n");

    for (;;) {
        struct recompiler_request request;
        recompiler_request_queue.dequeue(request);
        exec_recompiler_request(
            recompiler_backend, &request);
    }
}

/**
 * @brief Interpreter thead routine.
 * Loops interpreting machine instructions.
 * The interpreter will run in three main modes:
 *  1. interpreter execution, until coming to a block starting point,
 *     and then when the recompilation hasn't been issued or completed.
 *  2. recompiled code execution, when available. Always starts at the
 *     target of a branching instruction, and stops at the next branch
 *     or synchronous exception.
 *  3. step-by-step execution from the GUI. The interpreter is stopped
 *     during this time, and instructions are executed from the main thread.
 */
static
void interpreter_routine(void) {
    fmt::print(fmt::fg(fmt::color::dark_orange),
        "interpreter thread starting\n");

    for (;;) {
        std::unique_lock<std::mutex> lock(interpreter_mutex);
        interpreter_semaphore.wait(lock, [] {
            return !interpreter_halted.load(std::memory_order_acquire) ||
                interpreter_stopped.load(std::memory_order_acquire); });

        if (interpreter_stopped.load(std::memory_order_acquire)) {
            fmt::print(fmt::fg(fmt::color::dark_orange),
                "interpreter thread exiting\n");
            return;
        }

        fmt::print(fmt::fg(fmt::color::dark_orange),
            "interpreter thread resuming\n");

        lock.unlock();

        unsigned long cycles = state.cycles;
        exec_cpu_interpreter(0);
        exec_rsp_interpreter(state.cycles - cycles);

        while (!interpreter_halted.load(std::memory_order_relaxed)) {
            // Ensure that the interpreter is at a jump
            // at the start of the loop contents.
            cycles = state.cycles;
            check_cpu_events();
            instruction_blocks++;
            // trace_point(state.cpu.nextPc, state.cycles);
#if ENABLE_RECOMPILER
            exec_interpreter(&recompiler_request_queue, recompiler_backend);
#else
            exec_cpu_interpreter(1);
#endif /* ENABLE_RECOMPILER */
            exec_rsp_interpreter(state.cycles - cycles);
        }

        fmt::print(fmt::fg(fmt::color::dark_orange),
            "interpreter thread halting\n");
    }
}

void start(void) {
#if ENABLE_RECOMPILER
    if (recompiler_backend == NULL) {
        recompiler_backend = ir_mips_recompiler_backend();
    }
    if (recompiler_cache.buffers == NULL) {
        recompiler_cache.buffers =
            alloc_code_buffer_array(CACHE_PAGE_COUNT, 0x20000);
    }
    if (recompiler_thread == NULL) {
        recompiler_thread = new std::thread(recompiler_routine);
    }
#endif /* ENABLE_RECOMPILER */
    if (interpreter_thread == NULL) {
        interpreter_halted = true;
        interpreter_halted_reason = "reset";
        interpreter_thread = new std::thread(interpreter_routine);
    }
}

void stop(void) {
    if (interpreter_thread != NULL) {
        interpreter_halted.store(true, std::memory_order_release);
        interpreter_stopped.store(true, std::memory_order_release);
        interpreter_semaphore.notify_one();
        interpreter_thread->join();
        delete interpreter_thread;
        interpreter_thread = NULL;
    }
}

void reset(void) {
    R4300::state.reset();
    recompiler_cycles = 0;
}

void halt(std::string reason) {
    if (!interpreter_halted) {
        interpreter_halted_reason = reason;
        interpreter_halted.store(true, std::memory_order_release);
    }
}

bool halted(void) {
    return interpreter_halted;
}

std::string halted_reason(void) {
    return interpreter_halted_reason;
}

void step(void) {
    if (interpreter_thread != NULL &&
        interpreter_halted.load(std::memory_order_acquire)) {
        R4300::step();
        RSP::step();
    }
}

void resume(void) {
    if (interpreter_thread != NULL &&
        interpreter_halted.load(std::memory_order_acquire)) {
        interpreter_halted.store(false, std::memory_order_release);
        interpreter_semaphore.notify_one();
    }
}

}; /* namespace core */
