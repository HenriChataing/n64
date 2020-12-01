
#include <iomanip>
#include <iostream>

#include <assembly/disassembler.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <core.h>
#include <debugger.h>
#include <interpreter.h>

using namespace R4300;
using namespace n64;

namespace interpreter::cpu {

/**
 * @brief Preprocessor template for I-type instructions.
 *
 * The registers and immediate value are automatically extracted from the
 * instruction and added as rs, rt, imm in a new scope.
 *
 * @param instr             Original instruction
 * @param extend            Extension method (sign or zero extend)
 */
#define IType(instr, extend) \
    u32 rs = assembly::getRs(instr); \
    u32 rt = assembly::getRt(instr); \
    u64 imm = extend<u64, u16>(assembly::getImmediate(instr)); \
    (void)rs; (void)rt; (void)imm;

/**
 * @brief Preprocessor template for R-type instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamnt in a new scope.
 *
 * @param instr             Original instruction
 */
#define RType(instr) \
    u32 rd = assembly::getRd(instr); \
    u32 rs = assembly::getRs(instr); \
    u32 rt = assembly::getRt(instr); \
    u32 shamnt = assembly::getShamnt(instr); \
    (void)rd; (void)rs; (void)rt; (void)shamnt;

/**
 * Check whether a virtual memory address is correctly aligned
 * for a memory access, raise AddressError otherwise.
 */
#define checkAddressAlignment(vAddr, bytes, instr, load)                       \
    if (((vAddr) & ((bytes) - 1)) != 0) {                                      \
        takeException(AddressError, (vAddr), (instr), (load));                 \
        return;                                                                \
    }

/**
 * Check whether Cop1 is currently enabled in SR,
 * raise CoprocessorUnusable otherwise.
 */
#define checkCop1Usable()                                                      \
    if (!state.cp0reg.CU1()) {                                                 \
        takeException(CoprocessorUnusable, 0, instr, false, 1u);               \
        return;                                                                \
    }

/**
 * Take the exception \p exn and return from the current function
 * if it is not None.
 */
#define checkException(exn, vAddr, instr, load, ce)                            \
({                                                                             \
    Exception __exn = (exn);                                                   \
    if (__exn != Exception::None) {                                            \
        takeException(__exn, (vAddr), (instr), (load), (ce));                  \
        return;                                                                \
    }                                                                          \
})


void eval_Reserved(u32 instr) {
    core::halt("CPU reserved instruction");
}

/* SPECIAL opcodes */

void eval_ADD(u32 instr) {
    RType(instr);
    i32 res;
    i32 a = (i32)(u32)state.reg.gpr[rs];
    i32 b = (i32)(u32)state.reg.gpr[rt];
    if (__builtin_add_overflow(a, b, &res)) {
        core::halt("ADD IntegerOverflow");
    }
    state.reg.gpr[rd] = sign_extend<u64, u32>((u32)res);
}

void eval_ADDU(u32 instr) {
    RType(instr);
    u32 res = state.reg.gpr[rs] + state.reg.gpr[rt];
    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_AND(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rs] & state.reg.gpr[rt];
}

void eval_BREAK(u32 instr) {
    RType(instr);
    core::halt("BREAK");
}

void eval_DADD(u32 instr) {
    RType(instr);
    i64 res;
    i64 a = (i64)state.reg.gpr[rs];
    i64 b = (i64)state.reg.gpr[rt];
    if (__builtin_add_overflow(a, b, &res)) {
        core::halt("DADD IntegerOverflow");
    }
    state.reg.gpr[rd] = (u64)res;
}

void eval_DADDU(u32 instr) {
    RType(instr);
    u64 res = state.reg.gpr[rs] + state.reg.gpr[rt];
    state.reg.gpr[rd] = res;
}

void eval_DDIV(u32 instr) {
    RType(instr);
    i64 lo = (i64)state.reg.gpr[rs] / (i64)state.reg.gpr[rt];
    i64 hi = (i64)state.reg.gpr[rs] % (i64)state.reg.gpr[rt];
    state.reg.multLo = (u64)lo;
    state.reg.multHi = (u64)hi;
}

void eval_DDIVU(u32 instr) {
    RType(instr);
    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
}

void eval_DIV(u32 instr) {
    RType(instr);
    // Must use 64bit integers here to prevent signed overflow.
    i64 num = (i32)state.reg.gpr[rs];
    i64 denum = (i32)state.reg.gpr[rt];
    if (denum != 0) {
        state.reg.multLo = sign_extend<u64, u32>((u64)(num / denum));
        state.reg.multHi = sign_extend<u64, u32>((u64)(num % denum));
    } else {
        debugger::undefined("Divide by 0 (DIV)");
        // Undefined behaviour here according to the reference
        // manual. The machine behaviour is as implemented.
        state.reg.multLo = num < 0 ? 1 : UINT64_C(-1);
        state.reg.multHi = sign_extend<u64, u32>((u64)num);
    }
}

void eval_DIVU(u32 instr) {
    RType(instr);
    u32 num = state.reg.gpr[rs];
    u32 denum = state.reg.gpr[rt];
    if (denum != 0) {
        state.reg.multLo = sign_extend<u64, u32>(num / denum);
        state.reg.multHi = sign_extend<u64, u32>(num % denum);
    } else {
        debugger::undefined("Divide by 0 (DIVU)");
        // Undefined behaviour here according to the reference
        // manual. The machine behaviour is as implemented.
        state.reg.multLo = UINT64_C(-1);
        state.reg.multHi = sign_extend<u64, u32>(num);
    }
}

static void mult_u64(u64 x, u64 y, u64 *hi, u64 *lo) {
    u64 a = x >> 32, b = x & 0xffffffffllu;
    u64 c = y >> 32, d = y & 0xffffffffllu;

    u64 ac = a * c;
    u64 bc = b * c;
    u64 ad = a * d;
    u64 bd = b * d;

    u64 mid34 = (bd >> 32) + (bc & 0xffffffffllu) + (ad & 0xffffffffllu);

    *hi = ac + (bc >> 32) + (ad >> 32) + (mid34 >> 32);
    *lo = (mid34 << 32) | (bd & 0xffffffffllu);
}

void eval_DMULT(u32 instr) {
    RType(instr);
    mult_u64(state.reg.gpr[rs], state.reg.gpr[rt],
             &state.reg.multHi, &state.reg.multLo);
    if ((i64)state.reg.gpr[rs] < 0)
        state.reg.multHi -= state.reg.gpr[rt];
    if ((i64)state.reg.gpr[rt] < 0)
        state.reg.multHi -= state.reg.gpr[rs];
}

void eval_DMULTU(u32 instr) {
    RType(instr);
    mult_u64(state.reg.gpr[rs], state.reg.gpr[rt],
             &state.reg.multHi, &state.reg.multLo);
}

void eval_DSLL(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rt] << shamnt;
}

