
#include <cstdlib>
#include <fstream>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/color.h>
#include <fmt/format.h>

#include <assembly/disassembler.h>
#include <assembly/registers.h>
#include <interpreter/interpreter.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/code_buffer.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <recompiler/target/x86_64.h>
#include <r4300/state.h>
#include <core.h>
#include <debugger.h>
#include <trace.h>

using namespace Memory;
using namespace R4300;
using namespace n64;

namespace Test {

/** Implement a bus replaying memory accesses previously recorded with the
 * LoggingBus. An exception is raised if an attempt is made at :
 *  - accessing an unrecorded memory location
 *  - making out-of-order memory accesses
 */
class ReplayBus: public Memory::Bus
{
private:
    bool _bad;

public:
    ReplayBus(unsigned bits) : Bus(bits) {}
    virtual ~ReplayBus() {}

    std::vector<Memory::BusTransaction> log;
    unsigned index;

    void reset(Memory::BusTransaction *log, size_t log_len) {
        this->log.clear();
        this->index = 0;
        this->_bad = false;
        for (size_t i = 0; i < log_len; i++) {
            this->log.push_back(log[i]);
        }
    }

    bool bad() {
        return _bad;
    }

    virtual bool load(unsigned bytes, u64 addr, u64 *val) {
        if (index >= log.size()) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access: outside recorded trace\n");
            _bad = true;
            return false;
        }
        if (!log[index].load ||
            log[index].bytes != bytes ||
            log[index].address != addr) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:  load_u{}(0x{:x})\n",
                bytes * 8, addr);
            if (log[index].load) {
                fmt::print(fmt::emphasis::italic,
                    "    expected: load_u{}(0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    expected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            }
            _bad = true;
            return false;
        }
        *val = log[index++].value;
        return true;
    }
    virtual bool store(unsigned bytes, u64 addr, u64 val) {
        if (index >= log.size()) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access: outside recorded trace\n");
            _bad = true;
            return false;
        }
        if (log[index].load ||
            log[index].bytes != bytes ||
            log[index].address != addr ||
            log[index].value != val) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:  store_u{}(0x{:x}, 0x{:x})\n",
                bytes * 8, addr, val);
            if (log[index].load) {
                fmt::print(fmt::emphasis::italic,
                    "    expected: load_u{}(0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    expected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            }
            _bad = true;
            return false;
        }
        index++;
        return true;
    }
};

}; /* Test */

static inline void print_register_diff(uintmax_t left, uintmax_t right,
                                       const std::string &name) {
    if (left != right) {
        fmt::print(fmt::emphasis::italic,
            "    {:<8}: {:<16x} - {:x}\n", name, left, right);
    }
}

bool match_cpureg(const cpureg &left, const cpureg &right) {
    for (unsigned nr = 0; nr < 32; nr++) {
        if (left.gpr[nr] != right.gpr[nr])
            return false;
    }
    return left.multLo == right.multLo &&
           left.multHi == right.multHi;
}

void print_cpureg_diff(const cpureg &left, const cpureg &right) {
    for (unsigned nr = 0; nr < 32; nr++) {
        print_register_diff(left.gpr[nr], right.gpr[nr],
            n64::assembly::cpu::RegisterNames[nr]);
    }
    print_register_diff(left.multLo, right.multLo, "multlo");
    print_register_diff(left.multHi, right.multHi, "multhi");
}

