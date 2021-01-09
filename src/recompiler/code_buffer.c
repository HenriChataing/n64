
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <recompiler/code_buffer.h>

code_buffer_t *alloc_code_buffer_array(size_t count, size_t capacity) {
    /* Allocate page aligned code buffer. */
    long page_size;
    code_buffer_t *buffers;
    unsigned char *ptr;
    int ret;

    /* Allocate code buffer objects. */
    buffers = malloc(count * sizeof(*buffers));
    if (buffers == NULL) {
        return NULL;
    }

    /* Allocate code buffer memory. */
    page_size = sysconf(_SC_PAGESIZE);
    capacity = ((capacity + page_size - 1) / page_size) * page_size;
    ptr = aligned_alloc(page_size, count * capacity);
    if (ptr == NULL) {
        free(buffers);
        return NULL;
    }

    /* Change access permissions to code buffer to enable executing
     * code inside. */
    ret = mprotect(ptr, count * capacity, PROT_WRITE | PROT_READ | PROT_EXEC);
    if (ret < 0) {
        free(buffers);
        free(ptr);
        return NULL;
    }

    /* Initialize the code buffers and return. */
    for (unsigned nr = 0; nr < count; nr++) {
        buffers[nr].ptr = ptr + (nr * capacity);
        buffers[nr].capacity = capacity;
        buffers[nr].length = 0;
    }
    return buffers;
}

code_buffer_t *alloc_code_buffer(size_t capacity) {
    return alloc_code_buffer_array(1, capacity);
}

void free_code_buffer(code_buffer_t *emitter) {
    free_code_buffer_array(emitter);
}

void free_code_buffer_array(code_buffer_t *emitters) {
    if (emitters == NULL) {
        return;
    }

    free(emitters->ptr);
    free(emitters);
}

void clear_code_buffer(code_buffer_t *emitter) {
    emitter->length = 0;
}

unsigned char *code_buffer_ptr(code_buffer_t *emitter) {
    return emitter->ptr + emitter->length;
}

__attribute__((noreturn))
void fail_code_buffer(code_buffer_t *emitter) {
    longjmp(emitter->jmp_buf, -1);
}

void dump_code_buffer(FILE *fd, const code_buffer_t *emitter) {
    fprintf(fd, "   ");
    for (unsigned i = 0; i < emitter->length; i++) {
        if (i && (i % 16) == 0) fprintf(fd, "\n   ");
        fprintf(fd, " %02x", emitter->ptr[i]);
    }
    fprintf(fd, "\n");
}

/**
 * @brief Append \p b to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param b             Byte to append.
 */
void emit_u8(code_buffer_t *emitter, uint8_t b) {
    if (emitter->length >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = b;
}

/**
 * @brief Append \p w to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param w             Word to append.
 */
void emit_u16_le(code_buffer_t *emitter, uint16_t w) {
    if ((emitter->length + 1) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = w;
    emitter->ptr[emitter->length++] = w >> 8;
}

/**
 * @brief Append \p d to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param d             Double word to append.
 */
void emit_u32_le(code_buffer_t *emitter, uint32_t d) {
    if ((emitter->length + 3) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = d;
    emitter->ptr[emitter->length++] = d >> 8;
    emitter->ptr[emitter->length++] = d >> 16;
    emitter->ptr[emitter->length++] = d >> 24;
}

/**
 * @brief Append \p q to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param q             Quad word to append.
 */
void emit_u64_le(code_buffer_t *emitter, uint64_t q) {
    if ((emitter->length + 8) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = q;
    emitter->ptr[emitter->length++] = q >> 8;
    emitter->ptr[emitter->length++] = q >> 16;
    emitter->ptr[emitter->length++] = q >> 24;
    emitter->ptr[emitter->length++] = q >> 32;
    emitter->ptr[emitter->length++] = q >> 40;
    emitter->ptr[emitter->length++] = q >> 48;
    emitter->ptr[emitter->length++] = q >> 56;
}

/**
 * @brief Append \p b to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param b             Byte to append.
 */
void emit_i8(code_buffer_t *emitter, int8_t b) {
    emit_u8(emitter, (uint8_t)b);
}

/**
 * @brief Append \p w to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param w             Word to append.
 */
void emit_i16_le(code_buffer_t *emitter, int16_t w) {
    emit_u16_le(emitter, (uint16_t)w);
}

/**
 * @brief Append \p d to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param d             Double word to append.
 */
void emit_i32_le(code_buffer_t *emitter, int32_t d) {
    emit_u32_le(emitter, (uint32_t)d);
}

/**
 * @brief Append \p q to the code buffer \p emitter.
 * @param emitter       Pointer to a code buffer object.
 * @param q             Quad word to append.
 */
void emit_i64_le(code_buffer_t *emitter, int64_t q) {
    emit_u64_le(emitter, (uint64_t)q);
}
