
/*
 * This file implements the vector instructions using x86_64 sse2
 * features, overriding the default implementations in rsp.cc
 */

#include <cstring>
#include <iomanip>
#include <iostream>

#include <core.h>
#include <circular_buffer.h>
#include <assembly/disassembler.h>
#include <assembly/opcodes.h>
#include <assembly/registers.h>
#include <r4300/cpu.h>
#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <debugger.h>
#include <interpreter.h>
#include <types.h>

#include <immintrin.h>

using namespace R4300;
using namespace n64;

namespace interpreter::rsp {

/**
 * Helper for loading a vector register into an m128i value.
 */
static __m128i mm_load_vr(uint32_t vr) {
    return _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
}

/**
 * Helper for loading a vector register into an m128i value with
 * proper element selection applied.
 */
static __m128i mm_load_vr_elt(uint32_t vr, uint32_t e) {
    __m128i res;
    switch (e) {
    case 0 ... 1:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        break;
    case 2:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b10100000);
        res = _mm_shufflelo_epi16(res, 0b10100000);
        break;
    case 3:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b11110101);
        res = _mm_shufflelo_epi16(res, 0b11110101);
        break;
    case 4:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b00000000);
        res = _mm_shufflelo_epi16(res, 0b00000000);
        break;
    case 5:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b01010101);
        res = _mm_shufflelo_epi16(res, 0b01010101);
        break;
    case 6:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b10101010);
        res = _mm_shufflelo_epi16(res, 0b10101010);
        break;
    case 7:
        res = _mm_load_si128((__m128i *)(state.rspreg.vr + vr));
        res = _mm_shufflehi_epi16(res, 0b11111111);
        res = _mm_shufflelo_epi16(res, 0b11111111);
        break;
    case 8 ... 15:
        res = _mm_set1_epi16(state.rspreg.vr[vr].h[e - 8]);
        break;
    default:
        res = _mm_setzero_si128();
        break;
    }
    return res;
}

/**
 * Helper for storing a vector register from an m128i value.
 */
static void mm_store_vr(uint32_t vr, __m128i val) {
    return _mm_store_si128((__m128i *)(state.rspreg.vr + vr), val);
}

/**
 * Helper for loading the accumulator into high, middle, and low
 * vector registers.
 */
static void mm_load_acc(__m128i *acc_hi, __m128i *acc_md, __m128i *acc_lo) {
    *acc_hi = _mm_load_si128((__m128i *)state.rspreg.vacc.hi.h);
    *acc_md = _mm_load_si128((__m128i *)state.rspreg.vacc.md.h);
    *acc_lo = _mm_load_si128((__m128i *)state.rspreg.vacc.lo.h);
}

/**
 * Helper for storing the accumulator from high, middle, and low
 * vector registers.
 */
static void mm_store_acc(__m128i acc_hi, __m128i acc_md, __m128i acc_lo) {
    _mm_store_si128((__m128i *)state.rspreg.vacc.hi.h, acc_hi);
    _mm_store_si128((__m128i *)state.rspreg.vacc.md.h, acc_md);
    _mm_store_si128((__m128i *)state.rspreg.vacc.lo.h, acc_lo);
}

/**
 * Helper for updating the lower word of the accumulator.
 */
static void mm_store_acc_lo(__m128i acc_lo) {
    _mm_store_si128((__m128i *)state.rspreg.vacc.lo.h, acc_lo);
}

/**
 * Helper for adding eight 48bit values split over high,
 * middle, and low 128bit vectors.
 */
static void mm_add_epi48(__m128i const a_hi,
                         __m128i const a_md,
                         __m128i const a_lo,
                         __m128i const b_hi,
                         __m128i const b_md,
                         __m128i const b_lo,
                         __m128i *res_hi_ptr,
                         __m128i *res_md_ptr,
                         __m128i *res_lo_ptr) {

    // Add high, mid, low parts without carry.
    __m128i res_hi = _mm_add_epi16(a_hi, b_hi);
    __m128i res_md = _mm_add_epi16(a_md, b_md);
    __m128i res_lo = _mm_add_epi16(a_lo, b_lo);

    // Compute the carry for mid and low parts.
    // The result is 0 for no carry, -1 otherwise.
    __m128i signbit = _mm_set1_epi16(0x8000);
    __m128i carry_lo = _mm_cmpgt_epi16(
        _mm_xor_si128(b_lo, signbit),
        _mm_xor_si128(res_lo, signbit));
    __m128i carry_md = _mm_cmpgt_epi16(
        _mm_xor_si128(b_md, signbit),
        _mm_xor_si128(res_md, signbit));

    // Check if adding the carry to res_md overflows. This is the case
    // if res_md is already 0xffff, and the carry from res_lo is 0xffff.
    __m128i carry_md2 = _mm_and_si128(
        _mm_cmpeq_epi16(res_md, carry_lo), carry_lo);

    // Add the computed carries.
    res_hi = _mm_sub_epi16(res_hi, carry_md);
    res_hi = _mm_sub_epi16(res_hi, carry_md2);
    res_md = _mm_sub_epi16(res_md, carry_lo);

    *res_hi_ptr = res_hi;
    *res_md_ptr = res_md;
    *res_lo_ptr = res_lo;
}

