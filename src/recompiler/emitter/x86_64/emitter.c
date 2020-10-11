
#include <stdbool.h>
#include "emitter.h"

static inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);
}

static inline uint8_t rex(uint8_t w, uint8_t r, uint8_t x, uint8_t b) {
    return 0x40 | (w << 3) | (r << 2) | (x << 1) | (b << 0);
}

static inline uint8_t sib(uint8_t scale, uint8_t index, uint8_t base) {
    return ((scale & 0x3) << 6) | ((index & 0x7) << 3) | (base & 0x7);
}

static inline void emit_rex_reg_mem(code_buffer_t *emitter,
                            bool w, unsigned reg, x86_64_mem_t *mem) {

    unsigned base = mem->mode == INDIRECT ? mem->rm : mem->base;
    bool r = (reg >> 3) & 1;
    bool x = (mem->index >> 3) & 1;
    bool b = (base  >> 3) & 1;
    uint8_t rex_val = rex(w, r, x, b);

    if (rex_val != 0x40) emit_u8(emitter, rex_val);
}

static inline void emit_rex_reg_rm(code_buffer_t *emitter,
                            bool w, unsigned reg, unsigned rm) {
    bool r = (reg >> 3) & 1;
    bool b = (rm  >> 3) & 1;
    uint8_t rex_val = rex(w, r, 0, b);

    if (rex_val != 0x40) emit_u8(emitter, rex_val);
}

static inline void emit_reg_mem(code_buffer_t *emitter,
                                unsigned reg, x86_64_mem_t *mem) {
    bool has_sib    = mem->rm == 4;
    bool has_disp8  = mem->mode == INDIRECT_DISP8;
    bool has_disp32 = mem->mode == INDIRECT_DISP32 ||
                     (mem->mode == INDIRECT && mem->base == 5);

    unsigned scale =
        mem->scale == 1 ? 0 :
        mem->scale == 2 ? 1 :
        mem->scale == 4 ? 2 :
        mem->scale == 8 ? 3 : 0;

    emit_u8(emitter, modrm(mem->mode, reg, mem->rm));

    if (has_sib)    emit_u8(emitter, sib(scale, mem->index, mem->base));
    if (has_disp8)  emit_u8(emitter, (uint8_t)(int8_t)mem->disp);
    if (has_disp32) emit_u32_le(emitter, (uint32_t)mem->disp);
}

void patch_jmp_rel32(code_buffer_t *emitter,
    unsigned char *rel32, unsigned char *target) {

    if (rel32 == NULL) {
        return;
    }

    // The relative offset is added to the EIP registers, which
    // contains the address of the instruction immediately following,
    // the relative offset size need to be deducted.
    ptrdiff_t rel = target - rel32 - 4;
    if (rel < INT32_MIN || rel > INT32_MAX) {
        fail_code_buffer(emitter);
    }

    uint32_t _rel32 = (uint32_t)(int32_t)rel;
    rel32[0] = _rel32;
    rel32[1] = _rel32 >> 8;
    rel32[2] = _rel32 >> 16;
    rel32[3] = _rel32 >> 24;
}

void emit_add_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_u8(emitter, 0x04);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_eax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_u8(emitter, 0x05);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_rax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x05);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    emit_u8(emitter, 0x80);
    emit_u8(emitter, modrm(DIRECT, 0, r8));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x80);
    emit_reg_mem(emitter, 0, &m8);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    emit_u8(emitter, 0x81);
    emit_u8(emitter, modrm(DIRECT, 0, r32));
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x81);
    emit_reg_mem(emitter, 0, &m32);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32) {
    emit_rex_reg_rm(emitter, 1, 0, r64);
    emit_u8(emitter, 0x81);
    emit_u8(emitter, modrm(DIRECT, 0, r64));
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0x81);
    emit_reg_mem(emitter, 0, &m64);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8) {
    emit_u8(emitter, 0x83);
    emit_u8(emitter, modrm(DIRECT, 0, r32));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x83);
    emit_reg_mem(emitter, 0, &m32);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x83);
    emit_u8(emitter, modrm(DIRECT, 0, r64));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0x83);
    emit_reg_mem(emitter, 0, &m64);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    emit_u8(emitter, 0x00);
    emit_u8(emitter, modrm(DIRECT, sr8, dr8));
}

void emit_add_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x00);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_add_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x02);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_add_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x01);
    emit_u8(emitter, modrm(DIRECT, sr16, dr16));
}