void eval_DSLL32(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rt] << (shamnt + 32);
}

void eval_DSLLV(u32 instr) {
    RType(instr);
    shamnt = state.reg.gpr[rs] & 0x3flu;
    state.reg.gpr[rd] = state.reg.gpr[rt] << shamnt;
}

void eval_DSRA(u32 instr) {
    RType(instr);
    bool sign = (state.reg.gpr[rt] & (UINT64_C(1) << 63)) != 0;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (UINT64_C(1) << shamnt) - 1u;
        state.reg.gpr[rd] |= mask << (64 - shamnt);
    }
}

void eval_DSRA32(u32 instr) {
    RType(instr);
    bool sign = (state.reg.gpr[rt] & (UINT64_C(1) << 63)) != 0;
    shamnt += 32;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (UINT64_C(1) << shamnt) - 1u;
        state.reg.gpr[rd] |= mask << (64 - shamnt);
    }
}

void eval_DSRAV(u32 instr) {
    RType(instr);
    bool sign = (state.reg.gpr[rt] & (UINT64_C(1) << 63)) != 0;
    shamnt = state.reg.gpr[rs] & 0x3flu;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (UINT64_C(1) << shamnt) - 1u;
        state.reg.gpr[rd] |= mask << (64 - shamnt);
    }
}

void eval_DSRL(u32 instr) {
    RType(instr);
    // Right shift is logical for unsigned c types.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
}

void eval_DSRL32(u32 instr) {
    RType(instr);
    // Right shift is logical for unsigned c types.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> (shamnt + 32);
}

