
#ifndef _RECOMPILER_EMITTER_X86_64_H_INCLUDED_
#define _RECOMPILER_EMITTER_X86_64_H_INCLUDED_

#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

enum x86_64_register {
    /* 8bit registers */
    AL = 0,
    CL = 1,
    DL = 2,
    BL = 3,
    AH = 4,
    CH = 5,
    DH = 6,
    BH = 7,

    /* 16bit registers */
    AX = 0,
    CX = 1,
    DX = 2,
    BX = 3,
    SP = 4,
    BP = 5,
    SI = 6,
    DI = 7,

    /* 32bit registers */
    EAX = 0,
    ECX = 1,
    EDX = 2,
    EBX = 3,
    ESP = 4,
    EBP = 5,
    ESI = 6,
    EDI = 7,

    /* 64bit registers */
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15,
};

typedef struct x86_64_emitter {
    unsigned char *ptr;
    size_t length;
    size_t capacity;
    jmp_buf jmp_buf;
} x86_64_emitter_t;

typedef struct x86_64_mem {
    unsigned mode;
    unsigned rm;
    unsigned base;
    unsigned index;
    unsigned scale;
    int32_t disp;
} x86_64_mem_t;

typedef enum x86_64_mode {
    INDIRECT,
    INDIRECT_DISP8,
    INDIRECT_DISP32,
    DIRECT,
} x86_64_mode_t;


/**
 * Initialize the code emitter context. The function is used as exception
 * catch point for code emission failure. Hence, if the function returns
 * an error, it must be interpreted as a code emission memory shortage.
 *
 * @return 0 on success
 * @return -1 on code emission failure
 */
int x86_64_emitter_start(x86_64_emitter_t *emitter,
    unsigned char *ptr, size_t capacity);

static inline x86_64_mem_t mem_indirect(unsigned base) {
    return (x86_64_mem_t) { .mode = INDIRECT, .rm = base, };
}

static inline x86_64_mem_t mem_indirect_disp(unsigned base, int32_t disp) {
    return (x86_64_mem_t) {
        .mode = disp < INT8_MIN || disp > INT8_MAX ?
            INDIRECT_DISP32 : INDIRECT_DISP8,
        .rm = base, .disp = disp };
}

static inline x86_64_mem_t mem_indirect_scaled(
    unsigned base, unsigned index, unsigned scale) {
    return (x86_64_mem_t) { .mode = INDIRECT, .rm = 4,
        .base = base, .index = index, .scale = scale };
}

static inline x86_64_mem_t mem_indirect_scaled_disp(
    unsigned base, unsigned index, unsigned scale, int32_t disp) {
    return (x86_64_mem_t) {
        .mode = disp < INT8_MIN || disp > INT8_MAX ?
            INDIRECT_DISP32 : INDIRECT_DISP8,
        .rm = 4, .base = base, .index = index, .scale = scale };
}

void emit_u8(x86_64_emitter_t *emitter, uint8_t b);
void emit_u16_le(x86_64_emitter_t *emitter, uint16_t w);
void emit_u32_le(x86_64_emitter_t *emitter, uint32_t d);
void emit_u64_le(x86_64_emitter_t *emitter, uint64_t q);

void emit_add_al_imm8(x86_64_emitter_t *emitter, int8_t imm8);
void emit_add_eax_imm32(x86_64_emitter_t *emitter, int32_t imm32);
void emit_add_rax_imm32(x86_64_emitter_t *emitter, int32_t imm32);
void emit_add_r8_imm8(x86_64_emitter_t *emitter, unsigned r8, int8_t imm8);
void emit_add_m8_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m8, int8_t imm8);
void emit_add_r32_imm32(x86_64_emitter_t *emitter, unsigned r32, int32_t imm32);
void emit_add_m32_imm32(x86_64_emitter_t *emitter, x86_64_mem_t m32, int32_t imm32);
void emit_add_r64_imm32(x86_64_emitter_t *emitter, unsigned r64, int32_t imm32);
void emit_add_m64_imm32(x86_64_emitter_t *emitter, x86_64_mem_t m64, int32_t imm32);
void emit_add_r32_imm8(x86_64_emitter_t *emitter, unsigned r32, int8_t imm8);
void emit_add_m32_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m32, int8_t imm8);
void emit_add_r64_imm8(x86_64_emitter_t *emitter, unsigned r64, int8_t imm8);
void emit_add_m64_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m64, int8_t imm8);
void emit_add_r8_r8(x86_64_emitter_t *emitter, unsigned dr8, unsigned sr8);
void emit_add_m8_r8(x86_64_emitter_t *emitter, x86_64_mem_t m8, unsigned r8);
void emit_add_r8_m8(x86_64_emitter_t *emitter, unsigned r8, x86_64_mem_t m8);
void emit_add_r32_r32(x86_64_emitter_t *emitter, unsigned dr32, unsigned sr32);
void emit_add_m32_r32(x86_64_emitter_t *emitter, x86_64_mem_t m32, unsigned r32);
void emit_add_r32_m32(x86_64_emitter_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_add_r64_r64(x86_64_emitter_t *emitter, unsigned dr64, unsigned sr64);
void emit_add_m64_r64(x86_64_emitter_t *emitter, x86_64_mem_t m64, unsigned r64);
void emit_add_r64_m64(x86_64_emitter_t *emitter, unsigned r64, x86_64_mem_t m64);

#endif /* _RECOMPILER_EMITTER_X86_64_H_INCLUDED_ */
