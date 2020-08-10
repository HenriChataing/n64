
#ifndef _RECOMPILER_MIPS_H_INCLUDED_
#define _RECOMPILER_MIPS_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum ir_mips_register {
    /* General purpose registers. */
    REG_ZERO,   REG_AT,     REG_V0,     REG_V1,
    REG_A0,     REG_A1,     REG_A2,     REG_A3,
    REG_T0,     REG_T1,     REG_T2,     REG_T3,
    REG_T4,     REG_T5,     REG_T6,     REG_T7,
    REG_S0,     REG_S1,     REG_S2,     REG_S3,
    REG_S4,     REG_S5,     REG_S6,     REG_S7,
    REG_T8,     REG_T9,     REG_K0,     REG_K1,
    REG_GP,     REG_SP,     REG_FP,     REG_RA,
    /* Special registers. */
    REG_PC,     REG_MULTHI, REG_MULTLO,
    REG_MAX,
} ir_mips_register_t;

/**
 * @brief Return the backend for the MIPS recompiler.
 */
ir_recompiler_backend_t *ir_mips_recompiler_backend(void);

/**
 * @brief Disassemble a memory segment, producing IR bytecode.
 *
 * Disassebles a block or more of MIPS instructions starting
 * from the program counter \p address, The instructions are read
 * from the memory region delimited by \p ptr, \p len.
 *
 * The disassembly stops under the following conditions:
 * - the target address falls outside the delimited memory region,
 * - the target instruction is one of : JR, JALR, ERET, i.e. instructions
 *   with variable, context dependant target addresses.
 */
ir_graph_t *ir_mips_disassemble(ir_recompiler_backend_t *backend,
                                uint64_t address, unsigned char *ptr, size_t len);

/**
 * @brief Compile the provide IR bytecode to x86_64 assembly.
 */
void ir_x86_64_assemble(ir_instr_t *ir, unsigned char *ptr, size_t len);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_MIPS_H_INCLUDED_ */