bool match_cp0reg(const cp0reg &left, const cp0reg &right) {
    // Do not compare cause: as interrupts are not generated from the
    // memory trace, the cause cannot be properly updated.
    return left.index == right.index &&
           left.random == right.random &&
           left.entrylo0 == right.entrylo0 &&
           left.entrylo1 == right.entrylo1 &&
           left.context == right.context &&
           left.pagemask == right.pagemask &&
           left.wired == right.wired &&
           left.badvaddr == right.badvaddr &&
           left.count == right.count &&
           left.entryhi == right.entryhi &&
           left.compare == right.compare &&
           left.sr == right.sr &&
           left.epc == right.epc &&
           left.prid == right.prid &&
           left.config == right.config &&
           left.lladdr == right.lladdr &&
           left.watchlo == right.watchlo &&
           left.watchhi == right.watchhi &&
           left.xcontext == right.xcontext &&
           left.perr == right.perr &&
           left.cacheerr == right.cacheerr &&
           left.taglo == right.taglo &&
           left.taghi == right.taghi &&
           left.errorepc == right.errorepc;
}

void print_cp0reg_diff(const cp0reg &left, const cp0reg &right) {
    print_register_diff(left.index, right.index, "index");
    print_register_diff(left.random, right.random, "random");
    print_register_diff(left.entrylo0, right.entrylo0, "entrylo0");
    print_register_diff(left.entrylo1, right.entrylo1, "entrylo1");
    print_register_diff(left.context, right.context, "context");
    print_register_diff(left.pagemask, right.pagemask, "pagemask");
    print_register_diff(left.wired, right.wired, "wired");
    print_register_diff(left.badvaddr, right.badvaddr, "badvaddr");
    print_register_diff(left.count, right.count, "count");
    print_register_diff(left.entryhi, right.entryhi, "entryhi");
    print_register_diff(left.compare, right.compare, "compare");
    print_register_diff(left.sr, right.sr, "sr");
    print_register_diff(left.cause, right.cause, "cause");
    print_register_diff(left.epc, right.epc, "epc");
    print_register_diff(left.prid, right.prid, "prid");
    print_register_diff(left.config, right.config, "config");
    print_register_diff(left.lladdr, right.lladdr, "lladdr");
    print_register_diff(left.watchlo, right.watchlo, "watchlo");
    print_register_diff(left.watchhi, right.watchhi, "watchhi");
    print_register_diff(left.xcontext, right.xcontext, "xcontext");
    print_register_diff(left.perr, right.perr, "perr");
    print_register_diff(left.cacheerr, right.cacheerr, "cacheerr");
    print_register_diff(left.taglo, right.taglo, "taglo");
    print_register_diff(left.taghi, right.taghi, "taghi");
    print_register_diff(left.errorepc, right.errorepc, "errorepc");
}

bool match_cp1reg(const cp1reg &left, const cp1reg &right) {
    for (unsigned nr = 0; nr < 32; nr++) {
        if (left.fpr[nr] != right.fpr[nr])
            return false;
    }
    return left.fcr0 == right.fcr0 &&
           left.fcr31 == right.fcr31;
}

void print_cp1reg_diff(const cp1reg &left, const cp1reg &right) {
    for (unsigned nr = 0; nr < 32; nr++) {
        print_register_diff(left.fpr[nr], right.fpr[nr],
            fmt::format("fpr{}", nr));
    }
    print_register_diff(left.fcr0, right.fcr0, "fcr0");
    print_register_diff(left.fcr31, right.fcr31, "fcr31");
}

/** Trace synchronization structure. */
static struct trace_sync {
    sem_t         request;
    sem_t         response;
    size_t        memory_log_len;
    size_t        binary_len;
    int           status;
} *trace_sync;

/** Records the start and end register values for a recorded cpu trace. */
static struct trace_registers {
    u64           start_address, end_address;
    unsigned      start_cycles,  end_cycles;
    struct cpureg start_cpureg,  end_cpureg;
    struct cp0reg start_cp0reg,  end_cp0reg;
    struct cp1reg start_cp1reg,  end_cp1reg;
} *trace_registers;

/** Records the memory access log of a recorded cpu trace. */
static Memory::BusTransaction  *trace_memory_log;
static size_t   trace_memory_log_maxlen = 0x1000;

/** Records the executed binary code of a recorded cpu trace. */
static uint8_t *trace_binary;
static size_t   trace_binary_maxlen = 0x1000;

