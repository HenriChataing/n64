/*
 * Copyright (c) 2017-2017 Prove & Run S.A.S
 * All Rights Reserved.
 *
 * This software is the confidential and proprietary information of
 * Prove & Run S.A.S ("Confidential Information"). You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered
 * into with Prove & Run S.A.S
 *
 * PROVE & RUN S.A.S MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE
 * SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT. PROVE & RUN S.A.S SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING,
 * MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 */
/**
 * @file
 * @brief
 * @author Henri Chataing
 * @date July 04th, 2017 (creation)
 * @copyright (c) 2017-2017, Prove & Run and/or its affiliates.
 *   All rights reserved.
 */

#ifndef _EMITTER_H_INCLUDED_
#define _EMITTER_H_INCLUDED_

#include <cstdlib>
#include <cstddef>
#include <cstring>

#include "type.h"

namespace X86 {

class Mem
{
public:
    Mem(uint code) : mode(code), disp(0), size(0) {}
    Mem(uint code, i32 disp) : disp(disp) {
        i8 disp8 = disp;
        if ((i32)disp8 == disp) {
            mode = code | 0x40;
            size = 1;
        } else {
            mode = code | 0x80;
            size = 4;
        }
    }
    ~Mem() {}

    u8 mode;
    i32 disp;
    uint size;
};

template<typename T>
class Reg
{
public:
    Reg(u8 code) : code(code) {}
    ~Reg() {}

    Mem operator()() {
        return Mem(code);
    }
    Mem operator()(i32 d) {
        return Mem(code, d);
    }
    bool operator==(const Reg<T> &other) const {
        return code == other.code;
    }
    bool operator!=(const Reg<T> &other) const {
        return code != other.code;
    }

    u8 code;
};

extern Reg<u32> eax;
extern Reg<u32> ecx;
extern Reg<u32> edx;
extern Reg<u32> ebx;
extern Reg<u32> esp;
extern Reg<u32> ebp;
extern Reg<u32> esi;
extern Reg<u32> edi;

extern Reg<u16> ax;
extern Reg<u16> cx;
extern Reg<u16> dx;
extern Reg<u16> bx;
extern Reg<u16> sp;
extern Reg<u16> bp;
extern Reg<u16> si;
extern Reg<u16> di;

extern Reg<u8> al;
extern Reg<u8> cl;
extern Reg<u8> dl;
extern Reg<u8> bl;
extern Reg<u8> ah;
extern Reg<u8> ch;
extern Reg<u8> dh;
extern Reg<u8> bh;

static const u32 carry    = 1l << 0;
static const u32 zero     = 1l << 6;
static const u32 sign     = 1l << 7;
static const u32 overflow = 1l << 11;

class Emitter
{
public:
    Emitter(size_t codeSize);
    ~Emitter();

    const u8 *getPtr() const { return _codeBuffer + _codeLength; }
    size_t getSize() const { return _codeLength; }
    void dump() const;
    void dump(const u8 *start) const;

    u32 *CALL(const u8 *loc = NULL) { return jumpAbs(0xe8, loc); }
    void CALL(const Reg<u32> &r) { put(0xff); put(r.code | 0xd0); }
    void CALL(const Mem &m) { put(0xff); put(m.mode | 0x10); put(m); }
    void CALLF(const void *ptr) { put(0x9a); put((u32)ptr); }
    void CALLF(const void **ref) { put(0xff); put(0x18); put((u32)ref); }
    u32 *JMP(const u8 *loc = NULL) { return jumpAbs(0xeb, 0xe9, loc); }
    void JMP(const Reg<u32> &r) { put(0xff); put(r.code | 0xe0); }
    void JMP(const Mem &m) { put(0xff); put(m.mode | 0x20); put(m); }
    void JMPF(const void *ptr) { put(0xe9); put((u32)ptr); }
    void JMPF(const void **ref) { put(0xff); put(0x28); put((u32)ref); }

    void RETN() { put(0xc3); }
    void RETF() { put(0xcb); }

    void PUSHF() { put(0x9c); }
    void POPF() { put(0x9d); }
    void SAHF() { put(0x9e); }
    void LAHF() { put(0x9f); }
    void CLC() { put(0xf8); }
    void CMC() { put(0xf5); }
    void STC() { put(0xf9); }

