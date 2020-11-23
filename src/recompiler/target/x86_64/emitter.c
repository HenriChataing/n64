
#include <stdbool.h>
#include "emitter.h"

static inline bool is_int8(int64_t v) {
    return v >= INT8_MIN && v <= INT8_MAX;
}

static inline bool is_int32(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX;
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

static inline void emit_rex_reg_modrm(code_buffer_t *emitter,
                            bool w, unsigned reg, x86_64_mem_t *mem) {

    unsigned base = mem->mode == INDIRECT ||
                    mem->mode == DIRECT ? mem->rm : mem->base;
    bool r = (reg >> 3) & 1;
    bool x = (mem->index >> 3) & 1;
    bool b = (base  >> 3) & 1;
    uint8_t rex_val = rex(w, r, x, b);

    if (rex_val != 0x40) emit_u8(emitter, rex_val);
}

static inline void emit_reg_modrm(code_buffer_t *emitter,
                                unsigned reg, x86_64_mem_t *mem) {
    bool has_sib    = mem->mode != DIRECT && mem->rm == 4;
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
    if (has_disp8)  emit_i8(emitter, mem->disp);
    if (has_disp32) emit_i32_le(emitter, mem->disp);
}

/**
 * Generate instructions with specific addressing modes.
 * The abbreviations are taken from the Architectures Software
 * Developper's Manual, section A.2.1 Codes for Addressing Method.
 * The addressing mode K was added to identify a register encoded in the
 * lower three bits of an opcode byte.
 */

static
void emit_instruction_1_Eb_Gb(code_buffer_t *emitter, uint8_t opcode,
                              x86_64_mem_t *modrm, unsigned reg) {
    emit_rex_reg_modrm(emitter, 0, reg, modrm);
    emit_u8(emitter, opcode);
    emit_reg_modrm(emitter, reg, modrm);
}

static
void emit_instruction_1_Gb_Eb(code_buffer_t *emitter, uint8_t opcode,
                              unsigned reg, x86_64_mem_t *modrm) {
    emit_instruction_1_Eb_Gb(emitter, opcode, modrm, reg);
}

static
void emit_instruction_1_Eb(code_buffer_t *emitter,
                           uint8_t opcode, uint8_t opcode_ext,
                           x86_64_mem_t *modrm) {
    emit_instruction_1_Eb_Gb(emitter, opcode, modrm, opcode_ext);
}

static
void emit_instruction_1_Eb_1(code_buffer_t *emitter,
                           uint8_t opcode, uint8_t opcode_ext,
                           x86_64_mem_t *modrm) {
    emit_instruction_1_Eb(emitter, opcode, opcode_ext, modrm);
}

static
void emit_instruction_1_Eb_CL(code_buffer_t *emitter,
                              uint8_t opcode, uint8_t opcode_ext,
                              x86_64_mem_t *modrm) {
    emit_instruction_1_Eb(emitter, opcode, opcode_ext, modrm);
}

static
void emit_instruction_1_Eb_Ib(code_buffer_t *emitter,
                              uint8_t opcode, uint8_t opcode_ext,
                              x86_64_mem_t *modrm, int8_t imm) {
    emit_instruction_1_Eb_Gb(emitter, opcode, modrm, opcode_ext);
    emit_i8(emitter, imm);
}

static
void emit_instruction_1_AL_Ib(code_buffer_t *emitter, uint8_t opcode,
                              int8_t imm) {
    emit_u8(emitter, opcode);
    emit_i8(emitter, imm);
}

static
void emit_instruction_1_Kb_Ib(code_buffer_t *emitter, uint8_t opcode,
                              unsigned reg, int8_t imm) {
    x86_64_mem_t m = mem_direct(reg);
    emit_rex_reg_modrm(emitter, 0, 0, &m);
    emit_u8(emitter, opcode | reg);
    emit_i8(emitter, imm);
}

static
void emit_instruction_1_Ev_Gv(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, x86_64_mem_t *modrm,
                              unsigned reg) {
    if (size == 16) emit_u8(emitter, 0x66);
    emit_rex_reg_modrm(emitter, size == 64, reg, modrm);
    emit_u8(emitter, opcode);
    emit_reg_modrm(emitter, reg, modrm);
}

static
void emit_instruction_1_Gv_Ev(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, unsigned reg,
                              x86_64_mem_t *modrm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, reg);
}

static
void emit_instruction_1_Gv_M(code_buffer_t *emitter, unsigned size,
                             uint8_t opcode, unsigned reg,
                             x86_64_mem_t *modrm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, reg);
}

static
void emit_instruction_1_Ev(code_buffer_t *emitter, unsigned size,
                           uint8_t opcode, uint8_t opcode_ext,
                           x86_64_mem_t *modrm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, opcode_ext);
}

static
void emit_instruction_1_Ev_1(code_buffer_t *emitter, unsigned size,
                             uint8_t opcode, uint8_t opcode_ext,
                             x86_64_mem_t *modrm) {
    emit_instruction_1_Ev(emitter, size, opcode, opcode_ext, modrm);
}

static
void emit_instruction_1_Ev_CL(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, uint8_t opcode_ext,
                              x86_64_mem_t *modrm) {
    emit_instruction_1_Ev(emitter, size, opcode, opcode_ext, modrm);
}

static
void emit_instruction_1_Ev_Iz(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, uint8_t opcode_ext,
                              x86_64_mem_t *modrm, int32_t imm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, opcode_ext);
    if (size == 16) emit_i16_le(emitter, imm);
    else            emit_i32_le(emitter, imm);
}

static
void emit_instruction_1_Ev_Ib(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, uint8_t opcode_ext,
                              x86_64_mem_t *modrm, int8_t imm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, opcode_ext);
    emit_i8(emitter, imm);
}

static
void emit_instruction_1_Gv_Ev_Iz(code_buffer_t *emitter, unsigned size,
                                 uint8_t opcode, unsigned reg,
                                 x86_64_mem_t *modrm, int32_t imm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, reg);
    if (size == 16) emit_i16_le(emitter, imm);
    else            emit_i32_le(emitter, imm);
}

static
void emit_instruction_1_Gv_Ev_Ib(code_buffer_t *emitter, unsigned size,
                                 uint8_t opcode, unsigned reg,
                                 x86_64_mem_t *modrm, int8_t imm) {
    emit_instruction_1_Ev_Gv(emitter, size, opcode, modrm, reg);
    emit_i8(emitter, imm);
}

static
void emit_instruction_1_rAX_Iz(code_buffer_t *emitter, unsigned size,
                               uint8_t opcode, int32_t imm) {
    if (size == 16) emit_u8(emitter, 0x66);
    if (size == 64) emit_u8(emitter, rex(1, 0, 0, 0));
    emit_u8(emitter, opcode);
    if (size == 16) emit_i16_le(emitter, imm);
    else            emit_i32_le(emitter, imm);
}

