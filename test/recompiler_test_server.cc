
#include <cstdlib>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/color.h>
#include <fmt/format.h>

#include <assembly/disassembler.h>
#include <interpreter/interpreter.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <r4300/state.h>
#include <debugger.h>

using namespace Memory;
using namespace R4300;
using namespace n64;

namespace Memory {

/** Implement a bus replaying memory accesses previously recorded with the
 * LoggingBus. An exception is raised if an attempt is made at :
 *  - accessing an unrecorded memory location
 *  - making out-of-order memory accesses
 */
class ReplayBus: public Memory::Bus
{
public:
    ReplayBus(unsigned bits) : Bus(bits) {}
    virtual ~ReplayBus() {}

    std::vector<BusLog> log;
    unsigned index;

    void reset(BusLog *log, size_t log_len) {
        this->log.clear();
        this->index = 0;
        for (size_t i = 0; i < log_len; i++) {
            this->log.push_back(log[i]);
        }
    }

    virtual bool load(unsigned bytes, u64 addr, u64 *val) {
        if (index >= log.size()) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access: outside recorded trace\n");
            return false;
        }
        if (log[index].access != Memory::Load ||
            log[index].bytes != bytes ||
            log[index].address != addr) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:  load_u{}(0x{:x})\n",
                bytes * 8, addr);
            if (log[index].access == Memory::Store) {
                fmt::print(fmt::emphasis::italic,
                    "    expected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    expected: load_u{}(0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address);
            }
            return false;
        }
        *val = log[index++].value;
        return true;
    }
    virtual bool store(unsigned bytes, u64 addr, u64 val) {
        if (index >= log.size()) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access: outside recorded trace\n");
            return false;
        }
        if (log[index].access != Memory::Store ||
            log[index].bytes != bytes ||
            log[index].address != addr ||
            log[index].value != val) {
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:  store_u{}(0x{:x}, 0x{:x})\n",
                bytes * 8, addr, val);
            if (log[index].access == Memory::Store) {
                fmt::print(fmt::emphasis::italic,
                    "    expected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    expected: load_u{}(0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address);
            }
            return false;
        }
        index++;
        return true;
    }
};

}; /* Memory */

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
            fmt::format("r{}", nr));
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
static BusLog  *trace_memory_log;
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
    fmt::print("------------- ir disassembly --------------\n");
    ir_instr_t const *instr;
    ir_block_t const *block;
    char line[128];

    for (unsigned label = 0; label < graph->nr_blocks; label++) {
        fmt::print(".L{}:\n", label);
        block = &graph->blocks[label];
        for (instr = block->instrs; instr != NULL; instr = instr->next) {
            ir_print_instr(line, sizeof(line), instr);
            fmt::print("    {}\n", line);
        }
    }
}

void print_run_vars(void) {
    ir_const_t const *vars;
    unsigned nr_vars;
    ir_run_vars(&vars, &nr_vars);

    fmt::print(fmt::emphasis::italic, "variable values:\n");
    for (unsigned nr = 0; nr < nr_vars; nr++) {
        fmt::print(fmt::emphasis::italic,
            "    %{} = 0x{:x}\n", nr, vars[nr].int_);
    }
}

namespace interpreter::cpu {

void start_capture(void) {
    trace_registers->start_address = R4300::state.reg.pc;
    trace_registers->start_cycles = R4300::state.cycles;
    trace_registers->start_cpureg = R4300::state.reg;
    trace_registers->start_cp0reg = R4300::state.cp0reg;
    trace_registers->start_cp1reg = R4300::state.cp1reg;

    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    bus->capture(true);
}

void stop_capture(u64 address) {
    trace_registers->end_address = address;
    trace_registers->end_cycles = R4300::state.cycles;
    trace_registers->end_cpureg = R4300::state.reg;
    trace_registers->end_cp0reg = R4300::state.cp0reg;
    trace_registers->end_cp1reg = R4300::state.cp1reg;

    trace_sync->memory_log_len = 0;
    trace_sync->binary_len = 0;

    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    address = trace_registers->start_address;
    for (Memory::BusLog entry: bus->log) {
        if (entry.access == Memory::BusAccess::Load && entry.bytes == 4 &&
            (entry.address & 0xffffffflu) == (address & 0xffffffflu)) {
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
        u64 phys_address;
        translateAddress(address, &phys_address, false);
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
        debugger::halt("Recompiler test fail");

clear_capture:
    bus->capture(false);
    bus->clear();
}

}; /* namespace interpreter:cpu */

void run_interpreter(void) {
    void startGui();
    startGui();
}

int run_recompiler_test(ir_recompiler_backend_t *backend) {
    // Run the recompiler on the trace recorded.
    // Memory synchronization with interpreter process is handled by
    // the caller, the trace records can be safely accessed.

    ir_reset_backend(backend);
    ir_graph_t *graph = ir_mips_disassemble(
        backend, trace_registers->start_address,
        trace_binary, trace_sync->binary_len);

    Memory::ReplayBus *bus = dynamic_cast<Memory::ReplayBus*>(R4300::state.bus);
    const ir_instr_t *err_instr;
    char line[128];
    bool run = false;

    // Preliminary sanity checks on the generated intermediate
    // representation. Really should not fail here.
    if (!ir_typecheck(graph, &err_instr, line, sizeof(line))) {
        fmt::print(fmt::emphasis::italic, "typecheck failure:\n");
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        fmt::print(fmt::emphasis::italic, "in instruction:\n");
        ir_print_instr(line, sizeof(line), err_instr);
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        goto failure;
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

    if (!ir_run(backend, graph, &err_instr, line, sizeof(line))) {
        fmt::print(fmt::emphasis::italic, "run failure:\n");
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        fmt::print(fmt::emphasis::italic, "in instruction:\n");
        ir_print_instr(line, sizeof(line), err_instr);
        fmt::print(fmt::emphasis::italic, "    {}\n", line);
        goto failure;
    }

    // Finally, compare the registers values on exiting the recompiler,
    // with the recorded values. Make sure the whole memory trace was executed.
    if (R4300::state.reg.pc != trace_registers->end_address ||
        R4300::state.cycles != trace_registers->end_cycles ||
        !match_cpureg(trace_registers->end_cpureg, R4300::state.reg) ||
        !match_cp0reg(trace_registers->end_cp0reg, R4300::state.cp0reg) ||
        !match_cp1reg(trace_registers->end_cp1reg, R4300::state.cp1reg)) {
        fmt::print(fmt::fg(fmt::color::tomato), "run invalid:\n");
        goto failure;
    }

    // Success: the recompiler was validated on this trace.
    return 0;

failure:
    // Print debug information.
    if (run) {
        print_run_vars();
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
        }
    }

    print_trace_info();
    print_raw_disassembly();
    print_ir_disassembly(graph);

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
    delete R4300::state.bus;
    R4300::state.bus = new Memory::ReplayBus(32);

    // Allocate a recompiler backend.
    ir_recompiler_backend_t *backend = ir_mips_recompiler_backend();

    while (1) {
        // Wait for a trace from the interpreter process,
        // handle it and notify the test result.
        if (sem_wait(&trace_sync->request) != 0) break;
        trace_sync->status = run_recompiler_test(backend);
        if (sem_post(&trace_sync->response) != 0) break;
    }

    fmt::print(fmt::fg(fmt::color::tomato), "recompiler process exeiting\n");
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
        sizeof(BusLog) * trace_memory_log_maxlen;

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
    trace_memory_log = reinterpret_cast<BusLog *>(shared_mem);

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

    R4300::state.load(argv[1]);
    return start_recompiler_process();
}
