
#include <iostream>
#include <fstream>
#include <toml++/toml.h>
#include <fmt/format.h>
#include <fmt/color.h>

#include <r4300/state.h>
#include <debugger.h>

using namespace std::string_view_literals;

/* Define stubs for used, but unrequired machine features. */
namespace R4300 {
using namespace R4300;

State state;

State::State() {
    // No need to create the physical memory address space for this machine,
    // as the RSP does not use the main bus, but can address a limited
    // range of memory.
}

State::~State() {
}

void set_MI_INTR_REG(u32 bits) {
    debugger::halt("MI_INTR_REG unimplemented for RSP tests");
}

void clear_MI_INTR_REG(u32 bits) {
    debugger::halt("MI_INTR_REG unimplemented for RSP tests");
}

void write_DPC_STATUS_REG(u32 value) {
    debugger::halt("DPC_STATUS_REG unimplemented for RSP tests");
}

void write_DPC_START_REG(u32 value) {
    debugger::halt("DPC_START_REG unimplemented for RSP tests");
}

void write_DPC_END_REG(u32 value) {
    debugger::halt("DPC_END_REG unimplemented for RSP tests");
}

}; /* namespace R4300 */


int load_file(std::string filename, u8 *buffer, unsigned *size, bool exact) {
    FILE *fd = fopen(filename.c_str(), "r");
    if (fd == NULL) {
        debugger::error(Debugger::RSP,
            "cannot load input/output file '{}'",
            filename);
        return -1;
    }

    int result = fread(buffer, 1, *size, fd);
    fclose(fd);

    if (result < 0 || (result != (int)*size && exact)) {
        debugger::error(Debugger::RSP,
            "cannot load {} file bytes from '{}'",
            *size, filename);
        return -1;
    }

    *size = result;
    return 0;
}

static unsigned desc_element_size(toml::value<std::string> &elt) {
    std::string v128 = "v128", u32 = "u32";
    if ((*elt).compare(0, v128.size(), v128) == 0)
        return 16;
    if ((*elt).compare(0, u32.size(), u32) == 0)
        return 4;

    debugger::error(Debugger::RSP, "cannot identify desc format {}", *elt);
    return 0;
}

static unsigned desc_size(toml::array *desc) {
    unsigned size = 0;
    for (auto it = desc->begin(); it != desc->end(); it++) {
        if (it->is_string()) {
            size += desc_element_size(*it->as_string());
        } else {
            debugger::warn(Debugger::RSP,
                "skipping node of type {} in desc array", it->type());
        }
    }
    return size;
}

static int parse_test_case(toml::node const *test_case, std::string &test_name,
                           uint8_t *input, unsigned input_desc_size) {
    if (!test_case->is_table()) {
        debugger::error(Debugger::RSP, "test entry is not a table node");
        return -1;
    }

    toml::table const *test_table = test_case->as_table();
    auto name =        (*test_table)["name"];
    auto input_array = (*test_table)["input"];
    toml::array const *input_words;

    if (!name || !name.is_string()) {
        debugger::error(Debugger::RSP, "cannot identify test string node 'name'");
        return -1;
    }
    if (!input_array || !input_array.is_array()) {
        debugger::error(Debugger::RSP, "cannot identify test array node 'input'");
        return -1;
    }

    input_words = input_array.as_array();
    if ((4 * input_words->size()) != input_desc_size) {
        debugger::error(Debugger::RSP,
            "test array node 'input' has invalid length {}, against expected {}",
            (4 * input_words->size()), input_desc_size);
        return -1;
    }
    if (!input_words->is_homogeneous<int64_t>()) {
        debugger::error(Debugger::RSP,
            "test array node 'input' contains invalid entries");
        return -1;
    }

    test_name = **(name.as_string());
    for (unsigned i = 0; i < input_words->size(); i++) {
        int64_t word = input_words->get(i)->as_integer()->get();
        uint32_t uword = word;
        input[4 * i + 0] = uword >> 24;
        input[4 * i + 1] = uword >> 16;
        input[4 * i + 2] = uword >> 8;
        input[4 * i + 3] = uword >> 0;
    }

    return 0;
}

