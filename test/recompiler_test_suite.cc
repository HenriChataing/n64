
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <streambuf>

#include <toml++/toml.h>
#include <fmt/format.h>

#include <r4300/export.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/passes.h>
#include <recompiler/code_buffer.h>
#include <recompiler/target/x86_64.h>
#include <recompiler/target/mips.h>
#include <debugger.h>

namespace Memory {
using namespace Memory;

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

    std::vector<BusLog> log;
    unsigned index;

    void reset(std::vector<BusLog> &log) {
        this->log.clear();
        this->index = 0;
        this->_bad = false;
        std::copy(log.begin(), log.end(),
            std::back_inserter(this->log));
    }

    bool bad() {
        return _bad;
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
            _bad = true;
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:   load_u{}(0x{:x})\n",
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
            _bad = true;
            fmt::print(fmt::emphasis::italic,
                "unexpected memory access:\n");
            fmt::print(fmt::emphasis::italic,
                "    played:   store_u{}(0x{:x}, 0x{:x})\n",
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

/* Define stubs for used, but unrequired machine features. */
namespace R4300 {
using namespace R4300;

State state;

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
    return left.pc == right.pc &&
           left.multLo == right.multLo &&
           left.multHi == right.multHi;
}

void print_cpureg_diff(const cpureg &left, const cpureg &right) {
    for (unsigned nr = 0; nr < 32; nr++) {
        print_register_diff(left.gpr[nr], right.gpr[nr],
            fmt::format("r{}", nr));
    }
    print_register_diff(left.pc, right.pc, "pc");
    print_register_diff(left.multLo, right.multLo, "multlo");
    print_register_diff(left.multHi, right.multHi, "multhi");
}

bool match_cp0reg(const cp0reg &left, const cp0reg &right) {
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
           left.cause == right.cause &&
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

State::State() {
    // No need to create the physical memory address space for this machine,
    // memory loads and stores are implemented by replaying, in order, the
    // memory accesses of the original execution trace.
    bus = new Memory::ReplayBus(32);
}

State::~State() {
    delete bus;
}

void set_MI_INTR_REG(u32 bits) {
    debugger::halt("MI_INTR_REG unimplemented for recompiler tests");
}

void clear_MI_INTR_REG(u32 bits) {
    debugger::halt("MI_INTR_REG unimplemented for recompiler tests");
}

void write_DPC_STATUS_REG(u32 value) {
    debugger::halt("DPC_STATUS_REG unimplemented for recompiler tests");
}

void write_DPC_START_REG(u32 value) {
    debugger::halt("DPC_START_REG unimplemented for recompiler tests");
}

void write_DPC_END_REG(u32 value) {
    debugger::halt("DPC_END_REG unimplemented for recompiler tests");
}

void State::scheduleEvent(ulong timeout, void (*callback)()) {
}

void State::cancelEvent(void (*callback)()) {
}

void State::cancelAllEvents(void) {
}

void State::handleEvent(void) {
}

}; /* namespace R4300 */

struct test_header {
    std::string test_name;
    uint64_t start_address;
    std::string asm_code;
    unsigned char *bin_code;
    size_t bin_code_len;
    ir_graph_t *graph;
};

struct test_case {
    uint64_t end_address;
    uint64_t start_cycles;
    uint64_t end_cycles;
    std::vector<Memory::BusLog> trace;
    unsigned char *input;
    unsigned char *output;
};

struct test_statistics {
    unsigned total_pass;
    unsigned total_halted;
    unsigned total_failed;
    unsigned total_skipped;
};

void print_input_info(struct test_header &test) {
    fmt::print("------------- input {:<9} -------------\n", test.test_name);
    fmt::print("start: {:016x}\n", test.start_address);
    fmt::print("end: {:016x}\n", test.start_address + test.bin_code_len);
}

void print_raw_disassembly(struct test_header &test) {
    fmt::print("------------- raw disassembly -------------\n");
    fmt::print("{}", test.asm_code);
}

void print_ir_disassembly(struct test_header &test) {
    fmt::print("------------- ir disassembly --------------\n");

    ir_instr_t const *instr;
    ir_block_t const *block;
    char line[128];

    for (unsigned label = 0; label < test.graph->nr_blocks; label++) {
        fmt::print(".L{}:\n", label);
        block = &test.graph->blocks[label];
        for (instr = block->instrs; instr != NULL; instr = instr->next) {
            ir_print_instr(line, sizeof(line), instr);
            fmt::print("    {}\n", line);
        }
    }
}

void print_backend_error_log(recompiler_backend_t *backend) {
    char message[RECOMPILER_ERROR_MAX_LEN] = "";
    char const *module;

    while (next_recompiler_error(backend, &module, message)) {
        fmt::print(fmt::emphasis::italic, "{} failure:\n{}\n",
            module, message);
    }
}

void print_ir_assembly(struct test_header &test, const code_buffer_t *emitter) {
    fmt::print("--------------- ir assembly ---------------\n");
    dump_code_buffer(stdout, emitter);
}

bool print_typecheck(recompiler_backend_t *backend,
                     struct test_header &test, bool log_success) {

    bool success = ir_typecheck(backend, test.graph);
    if (success && log_success)  {
        fmt::print("------------- ir typecheck ----------------\n");
        fmt::print("typecheck success!\n");
    } else if (!success) {
        fmt::print("------------- ir typecheck ----------------\n");
        print_backend_error_log(backend);
    }
    return success;
}

bool print_run(recompiler_backend_t *backend,
               struct test_header &test) {

    bool success = ir_run(backend, test.graph) & !has_recompiler_error(backend);
    if (!success) {
        fmt::print("---------------- ir run -------------------\n");
        print_backend_error_log(backend);
    }
    return success;
}

bool print_assembly_run(recompiler_backend_t *backend,
                        struct test_header &test,
                        code_entry_t entry) {
    entry();
    return true;
}

static int parse_word_array(toml::array const *array,
                            unsigned char *bytes, size_t *len) {
    size_t array_len = 4 * array->size();
    if (array_len > *len) {
        debugger::error(Debugger::CPU,
            "array node has unsupported length {}, the maximum length is {}",
            array_len, *len);
        return -1;
    }

    *len = array_len;
    for (unsigned i = 0; i < array->size(); i++) {
        int64_t word = array->get(i)->as_integer()->get();
        uint32_t uword = word;
        bytes[4 * i + 0] = uword >> 24;
        bytes[4 * i + 1] = uword >> 16;
        bytes[4 * i + 2] = uword >> 8;
        bytes[4 * i + 3] = uword >> 0;
    }

    return 0;
}

static int parse_trace_entry(toml::node const *trace_node,
                             Memory::BusLog &entry) {
    if (!trace_node->is_table()) {
        debugger::error(Debugger::CPU, "test trace entry is not a table node");
        return -1;
    }

    toml::table const *trace_table = trace_node->as_table();
    auto type_node = (*trace_table)["type"];
    auto address_node = (*trace_table)["address"];
    auto value_node = (*trace_table)["value"];

    if (!type_node || !type_node.is_string()) {
        debugger::error(Debugger::CPU,
            "cannot identify string node 'type' of test entry");
        return -1;
    }
    if (!address_node || !address_node.is_string()) {
        debugger::error(Debugger::CPU,
            "cannot identify string node 'address' of test entry");
        return -1;
    }
    if (!value_node || !value_node.is_string()) {
        debugger::error(Debugger::CPU,
            "cannot identify string node 'value' of test entry");
        return -1;
    }

    std::string type = **(type_node.as_string());
    uint64_t address = strtoull((**address_node.as_string()).c_str(), NULL, 0);
    uint64_t value = strtoull((**value_node.as_string()).c_str(), NULL, 0);
    unsigned bytes;
    Memory::BusAccess access;

    if (type == "load_u8") {
        access = Memory::Load;
        bytes = 1;
    }
    else if (type == "load_u16") {
        access = Memory::Load;
        bytes = 2;
    }
    else if (type == "load_u32") {
        access = Memory::Load;
        bytes = 4;
    }
    else if (type == "load_u64") {
        access = Memory::Load;
        bytes = 8;
    }
    else if (type == "store_u8") {
        access = Memory::Store;
        bytes = 1;
    }
    else if (type == "store_u16") {
        access = Memory::Store;
        bytes = 2;
    }
    else if (type == "store_u32") {
        access = Memory::Store;
        bytes = 4;
    }
    else if (type == "store_u64") {
        access = Memory::Store;
        bytes = 8;
    }
    else {
        debugger::error(Debugger::CPU,
            "invalid 'type' value of test entry");
        return -1;
    }

    entry.address = address;
    entry.value = value;
    entry.access = access;
    entry.bytes = bytes;
    return 0;
}

static int parse_test_case(toml::node const *test_node,
                           test_case &test) {
    if (!test_node->is_table()) {
        debugger::error(Debugger::CPU, "test entry is not a table node");
        return -1;
    }

    toml::table const *test_table = test_node->as_table();
    auto end_address_node = (*test_table)["end_address"];
    auto start_cycles_node = (*test_table)["start_cycles"];
    auto end_cycles_node = (*test_table)["end_cycles"];
    auto trace_node = (*test_table)["trace"];
    toml::array const *trace_array;

    if (!end_address_node || !end_address_node.is_string()) {
        debugger::error(Debugger::CPU, "cannot identify test string node 'end_address'");
        return -1;
    }
    if (!start_cycles_node || !start_cycles_node.is_integer()) {
        debugger::error(Debugger::CPU, "cannot identify test integer node 'start_cycles'");
        return -1;
    }
    if (!end_cycles_node || !end_cycles_node.is_integer()) {
        debugger::error(Debugger::CPU, "cannot identify test integer node 'end_cycles'");
        return -1;
    }
    if (!trace_node || !trace_node.is_array()) {
        debugger::error(Debugger::CPU, "cannot identify test array node 'trace'");
        return -1;
    }

    test.end_address = strtoull((**end_address_node.as_string()).c_str(), NULL, 0);
    test.start_cycles =  start_cycles_node.as_integer()->get();
    test.end_cycles =  end_cycles_node.as_integer()->get();

    trace_array = trace_node.as_array();
    for (unsigned i = 0; i < trace_array->size(); i++) {
        Memory::BusLog entry;
        if (parse_trace_entry(trace_array->get(i), entry) < 0) {
            return -1;
        }
        test.trace.push_back(entry);
    }
    return 0;
}

int run_test_suite(recompiler_backend_t *backend,
                   code_buffer_t *emitter,
                   std::string const &test_suite_name,
                   struct test_statistics *stats,
                   bool verbose) {
    std::string test_filename   =
        "test/recompiler/" + test_suite_name + ".toml";
    std::string input_filename  =
        "test/recompiler/" + test_suite_name + ".input";
    std::string output_filename  =
        "test/recompiler/" + test_suite_name + ".output";
    toml::table test_table;

    /* Load the test description from the *.toml file, and parse
     * pertinent values, including the input and output desc format. */

    try {
        test_table = toml::parse_file(test_filename);
    } catch (const toml::parse_error &err) {
        debugger::error(Debugger::CPU, "error parsing file '{}' at position {}:{}",
            *err.source().path, err.source().begin.line,
            err.source().begin.column);
        debugger::error(Debugger::CPU, "{}", err.description());
        return -1;
    }

    auto bin_code_node = test_table["bin_code"];
    auto asm_code_node = test_table["asm_code"];
    auto start_address_node = test_table["start_address"];
    auto test_list = test_table["test"];

    if (!bin_code_node || !bin_code_node.is_array() ||
        !bin_code_node.as_array()->is_homogeneous<int64_t>()) {
        debugger::error(Debugger::CPU, "cannot identify array node 'bin_code'");
        return -1;
    }
    if (!asm_code_node || !asm_code_node.is_string()) {
        debugger::error(Debugger::CPU, "cannot identify string node 'asm_code'");
        return -1;
    }
    if (!start_address_node || !start_address_node.is_string()) {
        debugger::error(Debugger::CPU, "cannot identify integer node 'start_address'");
        return -1;
    }
    if (!test_list || !test_list.is_array() ||
        !test_list.as_array()->is_homogeneous<toml::table>()) {
        debugger::error(Debugger::CPU, "cannot identify array node 'test'");
        return -1;
    }

    uint8_t bin_code[1024];
    size_t bin_code_len = sizeof(bin_code);
    toml::array *test_array = test_list.as_array();
    unsigned nr_tests = test_array->size();
    code_entry_t entry;

    if (parse_word_array(bin_code_node.as_array(), bin_code, &bin_code_len) < 0) {
        return -1;
    }

    struct test_header header;
    header.start_address = strtoull((**start_address_node.as_string()).c_str(), NULL, 0);
    header.bin_code = bin_code;
    header.bin_code_len = bin_code_len;
    header.asm_code = **asm_code_node.as_string();

    clear_recompiler_backend(backend);
    header.graph = ir_mips_disassemble(backend, header.start_address,
                                       header.bin_code, header.bin_code_len);

    /* Disassembly can fail due to resource limitations. */
    if (header.graph == NULL) {
        debugger::error(Debugger::CPU, "failed to generate intermediate code");
        print_backend_error_log(backend);
        goto test_preparation_failure;
    }

    /* Type check the generated graph. */
    if (!print_typecheck(backend, header, false)) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        goto test_preparation_failure;
    }

    /* Optimize the generated graph. */
    ir_optimize(backend, header.graph);

    if (verbose) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        print_ir_disassembly(header);
    }

    /* Type check again to verify the result of the optimize pass. */
    if (!print_typecheck(backend, header, false)) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        goto test_preparation_failure;
    }