void emit_add_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x01);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_add_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x03);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_add_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x01);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_add_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x01);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_add_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x03);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_add_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x01);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_add_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x01);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_add_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x03);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_add_rN_rN(code_buffer_t *emitter, unsigned width,
                    unsigned drN, unsigned srN) {
    switch (width) {
    case 8:  emit_add_r8_r8(emitter, drN, srN); break;
    case 16: emit_add_r16_r16(emitter, drN, srN); break;
    case 32: emit_add_r32_r32(emitter, drN, srN); break;
    case 64: emit_add_r64_r64(emitter, drN, srN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_and_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x21);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_call(code_buffer_t *emitter, void *ptr) {
    // Near call.
    // The relative offset is added to the EIP registers, which
    // contains the address of the instruction immediately following,
    // the relative offset size need to be deducted.
    emit_u8(emitter, 0xe8);
    ptrdiff_t rel = (unsigned char *)ptr -
        (emitter->ptr + emitter->length) - 4;
    if (rel < INT32_MIN || rel > INT32_MAX) {
        fail_code_buffer(emitter);
    }
    emit_u32_le(emitter, (uint32_t)(int32_t)rel);
}

unsigned char *emit_call_rel32(code_buffer_t *emitter) {
    // Near call.
    unsigned char *rel32;
    emit_u8(emitter, 0xe8);
    rel32 = emitter->ptr + emitter->length;
    emit_u32_le(emitter, 0);
    return rel32;
}

void emit_cmp_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_u8(emitter, 0x3c);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_cmp_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    emit_u8(emitter, 0x38);
    emit_u8(emitter, modrm(DIRECT, sr8, dr8));
}

void emit_cmp_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x38);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_cmp_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x3a);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_cmp_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x39);
    emit_u8(emitter, modrm(DIRECT, sr16, dr16));
}

void emit_cmp_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x39);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_cmp_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x3b);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_cmp_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x39);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_cmp_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x39);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_cmp_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x3b);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_cmp_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x39);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_cmp_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x39);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_cmp_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x3b);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_cmp_rN_rN(code_buffer_t *emitter, unsigned width,
                    unsigned drN, unsigned srN) {
    switch (width) {
    case 8:  emit_cmp_r8_r8(emitter, drN, srN); break;
    case 16: emit_cmp_r16_r16(emitter, drN, srN); break;
    case 32: emit_cmp_r32_r32(emitter, drN, srN); break;
    case 64: emit_cmp_r64_r64(emitter, drN, srN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_cbw(code_buffer_t *emitter) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x98);
}

void emit_cwd(code_buffer_t *emitter) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x99);
}

void emit_cwde(code_buffer_t *emitter) {
    emit_u8(emitter, 0x98);
}

void emit_cdq(code_buffer_t *emitter) {
    emit_u8(emitter, 0x99);
}

void emit_cdqe(code_buffer_t *emitter) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x98);
}

void emit_cqo(code_buffer_t *emitter) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x99);
}

void emit_div_ax_r8(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xf6);
    emit_u8(emitter, modrm(DIRECT, 6, r8));
}

void emit_div_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 6, &m8);
    emit_u8(emitter, 0xf6);
    emit_reg_mem(emitter, 6, &m8);
}

void emit_div_dx_ax_r16(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 6, r16));
}

void emit_div_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 6, &m16);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 6, &m16);
}

void emit_div_edx_eax_r32(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 6, r32));
}

void emit_div_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 6, &m32);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 6, &m32);
}

void emit_div_rdx_rax_r64(code_buffer_t *emitter, unsigned r64) {
    emit_rex_reg_rm(emitter, 1, 6, r64);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 6, r64));
}

void emit_div_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, 6, &m64);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 6, &m64);
}

void emit_idiv_ax_r8(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xf6);
    emit_u8(emitter, modrm(DIRECT, 7, r8));
}

void emit_idiv_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 7, &m8);
    emit_u8(emitter, 0xf6);
    emit_reg_mem(emitter, 7, &m8);
}

void emit_idiv_dx_ax_r16(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 7, r16));
}

void emit_idiv_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 7, &m16);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 7, &m16);
}

void emit_idiv_edx_eax_r32(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 7, r32));
}

void emit_idiv_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 7, &m32);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 7, &m32);
}

void emit_idiv_rdx_rax_r64(code_buffer_t *emitter, unsigned r64) {
    emit_rex_reg_rm(emitter, 1, 7, r64);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 7, r64));
}

void emit_idiv_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, 7, &m64);
    emit_u8(emitter, 0xf7);
    emit_reg_mem(emitter, 7, &m64);
}

void emit_imul_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_u8(emitter, modrm(DIRECT, sr16, dr16));
}