    u32 *JO(const u8 *loc = NULL) { return jumpCond(0x70, 0x80, loc); }
    u32 *JNO(const u8 *loc = NULL) { return jumpCond(0x71, 0x81, loc); }
    u32 *JB(const u8 *loc = NULL) { return jumpCond(0x72, 0x82, loc); }
    inline u32 *JNAE(const u8 *loc = NULL) { return JB(loc); }
    inline u32 *JC(const u8 *loc = NULL) { return JB(loc); }
    u32 *JNB(const u8 *loc = NULL) { return jumpCond(0x73, 0x83, loc); }
    inline u32 *JAE(const u8 *loc = NULL) { return JNB(loc); }
    inline u32 *JNC(const u8 *loc = NULL) { return JNB(loc); }
    u32 *JZ(const u8 *loc = NULL) { return jumpCond(0x74, 0x84, loc); }
    inline u32 *JE(const u8 *loc = NULL) { return JZ(loc); }
    u32 *JNZ(const u8 *loc = NULL) { return jumpCond(0x75, 0x85, loc); }
    inline u32 *JNE(const u8 *loc = NULL) { return JNZ(loc); }
    u32 *JBE(const u8 *loc = NULL) { return jumpCond(0x76, 0x86, loc); }
    inline u32 *JNA(const u8 *loc = NULL) { return JBE(loc); }
    u32 *JNBE(const u8 *loc = NULL) { return jumpCond(0x77, 0x87, loc); }
    inline u32 *JA(const u8 *loc = NULL) { return JNBE(loc); }
    u32 *JS(const u8 *loc = NULL) { return jumpCond(0x78, 0x88, loc); }
    u32 *JNS(const u8 *loc = NULL) { return jumpCond(0x79, 0x89, loc); }
    u32 *JP(const u8 *loc = NULL) { return jumpCond(0x7a, 0x8a, loc); }
    inline u32 *JPE(const u8 *loc = NULL) { return JP(loc); }
    u32 *JNP(const u8 *loc = NULL) { return jumpCond(0x7b, 0x8b, loc); }
    inline u32 *JPO(const u8 *loc = NULL) { return JNP(loc); }
    u32 *JL(const u8 *loc = NULL) { return jumpCond(0x7c, 0x8c, loc); }
    inline u32 *JNGE(const u8 *loc = NULL) { return JL(loc); }
    u32 *JNL(const u8 *loc = NULL) { return jumpCond(0x7d, 0x8d, loc); }
    inline u32 *JGE(const u8 *loc = NULL) { return JNL(loc); }
    u32 *JLE(const u8 *loc = NULL) { return jumpCond(0x7e, 0x8e, loc); }
    inline u32 *JNG(const u8 *loc = NULL) { return JLE(loc); }
    u32 *JNLE(const u8 *loc = NULL) { return jumpCond(0x7f, 0x8f, loc); }
    inline u32 *JG(const u8 *loc = NULL) { return JNLE(loc); }

    /* Patch the relative offset for a forward jump instruction. */
    void setJump(u32 *ins, const u8 *loc) {
        ptrdiff_t rel = loc - (u8*)ins - 4;
        *ins = (u32)rel;
    }

    /* Missing instructions INC and DEC on BYTE memory locations. */
    void INC(const Reg<u8> &r) { put(0xfe); put(0xc0 | r.code); }
    void INC(const Reg<u32> &r) { put(0x40 | r.code); }
    void INC(const Mem &m) { put(0xff); put(m.mode); put(m); }
    void DEC(const Reg<u8> &r) { put(0xfe); put(0xc8 | r.code); }
    void DEC(const Reg<u32> &r) { put(0x48 | r.code); }
    void DEC(const Mem &m) { put(0xff); put(m.mode | 0x08); put(m); }
    void PUSH(const Reg<u32> &r) { put(0x50 | r.code); }
    void PUSH(const Mem &m) { put(0xff); put(m.mode | 0x30); put(m); }
    void PUSH(u32 v) { put(0x68); put(v); }
    void PUSH(u8 v) { put(0x6a); put(v); }
    void POP(const Reg<u32> &r) { put(0x58 | r.code); }
    void POP(const Mem &m) { put(0x8f); put(m.mode); put(m); }