static
void emit_instruction_1_Kv(code_buffer_t *emitter, unsigned size,
                           uint8_t opcode, unsigned reg) {
    if (size == 16) emit_u8(emitter, 0x66);
    x86_64_mem_t m = mem_direct(reg);
    emit_rex_reg_modrm(emitter, size == 64, 0, &m);
    emit_u8(emitter, opcode | (reg & 7));
}

static
void emit_instruction_1_Kv_Iv(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, unsigned reg, int64_t imm) {
    emit_instruction_1_Kv(emitter, size, opcode, reg);
    if (size == 16)      emit_i16_le(emitter, imm);
    else if (size == 32) emit_i32_le(emitter, imm);
    else                 emit_i64_le(emitter, imm);
}

static
unsigned char *emit_instruction_1_Jz(code_buffer_t *emitter, unsigned size,
                                     uint8_t opcode, int64_t rel) {
    // The relative offset is added to the EIP registers, which
    // contains the address of the instruction immediately following.
    // The relative offset size need to be deducted.
    emit_u8(emitter, opcode);
    unsigned char *ptr = emitter->ptr + emitter->length;
    if (size == 16) emit_i16_le(emitter, rel);
    else            emit_i32_le(emitter, rel);
    return ptr;
}

static
void emit_instruction_2_Eb_Gb(code_buffer_t *emitter, uint8_t opcode,
                              x86_64_mem_t *modrm, unsigned reg) {
    emit_rex_reg_modrm(emitter, 0, reg, modrm);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, opcode);
    emit_reg_modrm(emitter, reg, modrm);
}

static
void emit_instruction_2_Gb_Eb(code_buffer_t *emitter, uint8_t opcode,
                              unsigned reg, x86_64_mem_t *modrm) {
    emit_instruction_2_Eb_Gb(emitter, opcode, modrm, reg);
}

static
void emit_instruction_2_Eb(code_buffer_t *emitter, uint8_t opcode,
                           uint8_t opcode_ext, x86_64_mem_t *modrm) {
    emit_instruction_2_Eb_Gb(emitter, opcode, modrm, opcode_ext);
}

static
void emit_instruction_2_Ev_Gv(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, x86_64_mem_t *modrm,
                              unsigned reg) {
    if (size == 16) emit_u8(emitter, 0x66);
    emit_rex_reg_modrm(emitter, size == 64, reg, modrm);
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, opcode);
    emit_reg_modrm(emitter, reg, modrm);
}

static
void emit_instruction_2_Gv_Ev(code_buffer_t *emitter, unsigned size,
                              uint8_t opcode, unsigned reg,
                              x86_64_mem_t *modrm) {
    emit_instruction_2_Ev_Gv(emitter, size, opcode, modrm, reg);
}

static
unsigned char *emit_instruction_2_Jz(code_buffer_t *emitter, unsigned size,
                                     uint8_t opcode, int64_t rel) {
    // The relative offset is added to the EIP registers, which
    // contains the address of the instruction immediately following.
    // The relative offset size need to be deducted.
    emit_u8(emitter, 0x0f);
    emit_u8(emitter, opcode);
    unsigned char *ptr = emitter->ptr + emitter->length;
    if (size == 16) emit_i16_le(emitter, rel);
    else            emit_i32_le(emitter, rel);
    return ptr;
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
    if (!is_int32(rel)) {
        fail_code_buffer(emitter);
    }

    uint32_t _rel32 = (uint32_t)(int32_t)rel;
    rel32[0] = _rel32;
    rel32[1] = _rel32 >> 8;
    rel32[2] = _rel32 >> 16;
    rel32[3] = _rel32 >> 24;
}

void emit_add_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_instruction_1_AL_Ib(emitter, 0x04, imm8);
}

void emit_add_eax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_instruction_1_rAX_Iz(emitter, 32, 0x05, imm32);
}

void emit_add_rax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_instruction_1_rAX_Iz(emitter, 64, 0x05, imm32);
}

void emit_add_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x00, &m8, imm8);
}

void emit_add_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8) {
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x00, &m8, imm8);
}

void emit_add_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x00, &m32, imm32);
}

void emit_add_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x00, &m32, imm32);
}

void emit_add_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x00, &m64, imm32);
}

void emit_add_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x00, &m64, imm32);
}

void emit_add_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x00, &m32, imm8);
}

void emit_add_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x00, &m32, imm8);
}

void emit_add_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x00, &m64, imm8);
}

void emit_add_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x00, &m64, imm8);
}

void emit_add_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    x86_64_mem_t m8 = mem_direct(dr8);
    emit_instruction_1_Eb_Gb(emitter, 0x00, &m8, sr8);
}

void emit_add_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_instruction_1_Eb_Gb(emitter, 0x00, &m8, r8);
}

void emit_add_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_instruction_1_Gb_Eb(emitter, 0x02, r8, &m8);
}

void emit_add_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    x86_64_mem_t m16 = mem_direct(dr16);
    emit_instruction_1_Ev_Gv(emitter, 16, 0x01, &m16, sr16);
}

void emit_add_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_instruction_1_Ev_Gv(emitter, 16, 0x01, &m16, r16);
}

void emit_add_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_instruction_1_Gv_Ev(emitter, 16, 0x03, r16, &m16);
}

void emit_add_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    x86_64_mem_t m32 = mem_direct(dr32);
    emit_instruction_1_Ev_Gv(emitter, 32, 0x01, &m32, sr32);
}

void emit_add_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_instruction_1_Ev_Gv(emitter, 32, 0x01, &m32, r32);
}

void emit_add_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_instruction_1_Gv_Ev(emitter, 32, 0x03, r32, &m32);
}

void emit_add_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x01, &m64, sr64);
}

void emit_add_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_instruction_1_Ev_Gv(emitter, 64, 0x01, &m64, r64);
}

void emit_add_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_instruction_1_Gv_Ev(emitter, 64, 0x03, r64, &m64);
}


void emit_and_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x21, &m64, sr64);
}

void emit_call_r64(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev(emitter, 64, 0xff, 0x02, &m64);
}

unsigned char *emit_call_rel32(code_buffer_t *emitter, int32_t rel32) {
    return emit_instruction_1_Jz(emitter, 32, 0xe8, rel32);
}

void emit_call(code_buffer_t *emitter, void *ptr, unsigned r64) {
    // Call with relative address if the offset can fit in rel32,
    // otherwise generate call with absolute 64bit address.
    ptrdiff_t rel = (unsigned char *)ptr -
        (emitter->ptr + emitter->length + 1) - 4;
    if (is_int32(rel)) {
        emit_call_rel32(emitter, rel);
    } else {
        emit_mov_r64_imm64(emitter, r64, (int64_t)(uint64_t)(uintptr_t)ptr);
        emit_call_r64(emitter, r64);
    }
}

void emit_cmp_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_instruction_1_AL_Ib(emitter, 0x3c, imm8);
}

