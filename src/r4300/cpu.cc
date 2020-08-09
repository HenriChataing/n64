
#include <iomanip>
#include <iostream>
#include <fstream>

#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <r4300/export.h>
#include <debugger.h>

#include "eval.h"

namespace R4300 {

/**
 * @brief Check whether an interrupt exception is raised from the current state.
 *  Take the interrupt exception if this is the case.
 */
void checkInterrupt(void) {
    // For the interrupt to be taken, the interrupts must globally enabled
    // (IE = 1) and the particular interrupt must be unmasked (IM[irq] = 1).
    // Interrupt exceptions are also disabled during exception
    // handling (EXL = 1).
    if (!state.cp0reg.EXL() && state.cp0reg.IE() &&
        (state.cp0reg.IM() & state.cp0reg.IP())) {

        // Arrange for the interrupt to be taken at the following instruction :
        // The present instruction which enabled the interrupt must
        // not be repeated.
        //
        // Two cases here :
        // 1. called from instruction eval function,
        //    check next action to determine the following instruction.
        // 2. called from event handler. The result is the same, event
        //    handlers are always called before the instruction to execute
        //    is determined.
        switch (state.cpu.nextAction) {
        case State::Action::Continue:
            state.reg.pc += 4;
            state.cpu.delaySlot = false;
            break;

        case State::Action::Delay:
            state.reg.pc += 4;
            state.cpu.delaySlot = true;
            break;

        case State::Action::Jump:
            state.reg.pc = state.cpu.nextPc;
            state.cpu.delaySlot = false;
            break;
        }

        Eval::takeException(Interrupt, 0, false, false, 0);
    }
}

/**
 * @brief Set the selected interrupt pending bit in the Cause register.
 *  The Interrupt exception will be taken just before executing the next
 *  instruction if the conditions are met (see \ref checkInterrupt).
 *
 * @param irq           Interrupt number.
 */
void setInterruptPending(uint irq) {
    // Update the pending bits in the Cause register.
    state.cp0reg.cause |= CAUSE_IP(1lu << irq);
    checkInterrupt();
}

void clearInterruptPending(uint irq) {
    // Update the pending bits in the Cause register.
    state.cp0reg.cause &= ~CAUSE_IP(1lu << irq);
}

namespace Eval {

using namespace R4300::Eval;

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
    u32 rs = Mips::getRs(instr); \
    u32 rt = Mips::getRt(instr); \
    u64 imm = extend<u64, u16>(Mips::getImmediate(instr)); \
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
    u32 rd = Mips::getRd(instr); \
    u32 rs = Mips::getRs(instr); \
    u32 rt = Mips::getRt(instr); \
    u32 shamnt = Mips::getShamnt(instr); \
    (void)rd; (void)rs; (void)rt; (void)shamnt;

/**
 * @brief Raise an exception and update the state of the processor.
 *  The delay slot parameter is provided by the state member cpu.delaySlot.
 *
 * @param vAddr
 *      Virtual address being accessed. Required for AddressError,
 *      TLBRefill, XTLBRefill, TLBInvalid, TLBModified,
 *      VirtualCoherency exceptions.
 * @param instr
 *      Whether the exception was triggered by an instruction fetch.
 * @param load
 *      Whether the exception was triggered by a load or store operation.
 * @param ce
 *      Index of the coprocessor for CoprocessorUnusable exceptions.
 */
void takeException(Exception exn, u64 vAddr, bool instr, bool load, u32 ce)
{
    // Default vector valid for all general exceptions.
    u64 vector = 0x180u;
    u32 exccode = 0;

    // Following the diagrams printed in the following section of the
    // reference manual:
    //      5.4 Exception Handling and Servicing Flowcharts,

    // Code specific to each exception.
    // The exception code and vector is determined by the exception type.
    // Each exception may set various register fields.
    switch (exn) {
        // The Address Error exception occurs when an attempt is made to execute
        // one of the following:
        //      - load or store from/to an unaligned memory location.
        //          (e.g. LW from an address that is not aligned to a word)
        //      - reference the kernel address space from User or Supervisor
        //          mode
        //      - reference the supervisor address space from User mode
        case AddressError:
            exccode = load ? 4 : 5; // AdEL : AdES
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception AddressError({:08x},{})",
                vAddr, load);
            debugger::halt("AddressError");
            break;
        // TLB Refill occurs when there is no TLB entry that matches an
        // attempted reference to a mapped address space.
        case TLBRefill:
        case XTLBRefill:
            vector = exn == XTLBRefill ? 0x080llu : 0x000llu;
            /* fallthrough */
        // TLB Invalid occurs when a virtual address reference matches a
        // TLB entry that is marked invalid.
        case TLBInvalid:
            exccode = load ? 2 : 3; // TLBL : TLBS
            state.cp0reg.badvaddr = vAddr;
            state.cp0reg.entryhi &= ~0xffffffe000llu;
            state.cp0reg.entryhi |= (vAddr & 0xffffffe000llu);
            state.cp0reg.context &= (CONTEXT_PTEBASE_MASK << CONTEXT_PTEBASE_SHIFT);
            state.cp0reg.context |=
                ((vAddr >> 13) & CONTEXT_BADVPN2_MASK) << CONTEXT_BADVPN2_SHIFT;
            debugger::info(Debugger::CPU,
                "exception TLBRefill/TLBInvalid({:08x},{})",
                vAddr, load);
            // TODO : XContext
            break;
        // TLB Modified occurs when a store operation virtual address
        // reference to memory matches a TLB entry which is marked
        // valid but is not dirty (the entry is not writable).
        case TLBModified:
            exccode = 1; // Mod
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception TLBModified({:08x},{})", vAddr);
            debugger::halt("TLBModified");
            // TODO : Context, XContext, EntryHi
            break;
        // The Cache Error exception occurs when either a secondary cache ECC
        // error, primary cache parity error, or SysAD bus parity/ECC error
        // condition occurs and error detection is enabled.
        case CacheError:
            // vector = 0x100llu;
            debugger::info(Debugger::CPU, "exception CacheError");
            debugger::halt("CacheError");
            break;
        // A Virtual Coherency exception occurs when all of the following
        // conditions are true:
        //      - a primary cache miss hits in the secondary cache
        //      - bits 14:12 of the virtual address were not equal to the
        //        corresponding bits of the PIdx field of the secondary
        //        cache tag
        //      - the cache algorithm for the page (from the C field in the TLB)
        //        specifies that the page is cached
        case VirtualCoherency:
            exccode = instr ? 14 : 31; // VCEI : VCED
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception VirtualCoherency({:08x},{})",
                vAddr, instr);
            debugger::halt("VirtualCoherency");
            break;
        // A Bus Error exception is raised by board-level circuitry for events
        // such as bus time-out, backplane bus parity errors, and invalid
        // physical memory addresses or access types.
        case BusError:
            exccode = instr ? 6 : 7; // IBE : DBE
            debugger::info(Debugger::CPU, "exception BusError({})", instr);
            debugger::halt("BusError");
            break;
        // An Integer Overflow exception occurs when an ADD, ADDI, SUB, DADD,
        // DADDI or DSUB instruction results in a 2’s complement overflow
        case IntegerOverflow:
            exccode = 12; // Ov
            debugger::info(Debugger::CPU, "exception IntegerOverflow");
            debugger::halt("IntegerOverflow");
            break;
        // The Trap exception occurs when a TGE, TGEU, TLT, TLTU, TEQ, TNE,
        // TGEI, TGEUI, TLTI, TLTUI, TEQI, or TNEI instruction results in a TRUE
        // condition.
        case Trap:
            exccode = 13; // Tr
            debugger::info(Debugger::CPU, "exception Trap");
            debugger::halt("Trap");
            break;
        // A System Call exception occurs during an attempt to execute the
        // SYSCALL instruction.
        case SystemCall:
            exccode = 8; // Sys
            debugger::info(Debugger::CPU, "exception SystemCall");
            break;
        // A Breakpoint exception occurs when an attempt is made to execute the
        // BREAK instruction.
        case Breakpoint:
            exccode = 9; // Bp
            debugger::info(Debugger::CPU, "exception Breakpoint");
            debugger::halt("Breakpoint");
            break;
        // The Reserved Instruction exception occurs when one of the following
        // conditions occurs:
        //      - an attempt is made to execute an instruction with an undefined
        //        major opcode (bits 31:26)
        //      - an attempt is made to execute a SPECIAL instruction with an
        //        undefined minor opcode (bits 5:0)
        //      - an attempt is made to execute a REGIMM instruction with an
        //        undefined minor opcode (bits 20:16)
        //      - an attempt is made to execute 64-bit operations in 32-bit mode
        //        when in User or Supervisor mode
        case ReservedInstruction:
            exccode = 10; // RI
            debugger::info(Debugger::CPU, "exception ReservedInstruction");
            debugger::halt("ReservedInstruction");
            break;
        // The Coprocessor Unusable exception occurs when an attempt is made to
        // execute a coprocessor instruction for either:
        //      - a corresponding coprocessor unit that has not been marked
        //        usable, or
        //      - CP0 instructions, when the unit has not been marked usable
        //        and the process executes in either User or Supervisor mode.
        case CoprocessorUnusable:
            exccode = 11; // CpU
            debugger::info(Debugger::CPU, "exception CoprocessorUnusable({})", ce);
            break;
        // The Floating-Point exception is used by the floating-point
        // coprocessor.
        case FloatingPoint:
            exccode = 15; // FPEEXL
            debugger::info(Debugger::CPU, "exception FloatingPoint");
            debugger::halt("FloatingPoint");
            // TODO: Set FP Control Status Register
            break;
        // A Watch exception occurs when a load or store instruction references
        // the  physical address specified in the WatchLo/WatchHi System Control
        // Coprocessor (CP0) registers. The WatchLo register specifies whether a
        // load or store initiated this exception.
        case Watch:
            exccode = 23; // WATCH
            debugger::info(Debugger::CPU, "exception Watch");
            debugger::halt("Watch");
            // TODO: Set Watch register
            break;
        // The Interrupt exception occurs when one of the eight interrupt conditions
        // is asserted.
        case Interrupt:
            exccode = 0;
            debugger::info(Debugger::CPU, "exception Interrupt");
            break;
        default:
            debugger::halt("UndefinedException");
            break;
    }