void print_trace_info(void) {
    fmt::print("------------------ input ------------------\n");
    fmt::print("start_address:  {:016x}\n", trace_registers->start_address);
    fmt::print("end_address:    {:016x}\n", trace_registers->end_address);
    fmt::print("start_cycles:   {}\n", trace_registers->start_cycles);
    fmt::print("end_cycles:     {}\n", trace_registers->end_cycles);
    fmt::print("binary_len:     {}\n", trace_sync->binary_len);
    fmt::print("memory_log_len: {}\n", trace_sync->memory_log_len);
}

void print_raw_disassembly(void) {
    fmt::print("------------- raw disassembly -------------\n");
    for (unsigned i = 0; i < trace_sync->binary_len; i+=4) {
        u32 instr =
           ((uint32_t)trace_binary[i+0] << 24) |
           ((uint32_t)trace_binary[i+1] << 16) |
           ((uint32_t)trace_binary[i+2] << 8)  |
           ((uint32_t)trace_binary[i+3] << 0);
        fmt::print("    {}\n", assembly::cpu::disassemble(
            trace_registers->start_address + i, instr));
    }
}

void print_ir_disassembly(ir_graph_t *graph) {
    if (graph == NULL) {
        return;
    }

    fmt::print("------------- ir disassembly --------------\n");
    ir_instr_t const *instr;
    ir_block_t const *block;
    char line[128];

    for (unsigned label = 0; label < graph->nr_blocks; label++) {
        fmt::print(".L{}:\n", label);
        block = &graph->blocks[label];
        for (instr = block->entry; instr != NULL; instr = instr->next) {
            ir_print_instr(line, sizeof(line), instr);
            fmt::print("    {}\n", line);
        }
    }
}

void print_x86_64_assembly(code_entry_t binary, size_t binary_len) {
    fmt::print("--------------- ir assembly ---------------\n");
    unsigned char *ptr = (unsigned char *)binary;
    fprintf(stdout, "   ");
    for (unsigned i = 0; i < binary_len; i++) {
        if (i && (i % 16) == 0) fprintf(stdout, "\n   ");
        fprintf(stdout, " %02x", ptr[i]);
    }
    fprintf(stdout, "\n");
}

void print_backend_error_log(recompiler_backend_t *backend) {
    char message[RECOMPILER_ERROR_MAX_LEN] = "";
    char const *module;

    while (next_recompiler_error(backend, &module, message)) {
        fmt::print(fmt::emphasis::italic, "{} failure:\n{}\n",
            module, message);
    }
}

void print_memory_log(void) {
    fmt::print("--------------- memory log ----------------\n");

    for (unsigned nr = 0; nr < trace_sync->memory_log_len; nr++) {
        if (trace_memory_log[nr].load) {
            fmt::print("    load_u{}(0x{:x}) -> 0x{:x}\n",
                trace_memory_log[nr].bytes * 8,
                trace_memory_log[nr].address,
                trace_memory_log[nr].value);
        } else {
            fmt::print("    store_u{}(0x{:x}, 0x{:x})\n",
                trace_memory_log[nr].bytes * 8,
                trace_memory_log[nr].address,
                trace_memory_log[nr].value);
        }
    }
}