void emit_imul_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_imul_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_imul_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_imul_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_imul_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0xaf);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_imul_rN_rN(code_buffer_t *emitter, unsigned width,
                    unsigned drN, unsigned srN) {
    switch (width) {
    case 16: emit_imul_r16_r16(emitter, drN, srN); break;
    case 32: emit_imul_r32_r32(emitter, drN, srN); break;
    case 64: emit_imul_r64_r64(emitter, drN, srN); break;
    default: fail_code_buffer(emitter);
    }
}

unsigned char *emit_jmp_rel32(code_buffer_t *emitter) {
    unsigned char *rel32;
    emit_u8(emitter, 0xe9);
    rel32 = emitter->ptr + emitter->length;
    emit_u32_le(emitter, 0);
    return rel32;
}

unsigned char *emit_je_rel32(code_buffer_t *emitter) {
    unsigned char *rel32;
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x87);
    rel32 = emitter->ptr + emitter->length;
    emit_u32_le(emitter, 0);
    return rel32;
}

void emit_mov_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    emit_u8(emitter, 0xb0 | r8);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_mov_r16_imm16(code_buffer_t *emitter, unsigned r16, int16_t imm16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xb8 | r16);
    emit_u16_le(emitter, (uint16_t)imm16);
}

void emit_mov_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    emit_u8(emitter, 0xb8 | r32);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_mov_r64_imm64(code_buffer_t *emitter, unsigned r64, int64_t imm64) {
    emit_rex_reg_rm(emitter, 1, 0, r64);
    emit_u8(emitter, 0xb8 | (r64 & 0x7));
    emit_u64_le(emitter, (uint64_t)imm64);
}

void emit_mov_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, r8, &m8);
    emit_u8(emitter, 0x8a);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_mov_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, r16, &m16);
    emit_u8(emitter, 0x8b);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_mov_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, r32, &m32);
    emit_u8(emitter, 0x8b);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_mov_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x8b);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_mov_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x88);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_mov_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x89);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_mov_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x89);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_mov_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x89);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_mov_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x89);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_mov_rN_mN(code_buffer_t *emitter, unsigned width,
                    unsigned rN, x86_64_mem_t mN) {
    switch (width) {
    case 8:  emit_mov_r8_m8(emitter, rN, mN); break;
    case 16: emit_mov_r16_m16(emitter, rN, mN); break;
    case 32: emit_mov_r32_m32(emitter, rN, mN); break;
    case 64: emit_mov_r64_m64(emitter, rN, mN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_mov_mN_rN(code_buffer_t *emitter, unsigned width,
                    x86_64_mem_t mN, unsigned rN) {
    switch (width) {
    case 8:  emit_mov_m8_r8(emitter, mN, rN); break;
    case 16: emit_mov_m16_r16(emitter, mN, rN); break;
    case 32: emit_mov_m32_r32(emitter, mN, rN); break;
    case 64: emit_mov_m64_r64(emitter, mN, rN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_not_r8(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xf6);
    emit_u8(emitter, modrm(DIRECT, 2, r8));
}

void emit_not_r16(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 2, r16));
}

void emit_not_r32(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 2, r32));
}

void emit_not_r64(code_buffer_t *emitter, unsigned r64) {
    emit_rex_reg_rm(emitter, 1, 2, r64);
    emit_u8(emitter, 0xf7);
    emit_u8(emitter, modrm(DIRECT, 2, r64));
}

void emit_or_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x09);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_or_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x09);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_pop_r64(code_buffer_t *emitter, unsigned r64) {
    emit_rex_reg_rm(emitter, 0, 0, r64);
    emit_u8(emitter, 0x58 | (r64 & 0x7));
}

void emit_push_r64(code_buffer_t *emitter, unsigned r64) {
    emit_rex_reg_rm(emitter, 0, 0, r64);
    emit_u8(emitter, 0x50 | (r64 & 0x7));
}

void emit_ret(code_buffer_t *emitter) {
    // Near return.
    emit_u8(emitter, 0xc3);
}

void emit_sete_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x94);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setne_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x95);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_seta_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x97);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setae_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x93);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setb_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x92);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setbe_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x96);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setg_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x9f);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setge_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x9d);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setl_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x9c);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_setle_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, 0x9e);
    emit_reg_mem(emitter, 0, &m8);
}

void emit_shl_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8) {
    emit_u8(emitter, 0xc1);
    emit_u8(emitter, modrm(DIRECT, 4, r32));
    emit_u8(emitter, imm8);
}

void emit_shl_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8) {
    emit_rex_reg_rm(emitter, 1, 0, r64);
    emit_u8(emitter, 0xc1);
    emit_u8(emitter, modrm(DIRECT, 4, r64));
    emit_u8(emitter, imm8);
}