    void TEST(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x84, r0, r1); }
    void TEST(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x84, r0, r1); }

    void MOV(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x88, r0, r1); }
    void MOV(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x88, r0, r1); }
    void MOV(const Mem &m, const Reg<u8> &r) { binop(0x88, m, r); }
    void MOV(const Mem &m, const Reg<u32> &r) { binop(0x88, m, r); }
    void MOV(const Reg<u8> &r, const Mem &m) { binop(0x88, r, m); }
    void MOV(const Reg<u32> &r, const Mem &m) { binop(0x88, r, m); }
    void MOV(const Reg<u8> &r, u8 v) { put(0xb0 | r.code); put(v); }
    void MOV(const Mem &m, u8 v) { binop(0xc6, 0x0, m, v); }
    void MOV(const Reg<u32> &r, u32 v) { put(0xb8 | r.code); put(v); }
    void MOV(const Mem &m, u32 v) { binop(0xc6, 0x0, m, v); }

    void ADD(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x00, r0, r1); }
    void ADD(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x00, r0, r1); }
    void ADD(const Mem &m, const Reg<u8> &r) { binop(0x00, m, r); }
    void ADD(const Mem &m, const Reg<u32> &r) { binop(0x00, m, r); }
    void ADD(const Reg<u8> &r, const Mem &m) { binop(0x00, r, m); }
    void ADD(const Reg<u32> &r, const Mem &m) { binop(0x00, r, m); }
    void ADD(const Reg<u8> &r, u8 v) { binop(0x04, 0x80, 0x0, r, v); }
    void ADD(const Mem &m, u8 v) { binop(0x80, 0x0, m, v); }
    void ADD(const Reg<u32> &r, u32 v) { binop(0x05, 0x80, 0x0, r, v); }
    void ADD(const Mem &m, u32 v) { binop(0x80, 0x0, m, v); }

    void OR(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x08, r0, r1); }
    void OR(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x08, r0, r1); }
    void OR(const Mem &m, const Reg<u8> &r) { binop(0x08, m, r); }
    void OR(const Mem &m, const Reg<u32> &r) { binop(0x08, m, r); }
    void OR(const Reg<u8> &r, const Mem &m) { binop(0x08, r, m); }
    void OR(const Reg<u32> &r, const Mem &m) { binop(0x08, r, m); }
    void OR(const Reg<u8> &r, u8 v) { binop(0x0c, 0x80, 0x1, r, v); }
    void OR(const Mem &m, u8 v) { binop(0x80, 0x1, m, v); }
    void OR(const Reg<u32> &r, u32 v) { binop(0x0d, 0x80, 0x1, r, v); }
    void OR(const Mem &m, u32 v) { binop(0x80, 0x1, m, v); }

    void ADC(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x10, r0, r1); }
    void ADC(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x10, r0, r1); }
    void ADC(const Mem &m, const Reg<u8> &r) { binop(0x10, m, r); }
    void ADC(const Mem &m, const Reg<u32> &r) { binop(0x10, m, r); }
    void ADC(const Reg<u8> &r, const Mem &m) { binop(0x10, r, m); }
    void ADC(const Reg<u32> &r, const Mem &m) { binop(0x10, r, m); }
    void ADC(const Reg<u8> &r, u8 v) { binop(0x14, 0x80, 0x2, r, v); }
    void ADC(const Mem &m, u8 v) { binop(0x80, 0x2, m, v); }
    void ADC(const Reg<u32> &r, u32 v) { binop(0x15, 0x80, 0x2, r, v); }
    void ADC(const Mem &m, u32 v) { binop(0x80, 0x2, m, v); }

    void SBB(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x18, r0, r1); }
    void SBB(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x18, r0, r1); }
    void SBB(const Mem &m, const Reg<u8> &r) { binop(0x18, m, r); }
    void SBB(const Mem &m, const Reg<u32> &r) { binop(0x18, m, r); }
    void SBB(const Reg<u8> &r, const Mem &m) { binop(0x18, r, m); }
    void SBB(const Reg<u32> &r, const Mem &m) { binop(0x18, r, m); }
    void SBB(const Reg<u8> &r, u8 v) { binop(0x1c, 0x80, 0x3, r, v); }
    void SBB(const Mem &m, u8 v) { binop(0x80, 0x3, m, v); }
    void SBB(const Reg<u32> &r, u32 v) { binop(0x1d, 0x80, 0x3, r, v); }
    void SBB(const Mem &m, u32 v) { binop(0x80, 0x3, m, v); }

    void AND(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x20, r0, r1); }
    void AND(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x20, r0, r1); }
    void AND(const Mem &m, const Reg<u8> &r) { binop(0x20, m, r); }
    void AND(const Mem &m, const Reg<u32> &r) { binop(0x20, m, r); }
    void AND(const Reg<u8> &r, const Mem &m) { binop(0x20, r, m); }
    void AND(const Reg<u32> &r, const Mem &m) { binop(0x20, r, m); }
    void AND(const Reg<u8> &r, u8 v) { binop(0x24, 0x80, 0x4, r, v); }
    void AND(const Mem &m, u8 v) { binop(0x80, 0x4, m, v); }
    void AND(const Reg<u32> &r, u32 v) { binop(0x25, 0x80, 0x4, r, v); }
    void AND(const Mem &m, u32 v) { binop(0x80, 0x4, m, v); }

    void SUB(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x28, r0, r1); }
    void SUB(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x28, r0, r1); }
    void SUB(const Mem &m, const Reg<u8> &r) { binop(0x28, m, r); }
    void SUB(const Mem &m, const Reg<u32> &r) { binop(0x28, m, r); }
    void SUB(const Reg<u8> &r, const Mem &m) { binop(0x28, r, m); }
    void SUB(const Reg<u32> &r, const Mem &m) { binop(0x28, r, m); }
    void SUB(const Reg<u8> &r, u8 v) { binop(0x2c, 0x80, 0x5, r, v); }
    void SUB(const Mem &m, u8 v) { binop(0x80, 0x5, m, v); }
    void SUB(const Reg<u32> &r, u32 v) { binop(0x2d, 0x80, 0x5, r, v); }
    void SUB(const Mem &m, u32 v) { binop(0x80, 0x5, m, v); }

    void XOR(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x30, r0, r1); }
    void XOR(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x30, r0, r1); }
    void XOR(const Mem &m, const Reg<u8> &r) { binop(0x30, m, r); }
    void XOR(const Mem &m, const Reg<u32> &r) { binop(0x30, m, r); }
    void XOR(const Reg<u8> &r, const Mem &m) { binop(0x30, r, m); }
    void XOR(const Reg<u32> &r, const Mem &m) { binop(0x30, r, m); }
    void XOR(const Reg<u8> &r, u8 v) { binop(0x34, 0x80, 0x6, r, v); }
    void XOR(const Mem &m, u8 v) { binop(0x80, 0x6, m, v); }
    void XOR(const Reg<u32> &r, u32 v) { binop(0x35, 0x80, 0x6, r, v); }
    void XOR(const Mem &m, u32 v) { binop(0x80, 0x6, m, v); }

    void CMP(const Reg<u8> &r0, const Reg<u8> &r1) { binop(0x38, r0, r1); }
    void CMP(const Reg<u32> &r0, const Reg<u32> &r1) { binop(0x38, r0, r1); }
    void CMP(const Mem &m, const Reg<u8> &r) { binop(0x38, m, r); }
    void CMP(const Mem &m, const Reg<u32> &r) { binop(0x38, m, r); }
    void CMP(const Reg<u8> &r, const Mem &m) { binop(0x38, r, m); }
    void CMP(const Reg<u32> &r, const Mem &m) { binop(0x38, r, m); }
    void CMP(const Reg<u8> &r, u8 v) { binop(0x3c, 0x80, 0x7, r, v); }
    void CMP(const Mem &m, u8 v) { binop(0x80, 0x7, m, v); }
    void CMP(const Reg<u32> &r, u32 v) { binop(0x3d, 0x80, 0x7, r, v); }
    void CMP(const Mem &m, u32 v) { binop(0x80, 0x7, m, v); }

    void SHL(const Reg<u8> &r) { put(0xd0); put(0xe0 | r.code); }
    void SHL(const Reg<u8> &r, u8 s) { put(0xc0); put(0xe0 | r.code); put(s); }
    void SHL(const Reg<u32> &r, u8 s) { put(0xc1); put(0xe0 | r.code); put(s); }
    void SHL(const Mem &m, u8 s) { put(0xc1); put(0x20 | m.mode); put(m); put(s); }

    void SHR(const Reg<u8> &r) { put(0xd0); put(0xe8 | r.code); }
    void SHR(const Reg<u8> &r, u8 s) { put(0xc0); put(0xe8 | r.code); put(s); }
    void SHR(const Reg<u32> &r, u8 s) { put(0xc1); put(0xe8 | r.code); put(s); }
    void SHR(const Mem &m, u8 s) { put(0xc1); put(0x28 | m.mode); put(m); put(s); }

    void ROL(const Reg<u8> &r) { put(0xd0); put(0xc0 | r.code); }
    void ROL(const Reg<u8> &r, u8 s) { put(0xc0); put(0xc0 | r.code); put(s); }
    void ROL(const Reg<u32> &r, u8 s) { put(0xc1); put(0xc0 | r.code); put(s); }
    void ROL(const Mem &m, u8 s) { put(0xc1); put(m.mode); put(m); put(s); }

    void ROR(const Reg<u8> &r) { put(0xd0); put(0xc8 | r.code); }
    void ROR(const Reg<u8> &r, u8 s) { put(0xc0); put(0xc8 | r.code); put(s); }
    void ROR(const Reg<u32> &r, u8 s) { put(0xc1); put(0xc8 | r.code); put(s); }
    void ROR(const Mem &m, u8 s) { put(0xc1); put(0x08 | m.mode); put(m); put(s); }

    void RCL(const Reg<u8> &r) { put(0xd0); put(0xd0 | r.code); }
    void RCL(const Reg<u8> &r, u8 s) { put(0xc0); put(0xd0 | r.code); put(s); }
    void RCL(const Reg<u32> &r, u8 s) { put(0xc1); put(0xd0 | r.code); put(s); }
    void RCL(const Mem &m, u8 s) { put(0xc1); put(0x10 | m.mode); put(m); put(s); }

    void RCR(const Reg<u8> &r) { put(0xd0); put(0xd8 | r.code); }
    void RCR(const Reg<u8> &r, u8 s) { put(0xc0); put(0xd8 | r.code); put(s); }
    void RCR(const Reg<u32> &r, u8 s) { put(0xc1); put(0xd8 | r.code); put(s); }
    void RCR(const Mem &m, u8 s) { put(0xc1); put(0x18 | m.mode); put(m); put(s); }

