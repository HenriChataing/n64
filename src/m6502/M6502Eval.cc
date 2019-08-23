
#include <iostream>
#include <iomanip>

#include "M6502State.h"
#include "M6502Eval.h"
#include "M6502Asm.h"
#include "Memory.h"
#include "exception.h"

using namespace M6502;

#define A           (currentState->regs.a)
#define X           (currentState->regs.x)
#define Y           (currentState->regs.y)
#define P           (currentState->regs.p)
#define SP          (currentState->regs.sp)
#define PC          (currentState->regs.pc)
#define PC_HI       (currentState->regs.pc >> 8)
#define PC_LO       (currentState->regs.pc & 0xff)

#define P_C         (1 << 0)
#define P_Z         (1 << 1)
#define P_I         (1 << 2)
#define P_D         (1 << 3)
#define P_B         (1 << 4)
#define P_R         (1 << 5)
#define P_V         (1 << 6)
#define P_N         (1 << 7)

#define P_VN        (P_V|P_N)
#define P_IDB       (P_I|P_D|P_B|P_R)
#define P_IDBV      (P_I|P_D|P_B|P_R|P_V)
#define P_CIDB      (P_C|P_I|P_D|P_B|P_R)
#define P_CIDBV     (P_C|P_I|P_D|P_B|P_R|P_V)

#define GET_C()     (currentState->regs.p & P_C)
#define SET_N(v)    currentState->regs.p |= ((v) & P_N)
#define SET_Z(v)    currentState->regs.p |= (((v) == 0) << 1)
#define SET_NZ(v)                                                              \
    SET_N(v);                                                                  \
    SET_Z(v)
#define UPD_NZ(v)                                                              \
    P &= P_CIDBV;                                                              \
    SET_NZ(v)

#define PAGE_DIFF(addr0, addr1) ((((addr0) ^ (addr1)) & 0xff00) != 0)

static inline void ADC(u8 m) {
    u8 c = GET_C();
    u16 t0 = A + m + c;
    u16 t1 = (A & 0x7f) + (m & 0x7f) + c;
    P &= P_IDB;
    P |= (t0 >> 8) & P_C;
    P |= ((t0 >> 2) ^ (t1 >> 1)) & P_V;
    A = t0 & 0xff;
    SET_NZ(A);
}

static inline void AND(u8 m) {
    A &= m;
    UPD_NZ(A);
}

static inline u8 ASL(u8 m) {
    P &= P_IDBV;
    P |= (m >> 7) & 1;
    m = (m << 1) & 0xfe;
    SET_NZ(m);
    return m;
}

static inline void CMP(u8 m) {
    u8 t = A + ~m + 1;
    P &= P_IDBV;
    P |= (A >= m) & P_C;
    SET_NZ(t);
}

static inline void CPX(u8 m) {
    u8 t = X + ~m + 1;
    P &= P_IDBV;
    P |= (X >= m) & P_C;
    SET_NZ(t);
}

static inline void CPY(u8 m) {
    u8 t = Y + ~m + 1;
    P &= P_IDBV;
    P |= (Y >= m) & P_C;
    SET_NZ(t);
}

/** Unofficial opcode, composition of DEC and CMP. */
static inline u8 DCP(u8 m) {
    m--;
    u8 t = A + ~m + 1;
    P &= P_IDBV;
    P |= (A >= m) & P_C;
    SET_NZ(t);
    return m;
}

static inline u8 DEC(u8 m) {
    m--;
    UPD_NZ(m);
    return m;
}

static inline void DEX() {
    X--;
    UPD_NZ(X);
}

static inline void DEY() {
    Y--;
    UPD_NZ(Y);
}

static inline void EOR(u8 m) {
    A ^= m;
    UPD_NZ(A);
}

static inline u8 INC(u8 m) {
    m++;
    UPD_NZ(m);
    return m;
}

static inline void INX() {
    X++;
    UPD_NZ(X);
}

static inline void INY() {
    Y++;
    UPD_NZ(Y);
}

/** Unofficial opcode, composition of INC and SBC. */
static inline u8 ISB(u8 m) {
    u8 r = m+1;
    m = ~r;
    u8 c = GET_C();
    u16 t0 = A + m + c;
    u8 t1 = (A & 0x7f) + (m & 0x7f) + c;
    P &= P_IDB;
    P |= (t0 >> 8) & 1;
    P |= ((t0 >> 2) ^ (t1 >> 1)) & P_V;
    A = t0 & 0xff;
    SET_NZ(A);
    return r;
}

static inline u8 LSR(u8 m) {
    P &= P_IDBV;
    P |= m & 1;
    m = (m >> 1) & 0x7f;
    SET_Z(m);
    return m;
}

static inline void ORA(u8 m) {
    A |= m;
    UPD_NZ(A);
}

/** Unofficial opcode, composition of ROL and AND. */
static inline u8 RLA(u8 m) {
    u8 t = ((m << 1) & 0xfe) | (P & 1);
    P &= P_IDBV;
    P |= (m >> 7) & 1;
    A &= t;
    SET_NZ(A);
    return t;
}

static inline u8 ROL(u8 m) {
    u8 t = ((m << 1) & 0xfe) | (P & 1);
    P &= P_IDBV;
    P |= (m >> 7) & 1;
    SET_NZ(t);
    return t;
}

