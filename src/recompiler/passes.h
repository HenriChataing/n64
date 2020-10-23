
#ifndef _RECOMPILER_PASSES_H_INCLUDED_
#define _RECOMPILER_PASSES_H_INCLUDED_

#include <stdbool.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Perform a type checking pass on a generated instruction graph.
 * @param backend
 *      Pointer to the recompiler backend used to generate
 *      the instruction graph.
 * @param graph     Pointer to the instruction graph.
 * @return          True if the graph is well-typed, false otherwise.
 */
bool ir_typecheck(ir_recompiler_backend_t *backend,
                  ir_graph_t const *graph);

/**
 * Optimize an instruction graph.
 * @param backend   Pointer to the recompiler backend.
 * @param block     Pointer to the graph to optimize.
 */
void ir_optimize(ir_recompiler_backend_t *backend,
                 ir_graph_t *graph);

/**
 * @brief Execute the generated instruction graph.
 * The initial state is assumed to have been previously loaded
 * into the global variables.
 * @param backend
 *      Pointer to the recompiler backend used to generate
 *      the instruction graph.
 * @param graph     Pointer to the instruction graph.
 * @return
 *      True if the execution completed successfully, false otherwise.
 */
bool ir_run(ir_recompiler_backend_t *backend,
            ir_graph_t const *graph);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_PASSES_H_INCLUDED_ */