private:
    u8 *_codeBuffer;
    size_t _codeLength;
    size_t _codeSize;

    u32 *jumpCond(u8 ops, u8 opl, const u8 *loc);
    u32 *jumpAbs(u8 ops, u8 opl, const u8 *loc);
    u32 *jumpAbs(u8 opl, const u8 *loc);

    inline void binop(u8 op, const Reg<u8> &r0, const Reg<u8> &r1) {
        put(op); put(0xc0 | (r1.code << 3) | r0.code);
    }
    inline void binop(u8 op, const Reg<u32> &r0, const Reg<u32> &r1) {
        put(op | 0x1); put(0xc0 | (r1.code << 3) | r0.code);
    }
    inline void binop(u8 op, const Mem &m, const Reg<u8> &r) {
        put(op); put((r.code << 3) | m.mode);
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m);
    }
    inline void binop(u8 op, const Mem &m, const Reg<u32> &r) {
        put(op | 0x1); put((r.code << 3) | m.mode);
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m);
    }
    inline void binop(u8 op, const Reg<u8> &r, const Mem &m) {
        put(op | 0x2); put((r.code << 3) | m.mode);
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m);
    }
    inline void binop(u8 op, const Reg<u32> &r, const Mem &m) {
        put(op | 0x3); put((r.code << 3) | m.mode);
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m);
    }
    /* The register AL benefits from a shorter encoding. */
    inline void binop(u8 ops, u8 opl, u8 opx, const Reg<u8> &r, u8 v) {
        if (r.code) {
            put(opl); put(0xc0 | r.code | (opx << 3)); put(v);
        } else {
            put(ops); put(v);
        }
    }
    inline void binop(u8 op, u8 opx, const Mem &m, u8 v) {
        put(op); put(m.mode | (opx << 3));
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m); put(v);
    }
    /* The register EAX benefits from a shorter encoding. */
    inline void binop(u8 ops, u8 opl, u8 opx, const Reg<u32> &r, u32 v) {
        if (r.code) {
            put(opl | 0x1); put(0xc0 | r.code | (opx << 3)); put(v);
        } else {
            put(ops); put(v);
        }
    }
    inline void binop(u8 op, u8 opx, const Mem &m, u32 v) {
        put(op | 0x1); put(m.mode | (opx << 3));
        if ((m.mode & 0x7) == 0x4)
            put(0x24); // SIB byte to reference ESP
        put(m); put(v);
    }

    inline void put(u8 b) {
        _codeBuffer[_codeLength++] = b;
    }
    inline void put(u32 w) {
        memcpy(&_codeBuffer[_codeLength], &w, 4);
        _codeLength += 4;
    }
    inline void put(const Mem &m) {
        if (m.size == 1)
            _codeBuffer[_codeLength++] = m.disp;
        else if (m.size == 4) {
            memcpy(&_codeBuffer[_codeLength], &m.disp, 4);
            _codeLength += 4;
        }
    }
    /*
     * u32 constants are usually typed, calls to this method result from
     * ineteger promotion in opcode and/or mod values.
     */
    inline void put(int v) {
        put((u8)v);
    }
};

};

#endif /* _EMITTER_H_INCLUDED_ */