void emit_cmp_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x07, &m8, imm8);
}

void emit_cmp_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8) {
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x07, &m8, imm8);
}

void emit_cmp_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x07, &m32, imm32);
}

void emit_cmp_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x07, &m32, imm32);
}

void emit_cmp_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x07, &m64, imm32);
}

void emit_cmp_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x07, &m64, imm32);
}

void emit_cmp_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x07, &m32, imm8);
}

void emit_cmp_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x07, &m32, imm8);
}

void emit_cmp_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x07, &m64, imm8);
}

void emit_cmp_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x07, &m64, imm8);
}

void emit_cmp_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    x86_64_mem_t m8 = mem_direct(dr8);
    emit_instruction_1_Eb_Gb(emitter, 0x38, &m8, sr8);
}

void emit_cmp_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_instruction_1_Eb_Gb(emitter, 0x38, &m8, r8);
}

void emit_cmp_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_instruction_1_Gb_Eb(emitter, 0x3a, r8, &m8);
}

void emit_cmp_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    x86_64_mem_t m16 = mem_direct(dr16);
    emit_instruction_1_Ev_Gv(emitter, 16, 0x39, &m16, sr16);
}

void emit_cmp_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_instruction_1_Ev_Gv(emitter, 16, 0x39, &m16, r16);
}

void emit_cmp_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_instruction_1_Gv_Ev(emitter, 16, 0x3b, r16, &m16);
}

void emit_cmp_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    x86_64_mem_t m32 = mem_direct(dr32);
    emit_instruction_1_Ev_Gv(emitter, 32, 0x39, &m32, sr32);
}

void emit_cmp_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_instruction_1_Ev_Gv(emitter, 32, 0x39, &m32, r32);
}

void emit_cmp_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_instruction_1_Gv_Ev(emitter, 32, 0x3b, r32, &m32);
}

void emit_cmp_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x39, &m64, sr64);
}

void emit_cmp_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_instruction_1_Ev_Gv(emitter, 64, 0x39, &m64, r64);
}

void emit_cmp_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_instruction_1_Gv_Ev(emitter, 64, 0x3b, r64, &m64);
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
    emit_u8(emitter, 0x48);
    emit_u8(emitter, 0x98);
}

void emit_cqo(code_buffer_t *emitter) {
    emit_u8(emitter, 0x48);
    emit_u8(emitter, 0x99);
}

void emit_div_ax_r8(code_buffer_t *emitter, unsigned r8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb(emitter, 0xf6, 0x06, &m8);
}

void emit_div_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_1_Eb(emitter, 0xf6, 0x06, &m8);
}

void emit_div_dx_ax_r16(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev(emitter, 16, 0xf7, 0x06, &m16);
}

void emit_div_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_instruction_1_Ev(emitter, 16, 0xf7, 0x06, &m16);
}

void emit_div_edx_eax_r32(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev(emitter, 32, 0xf7, 0x06, &m32);
}

void emit_div_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_instruction_1_Ev(emitter, 32, 0xf7, 0x06, &m32);
}

void emit_div_rdx_rax_r64(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev(emitter, 64, 0xf7, 0x06, &m64);
}

void emit_div_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_instruction_1_Ev(emitter, 64, 0xf7, 0x06, &m64);
}

void emit_idiv_ax_r8(code_buffer_t *emitter, unsigned r8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb(emitter, 0xf6, 0x07, &m8);
}

void emit_idiv_ax_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_1_Eb(emitter, 0xf6, 0x07, &m8);
}

void emit_idiv_dx_ax_r16(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev(emitter, 16, 0xf7, 0x07, &m16);
}

void emit_idiv_dx_ax_m16(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_instruction_1_Ev(emitter, 16, 0xf7, 0x07, &m16);
}

void emit_idiv_edx_eax_r32(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev(emitter, 32, 0xf7, 0x07, &m32);
}

void emit_idiv_edx_eax_m32(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_instruction_1_Ev(emitter, 32, 0xf7, 0x07, &m32);
}

void emit_idiv_rdx_rax_r64(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev(emitter, 64, 0xf7, 0x07, &m64);
}

void emit_idiv_rdx_rax_m64(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_instruction_1_Ev(emitter, 64, 0xf7, 0x07, &m64);
}

void emit_imul_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    x86_64_mem_t m16 = mem_direct(sr16);
    emit_instruction_2_Gv_Ev(emitter, 16, 0xaf, dr16, &m16);
}

void emit_imul_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_instruction_2_Gv_Ev(emitter, 16, 0xaf, r16, &m16);
}

void emit_imul_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    x86_64_mem_t m32 = mem_direct(sr32);
    emit_instruction_2_Gv_Ev(emitter, 32, 0xaf, dr32, &m32);
}

void emit_imul_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_instruction_2_Gv_Ev(emitter, 32, 0xaf, r32, &m32);
}

void emit_imul_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(sr64);
    emit_instruction_2_Gv_Ev(emitter, 64, 0xaf, dr64, &m64);
}

void emit_imul_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_instruction_2_Gv_Ev(emitter, 64, 0xaf, r64, &m64);
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

void emit_lea_r64_m(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m) {
    emit_instruction_1_Gv_M(emitter, 64, 0x8d, r64, &m);
}

unsigned char *emit_jmp_rel32(code_buffer_t *emitter, int32_t rel32) {
    return emit_instruction_1_Jz(emitter, 32, 0xe9, rel32);
}

unsigned char *emit_je_rel32(code_buffer_t *emitter, int32_t rel32) {
    return emit_instruction_2_Jz(emitter, 32, 0x84, rel32);
}

unsigned char *emit_jne_rel32(code_buffer_t *emitter, int32_t rel32) {
    return emit_instruction_2_Jz(emitter, 32, 0x85, rel32);
}

void emit_mov_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    emit_instruction_1_Kb_Ib(emitter, 0xb0, r8, imm8);
}

void emit_mov_r16_imm16(code_buffer_t *emitter, unsigned r16, int16_t imm16) {
    emit_instruction_1_Kv_Iv(emitter, 16, 0xb8, r16, imm16);
}

void emit_mov_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    emit_instruction_1_Kv_Iv(emitter, 32, 0xb8, r32, imm32);
}

void emit_mov_r64_imm64(code_buffer_t *emitter, unsigned r64, int64_t imm64) {
    emit_instruction_1_Kv_Iv(emitter, 64, 0xb8, r64, imm64);
}

void emit_mov_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_instruction_1_Gb_Eb(emitter, 0x8a, r8, &m8);
}

void emit_mov_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_instruction_1_Gv_Ev(emitter, 16, 0x8b, r16, &m16);
}

void emit_mov_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_instruction_1_Gv_Ev(emitter, 32, 0x8b, r32, &m32);
}

void emit_mov_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_instruction_1_Gv_Ev(emitter, 64, 0x8b, r64, &m64);
}