void eval_DSRLV(u32 instr) {
    RType(instr);
    shamnt = state.reg.gpr[rs] & 0x3flu;
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
}

void eval_DSUB(u32 instr) {
    RType(instr);
    core::halt("DSUB");
}

void eval_DSUBU(u32 instr) {
    RType(instr);
    u64 res = state.reg.gpr[rs] - state.reg.gpr[rt];
    state.reg.gpr[rd] = res;
}

void eval_JALR(u32 instr) {
    RType(instr);
    u64 tg = state.reg.gpr[rs];
    state.reg.gpr[rd] = state.reg.pc + 8;
    state.cpu.nextAction = State::Action::Delay;
    state.cpu.nextPc = tg;
}

void eval_JR(u32 instr) {
    RType(instr);
    u64 tg = state.reg.gpr[rs];
    state.cpu.nextAction = State::Action::Delay;
    state.cpu.nextPc = tg;
}

void eval_MFHI(u32 instr) {
    RType(instr);
    // undefined if an instruction that follows modify
    // the special registers LO / HI
    state.reg.gpr[rd] = state.reg.multHi;
}

void eval_MFLO(u32 instr) {
    RType(instr);
    // undefined if an instruction that follows modify
    // the special registers LO / HI
    state.reg.gpr[rd] = state.reg.multLo;
}

void eval_MOVN(u32 instr) {
    RType(instr);
    core::halt("MOVN");
}

void eval_MOVZ(u32 instr) {
    RType(instr);
    core::halt("MOVZ");
}

void eval_MTHI(u32 instr) {
    RType(instr);
    state.reg.multHi = state.reg.gpr[rs];
}

void eval_MTLO(u32 instr) {
    RType(instr);
    state.reg.multLo = state.reg.gpr[rs];
}

void eval_MULT(u32 instr) {
    RType(instr);
    i32 a = (i32)(u32)state.reg.gpr[rs];
    i32 b = (i32)(u32)state.reg.gpr[rt];
    i64 m = (i64)a * (i64)b;
    state.reg.multLo = sign_extend<u64, u32>((u64)m);
    state.reg.multHi = sign_extend<u64, u32>((u64)m >> 32);
}

void eval_MULTU(u32 instr) {
    RType(instr);
    u32 a = (u32)state.reg.gpr[rs];
    u32 b = (u32)state.reg.gpr[rt];
    u64 m = (u64)a * (u64)b;
    state.reg.multLo = sign_extend<u64, u32>(m);
    state.reg.multHi = sign_extend<u64, u32>(m >> 32);
}

void eval_NOR(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = ~(state.reg.gpr[rs] | state.reg.gpr[rt]);
}

void eval_OR(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rs] | state.reg.gpr[rt];
}

void eval_SLL(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = sign_extend<u64, u32>(state.reg.gpr[rt] << shamnt);
}

void eval_SLLV(u32 instr) {
    RType(instr);
    shamnt = state.reg.gpr[rs] & 0x1flu;
    state.reg.gpr[rd] = sign_extend<u64, u32>(state.reg.gpr[rt] << shamnt);
}

void eval_SLT(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = (i64)state.reg.gpr[rs] < (i64)state.reg.gpr[rt];
}

void eval_SLTU(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rs] < state.reg.gpr[rt];
}

void eval_SRA(u32 instr) {
    RType(instr);
    bool sign = (state.reg.gpr[rt] & (1lu << 31)) != 0;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (1ul << (shamnt + 32)) - 1u;
        state.reg.gpr[rd] |= mask << (32 - shamnt);
    }
}

void eval_SRAV(u32 instr) {
    RType(instr);
    bool sign = (state.reg.gpr[rt] & (1lu << 31)) != 0;
    shamnt = state.reg.gpr[rs] & 0x1flu;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (1ul << (shamnt + 32)) - 1u;
        state.reg.gpr[rd] |= mask << (32 - shamnt);
    }
}