    // Set Cause Register : EXCCode, CE
    state.cp0reg.cause &= ~(CAUSE_EXCCODE_MASK | CAUSE_CE_MASK);
    state.cp0reg.cause |= CAUSE_EXCCODE(exccode) | CAUSE_CE(ce);
    // Check if exception within exception.
    if (!state.cp0reg.EXL()) {
        // Check if the exception was caused by a delay slot instruction.
        // Set EPC and Cause:BD accordingly.
        if (state.cpu.delaySlot) {
            state.cp0reg.epc = state.reg.pc - 4;
            state.cp0reg.cause |= CAUSE_BD;
        } else {
            state.cp0reg.epc = state.reg.pc;
            state.cp0reg.cause &= ~CAUSE_BD;
        }
    } else {
        // The vector is forced to 0x180 even for TLB/XTLB Miss in this case.
        vector = 0x180llu;
    }
    // Processor forced to Kernel Mode
    // & interrupt disabled.
    state.cp0reg.sr |= STATUS_EXL;
    // Check if exceuting bootstrap code
    // and jump to the designated handler.
    u64 pc;
    if (state.cp0reg.BEV()) {
        pc = 0xffffffffbfc00200llu + vector;
    } else {
        pc = 0xffffffff80000000llu + vector;
    }

    state.cpu.nextAction = State::Action::Jump;
    state.cpu.nextPc = pc;
}

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