namespace interpreter::cpu {

void start_capture(void) {
    fmt::print("start capture\n");
    trace_registers->start_address = R4300::state.reg.pc;
    trace_registers->start_cycles = R4300::state.cycles;
    trace_registers->start_cpureg = R4300::state.reg;
    trace_registers->start_cp0reg = R4300::state.cp0reg;
    trace_registers->start_cp1reg = R4300::state.cp1reg;

    DebugBus *bus = dynamic_cast<DebugBus *>(state.bus);
    bus->start_trace();
}

void stop_capture(u64 address) {
    trace_registers->end_address = address;
    trace_registers->end_cycles = R4300::state.cycles;
    trace_registers->end_cpureg = R4300::state.reg;
    trace_registers->end_cp0reg = R4300::state.cp0reg;
    trace_registers->end_cp1reg = R4300::state.cp1reg;

    trace_sync->memory_log_len = 0;
    trace_sync->binary_len = 0;

    DebugBus *bus = dynamic_cast<DebugBus *>(state.bus);
    address = trace_registers->start_address;

    fmt::print("stop capture\n");

    uint64_t phys_address;
    if (translate_address(address, &phys_address, false, NULL, NULL) != R4300::Exception::None) {
        fmt::print(fmt::fg(fmt::color::dark_orange),
            "cannot translate start address 0x{:x}\n", address);
        goto clear_capture;
    }

    for (Memory::BusTransaction entry: bus->trace) {
        if (entry.load && entry.bytes == 4 && entry.address == phys_address) {
            if ((trace_sync->binary_len + 4) > trace_binary_maxlen) {
                // Insufficient binary storage, cannot store the full binary
                // code for the recompiler.
                fmt::print(fmt::fg(fmt::color::dark_orange),
                    "out of binary memory\n");
                goto clear_capture;
            }
            trace_binary[trace_sync->binary_len + 0] = (uint8_t)(entry.value >> 24);
            trace_binary[trace_sync->binary_len + 1] = (uint8_t)(entry.value >> 16);
            trace_binary[trace_sync->binary_len + 2] = (uint8_t)(entry.value >> 8);
            trace_binary[trace_sync->binary_len + 3] = (uint8_t)(entry.value >> 0);
            trace_sync->binary_len += 4;
            phys_address += 4;
            address += 4;
        } else {
            if (trace_sync->memory_log_len >= trace_memory_log_maxlen) {
                // Insufficient memory log storage, cannot store the full
                // memory access log for the recompiler.
                fmt::print(fmt::fg(fmt::color::dark_orange),
                    "out of memory log memory\n");
                goto clear_capture;
            }
            trace_memory_log[trace_sync->memory_log_len] = entry;
            trace_sync->memory_log_len++;
        }
    }

    if (address != (R4300::state.reg.pc + 4)) {
        // Incomplete instruction fetch trace, discard the whole capture.
        // This will happen for all synchronous interruptions, as
        // the instruction fetch has yet to be performed when the interrupt
        // is taken.
        // fmt::print(fmt::fg(fmt::color::dark_orange),
        //     "incomplete instruction fetch trace\n");
        goto clear_capture;
    }
    if (trace_registers->end_address == (R4300::state.reg.pc + 8)) {
        // Missing instruction fetch for the suppressed delay instruction
        // of a branch likely, do a manual fetch to retrieve the missing
        // instruction at address (pc + 4).
        if ((trace_sync->binary_len + 4) > trace_binary_maxlen) {
            // Insufficient binary storage, cannot store the full binary
            // code for the recompiler.
            fmt::print(fmt::fg(fmt::color::dark_orange),
                "out of binary memory\n");
            goto clear_capture;
        }

        u32 instr;
        bus->load_u32(phys_address, &instr);
        trace_binary[trace_sync->binary_len + 0] = (uint8_t)(instr >> 24);
        trace_binary[trace_sync->binary_len + 1] = (uint8_t)(instr >> 16);
        trace_binary[trace_sync->binary_len + 2] = (uint8_t)(instr >> 8);
        trace_binary[trace_sync->binary_len + 3] = (uint8_t)(instr >> 0);
        trace_sync->binary_len += 4;
    }

    // Notify the recompiler process and wait for the test result.
    if (sem_post(&trace_sync->request) != 0) {
        fmt::print(fmt::fg(fmt::color::dark_orange),
            "failed to notify recompiler process\n");
        goto clear_capture;
    }
    if (sem_wait(&trace_sync->response) != 0) {
        fmt::print(fmt::fg(fmt::color::dark_orange),
            "failed to wait for recompiler process\n");
        goto clear_capture;
    }

    if (trace_sync->status != 0)
        core::halt("Recompiler test fail");

clear_capture:
    bus->end_trace();
    bus->clear_trace();
}

}; /* namespace interpreter:cpu */