void eval_SRL(u32 instr) {
    RType(instr);
    u64 res = (state.reg.gpr[rt] & 0xffffffffllu) >> shamnt;
    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_SRLV(u32 instr) {
    RType(instr);
    shamnt = state.reg.gpr[rs] & 0x1flu;
    u64 res = (state.reg.gpr[rt] & 0xfffffffflu) >> shamnt;
    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_SUB(u32 instr) {
    RType(instr);
    int32_t res;
    int32_t a = (int32_t)(uint32_t)state.reg.gpr[rs];
    int32_t b = (int32_t)(uint32_t)state.reg.gpr[rt];
    if (__builtin_sub_overflow(a, b, &res)) {
        core::halt("SUB IntegerOverflow");
    }
    state.reg.gpr[rd] = sign_extend<u64, u32>((uint32_t)res);
}

void eval_SUBU(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = sign_extend<u64, u32>(
        state.reg.gpr[rs] - state.reg.gpr[rt]);
}

void eval_SYNC(u32 instr) {
    (void)instr;
}

void eval_SYSCALL(u32 instr) {
    RType(instr);
    takeException(SystemCall, 0, false, false, 0);
}

void eval_TEQ(u32 instr) {
    RType(instr);
    core::halt("TEQ");
}

void eval_TGE(u32 instr) {
    RType(instr);
    core::halt("TGE");
}

void eval_TGEU(u32 instr) {
    RType(instr);
    core::halt("TGEU");
}

void eval_TLT(u32 instr) {
    RType(instr);
    core::halt("TLT");
}

void eval_TLTU(u32 instr) {
    RType(instr);
    core::halt("TLTU");
}

void eval_TNE(u32 instr) {
    RType(instr);
    core::halt("TNE");
}

void eval_XOR(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rs] ^ state.reg.gpr[rt];
}

/* REGIMM opcodes */

void eval_BGEZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.reg.gpr[rs] >= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BGEZL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely((i64)state.reg.gpr[rs] >= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLTZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.reg.gpr[rs] < 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLTZL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely((i64)state.reg.gpr[rs] < 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BGEZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    branch(r >= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BGEZALL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    branch_likely(r >= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLTZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    branch(r < 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLTZALL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    branch_likely(r < 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_TEQI(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TEQI");
}

void eval_TGEI(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TGEI");
}

void eval_TGEIU(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TGEIU");
}

void eval_TLTI(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TLTI");
}

void eval_TLTIU(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TLTIU");
}

void eval_TNEI(u32 instr) {
    IType(instr, sign_extend);
    core::halt("TNEI");
}

/* Other opcodes */

void eval_ADDI(u32 instr) {
    IType(instr, sign_extend);
    i32 res;
    i32 a = (i32)(u32)state.reg.gpr[rs];
    i32 b = (i32)(u32)imm;
    if (__builtin_add_overflow(a, b, &res)) {
        core::halt("ADDI IntegerOverflow");
    }
    state.reg.gpr[rt] = sign_extend<u64, u32>((u32)res);
}

void eval_ADDIU(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = sign_extend<u64, u32>(state.reg.gpr[rs] + imm);
}

void eval_ANDI(u32 instr) {
    IType(instr, zero_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] & imm;
}

void eval_BEQ(u32 instr) {
    IType(instr, sign_extend);
    branch(state.reg.gpr[rt] == state.reg.gpr[rs],
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BEQL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely(state.reg.gpr[rt] == state.reg.gpr[rs],
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BGTZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.reg.gpr[rs] > 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BGTZL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely((i64)state.reg.gpr[rs] > 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLEZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.reg.gpr[rs] <= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BLEZL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely((i64)state.reg.gpr[rs] <= 0,
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BNE(u32 instr) {
    IType(instr, sign_extend);
    branch(state.reg.gpr[rt] != state.reg.gpr[rs],
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_BNEL(u32 instr) {
    IType(instr, sign_extend);
    branch_likely(state.reg.gpr[rt] != state.reg.gpr[rs],
        state.reg.pc + 4 + (i64)(imm << 2),
        state.reg.pc + 8);
}

void eval_CACHE(u32 instr) {
    (void)instr;
}

void eval_COP2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 2);
}

void eval_COP3(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 3);
}

void eval_DADDI(u32 instr) {
    IType(instr, sign_extend);
    core::halt("DADDI");
}

void eval_DADDIU(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] + imm;
}

void eval_J(u32 instr) {
    u64 tg = assembly::getTarget(instr);
    tg = (state.reg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.cpu.nextAction = State::Action::Delay;
    state.cpu.nextPc = tg;
}

void eval_JAL(u32 instr) {
    u64 tg = assembly::getTarget(instr);
    tg = (state.reg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.reg.gpr[31] = state.reg.pc + 8;
    state.cpu.nextAction = State::Action::Delay;
    state.cpu.nextPc = tg;
}

void eval_LB(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u8 val;

    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u8(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = sign_extend<u64, u8>(val);
}

void eval_LBU(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u8 val;

    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u8(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = zero_extend<u64, u8>(val);
}

void eval_LD(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr, val;

    checkAddressAlignment(vAddr, 8, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u64(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = val;
}

void eval_LDC1(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr, val;

    checkCop1Usable();
    checkAddressAlignment(vAddr, 8, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u64(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.cp1reg.fpr_d[rt]->l = val;
}

void eval_LDC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    core::halt("LDC2");
}

void eval_LDL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, true),
        vAddr, false, false, 0);

    size_t count = 8 - (pAddr % 8);
    unsigned int shift = 56;
    u64 mask = (1llu << (64 - 8 * count)) - 1u;
    u64 val = 0;

    for (size_t nr = 0; nr < count; nr++, shift -= 8) {
        u8 byte = 0;
        if (!state.bus->load_u8(pAddr + nr, &byte)) {
            takeException(BusError, vAddr, false, false, 0);
            return;
        }
        val |= ((u64)byte << shift);
    }

    val = val | (state.reg.gpr[rt] & mask);
    state.reg.gpr[rt] = val;
}

void eval_LDR(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, true),
        vAddr, false, false, 0);

    size_t count = 1 + (pAddr % 8);
    unsigned int shift = 0;
    u64 mask = (1llu << (64 - 8 * count)) - 1u;
    mask <<= 8 * count;
    u64 val = 0;

    for (size_t nr = 0; nr < count; nr++, shift += 8) {
        u8 byte = 0;
        if (!state.bus->load_u8(pAddr - nr, &byte)) {
            takeException(BusError, vAddr, false, false);
            return;
        }
        val |= ((u64)byte << shift);
    }

    val = val | (state.reg.gpr[rt] & mask);
    state.reg.gpr[rt] = val;
}

void eval_LH(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u16 val;

    checkAddressAlignment(vAddr, 2, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u16(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = sign_extend<u64, u16>(val);
}

void eval_LHU(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u16 val;

    checkAddressAlignment(vAddr, 2, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u16(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = zero_extend<u64, u16>(val);
}

void eval_LL(u32 instr) {
    IType(instr, sign_extend);
    core::halt("LL");
}

void eval_LLD(u32 instr) {
    IType(instr, sign_extend);
    core::halt("LLD");
}

void eval_LUI(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = imm << 16;
}

void eval_LW(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u32 val;

    checkAddressAlignment(vAddr, 4, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u32(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = sign_extend<u64, u32>(val);
}

void eval_LWC1(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u32 val;

    checkCop1Usable();
    checkAddressAlignment(vAddr, 4, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u32(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.cp1reg.fpr_s[rt]->w = val;
}

void eval_LWC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    core::halt("LWC2");
}

void eval_LWC3(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 3);
    core::halt("LWC3");
}

void eval_LWL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, true),
        vAddr, false, false, 0);

    size_t count = 4 - (pAddr % 4);
    unsigned int shift = 24;
    u64 mask = (1llu << (32 - 8 * count)) - 1u;
    u64 val = 0;

    for (size_t nr = 0; nr < count; nr++, shift -= 8) {
        u8 byte = 0;
        if (!state.bus->load_u8(pAddr + nr, &byte)) {
            takeException(BusError, vAddr, false, false, 0);
            return;
        }
        val |= ((u64)byte << shift);
    }

    val = val | (state.reg.gpr[rt] & mask);
    state.reg.gpr[rt] = sign_extend<u64, u32>(val);
}

void eval_LWR(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, true),
        vAddr, false, false, 0);

    size_t count = 1 + (pAddr % 4);
    unsigned int shift = 0;
    u64 mask = (1llu << (32 - 8 * count)) - 1u;
    mask <<= 8 * count;
    u64 val = 0;

    for (size_t nr = 0; nr < count; nr++, shift += 8) {
        u8 byte = 0;
        if (!state.bus->load_u8(pAddr - nr, &byte)) {
            takeException(BusError, vAddr, false, false);
            return;
        }
        val |= ((u64)byte << shift);
    }

    val = val | (state.reg.gpr[rt] & mask);
    state.reg.gpr[rt] = sign_extend<u64, u32>(val);
}

void eval_LWU(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;
    u32 val;

    checkAddressAlignment(vAddr, 4, false, true);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u32(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = zero_extend<u64, u32>(val);
}

void eval_ORI(u32 instr) {
    IType(instr, zero_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] | imm;
}

void eval_SB(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u8(pAddr, state.reg.gpr[rt]) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SC(u32 instr) {
    IType(instr, sign_extend);
    core::halt("SC");
}

void eval_SCD(u32 instr) {
    IType(instr, sign_extend);
    core::halt("SCD");
}

void eval_SD(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkAddressAlignment(vAddr, 8, false, false);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u64(pAddr, state.reg.gpr[rt]) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SDC1(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkCop1Usable();
    checkAddressAlignment(vAddr, 8, false, false);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u64(pAddr, state.cp1reg.fpr_d[rt]->l) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SDC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    core::halt("SDC2");
}

void eval_SDL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);

    size_t count = 8 - (pAddr % 8);
    u64 val = state.reg.gpr[rt];
    unsigned int shift = 56;
    for (size_t nr = 0; nr < count; nr++, shift -= 8) {
        u64 byte = (val >> shift) & 0xfflu;
        if (!state.bus->store_u8(pAddr + nr, byte)) {
            takeException(BusError, vAddr, false, false, 0);
            return;
        }
    }
}

void eval_SDR(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);

    size_t count = 1 + (pAddr % 8);
    u64 val = state.reg.gpr[rt];
    unsigned int shift = 0;
    for (size_t nr = 0; nr < count; nr++, shift += 8) {
        u64 byte = (val >> shift) & 0xfflu;
        if (!state.bus->store_u8(pAddr - nr, byte)) {
            takeException(BusError, vAddr, false, false);
            return;
        }
    }
}

void eval_SH(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkAddressAlignment(vAddr, 2, false, false);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u16(pAddr, state.reg.gpr[rt]) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SLTI(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = (i64)state.reg.gpr[rs] < (i64)imm;
}

void eval_SLTIU(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] < imm;
}

void eval_SW(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkAddressAlignment(vAddr, 4, false, false);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u32(pAddr, state.reg.gpr[rt]) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SWC1(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkCop1Usable();
    checkAddressAlignment(vAddr, 4, false, false);
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u32(pAddr, state.cp1reg.fpr_s[rt]->w) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SWC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 2);
    core::halt("SWC2");
}

void eval_SWC3(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 2);
    core::halt("SWC3");
}

void eval_SWL(u32 instr) {
    IType(instr, sign_extend);
    // core::halt("SWL instruction");
    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);

    size_t count = 4 - (pAddr % 4);
    u32 val = state.reg.gpr[rt];
    unsigned int shift = 24;
    for (size_t nr = 0; nr < count; nr++, shift -= 8) {
        u64 byte = (val >> shift) & 0xfflu;
        if (!state.bus->store_u8(pAddr + nr, byte)) {
            takeException(BusError, vAddr, false, false, 0);
            return;
        }
    }
}

void eval_SWR(u32 instr) {
    IType(instr, sign_extend);
    // core::halt("SWR instruction");
    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translate_address(vAddr, &pAddr, false),
        vAddr, false, false, 0);

    size_t count = 1 + (pAddr % 4);
    u32 val = state.reg.gpr[rt];
    unsigned int shift = 0;
    for (size_t nr = 0; nr < count; nr++, shift += 8) {
        u64 byte = (val >> shift) & 0xfflu;
        if (!state.bus->store_u8(pAddr - nr, byte)) {
            takeException(BusError, vAddr, false, false);
            return;
        }
    }
}

void eval_XORI(u32 instr) {
    IType(instr, zero_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] ^ imm;
}


static void (*SPECIAL_callbacks[64])(u32) = {
    eval_SLL,       eval_Reserved,  eval_SRL,       eval_SRA,
    eval_SLLV,      eval_Reserved,  eval_SRLV,      eval_SRAV,
    eval_JR,        eval_JALR,      eval_MOVZ,      eval_MOVN,
    eval_SYSCALL,   eval_BREAK,     eval_Reserved,  eval_SYNC,
    eval_MFHI,      eval_MTHI,      eval_MFLO,      eval_MTLO,
    eval_DSLLV,     eval_Reserved,  eval_DSRLV,     eval_DSRAV,
    eval_MULT,      eval_MULTU,     eval_DIV,       eval_DIVU,
    eval_DMULT,     eval_DMULTU,    eval_DDIV,      eval_DDIVU,
    eval_ADD,       eval_ADDU,      eval_SUB,       eval_SUBU,
    eval_AND,       eval_OR,        eval_XOR,       eval_NOR,
    eval_Reserved,  eval_Reserved,  eval_SLT,       eval_SLTU,
    eval_DADD,      eval_DADDU,     eval_DSUB,      eval_DSUBU,
    eval_TGE,       eval_TGEU,      eval_TLT,       eval_TLTU,
    eval_TEQ,       eval_Reserved,  eval_TNE,       eval_Reserved,
    eval_DSLL,      eval_Reserved,  eval_DSRL,      eval_DSRA,
    eval_DSLL32,    eval_Reserved,  eval_DSRL32,    eval_DSRA32,
};

void eval_SPECIAL(u32 instr) {
    SPECIAL_callbacks[assembly::getFunct(instr)](instr);
}

static void (*REGIMM_callbacks[32])(u32) = {
    eval_BLTZ,      eval_BGEZ,      eval_BLTZL,     eval_BGEZL,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_TGEI,      eval_TGEIU,     eval_TLTI,      eval_TLTIU,
    eval_TEQI,      eval_Reserved,  eval_TNEI,      eval_Reserved,
    eval_BLTZAL,    eval_BGEZAL,    eval_BLTZALL,   eval_BGEZALL,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

void eval_REGIMM(u32 instr) {
    REGIMM_callbacks[assembly::getRt(instr)](instr);
}

static void (*CPU_callbacks[64])(u32) = {
    eval_SPECIAL,   eval_REGIMM,    eval_J,         eval_JAL,
    eval_BEQ,       eval_BNE,       eval_BLEZ,      eval_BGTZ,
    eval_ADDI,      eval_ADDIU,     eval_SLTI,      eval_SLTIU,
    eval_ANDI,      eval_ORI,       eval_XORI,      eval_LUI,
    eval_COP0,      eval_COP1,      eval_COP2,      eval_COP3,
    eval_BEQL,      eval_BNEL,      eval_BLEZL,     eval_BGTZL,
    eval_DADDI,     eval_DADDIU,    eval_LDL,       eval_LDR,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_LB,        eval_LH,        eval_LWL,       eval_LW,
    eval_LBU,       eval_LHU,       eval_LWR,       eval_LWU,
    eval_SB,        eval_SH,        eval_SWL,       eval_SW,
    eval_SDL,       eval_SDR,       eval_SWR,       eval_CACHE,
    eval_LL,        eval_LWC1,      eval_LWC2,      eval_LWC3,
    eval_LLD,       eval_LDC1,      eval_LDC2,      eval_LD,
    eval_SC,        eval_SWC1,      eval_SWC2,      eval_SWC3,
    eval_SCD,       eval_SDC1,      eval_SDC2,      eval_SD,
};

void eval_Instr(u32 instr) {
    // The null instruction is 'sll r0, r0, 0', i.e. a NOP.
    // As it is one of the most used instructions (to fill in delay slots),
    // perform a quick check to spare the instruction execution.
    if (instr) CPU_callbacks[assembly::getOpcode(instr)](instr);
}

void eval(void) {
    u64 vaddr = state.reg.pc;
    u64 paddr;
    u32 instr;

    state.cycles++;

    checkException(
        translate_address(vaddr, &paddr, false),
        vaddr, true, true, 0);
    checkException(
        state.bus->load_u32(paddr, &instr) ? None : BusError,
        vaddr, true, true, 0);

    debugger::debugger.cpuTrace.put(Debugger::TraceEntry(vaddr, instr));
    if (debugger::debugger.checkBreakpoint(paddr))
        core::halt("Breakpoint");

    eval_Instr(instr);
}

}; /* namespace interpreter::cpu */
