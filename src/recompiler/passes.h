
#ifndef _RECOMPILER_PASSES_H_INCLUDED_
#define _RECOMPILER_PASSES_H_INCLUDED_

#include <stdbool.h>
#include <recompiler/ir.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Perform a type checking pass on a generated instruction tree.
 * @param graph     Pointer to the instruction graph.
 * @param err_instr Pointer to a buffer where to write the faulty instruction
 *                  in case of typing failure.
 * @param err_msg   Buffer to render an error message into
 *                  in case of typing failure.
 * @param err_msg_len    Size in bytes of \p msg
 * @return          True iff the graph is well-typed.
 */
bool ir_typecheck(ir_graph_t const *graph, ir_instr_t const **err_instr,
                  char *err_msg, size_t err_msg_len);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _RECOMPILER_PASSES_H_INCLUDED_ */
