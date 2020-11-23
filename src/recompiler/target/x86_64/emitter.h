
#ifndef _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_
#define _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

#include <recompiler/code_buffer.h>

enum x86_64_register {
    /* 8bit registers */
    AL = 0,     CL = 1,     DL = 2,     BL = 3,
    AH = 4,     CH = 5,     DH = 6,     BH = 7,
    R8b = 8,    R9b = 9,    R10b = 10,  R11b = 11,
    R12b = 12,  R13b = 13,  R14b = 14,  R15b = 15,

    /* 16bit registers */
    AX = 0,     CX = 1,     DX = 2,     BX = 3,
    SP = 4,     BP = 5,     SI = 6,     DI = 7,
    R8w = 8,    R9w = 9,    R10w = 10,  R11w = 11,
    R12w = 12,  R13w = 13,  R14w = 14,  R15w = 15,

    /* 32bit registers */
    EAX = 0,    ECX = 1,    EDX = 2,    EBX = 3,
    ESP = 4,    EBP = 5,    ESI = 6,    EDI = 7,
    R8d = 8,    R9d = 9,    R10d = 10,  R11d = 11,
    R12d = 12,  R13d = 13,  R14d = 14,  R15d = 15,

    /* 64bit registers */
    RAX = 0,    RCX = 1,    RDX = 2,    RBX = 3,
    RSP = 4,    RBP = 5,    RSI = 6,    RDI = 7,
    R8 = 8,     R9 = 9,     R10 = 10,   R11 = 11,
    R12 = 12,   R13 = 13,   R14 = 14,   R15 = 15,
};

typedef enum x86_64_mode {
    INDIRECT,
    INDIRECT_DISP8,
    INDIRECT_DISP32,
    DIRECT,
} x86_64_mode_t;

typedef struct x86_64_mem {
    unsigned mode;
    unsigned rm;
    unsigned base;
    unsigned index;
    unsigned scale;
    int32_t disp;
} x86_64_mem_t;

static inline x86_64_mem_t mem_direct(unsigned reg) {
    return (x86_64_mem_t) { .mode = DIRECT, .rm = reg, };
}

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

/** Patch a previously generated jump relative offset to point to the correct
 * address. The call fails if the target cannot be reached. */
void patch_jmp_rel32(code_buffer_t *emitter, unsigned char *rel32,
                     unsigned char *target);

void emit_add_al_imm8(code_buffer_t *emitter, int8_t imm8);
void emit_add_eax_imm32(code_buffer_t *emitter, int32_t imm32);
void emit_add_rax_imm32(code_buffer_t *emitter, int32_t imm32);
void emit_add_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8);
void emit_add_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8);
void emit_add_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32);
void emit_add_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32);
void emit_add_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32);
void emit_add_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32);
void emit_add_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8);
void emit_add_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8);
void emit_add_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8);
void emit_add_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8);
void emit_add_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8);
void emit_add_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8);
void emit_add_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8);
void emit_add_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16);
void emit_add_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16);
void emit_add_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16);
void emit_add_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32);
void emit_add_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32);
void emit_add_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_add_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);
void emit_add_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64);
void emit_add_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64);

void emit_and_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);

void emit_call_r64(code_buffer_t *emitter, unsigned r64);
unsigned char *emit_call_rel32(code_buffer_t *emitter, int32_t rel32);
void emit_call(code_buffer_t *emitter, void *ptr, unsigned r64);

void emit_cmp_al_imm8(code_buffer_t *emitter, int8_t imm8);
void emit_cmp_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8);
void emit_cmp_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8);
void emit_cmp_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32);
void emit_cmp_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32);
void emit_cmp_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32);
void emit_cmp_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32);
void emit_cmp_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8);
void emit_cmp_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8);
void emit_cmp_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8);
void emit_cmp_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8);
void emit_cmp_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8);
void emit_cmp_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8);
void emit_cmp_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8);
void emit_cmp_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16);
void emit_cmp_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16);
void emit_cmp_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16);
void emit_cmp_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32);
void emit_cmp_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32);
void emit_cmp_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_cmp_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);
void emit_cmp_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64);
void emit_cmp_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64);

void emit_cbw(code_buffer_t *emitter);
void emit_cwd(code_buffer_t *emitter);
void emit_cwde(code_buffer_t *emitter);
void emit_cdq(code_buffer_t *emitter);
void emit_cdqe(code_buffer_t *emitter);
void emit_cqo(code_buffer_t *emitter);

void emit_div_ax_r8(code_buffer_t *emitter, unsigned r8);
void emit_div_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_div_dx_ax_r16(code_buffer_t *emitter, unsigned r16);
void emit_div_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_div_edx_eax_r32(code_buffer_t *emitter, unsigned r32);
void emit_div_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_div_rdx_rax_r64(code_buffer_t *emitter, unsigned r64);
void emit_div_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64);