u64 captureEnd;

void eval_Reserved(u32 instr) {
    debugger::halt("CPU reserved instruction");
}

/* SPECIAL opcodes */

void eval_ADD(u32 instr) {
    RType(instr);
    i32 res;
    i32 a = (i32)(u32)state.reg.gpr[rs];
    i32 b = (i32)(u32)state.reg.gpr[rt];
    if (__builtin_add_overflow(a, b, &res)) {
        debugger::halt("ADD IntegerOverflow");
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
    debugger::halt("BREAK");
}

void eval_DADD(u32 instr) {
    RType(instr);
    i64 res;
    i64 a = (i64)state.reg.gpr[rs];
    i64 b = (i64)state.reg.gpr[rt];
    if (__builtin_add_overflow(a, b, &res)) {
        debugger::halt("DADD IntegerOverflow");
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
    debugger::halt("DSUB");
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
    debugger::halt("MOVN");
}

void eval_MOVZ(u32 instr) {
    RType(instr);
    debugger::halt("MOVZ");
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
        debugger::halt("SUB IntegerOverflow");
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
    debugger::halt("TEQ");
}

void eval_TGE(u32 instr) {
    RType(instr);
    debugger::halt("TGE");
}

void eval_TGEU(u32 instr) {
    RType(instr);
    debugger::halt("TGEU");
}

void eval_TLT(u32 instr) {
    RType(instr);
    debugger::halt("TLT");
}

void eval_TLTU(u32 instr) {
    RType(instr);
    debugger::halt("TLTU");
}

void eval_TNE(u32 instr) {
    RType(instr);
    debugger::halt("TNE");
}

void eval_XOR(u32 instr) {
    RType(instr);
    state.reg.gpr[rd] = state.reg.gpr[rs] ^ state.reg.gpr[rt];
}

/* REGIMM opcodes */

void eval_BGEZ(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] >= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BGEZL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] >= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BLTZ(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] < 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLTZL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] < 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BGEZAL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    if (r >= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BGEZALL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    if (r >= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BLTZAL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    if (r < 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLTZALL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    i64 r = state.reg.gpr[rs];
    state.reg.gpr[31] = state.reg.pc + 8;
    if (r < 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_TEQI(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TEQI");
}

void eval_TGEI(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TGEI");
}

void eval_TGEIU(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TGEIU");
}

void eval_TLTI(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TLTI");
}

void eval_TLTIU(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TLTIU");
}

void eval_TNEI(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("TNEI");
}

/* Other opcodes */

void eval_ADDI(u32 instr) {
    IType(instr, sign_extend);
    i32 res;
    i32 a = (i32)(u32)state.reg.gpr[rs];
    i32 b = (i32)(u32)imm;
    if (__builtin_add_overflow(a, b, &res)) {
        debugger::halt("ADDI IntegerOverflow");
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
    captureEnd = state.reg.pc + 8;
    if (state.reg.gpr[rt] == state.reg.gpr[rs]) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BEQL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if (state.reg.gpr[rt] == state.reg.gpr[rs]) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BGTZ(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] > 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BGTZL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] > 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BLEZ(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] <= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLEZL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if ((i64)state.reg.gpr[rs] <= 0) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
}

void eval_BNE(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if (state.reg.gpr[rt] != state.reg.gpr[rs]) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BNEL(u32 instr) {
    IType(instr, sign_extend);
    captureEnd = state.reg.pc + 8;
    if (state.reg.gpr[rt] != state.reg.gpr[rs]) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(imm << 2);
    } else {
        state.reg.pc += 4;
    }
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
    debugger::halt("DADDI");
}

void eval_DADDIU(u32 instr) {
    IType(instr, sign_extend);
    state.reg.gpr[rt] = state.reg.gpr[rs] + imm;
}

void eval_J(u32 instr) {
    u64 tg = Mips::getTarget(instr);
    tg = (state.reg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.cpu.nextAction = State::Action::Delay;
    state.cpu.nextPc = tg;
}

void eval_JAL(u32 instr) {
    u64 tg = Mips::getTarget(instr);
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u64(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.cp1reg.fpr_d[rt]->l = val;
}

void eval_LDC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    debugger::halt("LDC2");
}

void eval_LDL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translateAddress(vAddr, &pAddr, true),
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
        translateAddress(vAddr, &pAddr, true),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u16(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.reg.gpr[rt] = zero_extend<u64, u16>(val);
}

void eval_LL(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("LL");
}

void eval_LLD(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("LLD");
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, true, 0);
    checkException(
        state.bus->load_u32(pAddr, &val) ? None : BusError,
        vAddr, false, true, 0);

    state.cp1reg.fpr_s[rt]->w = val;
}

void eval_LWC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    debugger::halt("LWC2");
}

void eval_LWC3(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 3);
    debugger::halt("LWC3");
}

void eval_LWL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translateAddress(vAddr, &pAddr, true),
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
        translateAddress(vAddr, &pAddr, true),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u8(pAddr, state.reg.gpr[rt]) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SC(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("SC");
}

void eval_SCD(u32 instr) {
    IType(instr, sign_extend);
    debugger::halt("SCD");
}

void eval_SD(u32 instr) {
    IType(instr, sign_extend);

    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    checkAddressAlignment(vAddr, 8, false, false);
    checkException(
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u64(pAddr, state.cp1reg.fpr_d[rt]->l) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SDC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, true, 2);
    debugger::halt("SDC2");
}

void eval_SDL(u32 instr) {
    IType(instr, sign_extend);

    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
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
        translateAddress(vAddr, &pAddr, false),
        vAddr, false, false, 0);
    checkException(
        state.bus->store_u32(pAddr, state.cp1reg.fpr_s[rt]->w) ? None : BusError,
        vAddr, false, false, 0);
}

void eval_SWC2(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 2);
    debugger::halt("SWC2");
}

