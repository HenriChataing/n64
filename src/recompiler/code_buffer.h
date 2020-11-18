
#ifndef _RECOMPILER_CODE_BUFFER_H_INCLUDED_
#define _RECOMPILER_CODE_BUFFER_H_INCLUDED_

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct code_buffer {
    unsigned char *ptr;
    size_t length;
    size_t capacity;
    jmp_buf jmp_buf;
} code_buffer_t;

typedef void (*code_entry_t)(void);

/** Allocate a buffer of size len with read,write,execute
 * memory access rights. */
code_buffer_t *alloc_code_buffer(size_t capacity);

/** Allocate an array of buffers each of size len with read,write,execute
 * memory access rights. */
code_buffer_t *alloc_code_buffer_array(size_t count, size_t capacity);

/** Destroy a code buffer. */
void free_code_buffer(code_buffer_t *emitter);

/** Destroy an array of code buffers. */
void free_code_buffer_array(code_buffer_t *emitters);

/** Clear the code buffer context. */
void clear_code_buffer(code_buffer_t *emitter);

/** Return the pointer to the buffer's current location. */
unsigned char *code_buffer_ptr(code_buffer_t *emitter);

/**
 * Initialize the code emitter context. The function is used as exception
 * catch point for code emission failure. Must be used as part of if()
 * condition, otherwise the behavior is undefined.
 * Evaluates to 0 on success, -1 on code emission failure.
 */
#define reset_code_buffer(emitter) setjmp(emitter->jmp_buf)

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
void emit_i8(code_buffer_t *emitter, int8_t b);
void emit_i16_le(code_buffer_t *emitter, int16_t w);
void emit_i32_le(code_buffer_t *emitter, int32_t d);
void emit_i64_le(code_buffer_t *emitter, int64_t q);

/**
 * Print the contents of the code buffer in hexadecimal format.
 * @param fd        Output stream
 * @param emitter   Code buffer to dump
 */
void dump_code_buffer(FILE *fd, const code_buffer_t *emitter);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_CODE_BUFFER_H_INCLUDED_ */