/**
 * Helper implementing a ternary operator.
 * Cond must contain valid boolean values, i.e. 0000 for false and 1111 for
 * true. Return the result of cond ? a : b for each component.
 */
static __m128i mm_select_epi16(__m128i const cond,
                               __m128i const a,
                               __m128i const b) {
    return _mm_or_si128(_mm_and_si128(cond, a), _mm_andnot_si128(cond, b));
}

/**
 * Helper implementing the binary complement function.
 */
static __m128i mm_not_si128(__m128i const a) {
    return _mm_xor_si128(a, _mm_set1_epi32(0xffffffff));
}

/**
 * Helper to perform signed clamp on a two word value.
 */
static __m128i mm_clamphi_epi48(__m128i const hi, __m128i const md) {
    __m128i const hi_sign = _mm_srai_epi16(hi, 15);
    __m128i const md_sign = _mm_srai_epi16(md, 15);
    __m128i const in_range = _mm_cmpeq_epi16(hi, md_sign);
    return mm_select_epi16(in_range, md,
        _mm_xor_si128(hi_sign, _mm_set1_epi16(0x7fff)));
}

/**
 * Helper to perform unsigned clamp on a two word value.
 */
static __m128i mm_clamphi_epu48(__m128i const hi, __m128i const md) {
    __m128i const hi_sign = _mm_srai_epi16(hi, 15);
    __m128i const md_sign = _mm_srai_epi16(md, 15);
    __m128i const in_range = _mm_cmpeq_epi16(hi, md_sign);
    return mm_select_epi16(hi_sign, _mm_setzero_si128(),
        mm_select_epi16(in_range, md, _mm_set1_epi16(UINT16_MAX)));
}

/**
 * Helper to perform unsigned clamp on a two word value.
 */
static __m128i mm_clamplo_epi48(__m128i const hi,
                                __m128i const md,
                                __m128i const lo) {
    __m128i const hi_sign = _mm_srai_epi16(hi, 15);
    __m128i const md_sign = _mm_srai_epi16(md, 15);
    __m128i const in_range = _mm_cmpeq_epi16(hi, md_sign);
    return mm_select_epi16(in_range, lo,
        _mm_xor_si128(hi_sign, _mm_set1_epi16(0xffff)));
}