void emit_idiv_ax_r8(code_buffer_t *emitter, unsigned r8);
void emit_idiv_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_idiv_dx_ax_r16(code_buffer_t *emitter, unsigned r16);
void emit_idiv_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_idiv_edx_eax_r32(code_buffer_t *emitter, unsigned r32);
void emit_idiv_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_idiv_rdx_rax_r64(code_buffer_t *emitter, unsigned r64);
void emit_idiv_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64);

void emit_imul_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16);
void emit_imul_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16);
void emit_imul_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32);
void emit_imul_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_imul_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);
void emit_imul_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64);
void emit_imul_rN_rN(code_buffer_t *emitter, unsigned width, unsigned drN, unsigned srN);

unsigned char *emit_jmp_rel32(code_buffer_t *emitter, int32_t rel32);
unsigned char *emit_je_rel32(code_buffer_t *emitter, int32_t rel32);
unsigned char *emit_jne_rel32(code_buffer_t *emitter, int32_t rel32);

void emit_lea_r64_m(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m);

void emit_mov_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8);
void emit_mov_r16_imm16(code_buffer_t *emitter, unsigned r16, int16_t imm16);
void emit_mov_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32);
void emit_mov_r64_imm64(code_buffer_t *emitter, unsigned r64, int64_t imm64);
void emit_mov_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8);
void emit_mov_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16);
void emit_mov_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_mov_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64);
void emit_mov_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8);
void emit_mov_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16);
void emit_mov_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32);
void emit_mov_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64);
void emit_mov_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);
void emit_mov_rN_mN(code_buffer_t *emitter, unsigned width, unsigned rN, x86_64_mem_t mN);
void emit_mov_mN_rN(code_buffer_t *emitter, unsigned width, x86_64_mem_t mN, unsigned rN);

void emit_not_r8(code_buffer_t *emitter, unsigned r8);
void emit_not_r16(code_buffer_t *emitter, unsigned r16);
void emit_not_r32(code_buffer_t *emitter, unsigned r32);
void emit_not_r64(code_buffer_t *emitter, unsigned r64);

void emit_or_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32);
void emit_or_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);

void emit_pop_r64(code_buffer_t *emitter, unsigned r64);

void emit_push_r64(code_buffer_t *emitter, unsigned r64);

void emit_ret(code_buffer_t *emitter);

void emit_sete_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setne_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_seta_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setae_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setb_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setbe_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setg_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setge_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setl_m8(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_setle_m8(code_buffer_t *emitter, x86_64_mem_t m8);

void emit_shl_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8);
void emit_shl_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8);
void emit_shl_r8_cl(code_buffer_t *emitter, unsigned r8);
void emit_shl_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_shl_r16_cl(code_buffer_t *emitter, unsigned r16);
void emit_shl_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_shl_r32_cl(code_buffer_t *emitter, unsigned r32);
void emit_shl_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_shl_r64_cl(code_buffer_t *emitter, unsigned r64);
void emit_shl_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64);
void emit_shl_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN);

void emit_shr_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8);
void emit_shr_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8);
void emit_shr_r8_cl(code_buffer_t *emitter, unsigned r8);
void emit_shr_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_shr_r16_cl(code_buffer_t *emitter, unsigned r16);
void emit_shr_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_shr_r32_cl(code_buffer_t *emitter, unsigned r32);
void emit_shr_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_shr_r64_cl(code_buffer_t *emitter, unsigned r64);
void emit_shr_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64);
void emit_shr_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN);

void emit_sra_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8);
void emit_sra_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8);
void emit_sra_r8_cl(code_buffer_t *emitter, unsigned r8);
void emit_sra_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_sra_r16_cl(code_buffer_t *emitter, unsigned r16);
void emit_sra_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_sra_r32_cl(code_buffer_t *emitter, unsigned r32);
void emit_sra_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_sra_r64_cl(code_buffer_t *emitter, unsigned r64);
void emit_sra_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64);
void emit_sra_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN);

void emit_sub_al_imm8(code_buffer_t *emitter, int8_t imm8);
void emit_sub_eax_imm32(code_buffer_t *emitter, int32_t imm32);
void emit_sub_rax_imm32(code_buffer_t *emitter, int32_t imm32);
void emit_sub_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8);
void emit_sub_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8);
void emit_sub_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32);
void emit_sub_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32);
void emit_sub_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32);
void emit_sub_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32);
void emit_sub_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8);
void emit_sub_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8);
void emit_sub_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8);
void emit_sub_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8);
void emit_sub_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8);
void emit_sub_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8);
void emit_sub_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8);
void emit_sub_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16);
void emit_sub_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16);
void emit_sub_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16);
void emit_sub_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32);
void emit_sub_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32);
void emit_sub_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32);
void emit_sub_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);
void emit_sub_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64);
void emit_sub_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64);