    clear_code_buffer(emitter);
    entry = ir_x86_64_assemble(backend, emitter, header.graph, NULL);
    if (entry == NULL) {
        debugger::error(Debugger::CPU, "failed to generate x86_64 binary code");
        goto test_preparation_failure;
    }

    if (verbose) {
        print_ir_disassembly(header);
        print_ir_assembly(header, emitter);
    }

    /* Load input parameters and output values from the files *.rsp
     * and *.golden */

    {  /* test execution */
    std::ifstream input(input_filename, std::ios::binary);
    std::ifstream output(output_filename, std::ios::binary);
    bool any_failed = false;

    if (input.bad() || output.bad()) {
        debugger::error(Debugger::CPU,
            "cannot load {} file '{}'",
            input.bad() ? "input" : "output",
            input.bad() ? input_filename : output_filename);
        input.close();
        output.close();
        return -1;
    }

    for (unsigned nr = 0; nr < nr_tests; nr++) {
        struct test_case test;
        if (parse_test_case(test_array->get(nr), test) < 0) {
            fmt::print("+ [test {}/{}] {}:?? -- SKIPPED\n",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::emphasis::italic,
                "Failed to parse the test case members\n");
            stats->total_skipped++;
            continue;
        }

        R4300::cpureg reg = { 0 };
        R4300::cp0reg cp0reg = { 0 };
        R4300::cp1reg cp1reg = { 0 };
        Memory::ReplayBus *bus =
            dynamic_cast<Memory::ReplayBus*>(R4300::state.bus);

        R4300::deserialize(input, R4300::state.reg);
        R4300::deserialize(input, R4300::state.cp0reg);
        R4300::deserialize(input, R4300::state.cp1reg);

        R4300::deserialize(output, reg);
        R4300::deserialize(output, cp0reg);
        R4300::deserialize(output, cp1reg);

        if (input.bad() || output.bad()) {
            fmt::print("+ [test {}/{}] {}:?? -- SKIPPED\n",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::emphasis::italic,
                "Failed to load {} state from '{}'",
                input.bad() ? "input" : "output",
                input.bad() ? input_filename : output_filename);
            any_failed = true;
            stats->total_skipped += nr_tests - nr;
            break;
        }

        R4300::state.cycles = test.start_cycles;
        R4300::state.cp1reg.setFprAliases(R4300::state.cp0reg.FR());
        reg.pc = test.end_address;
        bus->reset(test.trace);
        bool run_success;

        // if (!(run_success = print_run(backend, header)) ||
        if (!(run_success = print_assembly_run(backend, header, entry)) ||
            bus->bad() ||
            !match_cpureg(reg,    R4300::state.reg) ||
            !match_cp0reg(cp0reg, R4300::state.cp0reg) ||
            !match_cp1reg(cp1reg, R4300::state.cp1reg) ||
            R4300::state.cycles != test.end_cycles) {
            if (run_success) {
                fmt::print(fmt::emphasis::italic,
                    "register differences (expected, computed):\n");
                print_cpureg_diff(reg, R4300::state.reg);
                print_cp0reg_diff(cp0reg, R4300::state.cp0reg);
                print_cp1reg_diff(cp1reg, R4300::state.cp1reg);
                if (R4300::state.cycles != test.end_cycles) {
                    fmt::print(fmt::emphasis::italic,
                        "    cycles: {:<16} - {:}\n",
                        test.end_cycles, R4300::state.cycles);
                }
            }
            fmt::print("+ [test {}/{}] {}:- -- ",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::fg(fmt::color::tomato), "FAILED\n");
            any_failed = true;
            stats->total_failed++;
        }  else {
            fmt::print("+ [test {}/{}] {}:- -- ",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::fg(fmt::color::chartreuse), "PASS\n");
            stats->total_pass++;
        }
    }

    if (any_failed && !verbose) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        print_ir_assembly(header, emitter);
    }