static inline u8 ROR(u8 m) {
    u8 t = ((m >> 1) & 0x7f) | ((P << 7) & 0x80);
    P &= P_IDBV;
    P |= m & 1;
    SET_NZ(t);
    return t;
}

/** Unofficial opcode, composition of ROR and ADC. */
static inline u8 RRA(u8 m) {
    u8 t = ((m >> 1) & 0x7f) | ((P << 7) & 0x80);
    u8 c = m & 1;
    u16 t0 = A + t + c;
    u16 t1 = (A & 0x7f) + (t & 0x7f) + c;
    P &= P_IDB;
    P |= (t0 >> 8) & P_C;
    P |= ((t0 >> 2) ^ (t1 >> 1)) & P_V;
    A = t0 & 0xff;
    SET_NZ(A);
    return t;
}

static inline void SBC(u8 m) {
    m = ~m;
    u8 c = GET_C();
    u16 t0 = A + m + c;
    u8 t1 = (A & 0x7f) + (m & 0x7f) + c;
    P &= P_IDB;
    P |= (t0 >> 8) & 1;
    P |= ((t0 >> 2) ^ (t1 >> 1)) & P_V;
    A = t0 & 0xff;
    SET_NZ(A);
}

/** Unofficial opcode, composition of ASL and ORA. */
static inline u8 SLO(u8 m) {
    P &= P_IDBV;
    P |= (m >> 7) & 1;
    m = (m << 1) & 0xfe;
    A |= m;
    SET_NZ(A);
    return m;
}

/** Unofficial opcode, composition of LSR and EOR. */
static inline u8 SRE(u8 m) {
    P &= P_IDBV;
    P |= m & 1;
    m = (m >> 1) & 0x7f;
    A ^= m;
    SET_NZ(A);
    return m;
}

static inline void PUSH(u8 m) {
    Memory::ram[(u16)0x100 + (u16)SP] = m;
    SP--;
}

static inline u8 PULL() {
    SP++;
    return Memory::ram[(u16)0x100 + (u16)SP];
}

static inline void NOP(u16 m) {
    (void)m;
}

/*
 * Two interrupts (IRQ and NMI) and two instructions (PHP and BRK) PUSH
 * the flags to the stack. In the u8 PUSHed, bit 5 is always set to 1,
 * and bit 4 is 1 if from an instruction (PHP or BRK) or 0 if from an
 * interrupt line being PULLed low (IRQ or NMI).
 *
 * http://wiki.nesdev.com/w/index.php/CPU_status_flag_behavior
 */

static inline void BRK(u8 m) {
    (void)m;
    PUSH(PC_HI);
    PUSH(PC_LO);
    PUSH(P | 0x30);
    P |= P_I;
    PC = Memory::loadw(Memory::IRQ_ADDR);
}

static inline void JMP(u16 pc) {
    PC = pc;
}

static inline void JSR(u16 pc) {
    PC--;
    PUSH(PC_HI);
    PUSH(PC_LO);
    PC = pc;
}

static inline void RTI() {
    u8 hi, lo;
    P = PULL() & ~0x30;
    lo = PULL();
    hi = PULL();
    PC = WORD(hi, lo);
}

static inline void RTS() {
    u8 hi, lo;
    lo = PULL();
    hi = PULL();
    PC = WORD(hi, lo) + 1;
}

static inline void BIT(u8 m) {
    u8 t = A & m;
    P &= P_CIDB;
    P |= (m & P_VN);
    SET_Z(t);
}

static inline void CLC() {
    P &= ~P_C;
}

static inline void CLD() {
    P &= ~P_D;
}

static inline void CLI() {
    P &= ~P_I;
}

static inline void CLV() {
    P &= ~P_V;
}

/** Unofficial instruction. */
static inline void LAX(u8 m) {
    A = X = m;
    UPD_NZ(m);
}

static inline void LDA(u8 m) {
    A = m;
    UPD_NZ(m);
}

static inline void LDX(u8 m) {
    X = m;
    UPD_NZ(m);
}

static inline void LDY(u8 m) {
    Y = m;
    UPD_NZ(m);
}

static inline void PHA() {
    PUSH(A);
}

static inline void PHP() {
    /* cf comment in header control.h */
    PUSH(P | 0x30);
}

static inline void PLA() {
    A = PULL();
    UPD_NZ(A);
}

static inline void PLP() {
    P = PULL() & ~0x30;
}

static inline void SEC() {
    P |= P_C;
}

static inline void SED() {
    P |= P_D;
}

static inline void SEI() {
    P |= P_I;
}

static inline void TAX() {
    X = A;
    UPD_NZ(X);
}

static inline void TAY() {
    Y = A;
    UPD_NZ(Y);
}

static inline void TSX() {
    X = SP;
    UPD_NZ(X);
}

static inline void TXA() {
    A = X;
    UPD_NZ(A);
}

static inline void TXS() {
    SP = X;
}

static inline void TYA() {
    A = Y;
    UPD_NZ(A);
}

/**
 * Same as AND, with the difference that the status flag N is also copied
 * to the status flag C.
 */
static inline void AAC(u8 m) {
    A &= m;
    UPD_NZ(A);
    P &= ~P_C;
    P |= (P >> 7) & P_C;
}

/**
 * This opcode ANDs the contents of the A register with an immediate value and
 * then LSRs the result.
 */
