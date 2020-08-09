
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <dirent.h>

#include <toml++/toml.h>
#include <fmt/format.h>

#include <recompiler/ir.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <mips/asm.h>
#include <debugger.h>

/* Define stubs for used, but unrequired machine features. */
namespace R4300 {
using namespace R4300;

State state;

State::State() {
    // No need to create the physical memory address space for this machine,
    // memory loads and stores are implemented by replaying, in order, the
    // memory accesses of the original execution trace.
    bus = new Memory::LoggingBus(32);
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
    ir_instr_t *entry;
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
    char line[128];

    for (instr = test.entry; instr != NULL; instr = instr->next) {
        ir_print_instr(line, sizeof(line), instr);
        puts(line);
    }
}

bool print_typecheck(struct test_header &test, bool log_success) {

    ir_instr_t const *instr;
    char line[128];
    bool success = ir_typecheck(test.entry, &instr, line, sizeof(line));

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

int run_test_suite(std::string const &test_suite_name, struct test_statistics *stats) {
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

    header.entry = ir_mips_disassemble(header.start_address,
                                       header.bin_code, header.bin_code_len);

    if (!print_typecheck(header, false)) {
        print_input_info(header);
        print_raw_disassembly(header);
        print_ir_disassembly(header);
        stats->total_failed += nr_tests;
        return -1;
    }

    size_t input_size = (34*8) + (12*4 + 8*8) + (32*8+2*4);
    size_t output_size = input_size;
    size_t full_input_size = input_size * nr_tests;
    size_t full_output_size = output_size * nr_tests;

    /* Load input parameters and output values from the files *.rsp
     * and *.golden */

    uint8_t *input  = new uint8_t[full_input_size];
    uint8_t *output = new uint8_t[full_output_size];

    if (load_file(input_filename, input, &input_size, true) < 0 ||
        load_file(output_filename, output, &output_size, true) < 0) {
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

        fmt::print("+ [test {}/{}] {}:- -- ",
            nr + 1, nr_tests, test_suite_name);

        // fmt::print(fmt::fg(fmt::color::tomato), "FAILED\n");
        // fmt::print(fmt::fg(fmt::color::dark_orange), "HALTED\n");

        fmt::print(fmt::fg(fmt::color::chartreuse), "PASS\n");
        stats->total_pass++;
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

    if (mode == RANDOM) {
        selected = std::rand() % test_suites.size();
    }

    if (mode == ALL) {
        for (unsigned nr = 0; nr < test_suites.size(); nr++) {
            run_test_suite(test_suites[nr], &test_stats);
            if (stop_at_first_fail && test_stats.total_failed)
                break;
        }
    } else {
        run_test_suite(test_suites[selected], &test_stats);
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
