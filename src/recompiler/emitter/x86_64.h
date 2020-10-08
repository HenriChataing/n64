
#ifndef _RECOMPILER_EMITTER_X86_64_H_INCLUDED_
#define _RECOMPILER_EMITTER_X86_64_H_INCLUDED_

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

#include <recompiler/emitter/code_buffer.h>

/**
 * @brief Compile an IR program to x86_64 binary.
 *
 * Returns the start pointer of the compiled code. The memory allocated for the
 * binary code is consumed from the emitter.
 */
void *ir_x86_64_assemble(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_graph_t const *graph);

#endif /* _RECOMPILER_EMITTER_X86_64_H_INCLUDED_ */