static inline void ASR(u8 m) {
    AND(m);
    A = LSR(A);
}

/**
 * AND u8 with accumulator, then rotate one bit right in accu-mulator and
 * check bit 5 and 6:
 *      if both bits are 1: set C, clear V.
 *      if both bits are 0: clear C and V.
 *      if only bit 5 is 1: set V, clear C.
 *      if only bit 6 is 1: set C and V.
 */
static inline void ARR(u8 m) {
    AND(m);
    A = ROR(A);
    switch (A & 0x60) {
        case 0x00:
            P &= ~(P_C | P_V);
            break;
        case 0x20:
            P = (P & ~P_C) | P_V;
            break;
        case 0x40:
            P |= P_C | P_V;
            break;
        case 0x60:
            P = (P & ~P_V) | P_C;
            break;
        default:
            break;
    }
}

/**
 * AND u8 with accumulator, then transfer accumulator to X register.
 */
static inline void ATX(u8 m) {
    /*
     * The immediate value here is variable, with 0xee, 0xef, 0xfe, 0xff
     * identified as possible masks.
     */
    ORA(0xff);
    AND(m);
    X = A;
}

/**
 * ANDs the contents of the A and X registers (leaving the contents of A
 * intact), subtracts an immediate value, and then stores the result in X.
 * ... A few points might be made about the action of subtracting an immediate
 * value.  It actually works just like the CMP instruction, except that CMP
 * does not store the result of the subtraction it performs in any register.
 * This subtract operation is not affected by the state of the Carry flag,
 * though it does affect the Carry flag.  It does not affect the Overflow
 * flag.
 */
static inline void AXS(u8 m) {
    u8 oldA = A, oldP = P;
    AND(X);
    SEC(); /* clear borrow */
    SBC(m);
    X = A;
    A = oldA;
    P = (P & ~P_V) | (oldP & P_V);
}

static u8 getImmediate(void) {
    return Memory::load(PC + 1);
}

static u8 getZeroPage(void) {
    return Memory::load(Memory::load(PC + 1));
}

static u16 getZeroPageAddr(void) {
    return Memory::load(PC + 1);
}

static u8 getZeroPageX(void) {
    u8 addr = Memory::load(PC + 1) + X;
    return Memory::load(addr);
}

static u16 getZeroPageXAddr(void) {
    return (u8)(Memory::load(PC + 1) + X);
}

static u8 getZeroPageY(void) {
    u8 addr = Memory::load(PC + 1) + Y;
    return Memory::load(addr);
}

static u8 getZeroPageYAddr(void) {
    return (u8)(Memory::load(PC + 1) + Y);
}

static u8 getAbsolute(void) {
    return Memory::load(Memory::loadw(PC + 1));
}