static void print_array(uint8_t const *buffer, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        if (i && !(i % 16)) fmt::print("\n");
        fmt::print(fmt::emphasis::italic, " {:02x}", buffer[i]);
    }
    fmt::print("\n");
}

struct test_statistics {
    unsigned total_pass;
    unsigned total_halted;
    unsigned total_failed;
    unsigned total_skipped;
};

int run_test_suite(char const *test_suite_name, struct test_statistics *stats) {
    std::string test_filename   = "test/rsp/" + std::string(test_suite_name) + ".toml";
    std::string rsp_filename    = "test/rsp/" + std::string(test_suite_name) + ".rsp";
    std::string output_filename = "test/rsp/" + std::string(test_suite_name) + ".golden";
    toml::table test_table;

    /* Load the test description from the *.toml file, and parse
     * pertinent values, including the input and output desc format. */

    try {
        test_table = toml::parse_file(test_filename);
    } catch (const toml::parse_error &err) {
        debugger::error(Debugger::RSP, "error parsing file '{}' at position {}:{}",
            *err.source().path, err.source().begin.line,
            err.source().begin.column);
        debugger::error(Debugger::RSP, "{}", err.description());
        return -1;
    }

    auto input_desc = test_table["input_desc"];
    auto output_desc = test_table["output_desc"];
    auto test_list = test_table["test"];

    if (!input_desc || !input_desc.is_array()) {
        debugger::error(Debugger::RSP, "cannot identify array node 'input_desc'");
        return -1;
    }
    if (!output_desc || !output_desc.is_array()) {
        debugger::error(Debugger::RSP, "cannot identify array node 'output_desc'");
        return -1;
    }
    if (!test_list || !test_list.is_array() ||
        !test_list.as_array()->is_homogeneous<toml::table>()) {
        debugger::error(Debugger::RSP, "cannot identify array node 'test'");
        return -1;
    }

    unsigned input_desc_size = desc_size(input_desc.as_array());
    unsigned output_desc_size = desc_size(output_desc.as_array());
    toml::array *test_array = test_list.as_array();
    unsigned nr_tests = test_array->size();

    if (input_desc_size > 0x1000 || output_desc_size > 0x1000) {
        debugger::error(Debugger::RSP,
            "invalid input/output desc: size {}/{} larger than DMEM size",
            input_desc_size, output_desc_size);
        return -1;
    }

    debugger::debug(Debugger::RSP,
        "input_desc_size:{} output_desc_size:{} nr_tests:{}",
        input_desc_size, output_desc_size, nr_tests);

    /* Load input parameters and output values from the files *.rsp
     * and *.golden */

    uint8_t rsp[0x1000];
    uint8_t input[input_desc_size];
    uint8_t *output = new uint8_t[output_desc_size * nr_tests];

    unsigned rsp_size = 0x1000;
    unsigned output_size = output_desc_size * nr_tests;

    if (load_file(rsp_filename, rsp, &rsp_size, false) < 0 ||
        load_file(output_filename, output, &output_size, true) < 0) {
        delete output;
        return -1;
    }

    /* Start test execution:
     * 1. load RSP microcode into IMEM
     * 2. load test values for each round into DMEM
     * 2. run RSP starting from PC=0 until broke, or timeout
     * 3. extract result from DMEM and compare to expected values */

    /* FIXME: when the test results were generated the DMEM was not wiped in
     * between tests of the same instruction, this has an impact
     * on the result of STV, SWV, were previous test results pop up in the
     * output. The same with the accumulator.  */
    memset(R4300::state.dmem, 0, 0x1000);
    R4300::state.rspreg = (R4300::rspreg){};

    for (unsigned nr = 0; nr < nr_tests; nr++) {
        toml::node const *test_case = test_array->get(nr);
        uint8_t const *test_output = &output[nr * output_desc_size];
        std::string test_name;

        if (parse_test_case(test_case, test_name, input, input_desc_size) < 0) {
            fmt::print("+ [test {}/{}] {}:?? -- SKIPPED\n",
                nr + 1, nr_tests, test_suite_name);
            fmt::print(fmt::emphasis::italic,
                "Failed to parse the test case members\n");
            stats->total_skipped++;
            continue;
        }

        memcpy(R4300::state.imem, rsp, rsp_size);
        memcpy(R4300::state.dmem, input, input_desc_size);
        R4300::state.rspreg.pc = 0;
        R4300::state.rsp.nextAction = R4300::State::Action::Jump;
        R4300::state.rsp.nextPc = 0x0;
        R4300::state.hwreg.SP_STATUS_REG = 0;
        debugger::debugger.halted = false;

        fmt::print("+ [test {}/{}] {}:{} -- ",
            nr + 1, nr_tests, test_suite_name, test_name);

        while ((R4300::state.hwreg.SP_STATUS_REG & SP_STATUS_BROKE) == 0 &&
               !debugger::debugger.halted) {
            R4300::RSP::step();
        }

        if (debugger::debugger.halted) {
            fmt::print(fmt::fg(fmt::color::dark_orange), "HALTED\n");
            fmt::print(fmt::emphasis::italic,
                "The RSP stopped with the following halt reason: {}\n",
                debugger::debugger.haltedReason);
            stats->total_halted++;
            continue;
        }
        if (memcmp(R4300::state.dmem + 0x800, test_output, output_desc_size) != 0) {
            fmt::print(fmt::fg(fmt::color::tomato), "FAILED\n");
            fmt::print(fmt::emphasis::italic,
                "The RSP execution did not match the expected outcome:\n");
            fmt::print(fmt::emphasis::italic, "Input:\n");
            print_array(input, input_desc_size);
            fmt::print(fmt::emphasis::italic, "Output:\n");
            print_array(R4300::state.dmem + 0x800, output_desc_size);
            fmt::print(fmt::emphasis::italic, "Expected:\n");
            print_array(test_output, output_desc_size);
            stats->total_failed++;
            continue;
        }

        fmt::print(fmt::fg(fmt::color::chartreuse), "PASS\n");
        stats->total_pass++;
    }

    delete output;
    return 0;
}

