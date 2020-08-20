
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <dirent.h>

#include <toml++/toml.h>
#include <fmt/format.h>

#include <r4300/export.h>
#include <recompiler/ir.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <mips/asm.h>
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
public:
    ReplayBus(unsigned bits) : Bus(bits) {}
    virtual ~ReplayBus() {}

    std::vector<BusLog> log;
    unsigned index;

    void reset(std::vector<BusLog> &log) {
        this->log.clear();
        this->index = 0;
        std::copy(log.begin(), log.end(),
            std::back_inserter(this->log));
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
                    "    exected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    exected: load_u{}(0x{:x})\n",
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
                    "    exected: store_u{}(0x{:x}, 0x{:x})\n",
                    log[index].bytes * 8,
                    log[index].address,
                    log[index].value);
            } else {
                fmt::print(fmt::emphasis::italic,
                    "    exected: load_u{}(0x{:x})\n",
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

static inline bool match_register(uintmax_t left, uintmax_t right,
                                  const std::string &name) {
    if (left != right) {
        fmt::print(fmt::emphasis::italic,
            "    {>8}: {:<16x} - {:x}", name, left, right);
        return false;
    } else {
        return true;
    }
}

bool match_cpureg(const cpureg &left, const cpureg &right) {
    bool equal = true;
    for (unsigned nr = 0; nr < 32; nr++) {
        equal &= match_register(left.gpr[nr], right.gpr[nr],
            fmt::format("r{}", nr));
    }
    return match_register(left.pc, right.pc, "pc") &&
           match_register(left.multLo, right.multLo, "multlo") &&
           match_register(left.multHi, right.multHi, "multhi") && equal;
}

bool match_cp0reg(const cp0reg &left, const cp0reg &right) {
    return match_register(left.index, right.index, "index") &&
           match_register(left.random, right.random, "random") &&
           match_register(left.entrylo0, right.entrylo0, "entrylo0") &&
           match_register(left.entrylo1, right.entrylo1, "entrylo1") &&
           match_register(left.context, right.context, "context") &&
           match_register(left.pagemask, right.pagemask, "pagemask") &&
           match_register(left.wired, right.wired, "wired") &&
           match_register(left.badvaddr, right.badvaddr, "badvaddr") &&
           match_register(left.count, right.count, "count") &&
           match_register(left.entryhi, right.entryhi, "entryhi") &&
           match_register(left.compare, right.compare, "compare") &&
           match_register(left.sr, right.sr, "sr") &&
           match_register(left.cause, right.cause, "cause") &&
           match_register(left.epc, right.epc, "epc") &&
           match_register(left.prid, right.prid, "prid") &&
           match_register(left.config, right.config, "config") &&
           match_register(left.lladdr, right.lladdr, "lladdr") &&
           match_register(left.watchlo, right.watchlo, "watchlo") &&
           match_register(left.watchhi, right.watchhi, "watchhi") &&
           match_register(left.xcontext, right.xcontext, "xcontext") &&
           match_register(left.perr, right.perr, "perr") &&
           match_register(left.cacheerr, right.cacheerr, "cacheerr") &&
           match_register(left.taglo, right.taglo, "taglo") &&
           match_register(left.taghi, right.taghi, "taghi") &&
           match_register(left.errorepc, right.errorepc, "errorepc");
}

bool match_cp1reg(const cp1reg &left, const cp1reg &right) {
    bool equal = true;
    for (unsigned nr = 0; nr < 32; nr++) {
        equal &= match_register(left.fpr[nr], right.fpr[nr],
            fmt::format("fpr{}", nr));
    }
    return match_register(left.fcr0, right.fcr0, "fcr0") &&
           match_register(left.fcr31, right.fcr31, "fcr31") && equal;
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

bool print_typecheck(struct test_header &test, bool log_success) {

    ir_instr_t const *instr;
    char line[128];
    bool success = ir_typecheck(test.graph, &instr, line, sizeof(line));

    if (success && log_success)  {
        fmt::print("------------- ir typecheck ----------------\n");
        fmt::print("typecheck success!\n");
    } else if (!success) {
        fmt::print("------------- ir typecheck ----------------\n");
        fmt::print("typecheck failure:\n");
        puts(line);
        fmt::print("in instruction:\n");
        ir_print_instr(line, sizeof(line), instr);
        puts(line);
    }

    return success;
}

bool print_run(struct test_header &test,
               ir_recompiler_backend_t const *backend, bool log_success) {

    ir_instr_t const *instr;
    char line[128] = { 0 };
    bool success = ir_run(backend, test.graph, &instr, line, sizeof(line));

    if (success && log_success)  {
        fmt::print("---------------- ir run -------------------\n");
        fmt::print("run success!\n");
    } else if (!success) {
        fmt::print("---------------- ir run -------------------\n");
        if (line[0] != '\n') {
            fmt::print(fmt::emphasis::italic, "run failure:\n");
            fmt::print(fmt::emphasis::italic, "    {}\n", line);
            fmt::print(fmt::emphasis::italic, "in instruction:\n");
        } else {
            fmt::print(fmt::emphasis::italic, "run failure in instruction:\n");
        }
        ir_print_instr(line, sizeof(line), instr);
        fmt::print(fmt::emphasis::italic, "    {}\n", line);

        ir_const_t const *vars;
        unsigned nr_vars;
        ir_run_vars(&vars, &nr_vars);

        fmt::print(fmt::emphasis::italic, "variable values:\n");
        for (unsigned nr = 0; nr < nr_vars; nr++) {
            fmt::print(fmt::emphasis::italic,
                "    %{} = 0x{:x}\n", nr, vars[nr].int_);
        }
    }

    return success;
}

int load_file(std::string filename, u8 *buffer, size_t *size, bool exact) {
    FILE *fd = fopen(filename.c_str(), "r");
    if (fd == NULL) {
        debugger::error(Debugger::CPU,
            "cannot load input/output file '{}'",
            filename);
        return -1;
    }

    int result = fread(buffer, 1, *size, fd);
    fclose(fd);

    if (result < 0 || (result != (int)*size && exact)) {
        debugger::error(Debugger::CPU,
            "cannot load {} file bytes from '{}'",
            *size, filename);
        return -1;
    }

    *size = result;
    return 0;
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
    if (!address_node || !address_node.is_integer()) {
        debugger::error(Debugger::CPU,
            "cannot identify integer node 'address' of test entry");
        return -1;
    }
    if (!value_node || !value_node.is_integer()) {
        debugger::error(Debugger::CPU,
            "cannot identify integer node 'value' of test entry");
        return -1;
    }

    std::string type = **(type_node.as_string());
    int64_t address = address_node.as_integer()->get();
    int64_t value = value_node.as_integer()->get();
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

    entry.address = (uint64_t)address;
    entry.value = (uint64_t)value;
    entry.access = access;
    entry.bytes = bytes;
    return 0;
}

static int parse_test_case(toml::node const *test_case,
                           uint64_t *end_address,
                           std::vector<Memory::BusLog> &trace) {
    if (!test_case->is_table()) {
        debugger::error(Debugger::CPU, "test entry is not a table node");
        return -1;
    }

    toml::table const *test_table = test_case->as_table();
    auto end_address_node = (*test_table)["end_address"];
    auto trace_node = (*test_table)["trace"];
    toml::array const *trace_array;

    if (!end_address_node || !end_address_node.is_string()) {
        debugger::error(Debugger::CPU, "cannot identify test integer node 'end_address'");
        return -1;
    }
    if (!trace_node || !trace_node.is_array()) {
        debugger::error(Debugger::CPU, "cannot identify test array node 'trace'");
        return -1;
    }

    *end_address = strtoull((**end_address_node.as_string()).c_str(), NULL, 0);
    trace_array = trace_node.as_array();
    for (unsigned i = 0; i < trace_array->size(); i++) {
        Memory::BusLog entry;
        if (parse_trace_entry(trace_array->get(i), entry) < 0) {
            return -1;
        }
        trace.push_back(entry);
    }
    return 0;
}

int run_test_suite(ir_recompiler_backend_t *backend,
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

    if (parse_word_array(bin_code_node.as_array(), bin_code, &bin_code_len) < 0) {
        return -1;
    }

    struct test_header header;
    header.start_address = strtoull((**start_address_node.as_string()).c_str(), NULL, 0);
    header.bin_code = bin_code;
    header.bin_code_len = bin_code_len;
    header.asm_code = **asm_code_node.as_string();

    ir_reset_backend(backend);
    header.graph = ir_mips_disassemble(backend, header.start_address,
                                       header.bin_code, header.bin_code_len);

    if (!print_typecheck(header, false)) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        stats->total_failed += nr_tests;
        return -1;
    }
    if (verbose) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
    }

    size_t input_size =
        R4300::serializedCpuRegistersSize() +
        R4300::serializedCp0RegistersSize() +
        R4300::serializedCp1RegistersSize();
    size_t output_size = input_size;
    size_t full_input_size = input_size * nr_tests;
    size_t full_output_size = output_size * nr_tests;

    /* Load input parameters and output values from the files *.rsp
     * and *.golden */

    uint8_t *input  = new uint8_t[full_input_size];
    uint8_t *output = new uint8_t[full_output_size];
    uint8_t *input_cur = input;
    uint8_t *output_cur = output;
    bool any_failed = false;

    if (load_file(input_filename, input, &full_input_size, true) < 0 ||
        load_file(output_filename, output, &full_output_size, true) < 0) {
        delete input;
        delete output;
        return -1;
    }

    for (unsigned nr = 0; nr < nr_tests; nr++) {
        struct test_case test;
        if (parse_test_case(test_array->get(nr), &test.end_address, test.trace) < 0) {
            fmt::print("+ [test {}/{}] {}:?? -- SKIPPED\n",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::emphasis::italic,
                "Failed to parse the test case members\n");
            stats->total_skipped++;
            continue;
        }

        Memory::ReplayBus *bus = dynamic_cast<Memory::ReplayBus*>(R4300::state.bus);

        R4300::deserializeCpuRegisters(input_cur, R4300::state.reg);
        input_cur += R4300::serializedCpuRegistersSize();
        R4300::deserializeCp0Registers(input_cur, R4300::state.cp0reg);
        input_cur += R4300::serializedCp0RegistersSize();
        R4300::deserializeCp1Registers(input_cur, R4300::state.cp1reg);
        input_cur += R4300::serializedCp1RegistersSize();

        R4300::cpureg reg;
        R4300::cp0reg cp0reg;
        R4300::cp1reg cp1reg;
        R4300::deserializeCpuRegisters(output_cur, reg);
        output_cur += R4300::serializedCpuRegistersSize();
        R4300::deserializeCp0Registers(output_cur, cp0reg);
        output_cur += R4300::serializedCp0RegistersSize();
        R4300::deserializeCp1Registers(output_cur, cp1reg);
        output_cur += R4300::serializedCp1RegistersSize();

        bus->reset(test.trace);

        if (!print_run(header, backend, false) ||
            !match_cpureg(reg,    R4300::state.reg) ||
            !match_cp0reg(cp0reg, R4300::state.cp0reg) ||
            !match_cp1reg(cp1reg, R4300::state.cp1reg)) {
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

    if (any_failed) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
    }

    delete input;
    delete output;
    return 0;
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
    long selected = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "all") == 0) {
            mode = ALL;
        }
        else if (strcmp(argv[1], "rand") == 0) {
            mode = RANDOM;
        }
        else {
            char *end = NULL;
            selected = strtol(argv[1], &end, 0);
            mode = (end != argv[1]) ? SELECTED : RANDOM;
        }
    }

    std::vector<std::string> test_suites = list_test_suites();
    struct test_statistics test_stats = {};
    bool stop_at_first_fail = true;
    ir_recompiler_backend_t *backend = ir_mips_recompiler_backend();

    if (mode == RANDOM) {
        selected = std::rand() % test_suites.size();
    }

    if (mode == ALL) {
        for (unsigned nr = 0; nr < test_suites.size(); nr++) {
            run_test_suite(backend, test_suites[nr], &test_stats, false);
            if (stop_at_first_fail && test_stats.total_failed)
                break;
        }
    } else {
        run_test_suite(backend, test_suites[selected], &test_stats, true);
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
    return total_tests == test_stats.total_pass;
}