void run_interpreter(void) {
    void startGui();
    startGui();
}

int run_recompiler_test(recompiler_backend_t *backend) {

    Test::ReplayBus *bus = dynamic_cast<Test::ReplayBus*>(R4300::state.bus);
    uint64_t address = trace_registers->start_address;
    uint64_t phys_address;
    ir_graph_t *graph = NULL;
    code_buffer_t *emitter = NULL;
    code_entry_t binary = NULL;
    size_t binary_len = 0;
    bool run = false;

    // Translate the program counter, i.e. the block start address.
    // The block is skipped if the address is not from the physical ram.
    if (translate_address(address, &phys_address, false, NULL, NULL) != Exception::None ||
        phys_address >= 0x400000) {
        return 0;
    }

    // Run the recompiler on the trace recorded.
    // Memory synchronization with interpreter process is handled by
    // the caller, the trace records can be safely accessed.

    if (emitter == NULL) {
        fmt::print(fmt::fg(fmt::color::tomato), "emitter is not provided\n");
        goto failure;
    }

    clear_recompiler_backend(backend);
    graph = ir_mips_disassemble(
        backend, trace_registers->start_address,
        trace_binary, trace_sync->binary_len);

    // Preliminary sanity checks on the generated intermediate
    // representation. Really should not fail here.
    if (!ir_typecheck(backend, graph)) {
        print_backend_error_log(backend);
        goto failure;
    }

    // Optimize generated graph.
    ir_optimize(backend, graph);

    // Sanity checks on the optimized intermediate
    // representation. Really should not fail here.
    if (!ir_typecheck(backend, graph)) {
        print_backend_error_log(backend);
        goto failure;
    }

    // Re-compile to x86_64.
    binary = ir_x86_64_assemble(backend, emitter, graph, &binary_len);
    if (binary == NULL) {
        return 0;
    }

    // Load the trace registers and memory log into the state context,
    // then run the generated intermediate representation.
    // Can fail here due to unrecorded memory accesses.
    R4300::state.reg = trace_registers->start_cpureg;
    R4300::state.cp0reg = trace_registers->start_cp0reg;
    R4300::state.cp1reg = trace_registers->start_cp1reg;
    R4300::state.cycles = trace_registers->start_cycles;
    R4300::state.cp1reg.setFprAliases(R4300::state.cp0reg.FR());

    bus->reset(trace_memory_log, trace_sync->memory_log_len);
    run = true;

#if 0
    if (!ir_run(backend, graph, &err_instr, line, sizeof(line))) {
        fmt::print(fmt::emphasis::italic, "run failure:\n");
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        fmt::print(fmt::emphasis::italic, "in instruction:\n");
        ir_print_instr(line, sizeof(line), err_instr);
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        goto failure;
    }
#endif

    R4300::state.cpu.delaySlot = false;
    R4300::state.cpu.nextAction = R4300::State::Continue;

    // Run generated assembly.
    binary();

    // Post-binary state rectification.
    // The nextPc, nextAction need to be corrected after exiting from an
    // exception or interrupt to correctly follow up with interpreter execution.
    if (R4300::state.cpu.nextAction != R4300::State::Jump) {
        R4300::state.cpu.nextAction = R4300::State::Jump;
        R4300::state.cpu.nextPc = R4300::state.reg.pc;
    }

    // Check for interrupts.
    // Counter interrupts will be caught at the end of the block.
    // The ERET instruction can also activate pending interrupts.
    R4300::checkInterrupt();

    // The next action is always Jump at this point, execute it.
    R4300::state.cpu.nextAction = R4300::State::Continue;
    R4300::state.reg.pc = R4300::state.cpu.nextPc;
    R4300::state.cpu.delaySlot = false;

    // Finally, compare the registers values on exiting the recompiler,
    // with the recorded values. Make sure the whole memory trace was executed.
    if (R4300::state.reg.pc != trace_registers->end_address ||
        R4300::state.cycles != trace_registers->end_cycles ||
        !match_cpureg(trace_registers->end_cpureg, R4300::state.reg) ||
        !match_cp0reg(trace_registers->end_cp0reg, R4300::state.cp0reg) ||
        !match_cp1reg(trace_registers->end_cp1reg, R4300::state.cp1reg) ||
        bus->bad()) {
        fmt::print(fmt::fg(fmt::color::tomato), "run invalid:\n");
        goto failure;
    }

    // Success: the recompiler was validated on this trace.
    return 0;

failure:
    // Print debug information.
    if (run) {
        fmt::print(fmt::emphasis::italic,
            "register differences (expected, computed):\n");
        print_cpureg_diff(trace_registers->end_cpureg, R4300::state.reg);
        print_cp0reg_diff(trace_registers->end_cp0reg, R4300::state.cp0reg);
        print_cp1reg_diff(trace_registers->end_cp1reg, R4300::state.cp1reg);
        if (R4300::state.cycles != trace_registers->end_cycles) {
            fmt::print(fmt::emphasis::italic,
                "    cycles  : {:<16} - {:}\n",
                trace_registers->end_cycles, R4300::state.cycles);
        }
        if (R4300::state.reg.pc != trace_registers->end_address) {
            fmt::print(fmt::emphasis::italic,
                "    pc      : {:<16x} - {:16x}\n",
                trace_registers->end_address, R4300::state.reg.pc);
            fmt::print(fmt::emphasis::italic,
                "    next_pc : {:<16x}\n",
                R4300::state.cpu.nextPc);
        }
        fmt::print(fmt::emphasis::italic,
            "    sr      : {:<8x}\n",
            trace_registers->start_cp0reg.sr);
        fmt::print(fmt::emphasis::italic,
            "    ra      : {:<16x}\n",
            trace_registers->start_cpureg.gpr[31]);
        if (bus->bad()) {
            fmt::print(fmt::emphasis::italic,
                "memory trace invalid, index={}\n", bus->index);
        }
    }

    print_trace_info();
    print_memory_log();
    print_raw_disassembly();
    print_ir_disassembly(graph);
    print_x86_64_assembly(binary, binary_len);

    fmt::print(
        "----------------------------------------"
        "----------------------------------------\n");

    // Save the trace to non-regression tests.
    // TODO.
    return -1;
}