void emit_mov_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_instruction_1_Eb_Gb(emitter, 0x88, &m8, r8);
}

void emit_mov_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_instruction_1_Ev_Gv(emitter, 16, 0x89, &m16, r16);
}

void emit_mov_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_instruction_1_Ev_Gv(emitter, 32, 0x89, &m32, r32);
}

void emit_mov_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_instruction_1_Ev_Gv(emitter, 64, 0x89, &m64, r64);
}

void emit_mov_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x89, &m64, sr64);
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
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb(emitter, 0xf6, 0x02, &m8);
}

void emit_not_r16(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev(emitter, 16, 0xf7, 0x02, &m16);
}

void emit_not_r32(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev(emitter, 32, 0xf7, 0x02, &m32);
}

void emit_not_r64(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev(emitter, 64, 0xf7, 0x02, &m64);
}

void emit_or_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    x86_64_mem_t m32 = mem_direct(dr32);
    emit_instruction_1_Ev_Gv(emitter, 32, 0x09, &m32, sr32);
}

void emit_or_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x09, &m64, sr64);
}

void emit_pop_r64(code_buffer_t *emitter, unsigned r64) {
    emit_instruction_1_Kv(emitter, 64, 0x58, r64);
}

void emit_push_r64(code_buffer_t *emitter, unsigned r64) {
    emit_instruction_1_Kv(emitter, 64, 0x50, r64);
}

void emit_ret(code_buffer_t *emitter) {
    // Near return.
    emit_u8(emitter, 0xc3);
}

void emit_sete_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x94, 0, &m8);
}

void emit_setne_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x95, 0, &m8);
}

void emit_seta_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x97, 0, &m8);
}

void emit_setae_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x93, 0, &m8);
}

void emit_setb_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x92, 0, &m8);
}

void emit_setbe_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x96, 0, &m8);
}

void emit_setg_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x9f, 0, &m8);
}

void emit_setge_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x9d, 0, &m8);
}

void emit_setl_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x9c, 0, &m8);
}

void emit_setle_m8(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_2_Eb(emitter, 0x9e, 0, &m8);
}

void emit_shl_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0xc1, 0x04, &m32, imm8);
}

void emit_shl_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0xc1, 0x04, &m64, imm8);
}

void emit_shl_r8_cl(code_buffer_t *emitter, unsigned r8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x04, &m8);
}

void emit_shl_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x04, &m8);
}

void emit_shl_r16_cl(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x04, &m16);
}

void emit_shl_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x04, &m16);
}

void emit_shl_r32_cl(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x04, &m32);
}

void emit_shl_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x04, &m32);
}

void emit_shl_r64_cl(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x04, &m64);
}

void emit_shl_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x04, &m64);
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

void emit_shr_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0xc1, 0x05, &m32, imm8);
}

void emit_shr_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0xc1, 0x05, &m64, imm8);
}

void emit_shr_r8_cl(code_buffer_t *emitter, unsigned r8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x05, &m8);
}

void emit_shr_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x05, &m8);
}

void emit_shr_r16_cl(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x05, &m16);
}

void emit_shr_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x05, &m16);
}

void emit_shr_r32_cl(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x05, &m32);
}

void emit_shr_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x05, &m32);
}

void emit_shr_r64_cl(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x05, &m64);
}

void emit_shr_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x05, &m64);
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

void emit_sra_r32_imm8(code_buffer_t *emitter, unsigned r32, uint8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0xc1, 0x07, &m32, imm8);
}

void emit_sra_r64_imm8(code_buffer_t *emitter, unsigned r64, uint8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0xc1, 0x07, &m64, imm8);
}

void emit_sra_r8_cl(code_buffer_t *emitter, unsigned r8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x07, &m8);
}

void emit_sra_m8_cl(code_buffer_t *emitter, x86_64_mem_t m8) {
    emit_instruction_1_Eb_CL(emitter, 0xd2, 0x07, &m8);
}

void emit_sra_r16_cl(code_buffer_t *emitter, unsigned r16) {
    x86_64_mem_t m16 = mem_direct(r16);
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x07, &m16);
}

void emit_sra_m16_cl(code_buffer_t *emitter, x86_64_mem_t m16) {
    emit_instruction_1_Ev_CL(emitter, 16, 0xd3, 0x07, &m16);
}

void emit_sra_r32_cl(code_buffer_t *emitter, unsigned r32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x07, &m32);
}

void emit_sra_m32_cl(code_buffer_t *emitter, x86_64_mem_t m32) {
    emit_instruction_1_Ev_CL(emitter, 32, 0xd3, 0x07, &m32);
}

void emit_sra_r64_cl(code_buffer_t *emitter, unsigned r64) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x07, &m64);
}

void emit_sra_m64_cl(code_buffer_t *emitter, x86_64_mem_t m64) {
    emit_instruction_1_Ev_CL(emitter, 64, 0xd3, 0x07, &m64);
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

void emit_sub_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_instruction_1_AL_Ib(emitter, 0x2c, imm8);
}

void emit_sub_eax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_instruction_1_rAX_Iz(emitter, 32, 0x2d, imm32);
}

void emit_sub_rax_imm32(code_buffer_t *emitter, int32_t imm32) {
    emit_instruction_1_rAX_Iz(emitter, 64, 0x2d, imm32);
}

void emit_sub_r8_imm8(code_buffer_t *emitter, unsigned r8, int8_t imm8) {
    x86_64_mem_t m8 = mem_direct(r8);
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x05, &m8, imm8);
}

void emit_sub_m8_imm8(code_buffer_t *emitter, x86_64_mem_t m8, int8_t imm8) {
    emit_instruction_1_Eb_Ib(emitter, 0x80, 0x05, &m8, imm8);
}

void emit_sub_r32_imm32(code_buffer_t *emitter, unsigned r32, int32_t imm32) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x05, &m32, imm32);
}

void emit_sub_m32_imm32(code_buffer_t *emitter, x86_64_mem_t m32, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 32, 0x81, 0x05, &m32, imm32);
}

void emit_sub_r64_imm32(code_buffer_t *emitter, unsigned r64, int32_t imm32) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x05, &m64, imm32);
}

void emit_sub_m64_imm32(code_buffer_t *emitter, x86_64_mem_t m64, int32_t imm32) {
    emit_instruction_1_Ev_Iz(emitter, 64, 0x81, 0x05, &m64, imm32);
}

void emit_sub_r32_imm8(code_buffer_t *emitter, unsigned r32, int8_t imm8) {
    x86_64_mem_t m32 = mem_direct(r32);
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x05, &m32, imm8);
}

void emit_sub_m32_imm8(code_buffer_t *emitter, x86_64_mem_t m32, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 32, 0x83, 0x05, &m32, imm8);
}

void emit_sub_r64_imm8(code_buffer_t *emitter, unsigned r64, int8_t imm8) {
    x86_64_mem_t m64 = mem_direct(r64);
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x05, &m64, imm8);
}