void emit_test_al_imm8(code_buffer_t *emitter, int8_t imm8);
void emit_test_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8);
void emit_test_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8);

void emit_xor_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);


typedef enum x86_64_operand_kind {
    MEMORY,
    REGISTER,
    IMMEDIATE,
} x86_64_operand_kind_t;

typedef struct x86_64_operand {
    x86_64_operand_kind_t kind;
    unsigned size;
    union {
        x86_64_mem_t mem;
        unsigned reg;
        int64_t imm;
    };
} x86_64_operand_t;

static inline x86_64_operand_t op_mem_indirect(unsigned size, unsigned base) {
    return (x86_64_operand_t) {
        .mem = mem_indirect(base), .kind = MEMORY, .size = size, };
}

static inline x86_64_operand_t op_mem_indirect_disp(
    unsigned size, unsigned base, int32_t disp) {
    return (x86_64_operand_t) {
        .mem = mem_indirect_disp(base, disp), .kind = MEMORY,
        .size = size, };
}

static inline x86_64_operand_t op_mem_indirect_scaled(
    unsigned size, unsigned base, unsigned index, unsigned scale) {
    return (x86_64_operand_t) {
        .mem = mem_indirect_scaled(base, index, scale),
        .kind = MEMORY, .size = size, };
}

static inline x86_64_operand_t op_mem_indirect_scaled_disp(
    unsigned size, unsigned base, unsigned index, unsigned scale, int32_t disp) {
    return (x86_64_operand_t) {
        .mem = mem_indirect_scaled_disp(base, index, scale, disp),
        .kind = MEMORY, .size = size, };
}

static inline x86_64_operand_t op_reg(unsigned size, unsigned reg) {
    return (x86_64_operand_t) {
        .reg = reg, .kind = REGISTER, .size = size, };
}

static inline x86_64_operand_t op_imm(unsigned size, int64_t imm) {
    return (x86_64_operand_t) {
        .imm = imm, .kind = IMMEDIATE, .size = size, };
}

/**
 * Binary instruction group, strict implementation.
 * The operands op0 and op1 must have the same operand size.
 * The operands op0 and op1 must not be both memory operands.
 * The operand op0 must not be an immediate operand.
 * If the operand op1 is a 64 bit immediate operand, it must not be larger
 * than a signed 32 bit value.
 */

void emit_add_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_adc_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_and_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_xor_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_or_op0_op1(code_buffer_t *emitter,
                     x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_sbb_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_sub_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_cmp_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);

/**
 * Unary instruction group, strict implementation.
 * The operand op0 must not be an immediate operand.
 */

void emit_not_op0(code_buffer_t *emitter, x86_64_operand_t *op0);
void emit_neg_op0(code_buffer_t *emitter, x86_64_operand_t *op0);

/**
 * Shift instruction group, strict implementation.
 * The operand op1 must have operand size 8.
 * The operand op1 must be either an immediate or the register CL.
 * The operand op0 must not be an immediate operand.
 */

void emit_rol_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_ror_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_rcl_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_rcr_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_shl_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_shr_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
void emit_sra_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);

/**
 * Misc instruction group, strict implementation.
 */
void emit_mov_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1);
// void emit_test_op0_op1(code_buffer_t *emitter,
//                        x86_64_operand_t *op0, x86_64_operand_t *op1);
// void emit_sete_op0(code_buffer_t *emitter,
//                    x86_64_operand_t *op0);
// void emit_setne_op0(code_buffer_t *emitter,
//                     x86_64_operand_t *op0);
// void emit_seta_op0(code_buffer_t *emitter,
//                    x86_64_operand_t *op0);
// void emit_setae_op0(code_buffer_t *emitter,
//                     x86_64_operand_t *op0);
// void emit_setb_op0(code_buffer_t *emitter,
//                    x86_64_operand_t *op0);
// void emit_setbe_op0(code_buffer_t *emitter,
//                     x86_64_operand_t *op0);
// void emit_setg_op0(code_buffer_t *emitter,
//                    x86_64_operand_t *op0);
// void emit_setge_op0(code_buffer_t *emitter,
//                     x86_64_operand_t *op0);
// void emit_setl_op0(code_buffer_t *emitter,
//                    x86_64_operand_t *op0);
// void emit_setle_op0(code_buffer_t *emitter,
//                     x86_64_operand_t *op0);

