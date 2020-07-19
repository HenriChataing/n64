#include <cstdlib>
#include <iostream>
#include <ctime>

#include <recompiler/ir.h>
#include <recompiler/passes.h>
#include <recompiler/target/mips.h>
#include <mips/asm.h>
#include <fmt/format.h>

#include "recompiler/test_blocks.h"

enum {
    ALL,
    RANDOM,
    SELECTED,
};

void print_input_info(ir_block_t const *block, unsigned index) {
    fmt::print("------------- input #{:<8} -------------\n", index);
    fmt::print("start: {:016x}\n", block->address);
    fmt::print("end: {:016x}\n", block->address + block->len);
}

void print_raw_disassembly(ir_block_t const *block) {
    fmt::print("------------- raw disassembly -------------\n");

    for (unsigned i = 0; (i + 4) <= block->len; i+=4) {
        uint32_t instr =
            ((uint32_t)block->ptr[i] << 24) |
            ((uint32_t)block->ptr[i + 1] << 16) |
            ((uint32_t)block->ptr[i + 2] << 8) |
            ((uint32_t)block->ptr[i + 3] << 0);
        fmt::print(Mips::CPU::disas(block->address + i, instr));
        fmt::print("\n");
    }
}

void print_ir_disassembly(ir_instr_t const *entry) {
    fmt::print("------------- ir disassembly --------------\n");

    ir_instr_t const *instr;
    char line[128];

    for (instr = entry; instr != NULL; instr = instr->next) {
        ir_print_instr(line, sizeof(line), instr);
        puts(line);
    }
}

bool print_typecheck(ir_instr_t const *entry, bool log_success) {

    ir_instr_t const *instr;
    char line[128];
    bool success = ir_typecheck(entry, &instr, line, sizeof(line));

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

    if (mode == RANDOM) {
        selected = std::rand() % ir_mips_recompiler_tests.count;
    }

    if (mode == ALL) {
        unsigned index = 0;
        for (; index < ir_mips_recompiler_tests.count; index++) {
            ir_block_t const *block = &ir_mips_recompiler_tests.blocks[index];
            ir_instr_t *entry =
                ir_mips_disassemble(block->address, block->ptr, block->len);

            if (!print_typecheck(entry, false)) {
                print_input_info(block, index);
                print_raw_disassembly(block);
                print_ir_disassembly(entry);
                break;
            }
        }
        fmt::print("{} tests passed\n", index);
    } else {
        ir_block_t const *block = &ir_mips_recompiler_tests.blocks[selected];
        ir_instr_t *entry =
            ir_mips_disassemble(block->address, block->ptr, block->len);

        print_input_info(block, selected);
        print_typecheck(entry, true);
        print_raw_disassembly(block);
        print_ir_disassembly(entry);
    }

    return 0;
}