static u16 getAbsoluteAddr(void) {
    return Memory::loadw(PC + 1);
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address addr + x
 * (without wrapping), and repeated if the addition causes the high u8 to
 * change.
 */
static u8 getAbsoluteX(void) {
    u16 addr = Memory::loadw(PC + 1);
    u16 addrX = addr + X;
    if ((addr & 0xff00) != (addrX & 0xff00)) {
        (void)Memory::load((addr & 0xff00) | (addrX & 0x00ff));
        currentState->cycles++;
    }
    return Memory::load(addrX);
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address addr + x
 * (without wrapping).
 */
static u16 getAbsoluteXAddr(void) {
    u16 addr = Memory::loadw(PC + 1);
    u16 addrX = addr + X;
    (void)Memory::load((addr & 0xff00) | (addrX & 0x00ff));
    return addrX;
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address addr + y
 * (without wrapping), and repeated if the addition causes the high u8 to
 * change.
 */
static u8 getAbsoluteY(void) {
    u16 addr = Memory::loadw(PC + 1);
    u16 addrY = addr + Y;
    if ((addr & 0xff00) != (addrY & 0xff00)) {
        (void)Memory::load((addr & 0xff00) | (addrY & 0x00ff));
        currentState->cycles++;
    }
    return Memory::load(addrY);
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address addr + y
 * (without wrapping).
 */
static u16 getAbsoluteYAddr(void) {
    u16 addr = Memory::loadw(PC + 1);
    u16 addrY = addr + Y;
    (void)Memory::load((addr & 0xff00) | (addrY & 0x00ff));
    return addrY;
}

static u8 getIndexedIndirect(void) {
    u8 addr = Memory::load(PC + 1) + X;
    return Memory::load(Memory::loadzw(addr));
}

static u16 getIndexedIndirectAddr(void) {
    u8 addr = Memory::load(PC + 1) + X;
    return Memory::loadzw(addr);
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address
 * (without wrapping), and repeated if the addition causes the high u8 to
 * change.
 */
static u8 getIndirectIndexed(void) {
    u16 addr = Memory::load(PC + 1), addrY;
    addr = Memory::loadzw(addr);
    addrY = addr + Y;
    if ((addr & 0xff00) != (addrY & 0xff00)) {
        (void)Memory::load((addr & 0xff00) | (addrY & 0x00ff));
        currentState->cycles++;
    }
    return Memory::load(addrY);
}

/**
 * Implement the Oops cycle.
 * A first fecth is performed at the partially computed address addr + x
 * (without wrapping).
 */
static u16 getIndirectIndexedAddr(void) {
    u16 addr = Memory::load(PC + 1), addrY;
    addr = Memory::loadzw(addr);
    addrY = addr + Y;
    (void)Memory::load((addr & 0xff00) | (addrY & 0x00ff));
    return addrY;
}

static u16 getIndirect(void) {
    u16 lo = Memory::load(PC + 1);
    u16 hi = Memory::load(PC + 2);
    /*
     * Incorrect fetch if the address falls on a page boundary : JMP ($xxff).
     * In this case, the LSB is fetched from $xxff and the MSB from $xx00.
     * http://obelisk.me.uk/6502/reference.html#JMP
     */
    if (lo == 0xff) {
        hi = (hi << 8) & 0xff00;
        lo = Memory::load(hi | lo);
        hi = Memory::load(hi);
        return WORD(hi, lo);
    } else
        return Memory::loadw(WORD(hi, lo));
}


/** Generate the code for an instruction fetching a value from memory. */
#define CASE_LD_MEM(op, fun, fetch)                                            \
    case op: {                                                                 \
        u16 __m = fetch();                                                     \
        PC += Asm::instructions[op].bytes;                                     \
        fun(__m);                                                              \
        break;                                                                 \
    }

/** Generate the code for an instruction writing a value to memory. */
#define CASE_ST_MEM(op, reg, where)                                            \
    case op: {                                                                 \
        Memory::store(where(), reg);                                           \
        PC += Asm::instructions[op].bytes;                                     \
        break;                                                                 \
    }

/**
 * Generate the code for an instruction updating a value in memory.
 * The code implements the double write back behaviour, whereby
 * Read-Modify-Write instructions cause the original value to be written back
 * to memory one cycle before the modified value. This has a significant impact
 * on writes to state registers.
 */
#define CASE_UP_MEM(op, fun, where)                                            \
    case op: {                                                                 \
        u16 __addr = where();                                                  \
        PC += Asm::instructions[op].bytes;                                     \
        u8 __old = Memory::load(__addr);                                       \
        u8 __new = fun(__old);                                                 \
        Memory::store(__addr, __old);                                          \
        Memory::store(__addr, __new);                                          \
        break;                                                                 \
    }

/** Generate the code for an instruction updating the value of a register. */
#define CASE_UP_REG(op, fun, reg)                                              \
    case op: {                                                                 \
        PC += Asm::instructions[op].bytes;                                     \
        reg = fun(reg);                                                        \
        break;                                                                 \
    }

/**
 * Generate the code for an instruction with no operand.
 * The CPU still performs a dummy fetch of the next instruction byte.
 */
#define CASE_NN_EXP(op, e)                                                     \
    case op: {                                                                 \
        PC += Asm::instructions[op].bytes;                                     \
        (void)Memory::load(PC);                                                \
        e;                                                                     \
        break;                                                                 \
    }

#define CASE_NN_NOP() CASE_NN_EXP(NOP_IMP, )
#define CASE_NN_NOP_UO(op) CASE_NN_EXP(op, )
#define CASE_NN(op, fun) CASE_NN_EXP(op##_IMP, fun())
#define CASE_NN_UO(op, fun) CASE_NN_EXP(op, fun())

/** Create a banch instruction with the given condition. */
#define CASE_BR(op, cond)                                                      \
    case op##_REL: {                                                           \
        if (cond) {                                                            \
            u8 __off = Memory::load(PC + 1);                                   \
            u16 __pc;                                                          \
            PC += Asm::instructions[op##_REL].bytes;                           \
            if (__off & 0x80) {                                                \
                __off = ~__off + 1;                                            \
                __pc = PC - __off;                                             \
            } else                                                             \
                __pc = PC + __off;                                             \
            currentState->cycles += PAGE_DIFF(__pc, PC) + 1;                   \
            PC = __pc;                                                         \
        } else {                                                               \
            PC += Asm::instructions[op##_REL].bytes;                           \
        }                                                                      \
        break;                                                                 \
    }

#define CASE_LD_IMM(op, fun)  CASE_LD_MEM(op##_IMM, fun, getImmediate)
#define CASE_LD_ZPG(op, fun)  CASE_LD_MEM(op##_ZPG, fun, getZeroPage)
#define CASE_LD_ZPX(op, fun)  CASE_LD_MEM(op##_ZPX, fun, getZeroPageX)
#define CASE_LD_ZPY(op, fun)  CASE_LD_MEM(op##_ZPY, fun, getZeroPageY)
#define CASE_LD_ABS(op, fun)  CASE_LD_MEM(op##_ABS, fun, getAbsolute)
#define CASE_LD_ABA(op, fun)  CASE_LD_MEM(op##_ABS, fun, getAbsoluteAddr)
#define CASE_LD_ABX(op, fun)  CASE_LD_MEM(op##_ABX, fun, getAbsoluteX)
#define CASE_LD_ABY(op, fun)  CASE_LD_MEM(op##_ABY, fun, getAbsoluteY)
#define CASE_LD_IND(op, fun)  CASE_LD_MEM(op##_IND, fun, getIndirect)
#define CASE_LD_INX(op, fun)  CASE_LD_MEM(op##_INX, fun, getIndexedIndirect)
#define CASE_LD_INY(op, fun)  CASE_LD_MEM(op##_INY, fun, getIndirectIndexed)

#define CASE_LD_IMM_UO(op, fun)  CASE_LD_MEM(op, fun, getImmediate)
#define CASE_LD_ZPG_UO(op, fun)  CASE_LD_MEM(op, fun, getZeroPage)
#define CASE_LD_ZPX_UO(op, fun)  CASE_LD_MEM(op, fun, getZeroPageX)
#define CASE_LD_ABS_UO(op, fun)  CASE_LD_MEM(op, fun, getAbsolute)
#define CASE_LD_ABX_UO(op, fun)  CASE_LD_MEM(op, fun, getAbsoluteX)

#define CASE_ST_ZPG(op, reg)  CASE_ST_MEM(op##_ZPG, reg, getZeroPageAddr)
#define CASE_ST_ZPX(op, reg)  CASE_ST_MEM(op##_ZPX, reg, getZeroPageXAddr)
#define CASE_ST_ZPY(op, reg)  CASE_ST_MEM(op##_ZPY, reg, getZeroPageYAddr)
#define CASE_ST_ABS(op, reg)  CASE_ST_MEM(op##_ABS, reg, getAbsoluteAddr)
#define CASE_ST_ABX(op, reg)  CASE_ST_MEM(op##_ABX, reg, getAbsoluteXAddr)
#define CASE_ST_ABY(op, reg)  CASE_ST_MEM(op##_ABY, reg, getAbsoluteYAddr)
#define CASE_ST_INX(op, reg)  CASE_ST_MEM(op##_INX, reg, getIndexedIndirectAddr)
#define CASE_ST_INY(op, reg)  CASE_ST_MEM(op##_INY, reg, getIndirectIndexedAddr)

#define CASE_UP_ACC(op, fun)  CASE_UP_REG(op##_ACC, fun, currentState->regs.a)
#define CASE_UP_ZPG(op, fun)  CASE_UP_MEM(op##_ZPG, fun, getZeroPageAddr)
#define CASE_UP_ZPX(op, fun)  CASE_UP_MEM(op##_ZPX, fun, getZeroPageXAddr)
#define CASE_UP_ABS(op, fun)  CASE_UP_MEM(op##_ABS, fun, getAbsoluteAddr)
#define CASE_UP_ABX(op, fun)  CASE_UP_MEM(op##_ABX, fun, getAbsoluteXAddr)
#define CASE_UP_ABY(op, fun)  CASE_UP_MEM(op##_ABY, fun, getAbsoluteYAddr)
#define CASE_UP_INX(op, fun)  CASE_UP_MEM(op##_INX, fun, getIndexedIndirectAddr)
#define CASE_UP_INY(op, fun)  CASE_UP_MEM(op##_INY, fun, getIndirectIndexedAddr)

namespace M6502 {

/**
 * @brief Display debug information about the current instruction and the
 *   register state.
 * @param opcode first byte of the instruction pointed to by PC
 */
void trace(u8 opcode)
{
    u8 arg0, arg1;
    u16 jumpto;

    if (Asm::instructions[opcode].bytes > 1)
        arg0 = Memory::load(PC + 1);
    if (Asm::instructions[opcode].bytes > 2)
        arg1 = Memory::load(PC + 2);

    std::cerr << std::hex << std::uppercase << std::setfill('0');
    std::cerr << std::setw(4) << (int)PC << "  ";
    std::cerr << std::setw(2) << (int)opcode << " ";

    /* Raw instruction bytes. */
    switch (Asm::instructions[opcode].bytes)
    {
        case 1:
            std::cerr << "     ";
            break;
        case 2:
            std::cerr << std::setw(2) << (int)arg0 << "   ";
            break;
        default:
            std::cerr << std::setw(2) << (int)arg0 << " ";
            std::cerr << std::setw(2) << (int)arg1;
            break;
    }

    /* Identify non-standard instructions. */
    if (Asm::instructions[opcode].unofficial)
        std::cerr << " *";
    else
        std::cerr << "  ";

    /* Print instruction assembly name. */
    std::cerr << Asm::instructions[opcode].name << " ";

    /* Format operands. */
    switch (Asm::instructions[opcode].type)
    {
        case Asm::IMM:
            std::cerr << "#$" << std::setw(2) << (int)arg0 << "   ";
            break;
        case Asm::ZPG:
            std::cerr << "$" << std::setw(2) << (int)arg0 << "    ";
            break;
        case Asm::ZPX:
            std::cerr << "$" << std::setw(2) << (int)arg0 << ",X  ";
            break;
        case Asm::ZPY:
            std::cerr << "$" << std::setw(2) << (int)arg0 << ",Y  ";
            break;
        case Asm::ABS:
            std::cerr << "$" << std::setw(4);
            std::cerr << (int)(arg0 | ((int)arg1 << 8));
            std::cerr << "  ";
            break;
        case Asm::ABX:
            std::cerr << "$" << std::setw(4);
            std::cerr << (int)(arg0 | ((int)arg1 << 8));
            std::cerr << ",X";
            break;
        case Asm::ABY:
            std::cerr << "$" << std::setw(4);
            std::cerr << (int)(arg0 | ((int)arg1 << 8));
            std::cerr << ",Y";
            break;
        case Asm::IND:
            std::cerr << "($" << std::setw(4);
            std::cerr << (int)(arg0 | ((int)arg1 << 8));
            std::cerr << ")";
            break;
        case Asm::INX:
            std::cerr << "($" << std::setw(2) << (int)arg0 << ",X)";
            break;
        case Asm::INY:
            std::cerr << "($" << std::setw(2) << (int)arg0 << ",Y)";
            break;
        case Asm::REL:
            /*
             * Directly compute the jump address and display it instead of the
             * relative offset.
             */
            if (arg0 & 0x80) {
                arg0 = ~arg0 + 1;
                jumpto = PC + 2 - arg0;
            } else
                jumpto = PC + 2 + arg0;
            std::cerr << "$" << std::setw(4) << (int)jumpto << "  ";
            break;
        case Asm::IMP:
            std::cerr << "       ";
            break;
        case Asm::ACC:
            std::cerr << "A      ";
            break;
    }

    std::cerr << "  A:" << std::setw(2) << (int)A;
    std::cerr << " X:" << std::setw(2) << (int)X;
    std::cerr << " Y:" << std::setw(2) << (int)Y;
    std::cerr << " P:" << std::setw(2) << (int)P;
    std::cerr << " SP:" << std::setw(2) << (int)SP;
    std::cerr << " CYC:" << std::dec << currentState->cycles;
    std::cerr << std::nouppercase << std::endl;
}

namespace Eval {

/**
 * @brief Trigger an NMI (Non Maskable Interrupt).
 */
void triggerNMI()
{
    PUSH(PC_HI);
    PUSH(PC_LO);
    PUSH((P & ~0x30) | 0x20);
    P |= P_I;
    PC = Memory::loadw(Memory::NMI_ADDR);
    currentState->cycles += Asm::instructions[BRK_IMP].cycles;
    currentState->nmi = false;
}

/**
 * @brief Trigger an IRQ.
 */
void triggerIRQ()
{
    /* Check if IRQ mask bit is set. */
    if (P & P_I)
        return;
    PUSH(PC_HI);
    PUSH(PC_LO);
    PUSH((P & ~0x30) | 0x20);
    P |= P_I;
    PC = Memory::loadw(Memory::IRQ_ADDR);
    currentState->cycles += Asm::instructions[BRK_IMP].cycles;
    currentState->irq = false;
}

/**
 * @brief Execute a single instruction.
 */
void runOpcode(u8 opcode)
{
    /* Log present state and next instruction. */
    // trace(opcode);

    /* Exclude jamming instructions. */
    if (Asm::instructions[opcode].jam) {
        throw JammingInstruction(currentState->regs.pc, opcode);
    }

    /* Interpret instruction. */
    switch (opcode)
    {
        CASE_LD_IMM(ADC, ADC);
        CASE_LD_ZPG(ADC, ADC);
        CASE_LD_ZPX(ADC, ADC);
        CASE_LD_ABS(ADC, ADC);
        CASE_LD_ABX(ADC, ADC);
        CASE_LD_ABY(ADC, ADC);
        CASE_LD_INX(ADC, ADC);
        CASE_LD_INY(ADC, ADC);

        CASE_LD_IMM(AND, AND);
        CASE_LD_ZPG(AND, AND);
        CASE_LD_ZPX(AND, AND);
        CASE_LD_ABS(AND, AND);
        CASE_LD_ABX(AND, AND);
        CASE_LD_ABY(AND, AND);
        CASE_LD_INX(AND, AND);
        CASE_LD_INY(AND, AND);

        CASE_UP_ACC(ASL, ASL);
        CASE_UP_ZPG(ASL, ASL);
        CASE_UP_ZPX(ASL, ASL);
        CASE_UP_ABS(ASL, ASL);
        CASE_UP_ABX(ASL, ASL);

        CASE_BR(BCC, !(P & P_C));
        CASE_BR(BCS,  (P & P_C));
        CASE_BR(BEQ,  (P & P_Z));

        CASE_LD_ZPG(BIT, BIT);
        CASE_LD_ABS(BIT, BIT);

        CASE_BR(BMI,  (P & P_N));
        CASE_BR(BNE, !(P & P_Z));
        CASE_BR(BPL, !(P & P_N));

        CASE_LD_MEM(BRK_IMP, BRK, getImmediate)

        CASE_BR(BVC, !(P & P_V));
        CASE_BR(BVS,  (P & P_V));

        CASE_NN(CLC, CLC);
        CASE_NN(CLD, CLD);
        CASE_NN(CLI, CLI);
        CASE_NN(CLV, CLV);

        CASE_LD_IMM(CMP, CMP);
        CASE_LD_ZPG(CMP, CMP);
        CASE_LD_ZPX(CMP, CMP);
        CASE_LD_ABS(CMP, CMP);
        CASE_LD_ABX(CMP, CMP);
        CASE_LD_ABY(CMP, CMP);
        CASE_LD_INX(CMP, CMP);
        CASE_LD_INY(CMP, CMP);

        CASE_LD_IMM(CPX, CPX);
        CASE_LD_ZPG(CPX, CPX);
        CASE_LD_ABS(CPX, CPX);

        CASE_LD_IMM(CPY, CPY);
        CASE_LD_ZPG(CPY, CPY);
        CASE_LD_ABS(CPY, CPY);

        CASE_UP_ZPG(DEC, DEC);
        CASE_UP_ZPX(DEC, DEC);
        CASE_UP_ABS(DEC, DEC);
        CASE_UP_ABX(DEC, DEC);

        CASE_NN(DEX, DEX);
        CASE_NN(DEY, DEY);

        CASE_LD_IMM(EOR, EOR);
        CASE_LD_ZPG(EOR, EOR);
        CASE_LD_ZPX(EOR, EOR);
        CASE_LD_ABS(EOR, EOR);
        CASE_LD_ABX(EOR, EOR);
        CASE_LD_ABY(EOR, EOR);
        CASE_LD_INX(EOR, EOR);
        CASE_LD_INY(EOR, EOR);

        CASE_UP_ZPG(INC, INC);
        CASE_UP_ZPX(INC, INC);
        CASE_UP_ABS(INC, INC);
        CASE_UP_ABX(INC, INC);

        CASE_NN(INX, INX);
        CASE_NN(INY, INY);

        CASE_LD_ABA(JMP, JMP);
        CASE_LD_IND(JMP, JMP);
        CASE_LD_ABA(JSR, JSR);

        CASE_LD_IMM(LDA, LDA);
        CASE_LD_ZPG(LDA, LDA);
        CASE_LD_ZPX(LDA, LDA);
        CASE_LD_ABS(LDA, LDA);
        CASE_LD_ABX(LDA, LDA);
        CASE_LD_ABY(LDA, LDA);
        CASE_LD_INX(LDA, LDA);
        CASE_LD_INY(LDA, LDA);

        CASE_LD_IMM(LDX, LDX);
        CASE_LD_ZPG(LDX, LDX);
        CASE_LD_ZPY(LDX, LDX);
        CASE_LD_ABS(LDX, LDX);
        CASE_LD_ABY(LDX, LDX);

        CASE_LD_IMM(LDY, LDY);
        CASE_LD_ZPG(LDY, LDY);
        CASE_LD_ZPX(LDY, LDY);
        CASE_LD_ABS(LDY, LDY);
        CASE_LD_ABX(LDY, LDY);

        CASE_UP_ACC(LSR, LSR);
        CASE_UP_ZPG(LSR, LSR);
        CASE_UP_ZPX(LSR, LSR);
        CASE_UP_ABS(LSR, LSR);
        CASE_UP_ABX(LSR, LSR);

        CASE_NN_NOP();

        CASE_LD_IMM(ORA, ORA);
        CASE_LD_ZPG(ORA, ORA);
        CASE_LD_ZPX(ORA, ORA);
        CASE_LD_ABS(ORA, ORA);
        CASE_LD_ABX(ORA, ORA);
        CASE_LD_ABY(ORA, ORA);
        CASE_LD_INX(ORA, ORA);
        CASE_LD_INY(ORA, ORA);

        CASE_NN(PHA, PHA);
        CASE_NN(PHP, PHP);
        CASE_NN(PLA, PLA);
        CASE_NN(PLP, PLP);

        CASE_UP_ACC(ROL, ROL);
        CASE_UP_ZPG(ROL, ROL);
        CASE_UP_ZPX(ROL, ROL);
        CASE_UP_ABS(ROL, ROL);
        CASE_UP_ABX(ROL, ROL);

        CASE_UP_ACC(ROR, ROR);
        CASE_UP_ZPG(ROR, ROR);
        CASE_UP_ZPX(ROR, ROR);
        CASE_UP_ABS(ROR, ROR);
        CASE_UP_ABX(ROR, ROR);

        CASE_NN(RTI, RTI);
        CASE_NN(RTS, RTS);

        CASE_LD_IMM(SBC, SBC);
        CASE_LD_ZPG(SBC, SBC);
        CASE_LD_ZPX(SBC, SBC);
        CASE_LD_ABS(SBC, SBC);
        CASE_LD_ABX(SBC, SBC);
        CASE_LD_ABY(SBC, SBC);
        CASE_LD_INX(SBC, SBC);
        CASE_LD_INY(SBC, SBC);
        CASE_LD_IMM_UO(0xeb, SBC);

        CASE_NN(SEC, SEC);
        CASE_NN(SED, SED);
        CASE_NN(SEI, SEI);

        CASE_ST_ZPG(STA, A);
        CASE_ST_ZPX(STA, A);
        CASE_ST_ABS(STA, A);
        CASE_ST_ABX(STA, A);
        CASE_ST_ABY(STA, A);
        CASE_ST_INX(STA, A);
        CASE_ST_INY(STA, A);

        CASE_ST_ZPG(STX, X);
        CASE_ST_ZPY(STX, X);
        CASE_ST_ABS(STX, X);

        CASE_ST_ZPG(STY, Y);
        CASE_ST_ZPX(STY, Y);
        CASE_ST_ABS(STY, Y);

        CASE_NN(TAX, TAX);
        CASE_NN(TAY, TAY);
        CASE_NN(TSX, TSX);
        CASE_NN(TXA, TXA);
        CASE_NN(TXS, TXS);
        CASE_NN(TYA, TYA);

        /** Unofficial NOP instructions with IMM addressing. */
        CASE_LD_IMM_UO(0x80, NOP);
        CASE_LD_IMM_UO(0x82, NOP);
        CASE_LD_IMM_UO(0x89, NOP);
        CASE_LD_IMM_UO(0xc2, NOP);
        CASE_LD_IMM_UO(0xe2, NOP);

        /** Unofficial NOP instructions with ZPG addressing. */
        CASE_LD_ZPG_UO(0x04, NOP);
        CASE_LD_ZPG_UO(0x44, NOP);
        CASE_LD_ZPG_UO(0x64, NOP);

        /** Unofficial NOP instructions with ZPX addressing. */
        CASE_LD_ZPX_UO(0x14, NOP);
        CASE_LD_ZPX_UO(0x34, NOP);
        CASE_LD_ZPX_UO(0x54, NOP);
        CASE_LD_ZPX_UO(0x74, NOP);
        CASE_LD_ZPX_UO(0xd4, NOP);
        CASE_LD_ZPX_UO(0xf4, NOP);

        /** Unofficial NOP instructions with IMP addressing. */
        CASE_NN_NOP_UO(0x1a);
        CASE_NN_NOP_UO(0x3a);
        CASE_NN_NOP_UO(0x5a);
        CASE_NN_NOP_UO(0x7a);
        CASE_NN_NOP_UO(0xda);
        CASE_NN_NOP_UO(0xfa);

        /** Unofficial NOP instructions with ABS addressing. */
        CASE_LD_ABS_UO(0x0c, NOP);

        /** Unofficial NOP instructions with ABX addressing. */
        CASE_LD_ABX_UO(0x1c, NOP);
        CASE_LD_ABX_UO(0x3c, NOP);
        CASE_LD_ABX_UO(0x5c, NOP);
        CASE_LD_ABX_UO(0x7c, NOP);
        CASE_LD_ABX_UO(0xdc, NOP);
        CASE_LD_ABX_UO(0xfc, NOP);

        /** Unofficial LAX instruction. */
        CASE_LD_ZPG(LAX, LAX);
        CASE_LD_ZPY(LAX, LAX);
        CASE_LD_ABS(LAX, LAX);
        CASE_LD_ABY(LAX, LAX);
        CASE_LD_INX(LAX, LAX);
        CASE_LD_INY(LAX, LAX);

        /** Unofficial SAX instruction. */
        CASE_ST_ZPG(SAX, A & X);
        CASE_ST_ZPY(SAX, A & X);
        CASE_ST_ABS(SAX, A & X);
        CASE_ST_INX(SAX, A & X);

        /** Unofficial DCP instruction. */
        CASE_UP_ZPG(DCP, DCP);
        CASE_UP_ZPX(DCP, DCP);
        CASE_UP_ABS(DCP, DCP);
        CASE_UP_ABX(DCP, DCP);
        CASE_UP_ABY(DCP, DCP);
        CASE_UP_INX(DCP, DCP);
        CASE_UP_INY(DCP, DCP);

        /** Unofficial ISB instruction. */
        CASE_UP_ZPG(ISB, ISB);
        CASE_UP_ZPX(ISB, ISB);
        CASE_UP_ABS(ISB, ISB);
        CASE_UP_ABX(ISB, ISB);
        CASE_UP_ABY(ISB, ISB);
        CASE_UP_INX(ISB, ISB);
        CASE_UP_INY(ISB, ISB);

        /** Unofficial SLO instruction. */
        CASE_UP_ZPG(SLO, SLO);
        CASE_UP_ZPX(SLO, SLO);
        CASE_UP_ABS(SLO, SLO);
        CASE_UP_ABX(SLO, SLO);
        CASE_UP_ABY(SLO, SLO);
        CASE_UP_INX(SLO, SLO);
        CASE_UP_INY(SLO, SLO);

        /** Unofficial RLA instruction. */
        CASE_UP_ZPG(RLA, RLA);
        CASE_UP_ZPX(RLA, RLA);
        CASE_UP_ABS(RLA, RLA);
        CASE_UP_ABX(RLA, RLA);
        CASE_UP_ABY(RLA, RLA);
        CASE_UP_INX(RLA, RLA);
        CASE_UP_INY(RLA, RLA);

        /** Unofficial SRE instruction. */
        CASE_UP_ZPG(SRE, SRE);
        CASE_UP_ZPX(SRE, SRE);
        CASE_UP_ABS(SRE, SRE);
        CASE_UP_ABX(SRE, SRE);
        CASE_UP_ABY(SRE, SRE);
        CASE_UP_INX(SRE, SRE);
        CASE_UP_INY(SRE, SRE);

        /** Unofficial RRA instruction. */
        CASE_UP_ZPG(RRA, RRA);
        CASE_UP_ZPX(RRA, RRA);
        CASE_UP_ABS(RRA, RRA);
        CASE_UP_ABX(RRA, RRA);
        CASE_UP_ABY(RRA, RRA);
        CASE_UP_INX(RRA, RRA);
        CASE_UP_INY(RRA, RRA);

        /* Unofficial instructions with immediate addressing mode. */
        CASE_LD_IMM(AAC0, AAC);
        CASE_LD_IMM(AAC1, AAC);
        CASE_LD_IMM(ASR, ASR);
        CASE_LD_IMM(ARR, ARR);
        CASE_LD_IMM(ATX, ATX);
        CASE_LD_IMM(AXS, AXS);

        default:
            throw UnsupportedInstruction(currentState->regs.pc, opcode);
    }

    currentState->cycles += Asm::instructions[opcode].cycles;
}

};
};
