
#include <stdbool.h>
#include "x86_64.h"

int x86_64_emitter_start(x86_64_emitter_t *emitter,
    unsigned char *ptr, size_t capacity) {
    emitter->ptr = ptr;
    emitter->length = 0;
    emitter->capacity = capacity;

    switch (setjmp(emitter->jmp_buf)) {
    case 0: return 0;
    default: return -1;
    }
}

void emit_u8(x86_64_emitter_t *emitter, uint8_t b) {
    if (emitter->length >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = b;
}

void emit_u16_le(x86_64_emitter_t *emitter, uint16_t w) {
    if ((emitter->length + 1) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = w;
    emitter->ptr[emitter->length++] = w >> 8;
}

void emit_u32_le(x86_64_emitter_t *emitter, uint32_t d) {
    if ((emitter->length + 3) >= emitter->capacity)
        longjmp(emitter->jmp_buf, -1);
    emitter->ptr[emitter->length++] = d;
    emitter->ptr[emitter->length++] = d >> 8;
    emitter->ptr[emitter->length++] = d >> 16;
    emitter->ptr[emitter->length++] = d >> 24;
}

static inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);
}

static inline uint8_t rex(uint8_t w, uint8_t r, uint8_t x, uint8_t b) {
    return 0x40 | (w << 3) | (r << 2) | (x << 1) | (b << 0);
}

static inline uint8_t sib(uint8_t scale, uint8_t index, uint8_t base) {
    return ((scale & 0x3) << 6) | ((index & 0x7) << 3) | (base & 0x7);
}

static inline void emit_rex_reg_mem(x86_64_emitter_t *emitter,
                            bool w, unsigned reg, x86_64_mem_t *mem) {

    unsigned base = mem->mode == INDIRECT ? mem->rm : mem->base;
    bool r = (reg >> 3) & 1;
    bool x = (mem->index >> 3) & 1;
    bool b = (base  >> 3) & 1;
    uint8_t rex_val = rex(w, r, x, b);

    if (rex_val != 0x40) emit_u8(emitter, rex_val);
}

static inline void emit_rex_reg_rm(x86_64_emitter_t *emitter,
                            bool w, unsigned reg, unsigned rm) {
    bool r = (reg >> 3) & 1;
    bool b = (rm  >> 3) & 1;
    uint8_t rex_val = rex(w, r, 0, b);

    if (rex_val != 0x40) emit_u8(emitter, rex_val);
}

static inline void emit_reg_mem(x86_64_emitter_t *emitter,
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

void emit_add_al_imm8(x86_64_emitter_t *emitter, int8_t imm8) {
    emit_u8(emitter, 0x04);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_eax_imm32(x86_64_emitter_t *emitter, int32_t imm32) {
    emit_u8(emitter, 0x05);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_rax_imm32(x86_64_emitter_t *emitter, int32_t imm32) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x05);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r8_imm8(x86_64_emitter_t *emitter, unsigned r8, int8_t imm8) {
    emit_u8(emitter, 0x80);
    emit_u8(emitter, modrm(DIRECT, 0, r8));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m8_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m8, int8_t imm8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x80);
    emit_reg_mem(emitter, 0, &m8);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r32_imm32(x86_64_emitter_t *emitter, unsigned r32, int32_t imm32) {
    emit_u8(emitter, 0x81);
    emit_u8(emitter, modrm(DIRECT, 0, r32));
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_m32_imm32(x86_64_emitter_t *emitter, x86_64_mem_t m32, int32_t imm32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x81);
    emit_reg_mem(emitter, 0, &m32);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r64_imm32(x86_64_emitter_t *emitter, unsigned r64, int32_t imm32) {
    emit_rex_reg_rm(emitter, 1, 0, r64);
    emit_u8(emitter, 0x81);
    emit_u8(emitter, modrm(DIRECT, 0, r64));
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_m64_imm32(x86_64_emitter_t *emitter, x86_64_mem_t m64, int32_t imm32) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0x81);
    emit_reg_mem(emitter, 0, &m64);
    emit_u32_le(emitter, (uint32_t)imm32);
}

void emit_add_r32_imm8(x86_64_emitter_t *emitter, unsigned r32, int8_t imm8) {
    emit_u8(emitter, 0x83);
    emit_u8(emitter, modrm(DIRECT, 0, r32));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m32_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m32, int8_t imm8) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x83);
    emit_reg_mem(emitter, 0, &m32);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r64_imm8(x86_64_emitter_t *emitter, unsigned r64, int8_t imm8) {
    emit_rex_reg_rm(emitter, 1, 0, 0);
    emit_u8(emitter, 0x83);
    emit_u8(emitter, modrm(DIRECT, 0, r64));
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_m64_imm8(x86_64_emitter_t *emitter, x86_64_mem_t m64, int8_t imm8) {
    emit_rex_reg_mem(emitter, 1, 0, &m64);
    emit_u8(emitter, 0x83);
    emit_reg_mem(emitter, 0, &m64);
    emit_u8(emitter, (uint8_t)imm8);
}

void emit_add_r8_r8(x86_64_emitter_t *emitter, unsigned dr8, unsigned sr8) {
    emit_u8(emitter, 0x00);
    emit_u8(emitter, modrm(DIRECT, sr8, dr8));
}

void emit_add_m8_r8(x86_64_emitter_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x00);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_add_r8_m8(x86_64_emitter_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_rex_reg_mem(emitter, 0, 0, &m8);
    emit_u8(emitter, 0x02);
    emit_reg_mem(emitter, r8, &m8);
}

void emit_add_r32_r32(x86_64_emitter_t *emitter, unsigned dr32, unsigned sr32) {
    emit_u8(emitter, 0x01);
    emit_u8(emitter, modrm(DIRECT, sr32, dr32));
}

void emit_add_m32_r32(x86_64_emitter_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x01);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_add_r32_m32(x86_64_emitter_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_rex_reg_mem(emitter, 0, 0, &m32);
    emit_u8(emitter, 0x03);
    emit_reg_mem(emitter, r32, &m32);
}

void emit_add_r64_r64(x86_64_emitter_t *emitter, unsigned dr64, unsigned sr64) {
    emit_rex_reg_rm(emitter, 1, sr64, dr64);
    emit_u8(emitter, 0x01);
    emit_u8(emitter, modrm(DIRECT, sr64, dr64));
}

void emit_add_m64_r64(x86_64_emitter_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x01);
    emit_reg_mem(emitter, r64, &m64);
}

void emit_add_r64_m64(x86_64_emitter_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_rex_reg_mem(emitter, 1, r64, &m64);
    emit_u8(emitter, 0x03);
    emit_reg_mem(emitter, r64, &m64);
}