void emit_sub_m64_imm8(code_buffer_t *emitter, x86_64_mem_t m64, int8_t imm8) {
    emit_instruction_1_Ev_Ib(emitter, 64, 0x83, 0x05, &m64, imm8);
}

void emit_sub_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    x86_64_mem_t m8 = mem_direct(dr8);
    emit_instruction_1_Eb_Gb(emitter, 0x28, &m8, sr8);
}

void emit_sub_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_instruction_1_Eb_Gb(emitter, 0x28, &m8, r8);
}

void emit_sub_r8_m8(code_buffer_t *emitter, unsigned r8, x86_64_mem_t m8) {
    emit_instruction_1_Gb_Eb(emitter, 0x2a, r8, &m8);
}

void emit_sub_r16_r16(code_buffer_t *emitter, unsigned dr16, unsigned sr16) {
    x86_64_mem_t m16 = mem_direct(dr16);
    emit_instruction_1_Ev_Gv(emitter, 16, 0x29, &m16, sr16);
}

void emit_sub_m16_r16(code_buffer_t *emitter, x86_64_mem_t m16, unsigned r16) {
    emit_instruction_1_Ev_Gv(emitter, 16, 0x29, &m16, r16);
}

void emit_sub_r16_m16(code_buffer_t *emitter, unsigned r16, x86_64_mem_t m16) {
    emit_instruction_1_Gv_Ev(emitter, 16, 0x2b, r16, &m16);
}

void emit_sub_r32_r32(code_buffer_t *emitter, unsigned dr32, unsigned sr32) {
    x86_64_mem_t m32 = mem_direct(dr32);
    emit_instruction_1_Ev_Gv(emitter, 32, 0x29, &m32, sr32);
}

void emit_sub_m32_r32(code_buffer_t *emitter, x86_64_mem_t m32, unsigned r32) {
    emit_instruction_1_Ev_Gv(emitter, 32, 0x29, &m32, r32);
}

void emit_sub_r32_m32(code_buffer_t *emitter, unsigned r32, x86_64_mem_t m32) {
    emit_instruction_1_Gv_Ev(emitter, 32, 0x2b, r32, &m32);
}

void emit_sub_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x29, &m64, sr64);
}

void emit_sub_m64_r64(code_buffer_t *emitter, x86_64_mem_t m64, unsigned r64) {
    emit_instruction_1_Ev_Gv(emitter, 64, 0x29, &m64, r64);
}

void emit_sub_r64_m64(code_buffer_t *emitter, unsigned r64, x86_64_mem_t m64) {
    emit_instruction_1_Gv_Ev(emitter, 64, 0x2b, r64, &m64);
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

void emit_test_al_imm8(code_buffer_t *emitter, int8_t imm8) {
    emit_instruction_1_AL_Ib(emitter, 0xa8, imm8);
}

void emit_test_r8_r8(code_buffer_t *emitter, unsigned dr8, unsigned sr8) {
    x86_64_mem_t m8 = mem_direct(dr8);
    emit_instruction_1_Eb_Gb(emitter, 0x84, &m8, sr8);
}

void emit_test_m8_r8(code_buffer_t *emitter, x86_64_mem_t m8, unsigned r8) {
    emit_instruction_1_Eb_Gb(emitter, 0x84, &m8, r8);
}

void emit_xor_r64_r64(code_buffer_t *emitter, unsigned dr64, unsigned sr64) {
    x86_64_mem_t m64 = mem_direct(dr64);
    emit_instruction_1_Ev_Gv(emitter, 64, 0x31, &m64, sr64);
}


/** Compare two operands, return true if and only if they are identical. */
bool op_equals(x86_64_operand_t const *op0, x86_64_operand_t const *op1) {
    if (op0->kind != op1->kind) {
        return false;
    }
    else if (op0->kind == IMMEDIATE) {
        return op0->imm == op1->imm;
    }
    else if (op0->kind == REGISTER) {
        return op0->reg == op1->reg;
    }
    else if (op0->kind == MEMORY) {
        return op0->mem.mode == op1->mem.mode &&
            op0->mem.rm == op1->mem.rm &&
            op0->mem.base == op1->mem.base &&
            op0->mem.index == op1->mem.index &&
            op0->mem.scale == op1->mem.scale &&
            op0->mem.disp == op1->mem.disp;
    }
    else {
        return false;
    }
}

typedef enum x86_64_operand_pair_kind {
    MEMORY_MEMORY,
    MEMORY_REGISTER,
    MEMORY_IMMEDIATE,
    REGISTER_MEMORY,
    REGISTER_REGISTER,
    REGISTER_IMMEDIATE,
    IMMEDIATE_MEMORY,
    IMMEDIATE_REGISTER,
    IMMEDIATE_IMMEDIATE,
} x86_64_operand_pair_kind_t;

static inline
x86_64_operand_pair_kind_t op_pair_kind(x86_64_operand_t *op0,
                                        x86_64_operand_t *op1) {
    return op0->kind * 3 + op1->kind;
}

/**
 * Generate a binary instruction.
 * @param emitter       Selected code emitter.
 * @param opcode        Opcode for the Eb,Gb addressing mode.
 * @param opcode_ext    Opcode extension.
 * @param op0           Destination operand.
 * @param op1           Source operand.
 */
static void emit_binop_op0_op1(code_buffer_t *emitter,
                               uint8_t opcode, uint8_t opcode_ext,
                               x86_64_operand_t *op0,
                               x86_64_operand_t *op1) {
    unsigned size = op0->size;
    x86_64_mem_t m;

    switch (op_pair_kind(op0, op1)) {
    case MEMORY_REGISTER:
        if (size == 8) {
            emit_instruction_1_Eb_Gb(emitter,
                opcode | 0x00, &op0->mem, op1->reg);
        }
        else {
            emit_instruction_1_Ev_Gv(emitter, size,
                opcode | 0x01, &op0->mem, op1->reg);
        }
        break;

    case MEMORY_IMMEDIATE:
        if (size == 8) {
            emit_instruction_1_Eb_Ib(emitter,
                0x80, opcode_ext, &op0->mem, op1->imm);
        }
        else if (is_int8(op1->imm)) {
            emit_instruction_1_Ev_Ib(emitter, size,
                0x83, opcode_ext, &op0->mem, op1->imm);
        }
        else {
            emit_instruction_1_Ev_Iz(emitter, size,
                0x81, opcode_ext, &op0->mem, op1->imm);
        }
        break;

    case REGISTER_MEMORY:
        if (size == 8) {
            emit_instruction_1_Gb_Eb(emitter,
                opcode | 0x02, op0->reg, &op1->mem);
        }
        else {
            emit_instruction_1_Gv_Ev(emitter, size,
                opcode | 0x03, op0->reg, &op1->mem);
        }
        break;

    case REGISTER_REGISTER:
        m = mem_direct(op0->reg);
        if (size == 8) {
            emit_instruction_1_Eb_Gb(emitter,
                opcode | 0x00, &m, op1->reg);
        }
        else {
            emit_instruction_1_Ev_Gv(emitter, size,
                opcode | 0x01, &m, op1->reg);
        }
        break;

    case REGISTER_IMMEDIATE:
        m = mem_direct(op0->reg);
        if (size == 8 && op0->reg == AL) {
            emit_instruction_1_AL_Ib(emitter,
                opcode | 0x04, op1->imm);
        }
        else if (op0->reg == AX) {
            emit_instruction_1_rAX_Iz(emitter, size,
                opcode | 0x05, op1->imm);
        }
        else if (size == 8) {
            emit_instruction_1_Eb_Ib(emitter,
                0x80, opcode_ext, &m, op1->imm);
        }
        else if (is_int8(op1->imm)) {
            emit_instruction_1_Ev_Ib(emitter, size,
                0x83, opcode_ext, &m, op1->imm);
        }
        else {
            emit_instruction_1_Ev_Iz(emitter, size,
                0x81, opcode_ext, &m, op1->imm);
        }
        break;

    case MEMORY_MEMORY:
    case IMMEDIATE_MEMORY:
    case IMMEDIATE_REGISTER:
    case IMMEDIATE_IMMEDIATE:
    default:
        printf("emit_binop_op0_op1: bad parameters\n");
        fail_code_buffer(emitter);
        break;
    }
}

void emit_add_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x00, 0x00, op0, op1);
}

