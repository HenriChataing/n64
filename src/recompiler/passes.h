
#ifndef _RECOMPILER_PASSES_H_INCLUDED_
#define _RECOMPILER_PASSES_H_INCLUDED_

#include <stdbool.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>

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

/**
 * Optimize an instruction graph.
 * @param backend   Pointer to the recompiler backend.
 * @param block     Pointer to the graph to optimize.
 */
void ir_optimize(ir_recompiler_backend_t *backend,
                 ir_graph_t *graph);

/**
 * Execute the generated instruction graph. The initial state is assumed to have
 * been previously loaded.
 * @param backend   Pointer to the recompiler backend.
 * @param graph     Pointer to the instruction graph.
 * @param err_instr Pointer to a buffer where to write the faulty instruction
 *                  in case of run failure.
 * @param err_msg   Buffer to render an error message into
 *                  in case of run failure.
 * @param err_msg_len    Size in bytes of \p msg
 * @return          True iff the graph is well-typed.
 */
bool ir_run(ir_recompiler_backend_t const *backend,
            ir_graph_t const *graph,
            ir_instr_t const **err_instr,
            char *err_msg, size_t err_msg_len);

void ir_run_vars(ir_const_t const **vars, unsigned *nr_vars);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _RECOMPILER_PASSES_H_INCLUDED_ */
