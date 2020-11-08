
#include <atomic>
#include <condition_variable>
#include <mutex>

#include <fmt/color.h>
#include <fmt/format.h>

#include <interpreter.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/cache.h>
#include <recompiler/code_buffer.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <recompiler/target/x86_64.h>
#include <r4300/state.h>

#include "core.h"
#include "debugger.h"
#include "trace.h"

#define RECOMPILER_REQUEST_QUEUE_LEN 128

using namespace R4300;

struct recompiler_request {
    uint64_t virt_address;
    uint64_t phys_address;
    code_buffer_t *emitter;

    recompiler_request() :
        virt_address(0), phys_address(0),
        emitter(NULL) {}
    recompiler_request(uint64_t virt_address, uint64_t phys_address,
                       code_buffer_t *emitter) :
        virt_address(virt_address), phys_address(phys_address),
        emitter(emitter) {}
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
        semaphore.wait(lock, [this] {
            return this->notified.load(std::memory_order_acquire);
        });
        notified.store(false, std::memory_order_release);
    }

    uint32_t tail = this->tail.load(std::memory_order_acquire);
    request = buffer[tail % capacity];
    this->tail.store(tail + 1, std::memory_order_relaxed);
}

namespace core {

unsigned long recompiler_cycles;
unsigned long recompiler_requests;
unsigned long instruction_blocks;

static struct recompiler_request_queue recompiler_request_queue(
    RECOMPILER_REQUEST_QUEUE_LEN);
static recompiler_backend_t   *recompiler_backend;
static recompiler_cache_t     *recompiler_cache;
static std::thread            *recompiler_thread;
static std::thread            *interpreter_thread;
static std::mutex              interpreter_mutex;
static std::condition_variable interpreter_semaphore;
static std::atomic_bool        interpreter_halted;
static std::atomic_bool        interpreter_stopped;
static std::string             interpreter_halted_reason;

static
void exec_recompiler_request(struct recompiler_backend *backend,
                             struct recompiler_cache *cache,
                             struct recompiler_request *request) {
#if 0
    fmt::print(fmt::fg(fmt::color::chartreuse),
        "recompiler request: {:x} {:x}\n",
        request->phys_address, request->virt_address);
#endif
    // Update request count.
    recompiler_requests++;

    // Get the pointer to the buffer containing the MIPS assembly to
    // recompiler. The length is computed so as to not cross a cache page
    // boundary: the code must fit inside a cache range.
    uint64_t page_size = get_recompiler_cache_page_size(cache);
    uint64_t page_mask = ~(page_size - 1);
    uint64_t phys_address = request->phys_address;
    uint64_t phys_address_end = (phys_address + page_size) & page_mask;
    uint64_t phys_len = phys_address_end - phys_address;
    uint8_t *phys_ptr = state.dram + phys_address;
    ir_graph_t *graph;
    code_entry_t binary;
    size_t binary_len;

    clear_recompiler_backend(backend);
    graph = ir_mips_disassemble(
        backend, request->virt_address, phys_ptr, phys_len);

#if 0
    // Preliminary sanity checks on the generated intermediate
    // representation. Really should not fail here.
    if (!ir_typecheck(backend, graph)) {
        return;
    }
#endif

    // Optimize generated graph.
    ir_optimize(backend, graph);

#if 0
    // Sanity checks on the optimized intermediate
    // representation. Really should not fail here.
    if (!ir_typecheck(backend, graph)) {
        return;
    }
#endif

    // Re-compile to x86_64.
    binary = ir_x86_64_assemble(backend, request->emitter, graph, &binary_len);
    if (binary == NULL) {
        invalidate_recompiler_cache(cache, phys_address, phys_address + 1);
        return;
    }

    // TODO synchronize with interpreter thread
    // to check if the address range was invalidated while
    // the code was being recompiled.

    // Update the recompiler cache.
    if (update_recompiler_cache(cache, phys_address, binary, binary_len) < 0) {
        invalidate_recompiler_cache(cache, phys_address, phys_address + 1);
        return;
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
                      struct recompiler_backend *backend,
                      struct recompiler_cache *cache) {
    uint64_t virt_address = state.cpu.nextPc;
    uint64_t phys_address;
    unsigned long cycles = state.cycles;
    code_buffer_t *emitter = NULL;

    // Increment block count.
    instruction_blocks++;

    // First translate the virtual address.
    // The next action must be jump, the recompilation is triggered
    // at block starts only.
    Exception exn = translateAddress(
        state.cpu.nextPc, &phys_address, false);

    // Query the recompiler cache.
#if 1
    code_entry_t binary = exn != Exception::None || phys_address == 0 ? NULL :
        query_recompiler_cache(cache, phys_address, &emitter, NULL);
#else
    code_entry_t binary = NULL;
#endif

    trace_point(virt_address, state.cycles);

    // The recompiler cache did not contain the requested entry point,
    // run the recompiler until the next branching instruction.
    if (binary == NULL) {
        // Send a recompiler request if the conditions are met.
        // The request is dropped if the queue is full.
        if (emitter != NULL) {
            queue->enqueue(recompiler_request(
                virt_address, phys_address, emitter));
        }

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

/** Invalidate the recompiler cache for the provided address range. */
void invalidate_recompiler_cache(uint64_t start_phys_address,
                                 uint64_t end_phys_address) {
    invalidate_recompiler_cache(recompiler_cache,
        start_phys_address, end_phys_address);
}

/** Return the cache usage statistics. */
void get_recompiler_cache_stats(float *cache_usage,
                                float *buffer_usage) {
    get_recompiler_cache_stats(recompiler_cache,
        cache_usage, buffer_usage);
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
            recompiler_backend, recompiler_cache, &request);
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
            exec_interpreter(&recompiler_request_queue,
                recompiler_backend, recompiler_cache);
            exec_rsp_interpreter(state.cycles - cycles);
        }

        fmt::print(fmt::fg(fmt::color::dark_orange),
            "interpreter thread halting\n");
    }
}

void start(void) {
    if (recompiler_backend == NULL) {
        recompiler_backend = ir_mips_recompiler_backend();
    }
    if (recompiler_cache == NULL) {
        recompiler_cache =
            alloc_recompiler_cache(0x4000, 0x100, 0x20000, 0x2000);
    }
    if (interpreter_thread == NULL) {
        interpreter_halted = true;
        interpreter_halted_reason = "reset";
        interpreter_thread = new std::thread(interpreter_routine);
        recompiler_thread = new std::thread(recompiler_routine);
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
