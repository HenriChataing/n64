
#ifndef _RECOMPILER_EMITTER_X86_64_H_INCLUDED_
#define _RECOMPILER_EMITTER_X86_64_H_INCLUDED_

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

#include <recompiler/backend.h>
#include <recompiler/code_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef void (*x86_64_func_t)(void);

/**
 * @brief Compile an IR program to x86_64 binary.
 *
 * Returns the start pointer of the compiled code. The memory allocated for the
 * binary code is consumed from the emitter.
 */
x86_64_func_t ir_x86_64_assemble(ir_recompiler_backend_t const *backend,
                                 code_buffer_t *emitter,
                                 ir_graph_t const *graph);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_EMITTER_X86_64_H_INCLUDED_ */