    input.close();
    output.close();
    } /* test execution */
    return 0;

test_preparation_failure:
    fmt::print("+ [test -/{}] {}:- -- ", nr_tests, test_suite_name);
    fmt::print(fmt::fg(fmt::color::tomato), "FAILED\n");
    stats->total_failed += nr_tests;
    return -1;
}

std::vector<std::string> list_test_suites(void) {
    DIR* dirp = opendir("test/recompiler");
    std::vector<std::string> test_suites;
    struct dirent * dp;
    while ((dp = readdir(dirp)) != NULL) {
        size_t len = strlen(dp->d_name);
        char const *toml_ext = ".toml";
        if (len > strlen(toml_ext) &&
            memcmp(dp->d_name + len - strlen(toml_ext),
                   toml_ext, strlen(toml_ext)) == 0) {
            std::string test_name(dp->d_name, len - strlen(toml_ext));
            test_suites.push_back(test_name);
        }
    }
    closedir(dirp);
    return test_suites;
}


enum {
    ALL,
    RANDOM,
    SELECTED,
};

int main(int argc, char **argv) {
    std::srand(std::time(nullptr));
    int mode = RANDOM;
    unsigned long selected = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "all") == 0) {
            mode = ALL;
        }
        else if (strcmp(argv[1], "rand") == 0) {
            mode = RANDOM;
        }
        else {
            mode = SELECTED;
        }
    }

    std::vector<std::string> test_suites = list_test_suites();
    struct test_statistics test_stats = {};
    bool stop_at_first_fail = false;
    recompiler_backend_t *backend = ir_mips_recompiler_backend();
    code_buffer_t *emitter = alloc_code_buffer(0x4000);

    if (mode == SELECTED) {
        for (; selected < test_suites.size(); selected++) {
            if (test_suites[selected] == argv[1])
                break;
        }
        if (selected == test_suites.size()) {
            fmt::print(fmt::fg(fmt::color::tomato),
                "the selected test suite '{}' does not exist\n", argv[1]);
            return 1;
        }
    }

    if (mode == RANDOM) {
        selected = std::rand() % test_suites.size();
    }

    if (mode == ALL) {
        for (unsigned nr = 0; nr < test_suites.size(); nr++) {
            run_test_suite(backend, emitter, test_suites[nr], &test_stats, false);
            if (stop_at_first_fail && test_stats.total_failed)
                break;
        }
    } else {
        run_test_suite(backend, emitter, test_suites[selected], &test_stats, true);
    }

    unsigned total_tests =
        test_stats.total_pass +
        test_stats.total_halted +
        test_stats.total_failed +
        test_stats.total_skipped;
    fmt::print(fmt::emphasis::bold,
        "{} tests run; PASS:{} HALTED:{} FAILED:{} SKIPPED:{}\n",
        total_tests,
        test_stats.total_pass,
        test_stats.total_halted,
        test_stats.total_failed,
        test_stats.total_skipped);
    return total_tests != test_stats.total_pass;
}
