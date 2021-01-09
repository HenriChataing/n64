
#ifndef _RECOMPILER_CODE_BUFFER_H_INCLUDED_
#define _RECOMPILER_CODE_BUFFER_H_INCLUDED_

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @struct code_buffer
 * @brief Implement a writable, exectuable buffer.
 *
 * @var code_buffer::ptr
 * @brief Pointer to the allocated buffer memory.
 * @details The memory is page aligned, and modified to have
 *      executable rights.
 *
 * @var code_buffer::length
 * @brief Number of used bytes.
 *
 * @var code_buffer::capacity
 * @brief Capacity of the buffer in bytes.
 *
 * @var code_buffer::jmp_buf
 * @brief Jump buffer for exception handling.
 * @details This jump buffer is used by \ref catch_code_buffer_error()
 *    to catch generation errors raised with \ref fail_code_buffer().
 */
typedef struct code_buffer {
    unsigned char *ptr;
    size_t length;
    size_t capacity;
    jmp_buf jmp_buf;
} code_buffer_t;

/** @brief Type of code buffer entry points. */
typedef void (*code_entry_t)(void);

/**
 * @brief Allocate a buffer of size \p capacity with read,write,execute
 *  memory access rights.
 * @param capacity
 *      Required buffer capacity. The value is rounded up to the
 *      host page size.
 * @return
 *      Pointer to the allocated code buffer object,
 *      or NULL on failure.
 */
code_buffer_t *alloc_code_buffer(size_t capacity);

/** Allocate an array of buffers each of size len with read,write,execute
 * memory access rights. */

/**
 * @brief Allocate an array of buffers each of size \p capacity with
 *  read,write,execute memory access rights.
 * @param count
 *      Required buffer count.
 * @param capacity
 *      Required buffer capacity. The value is rounded up to the
 *      host page size.
 * @return
 *      Pointer to the allocated array of code buffer objects,
 *      or NULL on failure.
 */
code_buffer_t *alloc_code_buffer_array(size_t count, size_t capacity);

/**
 * @brief Destroy a code buffer.
 * @param emitter       Pointer to the code buffer object to free.
 */
void free_code_buffer(code_buffer_t *emitter);

/**
 * @brief Destroy an array of code buffers.
 * @param emitters      Pointer to the code buffer array to free.
 */
void free_code_buffer_array(code_buffer_t *emitters);

/**
 * @brief Clear the code buffer context.
 * @details Resets the internal cursor. It is undefined to execute code
 *  previously allocated from this buffer after \ref clear_code_buffer()
 *  is called.
 * @param emitter       Pointer to the code buffer object.
 */
void clear_code_buffer(code_buffer_t *emitter);

/**
 * @brief Return the pointer to the buffer's current location.
 * @details Can be used to take the address of a code block before starting
 *  the generation.
 * @param emitter       Pointer to the code buffer object.
 */
unsigned char *code_buffer_ptr(code_buffer_t *emitter);

/**
 * @brief Catch code generation errors.
 *
 * Code generation errors may be raised on buffer overflow, or
 * for invalid instruction parameters.
 *
 * Sets a long jump point for catching code generation errors.
 * Must be used as part of if() condition, otherwise the behavior is undefined.
 * Evaluates to `0` on success, `-1` when an error was raised.
 */
#define catch_code_buffer_error(emitter) setjmp(emitter->jmp_buf)

/**
 * @brief Raise an exception on the code emitter,
 * @details Jumps to the latest call to \ref catch_code_buffer_error().
 * Undefined if called before \ref catch_code_buffer_error().
 */
__attribute__((noreturn))
void fail_code_buffer(code_buffer_t *emitter);

/**
 * @brief Print the contents of the code buffer in hexadecimal format.
 * @param fd        Output stream.
 * @param emitter   Pointer to the code buffer object to dump.
 */
void dump_code_buffer(FILE *fd, code_buffer_t const *emitter);

void emit_u8(code_buffer_t *emitter, uint8_t b);
void emit_u16_le(code_buffer_t *emitter, uint16_t w);
void emit_u32_le(code_buffer_t *emitter, uint32_t d);
void emit_u64_le(code_buffer_t *emitter, uint64_t q);
void emit_i8(code_buffer_t *emitter, int8_t b);
void emit_i16_le(code_buffer_t *emitter, int16_t w);
void emit_i32_le(code_buffer_t *emitter, int32_t d);
void emit_i64_le(code_buffer_t *emitter, int64_t q);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_CODE_BUFFER_H_INCLUDED_ */
