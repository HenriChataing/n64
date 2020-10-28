
#ifndef _RECOMPILER_TARGET_MIPS_H_INCLUDED_
#define _RECOMPILER_TARGET_MIPS_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Return the backend for the MIPS recompiler.
 */
recompiler_backend_t *ir_mips_recompiler_backend(void);

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
ir_graph_t *ir_mips_disassemble(recompiler_backend_t *backend,
                                uint64_t address, unsigned char *ptr, size_t len);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_TARGET_MIPS_H_INCLUDED_ */