void emit_shl_r8_cl(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xd2);
    emit_u8(emitter, modrm(DIRECT, 4, r8));
}

void emit_shl_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0xd2);
    emit_reg_mem(emitter, 4, &m8);
}

void emit_shl_r16_cl(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 4, r16));
}

void emit_shl_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 4, &m16);
}

void emit_shl_r32_cl(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 4, r32));
}

void emit_shl_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 4, &m32);
}

void emit_shl_r64_cl(code_buffer_t *emitter, unsigned r64) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 4, r64));
}

void emit_shl_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 4, &m64);
}

void emit_shl_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN) {
    switch (width) {
    case 8:  emit_shl_r8_cl(emitter, rN); break;
    case 16: emit_shl_r16_cl(emitter, rN); break;
    case 32: emit_shl_r32_cl(emitter, rN); break;
    case 64: emit_shl_r64_cl(emitter, rN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_shr_r8_cl(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xd2);
    emit_u8(emitter, modrm(DIRECT, 5, r8));
}

void emit_shr_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0xd2);
    emit_reg_mem(emitter, 5, &m8);
}

void emit_shr_r16_cl(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 5, r16));
}

void emit_shr_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 5, &m16);
}

void emit_shr_r32_cl(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 5, r32));
}

void emit_shr_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 5, &m32);
}

void emit_shr_r64_cl(code_buffer_t *emitter, unsigned r64) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 5, r64));
}

void emit_shr_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 5, &m64);
}

void emit_shr_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN) {
    switch (width) {
    case 8:  emit_shr_r8_cl(emitter, rN); break;
    case 16: emit_shr_r16_cl(emitter, rN); break;
    case 32: emit_shr_r32_cl(emitter, rN); break;
    case 64: emit_shr_r64_cl(emitter, rN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_sra_r8_cl(code_buffer_t *emitter, unsigned r8) {
    emit_u8(emitter, 0xd2);
    emit_u8(emitter, modrm(DIRECT, 7, r8));
}

void emit_sra_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0xd2);
    emit_reg_mem(emitter, 7, &m8);
}

void emit_sra_r16_cl(code_buffer_t *emitter, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 7, r16));
}

void emit_sra_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 7, &m16);
}

void emit_sra_r32_cl(code_buffer_t *emitter, unsigned r32) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 7, r32));
}

void emit_sra_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 7, &m32);
}

void emit_sra_r64_cl(code_buffer_t *emitter, unsigned r64) {
    emit_u8(emitter, 0xd3);
    emit_u8(emitter, modrm(DIRECT, 7, r64));
}

void emit_sra_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0xd3);
    emit_reg_mem(emitter, 7, &m64);
}

void emit_sra_rN_cl(code_buffer_t *emitter, unsigned width, unsigned rN) {
    switch (width) {
    case 8:  emit_sra_r8_cl(emitter, rN); break;
    case 16: emit_sra_r16_cl(emitter, rN); break;
    case 32: emit_sra_r32_cl(emitter, rN); break;
    case 64: emit_sra_r64_cl(emitter, rN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_sub_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32) {
    emit_rex_reg_rm(emitter, 1, 5, r64);
    emit_u8(emitter, 0x81);
    emit_u8(emitter, modrm(DIRECT, 5, r64));
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_sub_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    emit_u8(emitter, 0x28);
    emit_u8(emitter, modrm(DIRECT, sr8, dr8));
}

void emit_sub_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x28);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_sub_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x2a);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_sub_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    emit_u8(emitter, 0x66);
    emit_u8(emitter, 0x29);
    emit_u8(emitter, modrm(DIRECT, sr16, dr16));
}

void emit_sub_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x29);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_sub_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_u8(emitter, 0x66);
    emit_rex_reg_mem(emitter, 0, 0, &m16);
    emit_u8(emitter, 0x2b);
    emit_reg_mem(emitter, r16, &m16);
}

void emit_sub_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x29);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_sub_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x29);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_sub_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x2b);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_sub_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x29);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_sub_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x29);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_sub_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x2b);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_sub_rN_rN(code_buffer_t *emitter, unsigned width,
                    unsigned drN, unsigned srN) {
    switch (width) {
    case 8:  emit_sub_r8_r8(emitter, drN, srN); break;
    case 16: emit_sub_r16_r16(emitter, drN, srN); break;
    case 32: emit_sub_r32_r32(emitter, drN, srN); break;
    case 64: emit_sub_r64_r64(emitter, drN, srN); break;
    default: fail_code_buffer(emitter);
    }
}

void emit_xor_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x31);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}