void emit_adc_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x10, 0x02, op0, op1);
}

void emit_and_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x20, 0x04, op0, op1);
}

void emit_xor_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x30, 0x06, op0, op1);
}

void emit_or_op0_op1(code_buffer_t *emitter,
                     x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x08, 0x01, op0, op1);
}

void emit_sbb_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x18, 0x03, op0, op1);
}

void emit_sub_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x28, 0x05, op0, op1);
}

void emit_cmp_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_binop_op0_op1(emitter, 0x38, 0x07, op0, op1);
}

/**
 * Generate a unary instruction.
 * @param emitter       Selected code emitter.
 * @param opcode_ext    Opcode extension.
 * @param op0           Source operand.
 */
static void emit_unop_op0(code_buffer_t *emitter, uint8_t opcode_ext,
                          x86_64_operand_t *op0) {
    unsigned size = op0->size;
    x86_64_mem_t m;

    switch (op0->kind) {
    case MEMORY:
        if (size == 8) {
            emit_instruction_1_Eb(emitter, 0xf6, opcode_ext, &op0->mem);
        }
        else {
            emit_instruction_1_Ev(emitter, size, 0xf7, opcode_ext, &op0->mem);
        }
        break;

    case REGISTER:
        m = mem_direct(op0->reg);
        if (size == 8) {
            emit_instruction_1_Eb(emitter, 0xf6, opcode_ext, &m);
        }
        else {
            emit_instruction_1_Ev(emitter, size, 0xf7, opcode_ext, &m);
        }
        break;

    case IMMEDIATE:
    default:
        printf("emit_neg_op0: bad parameters\n");
        fail_code_buffer(emitter);
        break;
    }
}

void emit_not_op0(code_buffer_t *emitter, x86_64_operand_t *op0) {
    emit_unop_op0(emitter, 0x02, op0);
}

void emit_neg_op0(code_buffer_t *emitter, x86_64_operand_t *op0) {
    emit_unop_op0(emitter, 0x03, op0);
}

/**
 * Generate an shift instruction.
 * @param emitter       Selected code emitter.
 * @param opcode_ext    Opcode extension.
 * @param op0           Destination operand.
 * @param op1           Source operand.
 */
static void emit_shift_op0_op1(code_buffer_t *emitter,
                               uint8_t opcode_ext,
                               x86_64_operand_t *op0,
                               x86_64_operand_t *op1) {
    unsigned size = op0->size;
    x86_64_mem_t m;

    switch (op_pair_kind(op0, op1)) {
    case MEMORY_REGISTER:
        if (size == 8) {
            emit_instruction_1_Eb_CL(emitter,
                0xd2, opcode_ext, &op0->mem);
        }
        else {
            emit_instruction_1_Ev_CL(emitter, size,
                0xd3, opcode_ext, &op0->mem);
        }
        break;

    case MEMORY_IMMEDIATE:
        if (size == 8 && op1->imm == 1) {
            emit_instruction_1_Eb_1(emitter,
                0xd0, opcode_ext, &op0->mem);
        }
        else if (size == 8) {
            emit_instruction_1_Eb_Ib(emitter,
                0xc0, opcode_ext, &op0->mem, op1->imm);
        }
        else if (op1->imm == 1) {
            emit_instruction_1_Ev_1(emitter, size,
                0xd1, opcode_ext, &op0->mem);
        }
        else {
            emit_instruction_1_Ev_Ib(emitter, size,
                0xc1, opcode_ext, &op0->mem, op1->imm);
        }
        break;

    case REGISTER_REGISTER:
        m = mem_direct(op0->reg);
        if (size == 8) {
            emit_instruction_1_Eb_CL(emitter,
                0xd2, opcode_ext, &m);
        }
        else {
            emit_instruction_1_Ev_CL(emitter, size,
                0xd3, opcode_ext, &m);
        }
        break;

    case REGISTER_IMMEDIATE:
        m = mem_direct(op0->reg);
        if (size == 8 && op1->imm == 1) {
            emit_instruction_1_Eb_1(emitter,
                0xd0, opcode_ext, &m);
        }
        else if (size == 8) {
            emit_instruction_1_Eb_Ib(emitter,
                0xc0, opcode_ext, &m, op1->imm);
        }
        else if (op1->imm == 1) {
            emit_instruction_1_Ev_1(emitter, size,
                0xd1, opcode_ext, &m);
        }
        else {
            emit_instruction_1_Ev_Ib(emitter, size,
                0xc1, opcode_ext, &m, op1->imm);
        }
        break;

    case MEMORY_MEMORY:
    case REGISTER_MEMORY:
    case IMMEDIATE_MEMORY:
    case IMMEDIATE_REGISTER:
    case IMMEDIATE_IMMEDIATE:
    default:
        printf("emit_shift_op0_op1: bad parameters\n");
        fail_code_buffer(emitter);
        break;
    }
}

void emit_rol_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x00, op0, op1);
}

void emit_ror_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x01, op0, op1);
}

void emit_rcl_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x02, op0, op1);
}

void emit_rcr_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x03, op0, op1);
}

void emit_shl_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x04, op0, op1);
}

void emit_shr_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x05, op0, op1);
}

void emit_sra_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0, x86_64_operand_t *op1) {
    emit_shift_op0_op1(emitter, 0x07, op0, op1);
}