void run_recompiler(void) {
    // First things first, replace the state memory bus by a replay bus.
    // The recompiler will be replaying the traces sent over by the interpreter.
    R4300::state.swapMemoryBus(new Test::ReplayBus(32));

    // Allocate a recompiler backend.
    recompiler_backend_t *backend = ir_mips_recompiler_backend();
    if (backend == NULL) {
        fmt::print(fmt::fg(fmt::color::tomato),
            "failed to allocate recompiler backend\n");
        return;
    }

    while (1) {
        // Wait for a trace from the interpreter process,
        // handle it and notify the test result.
        if (sem_wait(&trace_sync->request) != 0) break;
        trace_sync->status = run_recompiler_test(backend);
        if (sem_post(&trace_sync->response) != 0) break;
    }

    fmt::print(fmt::fg(fmt::color::tomato), "recompiler process exiting\n");
    free(backend);
}

void *alloc_shared_memory(size_t size) {
    // Set read and write access permissions.
    int protection = PROT_READ | PROT_WRITE;

    // The buffer will be shared (meaning other processes can access it), but
    // anonymous (meaning third-party processes cannot obtain an address for it),
    // so only this process and its children will be able to use it:
    int visibility = MAP_SHARED | MAP_ANONYMOUS;

    // Allocate the shared memory.
    return mmap(NULL, size, protection, visibility, -1, 0);
}