char const *rsp_test_suites[] = {
    "compelt",
    "lbv_sbv",
    "ldv_sdv",
    "lfv_sfv",
    "lhv_shv",
    "llv_slv",
    "lpv_spv",
    "lqv_sqv",
    "lrv_srv",
    "lsv_ssv",
    "ltv",
    "luv_suv",
    "memaccess",
    "mfc2",
    "mtc2",
    "stv",
    "swv",
    "vadd",
    "vaddc",
    "vch",
    "vcl",
    "vcr",
    "veq",
    "vge",
    "vlogical",
    "vlt",
    "vmacf",
    "vmacu",
    "vmadh",
    "vmadl",
    "vmadm",
    "vmadn",
    "vmrg",
    "vmudh",
    "vmudl",
    "vmudm",
    "vmudn",
    "vmulf",
    "vmulu",
    "vne",
    "vrcp",
    "vrcpl",
    "vrsq",
    "vsub",
    "vsubb",
    "vsubc",
    "vsucb",
};

int main(void) {
    struct test_statistics test_stats = {};
    unsigned nr_test_suites =
        sizeof(rsp_test_suites) / sizeof(rsp_test_suites[0]);
    bool stop_at_first_fail = true;
    for (unsigned nr = 0; nr < nr_test_suites; nr++) {
        run_test_suite(rsp_test_suites[nr], &test_stats);
        if (stop_at_first_fail && test_stats.total_failed)
            break;
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