void emit_mov_op0_op1(code_buffer_t *emitter,
                      x86_64_operand_t *op0,
                      x86_64_operand_t *op1) {
    unsigned size = op0->size;
    x86_64_mem_t m;

    switch (op_pair_kind(op0, op1)) {
    case MEMORY_REGISTER:
        if (size == 8) {
            emit_instruction_1_Eb_Gb(emitter,
                0x88, &op0->mem, op1->reg);
        }
        else {
            emit_instruction_1_Ev_Gv(emitter, size,
                0x89, &op0->mem, op1->reg);
        }
        break;

    case MEMORY_IMMEDIATE:
        if (size == 8) {
            emit_instruction_1_Eb_Ib(emitter,
                0xc6, 0x00, &op0->mem, op1->imm);
        }
        else {
            emit_instruction_1_Ev_Iz(emitter, size,
                0xc7, 0x00, &op0->mem, op1->imm);
        }
        break;

    case REGISTER_MEMORY:
        if (size == 8) {
            emit_instruction_1_Gb_Eb(emitter,
                0x8a, op0->reg, &op1->mem);
        }
        else {
            emit_instruction_1_Gv_Ev(emitter, size,
                0x8b, op0->reg, &op1->mem);
        }
        break;

    case REGISTER_REGISTER:
        m = mem_direct(op0->reg);
        if (size == 8) {
            emit_instruction_1_Eb_Gb(emitter,
                0x88, &m, op1->reg);
        }
        else {
            emit_instruction_1_Ev_Gv(emitter, size,
                0x89, &m, op1->reg);
        }
        break;

    case REGISTER_IMMEDIATE:
        if (size == 8) {
            emit_instruction_1_Kb_Ib(emitter,
                0xb0, op0->reg, op1->imm);
        }
        else {
            emit_instruction_1_Kv_Iv(emitter, size,
                0xb8, op0->reg, op1->imm);
        }
        break;

    case MEMORY_MEMORY:
    case IMMEDIATE_MEMORY:
    case IMMEDIATE_REGISTER:
    case IMMEDIATE_IMMEDIATE:
    default:
        printf("emit_mov_op0_op1: bad parameters %d %d\n", op0->kind, op1->kind);
        fail_code_buffer(emitter);
        break;
    }
}

/**
 * Convert a binary instruction with separate destination operand into
 * a simple instruction operating on the left operand. Namely insert
 * a move from the left operand to the destination if necessary.
 * @param emitter       Selected code emitter.
 * @param opcode        Opcode for the Eb,Gb addressing mode.
 * @param opcode_ext    Opcode extension.
 * @param dst           Destination operand. Must not be an immediate operand.
 * @param src0          First source operand.
 * @param src1          Second source operand.
 */
static void emit_binop_dst_src0_src1(code_buffer_t *emitter,
                                     uint8_t opcode, uint8_t opcode_ext,
                                     x86_64_operand_t *dst,
                                     x86_64_operand_t *src0,
                                     x86_64_operand_t *src1) {
    if (dst->size != src0->size ||
        dst->size != src1->size) {
        // raise_recompiler_error(emitter->error_handler,
        //     "emit_add: operands have differing widths");
        fail_code_buffer(emitter);
        return;
    }

#define mode_index(dst, src0, src1, eq) \
    (18 * (dst) + 6 * (src0) + 2 * (src1) + (eq))

    enum { MODE_INVAL = 0, MODE_0, MODE_1, MODE_2, MODE_3, };
    static unsigned char modes[54] = {
        [mode_index(MEMORY,   MEMORY,   MEMORY,    0)] = MODE_2,
        [mode_index(MEMORY,   MEMORY,   MEMORY,    1)] = MODE_2,
        [mode_index(MEMORY,   MEMORY,   REGISTER,  0)] = MODE_2,
        [mode_index(MEMORY,   MEMORY,   REGISTER,  1)] = MODE_0,
        [mode_index(MEMORY,   MEMORY,   IMMEDIATE, 0)] = MODE_2,
        [mode_index(MEMORY,   MEMORY,   IMMEDIATE, 1)] = MODE_0,
        [mode_index(MEMORY,   REGISTER, MEMORY,    0)] = MODE_2,
        [mode_index(MEMORY,   REGISTER, REGISTER,  0)] = MODE_1,
        [mode_index(MEMORY,   REGISTER, IMMEDIATE, 0)] = MODE_1,
        [mode_index(REGISTER, MEMORY,   MEMORY,    0)] = MODE_1,
        [mode_index(REGISTER, MEMORY,   REGISTER,  0)] = MODE_1,
        [mode_index(REGISTER, MEMORY,   IMMEDIATE, 0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, MEMORY,    0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, MEMORY,    1)] = MODE_0,
        [mode_index(REGISTER, REGISTER, REGISTER,  0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, REGISTER,  1)] = MODE_0,
        [mode_index(REGISTER, REGISTER, IMMEDIATE, 0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, IMMEDIATE, 1)] = MODE_0,
    };

    bool index = mode_index(dst->kind, src0->kind, src1->kind, op_equals(dst, src0));
    unsigned char mode = modes[index];

#undef mode_index

    // XXX TODO temporary register must be configurable.
    x86_64_operand_t tmp0 = op_reg(src0->size, RAX);
    x86_64_operand_t tmp1 = op_reg(src0->size, RCX);

    // If either source operand is a 64 bit immediate value that is larger
    // than the 32 bit range, the mode is automatically promoted to
    // mode 2 or 3.
    if (src0->kind == IMMEDIATE && src0->size == 64 && !is_int32(src0->imm)) {
        mode = MODE_3;
    }
    if (src1->kind == IMMEDIATE && src1->size == 64 && !is_int32(src1->imm)) {
        mode = MODE_2;
    }

    switch (mode) {
    case MODE_0:
        emit_binop_op0_op1(emitter, opcode, opcode_ext, src0, src1);
        break;
    case MODE_1:
        emit_mov_op0_op1(emitter, dst, src0);
        emit_binop_op0_op1(emitter, opcode, opcode_ext, dst, src1);
        break;
    case MODE_2:
        emit_mov_op0_op1(emitter, &tmp0, src0);
        emit_binop_op0_op1(emitter, opcode, opcode_ext, &tmp0, src1);
        emit_mov_op0_op1(emitter, dst, &tmp0);
        break;
    case MODE_3:
        emit_mov_op0_op1(emitter, &tmp0, src0);
        emit_mov_op0_op1(emitter, &tmp1, src1);
        emit_binop_op0_op1(emitter, opcode, opcode_ext, &tmp0, &tmp1);
        emit_mov_op0_op1(emitter, dst, &tmp0);
        break;
    case MODE_INVAL:
    default:
        // raise_recompiler_error(emitter->error_handler,
        //     "emit_%s: invalid destination or left operand", name);
        fail_code_buffer(emitter);
        break;
    }
}

void emit_add_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x00, 0x00, dst, src0, src1);
}

