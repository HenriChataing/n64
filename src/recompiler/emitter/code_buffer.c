
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <recompiler/emitter/code_buffer.h>

code_buffer_t *alloc_code_buffer(size_t capacity) {
    /* Allocate page aligned code buffer. */
    code_buffer_t *buffer;
    void *ptr;
    int ret;
    long page_size;

    /* Allocate code buffer object. */
    buffer = malloc(sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }

    /* Allocate code buffer memory. */
    page_size = sysconf(_SC_PAGESIZE);
    ptr = aligned_alloc(page_size, capacity);
    if (ptr == NULL) {
        free(buffer);
        return NULL;
    }

    /* Change access permissions to code buffer to enable executing
     * code inside. */
    ret = mprotect(ptr, capacity, PROT_WRITE | PROT_READ | PROT_EXEC);
    if (ret < 0) {
        free(buffer);
        free(ptr);
        return NULL;
    }

    buffer->ptr = ptr;
    buffer->capacity = capacity;
    buffer->length = 0;
    return buffer;
}

void clear_code_buffer(code_buffer_t *emitter) {
    emitter->length = 0;
}

__attribute__((noreturn))
void fail_code_buffer(code_buffer_t *emitter) {
    longjmp(emitter->jmp_buf, -1);
}

void emit_u8(code_buffer_t *emitter, uint8_t b) {
    if (emitter->length >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = b;
}

void emit_u16_le(code_buffer_t *emitter, uint16_t w) {
    if ((emitter->length + 1) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = w;
    emitter->ptr[emitter->length++] = w >> 8;
}

void emit_u32_le(code_buffer_t *emitter, uint32_t d) {
    if ((emitter->length + 3) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = d;
    emitter->ptr[emitter->length++] = d >> 8;
    emitter->ptr[emitter->length++] = d >> 16;
    emitter->ptr[emitter->length++] = d >> 24;
}

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

void dump_code_buffer(FILE *fd, const code_buffer_t *emitter) {
    fprintf(fd, "   ");
    for (unsigned i = 0; i < emitter->length; i++) {
        if (i && (i % 16) == 0) fprintf(fd, "\n   ");
        fprintf(fd, " %02x", emitter->ptr[i]);
    }
    fprintf(fd, "\n");
}