void eval_SWC3(u32 instr) {
    takeException(CoprocessorUnusable, 0, false, false, 2);
    debugger::halt("SWC3");
}

void eval_SWL(u32 instr) {
    IType(instr, sign_extend);
    // debugger::halt("SWL instruction");
    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translateAddress(vAddr, &pAddr, false),
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
    // debugger::halt("SWR instruction");
    // @todo only BigEndianMem & !ReverseEndian for now
    u64 vAddr = state.reg.gpr[rs] + imm;
    u64 pAddr;

    // Not calling checkAddressAlignment:
    // this instruction specifically ignores the alignment
    checkException(
        translateAddress(vAddr, &pAddr, false),
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


void (*SPECIAL_callbacks[64])(u32) = {
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
    SPECIAL_callbacks[Mips::getFunct(instr)](instr);
}

void (*REGIMM_callbacks[32])(u32) = {
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
    REGIMM_callbacks[Mips::getRt(instr)](instr);
}

void (*CPU_callbacks[64])(u32) = {
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

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 */
static void eval()
{
    u64 vAddr = state.reg.pc;
    u64 pAddr;
    u32 instr;

    state.cycles++;

    checkException(
        translateAddress(vAddr, &pAddr, false),
        vAddr, true, true, 0);
    checkException(
        state.bus->load_u32(pAddr, &instr) ? None : BusError,
        vAddr, true, true, 0);

    debugger::debugger.cpuTrace.put(Debugger::TraceEntry(vAddr, instr));
    if (debugger::debugger.checkBreakpoint(pAddr))
        debugger::halt("Breakpoint");

    // The null instruction is 'sll r0, r0, 0', i.e. a NOP.
    // It is one of the most used instructions (to fill in delay slots).
    if (instr) {
        CPU_callbacks[Mips::getOpcode(instr)](instr);
    }
}

static std::map<u64, unsigned> blockStart;
static unsigned captureCount;
static bool captureRunning = false;
static u64  captureStart;
static struct cpureg captureCpuPre;
static struct cp0reg captureCp0Pre;
static struct cp1reg captureCp1Pre;

void startCapture(void) {
    if (captureCount > 1000) {
        return;
    }
    if (blockStart.find(state.reg.pc) == blockStart.end()) {
        blockStart[state.reg.pc] = 1;
        return;
    }

    unsigned count = blockStart[state.reg.pc] + 1;
    blockStart[state.reg.pc] = count;
    if (count < 1000 || count > 1500 || (count % 100)) {
        return;
    }

    debugger::warn(Debugger::CPU, "starting capture for address {:x}",
        state.reg.pc);

    captureRunning = true;
    captureStart = state.reg.pc;
    captureEnd = -1llu;
    captureCpuPre = state.reg;
    captureCp0Pre = state.cp0reg;
    captureCp1Pre = state.cp1reg;
    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    bus->capture(true);
}

void stopCapture(u64 finalAddress) {
    if (!captureRunning)
        return;

    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    std::string filename =
        fmt::format("test/recompiler/test_{:08x}.toml",
            captureStart & 0xfffffffflu);
    std::string filename_pre =
        fmt::format("test/recompiler/test_{:08x}.input",
            captureStart & 0xfffffffflu);
    std::string filename_post =
        fmt::format("test/recompiler/test_{:08x}.output",
            captureStart & 0xfffffffflu);
    std::FILE *of, *pref, *postf;
    bool exists;

    debugger::warn(Debugger::CPU, "saving capture for address {:x}",
        captureStart);

    of = std::fopen(filename.c_str(), "r");
    exists = of != NULL;
    if (of) std::fclose(of);

    of = std::fopen(filename.c_str(), "a");
    pref = std::fopen(filename_pre.c_str(), "ab");
    postf = std::fopen(filename_post.c_str(), "ab");

    if (of == NULL || pref == NULL || postf == NULL) {
        debugger::error(Debugger::CPU, "cannot open capture files\n");
        debugger::halt("failed to open capture files");
        return;
    }

    if (!exists) {
        fmt::print(of, "start_address = \"0x{:016x}\"\n\n", captureStart);

        std::string asm_code;
        std::string bin_code;

        u64 address = captureStart;
        unsigned count = 0;
        for (Memory::BusLog entry: bus->log) {
            debugger::warn(Debugger::CPU,
                "  {}_{}(0x{:x}, 0x{:x})",
                entry.access == Memory::BusAccess::Load ? "load" : "store",
                entry.bytes * 8, entry.address, entry.value);

            if (entry.access == Memory::BusAccess::Load && entry.bytes == 4 &&
                (entry.address & 0xffffffflu) == (address & 0xffffffflu)) {
                if ((count % 4) == 0) bin_code += "\n   ";
                bin_code += fmt::format(" 0x{:08x},", entry.value);
                asm_code += "    " + Mips::CPU::disas(address, entry.value) + "\n";
                address += 4;
                count++;
            }
        }
        if (address == state.reg.pc) {
            // Missing instruction fetch for the suppressed delay instruction
            // of a branch likely.
            u32 instr;
            u64 phys_address;
            translateAddress(address, &phys_address, false);
            bus->load_u32(address, &instr);
            if ((count % 4) == 0) bin_code += "\n   ";
            bin_code += fmt::format(" 0x{:08x},", instr);
            asm_code += "    " + Mips::CPU::disas(address, instr) + "\n";
            address += 4;
            count++;
        }
        if (address != (state.reg.pc + 4)) {
            debugger::warn(Debugger::CPU,
                "incomplete memory trace: missing instruction fetches {}/{}/{}",
                count, bus->log.size(), state.reg.pc - captureStart + 4);
            debugger::halt(
                "incomplete memory trace: missing instruction fetches");
        }

        fmt::print(of, "asm_code = \"\"\"\n{}\"\"\"\n\n", asm_code);
        fmt::print(of, "bin_code = [{}\n]\n\n", bin_code);
    }

    fmt::print(of, "[[test]]\n");
    fmt::print(of, "end_address = \"0x{:016x}\"\n", finalAddress);
    fmt::print(of, "trace = [\n");
    u64 address = captureStart;
    for (Memory::BusLog entry: bus->log) {
        if (entry.access == Memory::BusAccess::Load && entry.bytes == 4 &&
            (entry.address & 0xffffffflu) == (address & 0xffffffflu)) {
            address += 4;
        } else {
            fmt::print(of,
                "    {{ type = \"{}_u{}\", address = \"0x{:08x}\", value = \"0x{:x}\" }},\n",
                entry.access == Memory::BusAccess::Load ? "load" : "store",
                entry.bytes * 8, entry.address, entry.value);
        }
    }
    fmt::print(of, "]\n\n");

    serializeCpuRegisters(pref, captureCpuPre);
    serializeCp0Registers(pref, captureCp0Pre);
    serializeCp1Registers(pref, captureCp1Pre);

    serializeCpuRegisters(postf, state.reg);
    serializeCp0Registers(postf, state.cp0reg);
    serializeCp1Registers(postf, state.cp1reg);

    std::fclose(of);
    std::fclose(pref);
    std::fclose(postf);
    bus->capture(false);
    bus->clear();
    captureRunning = false;
    captureCount++;
}

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
void step()
{
    if (state.cycles >= state.cpu.nextEvent) {
        state.handleEvent();
    }
    if (state.cpu.nextAction != State::Action::Jump &&
        (state.reg.pc + 4) >= captureEnd) {
        stopCapture(state.reg.pc + 4);
    }

    switch (state.cpu.nextAction) {
        case State::Action::Continue:
            state.reg.pc += 4;
            state.cpu.delaySlot = false;
            eval();
            break;

        case State::Action::Delay:
            state.reg.pc += 4;
            state.cpu.nextAction = State::Action::Jump;
            state.cpu.delaySlot = true;
            eval();
            break;

        case State::Action::Jump:
            stopCapture(state.cpu.nextPc);
            state.reg.pc = state.cpu.nextPc;
            state.cpu.nextAction = State::Action::Continue;
            state.cpu.delaySlot = false;
            startCapture();
            eval();
            break;
    }
}

}; /* namespace Eval */
}; /* namespace R4300 */
