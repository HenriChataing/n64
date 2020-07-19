
#ifndef _RECOMPILER_PASSES_H_INCLUDED_
#define _RECOMPILER_PASSES_H_INCLUDED_

#include <stdbool.h>
#include <recompiler/ir.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Perform a type checking pass on a generated instruction tree.
 * @param entry     Entry point of a non-looping instruction graph.
 * @param instr     Pointer to a buffer where to write the faulty instruction
 *                  in case of typing failure.
 * @param errmsg    Buffer to render an error message into
 *                  in case of typing failure.
 * @param errmsg_len    Size in bytes of \p msg
 * @return          True iff the graph is well-typed.
 */
bool ir_typecheck(ir_instr_t const *entry, ir_instr_t const **errinstr,
                  char *errmsg, size_t errmsg_len);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _RECOMPILER_PASSES_H_INCLUDED_ */
