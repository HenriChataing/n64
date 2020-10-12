
#ifndef _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_
#define _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

#include <recompiler/code_buffer.h>

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
void patch_jmp_rel32(code_buffer_t *emitter, unsigned char *rel32, unsigned char *target);

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
void emit_add_rN_rN(code_buffer_t *emitter, unsigned width, unsigned drN, unsigned srN);

void emit_and_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);

void emit_call(code_buffer_t *emitter, void *ptr);
unsigned char *emit_call_rel32(code_buffer_t *emitter);

void emit_cmp_al_imm8(code_buffer_t *emitter, int8_t imm8);
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
void emit_cmp_rN_rN(code_buffer_t *emitter, unsigned width, unsigned drN, unsigned srN);

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

unsigned char *emit_jmp_rel32(code_buffer_t *emitter);
unsigned char *emit_je_rel32(code_buffer_t *emitter);

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

void emit_shr_r8_cl(code_buffer_t *emitter, unsigned r8);
void emit_shr_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_shr_r16_cl(code_buffer_t *emitter, unsigned r16);
void emit_shr_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_shr_r32_cl(code_buffer_t *emitter, unsigned r32);
void emit_shr_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_shr_r64_cl(code_buffer_t *emitter, unsigned r64);
void emit_shr_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64);
void emit_shr_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN);

void emit_sra_r8_cl(code_buffer_t *emitter, unsigned r8);
void emit_sra_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8);
void emit_sra_r16_cl(code_buffer_t *emitter, unsigned r16);
void emit_sra_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16);
void emit_sra_r32_cl(code_buffer_t *emitter, unsigned r32);
void emit_sra_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32);
void emit_sra_r64_cl(code_buffer_t *emitter, unsigned r64);
void emit_sra_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64);
void emit_sra_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN);

void emit_sub_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32);
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
void emit_sub_rN_rN(code_buffer_t *emitter, unsigned width, unsigned drN, unsigned srN);

void emit_xor_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64);

#endif /* _RECOMPILER_TARGET_X86_64_EMITTER_H_INCLUDED_ */