/**
 * Binary instruction group, generalized implementation.
 * The operands dst, op0 and op1 must have the same operand size.
 * The operands op0 and op1 may be both memory operands.
 * The operand dst must not be an immediate operand.
 * If the operand op1 is a 64 bit immediate operand, it may be larger
 * than a signed 32 bit value.
 *
 * The following table describes the generated code depending on the
 * operand types. The operand type combinations not listed here are
 * considered invalid. The 'result' column has one mode for the case where
 * \ref dst and \ref op0 are different, and one case where they are identical.
 *
 *  - MODE_0: binop op0,op1
 *  - MODE_1: mov dst,op0 / binop dst,op1
 *  - MODE_2: mov r,op0 / binop r,op1 / mov dst,r
 *  - MODE_3: mov r0,op0 / mov r1,op1 / binop r0,r1 / mov dst,r0
 *
 *  +-----+-----+-----+-----------------+
 *  | dst | op0 | op1 | result          |
 *  +-----+-----+-----+-----------------+
 *  | mem | mem | mem | MODE_2 / MODE_2 |
 *  | mem | mem | reg | MODE_2 / MODE_0 |
 *  | mem | mem | imm | MODE_2 / MODE_0 |
 *  | mem | reg | mem | MODE_2 / NA     |
 *  | mem | reg | reg | MODE_1 / NA     |
 *  | mem | reg | imm | MODE_1 / NA     |
 *  | reg | mem | mem | MODE_1 / NA     |
 *  | reg | mem | reg | MODE_1 / NA     |
 *  | reg | mem | imm | MODE_1 / NA     |
 *  | reg | reg | mem | MODE_1 / MODE_0 |
 *  | reg | reg | reg | MODE_1 / MODE_0 |
 *  | reg | reg | imm | MODE_1 / MODE_0 |
 *  +-----+-----+-----+-----------------+
 */

void emit_add_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_adc_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_and_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_xor_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_or_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                           x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_sbb_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_sub_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_cmp_src0_src1(code_buffer_t *emitter,
                        x86_64_operand_t *src0, x86_64_operand_t *src1);

/**
 * Unary instruction group, generalized implementation.
 * The operands dst, op0 must have the same operand size.
 * The operand dst must not be an immediate operand.
 * The operand op0 must not be an immediate operand.
 *
 * The following table describes the generated code depending on the
 * operand types. The operand type combinations not listed here are
 * considered invalid. The 'result' column has one mode for the case where
 * \ref dst and \ref op0 are different, and one case where they are identical.
 *
 *  - MODE_0: unop op0
 *  - MODE_1: mov dst,op0 / unop dst
 *  - MODE_2: mov r,op0 / unop r / mov dst,r
 *
 *  +-----+-----+-----------------+
 *  | dst | op0 | result          |
 *  +-----+-----+-----------------+
 *  | mem | mem | MODE_2 / MODE_0 |
 *  | mem | reg | MODE_1 / NA     |
 *  | reg | mem | MODE_1 / NA     |
 *  | reg | reg | MODE_1 / MODE_0 |
 *  +-----+-----+-----------------+
 */

void emit_not_dst_src0(code_buffer_t *emitter, x86_64_operand_t *dst,
                       x86_64_operand_t *src0);
void emit_neg_dst_src0(code_buffer_t *emitter, x86_64_operand_t *dst,
                       x86_64_operand_t *src0);

/**
 * Shift instruction group, generalized implementation.
 * The operands dst, op0 must have the same operand size.
 * The operand op1 must have operand size 8.
 * The operands dst, op0 must not use the register rCX.
 *
 * The following table describes the generated code depending on the
 * operand types. The operand type combinations not listed here are
 * considered invalid. The 'result' column has one mode for the case where
 * \ref dst and \ref op0 are different, and one case where they are identical.
 *
 *  + op1 is an immediate or the register CL:
 *    - MODE_0: shift op0,op1
 *    - MODE_1: mov dst,op0 / shift dst,op1
 *    - MODE_2: mov r,op0 / shift r,op1 / mov dst,r
 *
 *  + op1 is neither an immediate nor the register CL:
 *    - MODE_0: mov CL,op1 / shift op0,CL
 *    - MODE_1: mov CL,op1 / mov dst,op0 / shift dst,CL
 *    - MODE_2: mov CL,op1 / mov r,op0 / shift r,CL / mov dst,r
 */

void emit_rol_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_ror_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_rcl_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_rcr_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_shl_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_shr_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);
void emit_sra_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1);

/**
 * Misc instruction group, generalized implementation.
 */
void emit_mov_dst_src0(code_buffer_t *emitter,
                       x86_64_operand_t *dst, x86_64_operand_t *src0);
// void emit_test_src0_src1(code_buffer_t *emitter,
//                          x86_64_operand_t *src0, x86_64_operand_t *src1);
// void emit_lea_dst_m(code_buffer_t *emitter, x86_64_operand_t *op0,
//                     x86_64_mem_t m);

#endif /* _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_ */
