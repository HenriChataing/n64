
#ifndef _RECOMPILER_EMITTER_CODE_BUFFER_H_INCLUDED_
#define _RECOMPILER_EMITTER_CODE_BUFFER_H_INCLUDED_

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

typedef struct code_buffer {
    unsigned char *ptr;
    size_t length;
    size_t capacity;
    jmp_buf jmp_buf;
} code_buffer_t;

/** Allocate a buffer of size len with read,write,execute
 * memory access rights. */
code_buffer_t *alloc_code_buffer(size_t capacity);

/**
 * Initialize the code emitter context. The function is used as exception
 * catch point for code emission failure. Hence, if the function returns
 * an error, it must be interpreted as a code emission memory shortage.
 *
 * @return 0 on success
 * @return -1 on code emission failure
 */
int reset_code_buffer(code_buffer_t *emitter);

/**
 * Raise an exception on the code emitter, jumping to the latest reset call.
 * Undefined if called before \ref reset_code_buffer.
 */
__attribute__((noreturn))
void fail_code_buffer(code_buffer_t *emitter);

void emit_u8(code_buffer_t *emitter, uint8_t b);
void emit_u16_le(code_buffer_t *emitter, uint16_t w);
void emit_u32_le(code_buffer_t *emitter, uint32_t d);
void emit_u64_le(code_buffer_t *emitter, uint64_t q);

#endif /* _RECOMPILER_EMITTER_CODE_BUFFER_H_INCLUDED_ */