void eval_VAND(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_and_si128(a, b);

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

void eval_VMACF(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute multiplication, with sign extension.
    // Shift the result left by one.
    __m128i mul_lo = _mm_mullo_epi16(a, b);
    __m128i mul_md = _mm_mulhi_epi16(a, b);
    __m128i mul_hi = _mm_srai_epi16(mul_md, 15);

    mul_hi = _mm_or_si128(
        _mm_slli_epi16(mul_hi, 1),
        _mm_srli_epi16(mul_md, 15));
    mul_md = _mm_or_si128(
        _mm_slli_epi16(mul_md, 1),
        _mm_srli_epi16(mul_lo, 15));
    mul_lo =
        _mm_slli_epi16(mul_lo, 1);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        mul_hi, mul_md, mul_lo,
        &acc_hi, &acc_md, &acc_lo);

    // The result is the middle word of the accumulator,
    // signed clamped.
    __m128i res = mm_clamphi_epi48(acc_hi, acc_md);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

/** SSE2 implementation not required */
void eval_VMACQ(u32 instr);

void eval_VMACU(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute multiplication, with sign extension.
    // Shift the result left by one.
    __m128i mul_lo = _mm_mullo_epi16(a, b);
    __m128i mul_md = _mm_mulhi_epi16(a, b);
    __m128i mul_hi = _mm_srai_epi16(mul_md, 15);

    mul_hi = _mm_or_si128(
        _mm_slli_epi16(mul_hi, 1),
        _mm_srli_epi16(mul_md, 15));
    mul_md = _mm_or_si128(
        _mm_slli_epi16(mul_md, 1),
        _mm_srli_epi16(mul_lo, 15));
    mul_lo =
        _mm_slli_epi16(mul_lo, 1);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        mul_hi, mul_md, mul_lo,
        &acc_hi, &acc_md, &acc_lo);

    // The result is the middle word of the accumulator,
    // unsigned clamped.
    __m128i const res = mm_clamphi_epu48(acc_hi, acc_md);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

void eval_VMADH(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute multiplication, with sign extension.
    __m128i const mul_lo = _mm_mullo_epi16(a, b);
    __m128i mul_hi = _mm_mulhi_epi16(a, b);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        mul_hi, mul_lo, _mm_setzero_si128(),
        &acc_hi, &acc_md, &acc_lo);

    // The result is the middle word of the accumulator,
    // signed clamped.
    __m128i const res = mm_clamphi_epi48(acc_hi, acc_md);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

void eval_VMADL(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute high word of multiplication.
    __m128i const mul_hi = _mm_mulhi_epu16(a, b);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        _mm_setzero_si128(), _mm_setzero_si128(), mul_hi,
        &acc_hi, &acc_md, &acc_lo);

    // The result is the lower word of the accumulator,
    // unsigned clamped.
    __m128i const res = mm_clamplo_epi48(acc_hi, acc_md, acc_lo);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

void eval_VMADM(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute multiplication, with sign extension.
    __m128i const mul_lo = _mm_mullo_epi16(a, b);
    __m128i mul_md = _mm_mulhi_epi16(a, b);
    mul_md = _mm_add_epi16(mul_md, _mm_and_si128(a, _mm_srai_epi16(b, 15)));
    __m128i mul_hi = _mm_srai_epi16(mul_md, 15);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        mul_hi, mul_md, mul_lo,
        &acc_hi, &acc_md, &acc_lo);

    // The result is the middle word of the accumulator,
    // signed clamped.
    __m128i const res = mm_clamphi_epi48(acc_hi, acc_md);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

void eval_VMADN(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);
    __m128i acc_hi, acc_md, acc_lo;
    mm_load_acc(&acc_hi, &acc_md, &acc_lo);

    // Compute multiplication, with sign extension.
    __m128i const mul_lo = _mm_mullo_epi16(a, b);
    __m128i mul_md = _mm_mulhi_epi16(a, b);
    mul_md = _mm_add_epi16(mul_md, _mm_and_si128(b, _mm_srai_epi16(a, 15)));
    __m128i mul_hi = _mm_srai_epi16(mul_md, 15);

    // Add the multiplication result to the accumulator.
    mm_add_epi48(
        acc_hi, acc_md, acc_lo,
        mul_hi, mul_md, mul_lo,
        &acc_hi, &acc_md, &acc_lo);

    // The result is the lower word of the accumulator,
    // unsigned clamped.
    __m128i const res = mm_clamplo_epi48(acc_hi, acc_md, acc_lo);

    mm_store_vr(vd, res);
    mm_store_acc(acc_hi, acc_md, acc_lo);
}

/** SSE2 implementation not required */
void eval_VMOV(u32 instr);

void eval_VNAND(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_xor_si128(
        _mm_and_si128(a, b),
        _mm_set1_epi32(0xffffffff));

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

/** SSE2 implementation not required */
void eval_VNOP(u32 instr);

/** SSE2 implementation not required */
void eval_VNULL(u32 instr);

void eval_VNOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_xor_si128(
        _mm_or_si128(a, b),
        _mm_set1_epi32(0xffffffff));

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

void eval_VNXOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_xor_si128(
        _mm_xor_si128(a, b),
        _mm_set1_epi32(0xffffffff));

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

void eval_VOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_or_si128(a, b);

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

void eval_VXOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

    // Load inputs.
    __m128i const a = mm_load_vr(vs);
    __m128i const b = mm_load_vr_elt(vt, e);

    // Compute binary and result.
    __m128i const res = _mm_xor_si128(a, b);

    // Save result to output register and lower accumulator word.
    mm_store_vr(vd, res);
    mm_store_acc_lo(res);
}

}; /* namespace interpreter::rsp */