void emit_adc_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x10, 0x02, dst, src0, src1);
}

void emit_and_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x20, 0x04, dst, src0, src1);
}

void emit_xor_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x30, 0x06, dst, src0, src1);
}

void emit_or_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                           x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x08, 0x01, dst, src0, src1);
}

void emit_sbb_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x18, 0x03, dst, src0, src1);
}

void emit_sub_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_binop_dst_src0_src1(emitter, 0x28, 0x05, dst, src0, src1);
}

void emit_cmp_src0_src1(code_buffer_t *emitter,
                        x86_64_operand_t *src0, x86_64_operand_t *src1) {
    if ((src0->kind == MEMORY && src1->kind == MEMORY) ||
        src0->kind == IMMEDIATE) {
        x86_64_operand_t tmp = op_reg(src0->size, RAX); // XXX
        emit_mov_op0_op1(emitter, &tmp, src0);
        emit_cmp_op0_op1(emitter, &tmp, src1);
    } else {
        emit_cmp_op0_op1(emitter, src0, src1);
    }
}

/**
 * Convert an unary instruction with separate destination operand into
 * a simple instruction operating on the source operand. Namely insert
 * a move from the source operand to the destination if necessary.
 * @param emitter       Selected code emitter.
 * @param opcode_ext    Opcode extension.
 * @param dst           Destination operand. Must not be an immediate operand.
 * @param src0          Source operand.
 */
static void emit_unop_dst_src0(code_buffer_t *emitter, uint8_t opcode_ext,
                               x86_64_operand_t *dst,
                               x86_64_operand_t *src0) {
    if (dst->size != src0->size) {
        // raise_recompiler_error(emitter->error_handler,
        //     "emit_add: operands have differing widths");
        fail_code_buffer(emitter);
        return;
    }

#define mode_index(dst, src0, eq) \
    (6 * (dst) + 2 * (src0) + (eq))

    enum { MODE_INVAL = 0, MODE_0, MODE_1, MODE_2, };
    static unsigned char modes[18] = {
        [mode_index(MEMORY,   MEMORY,   0)] = MODE_2,
        [mode_index(MEMORY,   MEMORY,   1)] = MODE_0,
        [mode_index(MEMORY,   REGISTER, 0)] = MODE_1,
        [mode_index(REGISTER, MEMORY,   0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, 0)] = MODE_1,
        [mode_index(REGISTER, REGISTER, 1)] = MODE_0,
    };

    bool index = mode_index(dst->kind, src0->kind, op_equals(dst, src0));
    unsigned char mode = modes[index];

#undef mode_index

    // XXX TODO temporary register must be configurable.
    x86_64_operand_t tmp = op_reg(src0->size, RAX);

    switch (mode) {
    case MODE_0:
        emit_unop_op0(emitter, opcode_ext, src0);
        break;
    case MODE_1:
        emit_mov_op0_op1(emitter, dst, src0);
        emit_unop_op0(emitter, opcode_ext, src0);
        break;
    case MODE_2:
        emit_mov_op0_op1(emitter, &tmp, src0);
        emit_unop_op0(emitter, opcode_ext, &tmp);
        emit_mov_op0_op1(emitter, dst, &tmp);
        break;
    case MODE_INVAL:
    default:
        // raise_recompiler_error(emitter->error_handler,
        //     "emit_%s: invalid destination or left operand", name);
        fail_code_buffer(emitter);
        break;
    }
}

void emit_not_dst_src0(code_buffer_t *emitter, x86_64_operand_t *dst,
                       x86_64_operand_t *src0) {
    emit_unop_dst_src0(emitter, 0x02, dst, src0);
}

void emit_neg_dst_src0(code_buffer_t *emitter, x86_64_operand_t *dst,
                       x86_64_operand_t *src0) {
    emit_unop_dst_src0(emitter, 0x03, dst, src0);
}

/**
 * Convert a shift instruction with separate destination operand into
 * a simple instruction operating on the left operand. Namely insert
 * a move from the left operand to the destination if necessary.
 * @param emitter       Selected code emitter.
 * @param opcode_ext    Opcode extension.
 * @param dst           Destination operand. Must not be an immediate operand.
 * @param src0          First source operand.
 * @param src1          Second source operand.
 */
static void emit_shift_dst_src0_src1(code_buffer_t *emitter,
                                     uint8_t opcode_ext,
                                     x86_64_operand_t *dst,
                                     x86_64_operand_t *src0,
                                     x86_64_operand_t *src1) {
    if (dst->size != src0->size || src1->size != 8) {
        // raise_recompiler_error(emitter->error_handler,
        //     "emit_add: operands have differing widths");
        fail_code_buffer(emitter);
        return;
    }

    // XXX TODO temporary register must be configurable.
    x86_64_operand_t cl = op_reg(8, CL);
    x86_64_operand_t tmp = op_reg(src0->size, RAX);

    if (src1->kind != IMMEDIATE &&
        !(src1->kind == REGISTER && src1->reg == CL)) {
        emit_mov_op0_op1(emitter, &cl, src1);
        src1 = &cl;
    }

    if (op_equals(dst, src0)) {
        /* MODE_0 */
        emit_shift_op0_op1(emitter, opcode_ext, dst, src1);
    }
    else if ((dst->kind == MEMORY && src0->kind == MEMORY) ||
             (src0->kind == IMMEDIATE && !is_int32(src0->imm))) {
        /* MODE_2 */
        emit_mov_op0_op1(emitter, &tmp, src0);
        emit_shift_op0_op1(emitter, opcode_ext, &tmp, src1);
        emit_mov_op0_op1(emitter, dst, &tmp);
    }
    else {
        /* MODE_1 */
        emit_mov_op0_op1(emitter, dst, src0);
        emit_shift_op0_op1(emitter, opcode_ext, dst, src1);
    }
}

void emit_rol_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x00, dst, src0, src1);
}

void emit_ror_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x01, dst, src0, src1);
}

void emit_rcl_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x02, dst, src0, src1);
}

void emit_rcr_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x03, dst, src0, src1);
}

void emit_shl_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x04, dst, src0, src1);
}

void emit_shr_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x05, dst, src0, src1);
}

void emit_sra_dst_src0_src1(code_buffer_t *emitter, x86_64_operand_t *dst,
                            x86_64_operand_t *src0, x86_64_operand_t *src1) {
    emit_shift_dst_src0_src1(emitter, 0x07, dst, src0, src1);
}


void emit_mov_dst_src0(code_buffer_t *emitter,
                       x86_64_operand_t *dst, x86_64_operand_t *src0) {
    if (dst->kind == MEMORY && src0->kind == MEMORY) {
        x86_64_operand_t tmp = op_reg(src0->size, RAX);
        emit_mov_op0_op1(emitter, &tmp, src0);
        emit_mov_op0_op1(emitter, dst, &tmp);
    } else {
        emit_mov_op0_op1(emitter, dst, src0);
    }
}