int start_recompiler_process(void) {
    // Allocate shared memory to communicate between the interpreter and
    // the recompiler processes.
    size_t shared_mem_len =
        sizeof(*trace_sync) +
        sizeof(*trace_registers) +
        trace_binary_maxlen +
        sizeof(Memory::BusTransaction) * trace_memory_log_maxlen;

    unsigned char *shared_mem = reinterpret_cast<unsigned char *>(
        alloc_shared_memory(shared_mem_len));
    if (shared_mem == NULL) {
        fmt::print("failed to allocate shared memory\n");
        return -1;
    }

    // Allocate the shared trace data structures from the allocated
    // shared memory buffer.
    trace_sync = reinterpret_cast<struct trace_sync *>(shared_mem);
    shared_mem += sizeof(*trace_sync);
    trace_registers = reinterpret_cast<struct trace_registers *>(shared_mem);
    shared_mem += sizeof(*trace_registers);
    trace_binary = shared_mem;
    shared_mem += trace_binary_maxlen;
    trace_memory_log = reinterpret_cast<Memory::BusTransaction *>(shared_mem);

    // Initialize the semaphores used to synchronize the interpreter and
    // recompiler processes.
    trace_sync->status = 0;
    if (sem_init(&trace_sync->request,  1, 0) != 0 ||
        sem_init(&trace_sync->response, 1, 0) != 0) {
        fmt::print("failed to initialize the synchronization semaphores\n");
        sem_destroy(&trace_sync->request);
        (void)munmap(shared_mem, shared_mem_len);
        return -1;
    }

    // Fork to create the recompiler process.
    pid_t pid = fork();

    // Fork failed, abort.
    if (pid == -1) {
        fmt::print("failed to create the recompiler process\n");
        sem_destroy(&trace_sync->request);
        sem_destroy(&trace_sync->response);
        (void)munmap(shared_mem, shared_mem_len);
        return -1;
    }

    // In child, start the recompiler loop.
    if (pid == 0) {
        run_recompiler();
    }
    // In parent, start the interpreter loop.
    else {
        run_interpreter();
    }

    // End of both the recompiler and interpreter loops.
    sem_destroy(&trace_sync->request);
    sem_destroy(&trace_sync->response);
    (void)munmap(shared_mem, shared_mem_len);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fmt::print("Missing path to .z64/.N64 ROM image\n");
        return -1;
    }

    std::string rom_file = argv[1];
    std::ifstream rom_contents(rom_file);
    if (!rom_contents.good()) {
        std::cout << "ROM file '" << rom_file << "' not found" << std::endl;
        exit(1);
    }

    R4300::state.load(rom_contents);
    R4300::state.swapMemoryBus(new DebugBus(32));
    return start_recompiler_process();
}

namespace core {

unsigned long recompiler_cycles;
unsigned long recompiler_clears;
unsigned long recompiler_requests;

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

    (void)start_phys_address;
    (void)end_phys_address;
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
            interpreter::cpu::stop_capture(state.cpu.nextPc);
            if (nr_jumps-- <= 0) return true;
            state.reg.pc = state.cpu.nextPc;
            state.cpu.nextAction = State::Action::Continue;
            state.cpu.delaySlot = false;
            interpreter::cpu::start_capture();
            interpreter::cpu::eval();
        }
    }
    return false;
}

/** Return the cache usage statistics. */
void get_recompiler_cache_stats(float *cache_usage,
                                float *buffer_usage) {
    *cache_usage = 0;
    *buffer_usage = 0;
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
            // trace_point(state.cpu.nextPc, state.cycles);
            exec_cpu_interpreter(1);
            exec_rsp_interpreter(state.cycles - cycles);
        }

        fmt::print(fmt::fg(fmt::color::dark_orange),
            "interpreter thread halting\n");
    }
}

void start(void) {
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
