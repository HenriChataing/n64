
#include <iomanip>
#include <iostream>

#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <debugger.h>

#include "eval.h"

namespace R4300 {
namespace Eval {

/**
 * Check whether a virtual memory address is correctly aligned for a memory
 * access.
 */
#define checkAddressAlignment(vAddr, bytes, delay, instr, load) \
    if (((vAddr) & ((bytes) - 1)) != 0) { \
        takeException(AddressError, (vAddr), (delay), (instr), (load)); \
        return true; \
    }

#define checkCop1Usable() \
    if (!CU1()) { \
        takeException(CoprocessorUnusable, 0, \
                      delaySlot, instr, false, 1u); \
        return true; \
    }

/**
 * @brief Preprocessor template for I-type instructions.
 *
 * The registers and immediate value are automatically extracted from the
 * instruction and added as rs, rt, imm in a new scope. The immediate value
 * is sign extended into a 64 bit unsigned integer.
 *
 * @param opcode            Instruction opcode
 * @param instr             Original instruction
 * @param extend            Extension method (sign or zero extend)
 * @param ...               Instruction implementation
 */
#define IType(opcode, instr, extend, ...) \
    case opcode: { \
        u32 rs = Mips::getRs(instr); \
        u32 rt = Mips::getRt(instr); \
        u64 imm = extend<u64, u16>(Mips::getImmediate(instr)); \
        (void)rs; (void)rt; (void)imm; \
        __VA_ARGS__; \
        break; \
    }

/**
 * @brief Preprocessor template for J-type instructions.
 *
 * The target is automatically extracted from the instruction and added as
 * tg in a new scope. The target is sign extended into a 64 bit unsigned
 * integer.
 *
 * @param opcode            Instruction opcode
 * @param instr             Original instruction
 * @param ...               Instruction implementation
 */
#define JType(opcode, instr, ...) \
    case opcode: { \
        u64 tg = Mips::getTarget(instr); \
        (void)tg; \
        __VA_ARGS__; \
        break; \
    }

/**
 * @brief Preprocessor template for R-type instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamnt in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param instr             Original instruction
 * @param ...               Instruction implementation
 */
#define RType(opcode, instr, ...) \
    case opcode: { \
        u32 rd = Mips::getRd(instr); \
        u32 rs = Mips::getRs(instr); \
        u32 rt = Mips::getRt(instr); \
        u32 shamnt = Mips::getShamnt(instr); \
        (void)rd; (void)rs; (void)rt; (void)shamnt; \
        __VA_ARGS__; \
        break; \
    }

/**
 * @brief Preprocessor template for branching instructions.
 *
 * The macro generates the code for the "normal" and "branch likely"
 * instructions.
 *
 * The registers and immediate value are automatically extracted from the
 * instruction and added as rs, rt, imm in a new scope. The immediate value
 * is sign extended into a 64 bit unsigned integer.
 *
 * @param opcode            Instruction opcode
 * @param instr             Original instruction
 * @param ...               Test condition
 */
#define BType(opcode, instr, ...) \
    IType(opcode, instr, sign_extend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4, true); \
            state.reg.pc += 4 + (i64)(imm << 2); \
            state.branch = true; \
        } \
    }) \
    IType(opcode##L, instr, sign_extend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4, true); \
            state.reg.pc += 4 + (i64)(imm << 2); \
            state.branch = true; \
        } else \
            state.reg.pc += 4; \
    })

/**
 * @brief Raise an exception and update the state of the processor.
 *
 * @param vAddr         Virtual address being accessed.
 * @param delaySlot     Whether the exception occured in a branch delay
 *                      instruction.
 * @param instr         Whether the exception was triggered by an instruction
 *                      fetch.
 * @param load          Whether the exception was triggered by a load or store
 *                      operation.
 */
void takeException(Exception exn, u64 vAddr,
                   bool delaySlot, bool instr, bool load, u32 ce)
{
    u32 exccode = 0;
    // Default vector valid for all general exceptions.
    u64 vector = 0x180llu;

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
            debugger.halt("AddressError");
            break;
        // TLB Refill occurs when there is no TLB entry that matches an
        // attempted reference to a mapped address space.
        case TLBRefill:
        case XTLBRefill:
            vector = exn == XTLBRefill ? 0x080llu : 0x000llu;
            /* fallthroug */
        // TLB Invalid occurs when a virtual address reference matches a
        // TLB entry that is marked invalid.
        case TLBInvalid:
            exccode = load ? 2 : 3; // TLBL : TLBS
            state.cp0reg.badvaddr = vAddr;
            debugger.halt("TLBInvalid / TLBRefill");
            // TODO : Context, XContext, EntryHi
            break;
        // TLB Modified occurs when a store operation virtual address
        // reference to memory matches a TLB entry which is marked
        // valid but is not dirty (the entry is not writable).
        case TLBModified:
            exccode = 1; // Mod
            state.cp0reg.badvaddr = vAddr;
            debugger.halt("TLBModified");
            // TODO : Context, XContext, EntryHi
            break;
        // The Cache Error exception occurs when either a secondary cache ECC
        // error, primary cache parity error, or SysAD bus parity/ECC error
        // condition occurs and error detection is enabled.
        case CacheError:
            // vector = 0x100llu;
            throw "UnsupportedException";
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
            debugger.halt("VirtualCoherency");
            break;
        // A Bus Error exception is raised by board-level circuitry for events
        // such as bus time-out, backplane bus parity errors, and invalid
        // physical memory addresses or access types.
        case BusError:
            exccode = instr ? 6 : 7; // IBE : DBE
            debugger.halt("BusError");
            break;
        // An Integer Overflow exception occurs when an ADD, ADDI, SUB, DADD,
        // DADDI or DSUB instruction results in a 2’s complement overflow
        case IntegerOverflow:
            exccode = 12; // Ov
            debugger.halt("IntegerOverflow");
            break;
        // The Trap exception occurs when a TGE, TGEU, TLT, TLTU, TEQ, TNE,
        // TGEI, TGEUI, TLTI, TLTUI, TEQI, or TNEI instruction results in a TRUE
        // condition.
        case Trap:
            exccode = 13; // Tr
            debugger.halt("Trap");
            break;
        // A System Call exception occurs during an attempt to execute the
        // SYSCALL instruction.
        case SystemCall:
            exccode = 8; // Sys
            debugger.halt("SystemCall");
            break;
        // A Breakpoint exception occurs when an attempt is made to execute the
        // BREAK instruction.
        case Breakpoint:
            exccode = 9; // Bp
            debugger.halt("Breakpoint");
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
            debugger.halt("ReservedInstruction");
            break;
        // The Coprocessor Unusable exception occurs when an attempt is made to
        // execute a coprocessor instruction for either:
        //      - a corresponding coprocessor unit that has not been marked
        //        usable, or
        //      - CP0 instructions, when the unit has not been marked usable
        //        and the process executes in either User or Supervisor mode.
        case CoprocessorUnusable:
            exccode = 11; // CpU
            break;
        // The Floating-Point exception is used by the floating-point
        // coprocessor.
        case FloatingPoint:
            exccode = 15; // FPEEXL
            debugger.halt("FloatingPoint");
            // TODO: Set FP Control Status Register
            break;
        // A Watch exception occurs when a load or store instruction references
        // the  physical address specified in the WatchLo/WatchHi System Control
        // Coprocessor (CP0) registers. The WatchLo register specifies whether a
        // load or store initiated this exception.
        case Watch:
            exccode = 23; // WATCH
            debugger.halt("Watch");
            // TODO: Set Watch register
            break;
        // The Interrupt exception occurs when one of the eight interrupt conditions
        // is asserted.
        case Interrupt:
            exccode = 0;
            break;
        default:
            throw "UnsupportedException";
    }

    // Set Cause Register : EXCCode, CE
    state.cp0reg.cause &= ~(CAUSE_EXCCODE_MASK | CAUSE_CE_MASK);
    state.cp0reg.cause |= CAUSE_EXCCODE(exccode) | CAUSE_CE(ce);
    // Check if exception within exception.
    if (!EXL()) {
        // Check if the exception was caused by a delay slot instruction.
        // Set EPC and Cause:BD accordingly.
        if (delaySlot) {
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
    if (BEV()) {
        state.reg.pc = 0xffffffffbfc00200llu + vector;
    } else {
        state.reg.pc = 0xffffffff80000000llu + vector;
    }
}

#define returnException(...) \
    { \
        takeException(__VA_ARGS__); \
        return true; \
    }

/**
 * @brief Check whether an interrupt exception is raised from the current state.
 *  Take the interrupt exception if this is the case.
 *
 * @param vAddr         Current value of the program counter
 * @param delaySlot     True iff \p vAddr points to a delay slot instruction
 * @return              True iff an Interrupt Exception is taken
 */
bool checkInterrupt(u64 vAddr, bool delaySlot)
{
    // For the interrupt to be taken, the interrupts must globally enabled
    // (IE = 1) and the particular interrupt must be unmasked (IM[irq] = 1).
    // Interrupt exceptions are also disabled during exception
    // handling (EXL = 1).
    if (EXL() || !IE() || (IM() & IP()) == 0) {
        return false;
    } else {
        takeException(Interrupt, vAddr, delaySlot, false, false);
        return true;
    }
}

/**
 * @brief Set the selected interrupt pending bit in the Cause register.
 *  The Interrupt exception will be taken just before executing the next
 *  instruction if the conditions are met (see \ref checkInterrupt).
 *
 * @param irq           Interrupt number.
 */
void setInterruptPending(uint irq)
{
    // Update the pending bits in the Cause register.
    state.cp0reg.cause |= CAUSE_IP(1lu << irq);
}

void clearInterruptPending(uint irq)
{
    // Update the pending bits in the Cause register.
    state.cp0reg.cause &= ~CAUSE_IP(1lu << irq);
}

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
bool step()
{
    u64 pc = R4300::state.reg.pc;
    R4300::state.branch = false;
    bool exn = eval(pc, false);

    if (!R4300::state.branch && !exn)
        R4300::state.reg.pc += 4;
    return exn;
}

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 *
 * @param vAddr         Virtual address of the instruction to execute.
 * @param delaySlot     Whether the instruction executed is in a
 *                      branch delay slot.
 * @return true if the instruction caused an exception
 */
bool eval(u64 vAddr, bool delaySlot)
{
    u64 pAddr;
    u64 instr;
    u32 opcode;
    R4300::Exception exn;

    state.cp0reg.incrCount();

    // CPU freq is 93.75 MHz, refresh frequency is assumed 60Hz.
    if (state.cycles++ == state.hwreg.vi_NextIntr) {
        state.hwreg.vi_NextIntr += state.hwreg.vi_IntrInterval;
        set_MI_INTR_REG(MI_INTR_VI);
    }
    if (checkInterrupt(vAddr, delaySlot)) {
        return true;
    }

    exn = translateAddress(vAddr, &pAddr, false);

    if (exn != R4300::None)
        returnException(exn, vAddr, delaySlot, true, true);
    if (!state.physmem.load(4, pAddr, &instr))
        returnException(BusError, vAddr, delaySlot, true, true);

    debugger.cpuTrace.put(TraceEntry(vAddr, instr));

    using namespace Mips::Opcode;
    using namespace Mips::Special;
    using namespace Mips::Regimm;
    using namespace Mips::Cop0;
    using namespace Mips::Copz;
    using namespace R4300;

    switch (opcode = Mips::getOpcode(instr)) {
        case SPECIAL:
            switch (Mips::getFunct(instr)) {
                RType(ADD, instr, {
                    int32_t res;
                    int32_t a = (int32_t)(uint32_t)state.reg.gpr[rs];
                    int32_t b = (int32_t)(uint32_t)state.reg.gpr[rt];
                    if (__builtin_add_overflow(a, b, &res)) {
                        debugger.halt("ADD IntegerOverflow");
                    }
                    state.reg.gpr[rd] = sign_extend<u64, u32>((uint32_t)res);
                })
                RType(ADDU, instr, {
                    u64 res = state.reg.gpr[rs] + state.reg.gpr[rt];
                    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
                })
                RType(AND, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] & state.reg.gpr[rt];
                })
                case BREAK:
                    throw "BREAK trap";
                RType(DADD, instr, { throw "Unsupported"; })
                RType(DADDU, instr, { throw "Unsupported"; })
                RType(DDIV, instr, { throw "Unsupported"; })
                RType(DDIVU, instr, {
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                })
                RType(DIV, instr, {
                    // @todo both operands must be valid 32 bit signed values
                    state.reg.multLo = sign_extend<u64, u32>(state.reg.gpr[rs] / state.reg.gpr[rt]);
                    state.reg.multHi = sign_extend<u64, u32>(state.reg.gpr[rs] % state.reg.gpr[rt]);
                })
                RType(DIVU, instr, {
                    // @todo both operands must be valid 32 bit signed values
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                })
                RType(DMULT, instr, { throw "Unsupported"; })
                RType(DMULTU, instr, {
                    u64 x = state.reg.gpr[rs];
                    u64 y = state.reg.gpr[rt];
                    u64 a = x >> 32, b = x & 0xffffffffllu;
                    u64 c = y >> 32, d = y & 0xffffffffllu;

                    u64 ac = a * c;
                    u64 bc = b * c;
                    u64 ad = a * d;
                    u64 bd = b * d;

                    u64 mid34 = (bd >> 32) + (bc & 0xffffffffllu) + (ad & 0xffffffffllu);

                    state.reg.multHi = ac + (bc >> 32) + (ad >> 32) + (mid34 >> 32);
                    state.reg.multLo = (mid34 << 32) | (bd & 0xffffffffllu);
                })
                RType(DSLL, instr, { throw "Unsupported"; })
                RType(DSLL32, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rt] << (shamnt + 32);
                })
                RType(DSLLV, instr, { throw "Unsupported"; })
                RType(DSRA, instr, { throw "Unsupported"; })
                RType(DSRA32, instr, {
                    bool sign = (state.reg.gpr[rt] & (1lu << 63)) != 0;
                    unsigned int sh = shamnt + 32;
                    // Right shift is logical for unsigned c types,
                    // we need to add the type manually.
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> sh;
                    if (sign) {
                        u64 mask = (1ul << sh) - 1u;
                        state.reg.gpr[rd] |= mask << (64 - sh);
                    }
                })
                RType(DSRAV, instr, { throw "Unsupported"; })
                RType(DSRL, instr, { throw "Unsupported"; })
                RType(DSRL32, instr, { throw "Unsupported"; })
                RType(DSRLV, instr, { throw "Unsupported"; })
                RType(DSUB, instr, { throw "Unsupported"; })
                RType(DSUBU, instr, { throw "Unsupported"; })
                RType(JALR, instr, {
                    u64 tg = state.reg.gpr[rs];
                    state.reg.gpr[rd] = state.reg.pc + 8;
                    eval(state.reg.pc + 4, true);
                    debugger.newStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
                    state.reg.pc = tg;
                    state.branch = true;
                })
                RType(JR, instr, {
                    u64 tg = state.reg.gpr[rs];
                    eval(state.reg.pc + 4, true);
                    debugger.deleteStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
                    state.reg.pc = tg;
                    state.branch = true;
                })
                RType(MFHI, instr, {
                    // undefined if an instruction that follows modify
                    // the special registers LO / HI
                    state.reg.gpr[rd] = state.reg.multHi;
                })
                RType(MFLO, instr, {
                    // undefined if an instruction that follows modify
                    // the special registers LO / HI
                    state.reg.gpr[rd] = state.reg.multLo;
                })
                RType(MOVN, instr, { throw "Unsupported"; })
                RType(MOVZ, instr, { throw "Unsupported"; })
                RType(MTHI, instr, {
                    state.reg.multHi = state.reg.gpr[rs];
                })
                RType(MTLO, instr, {
                    state.reg.multLo = state.reg.gpr[rs];
                })
                RType(MULT, instr, { throw "Unsupported"; })
                RType(MULTU, instr, {
                    // @todo instruction takes 3 cycles
                    u64 m = (uint32_t)state.reg.gpr[rs] * (uint32_t)state.reg.gpr[rt];
                    state.reg.multLo = sign_extend<u64, u32>(m);
                    state.reg.multHi = sign_extend<u64, u32>(m >> 32);
                })
                RType(NOR, instr, {
                    state.reg.gpr[rd] = ~(state.reg.gpr[rs] | state.reg.gpr[rt]);
                })
                RType(OR, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] | state.reg.gpr[rt];
                })
                RType(SLL, instr, {
                    state.reg.gpr[rd] = sign_extend<u64, u32>(state.reg.gpr[rt] << shamnt);
                })
                RType(SLLV, instr, {
                    unsigned int shamnt = state.reg.gpr[rs] & 0x1flu;
                    state.reg.gpr[rd] = sign_extend<u64, u32>(state.reg.gpr[rt] << shamnt);
                })
                RType(SLT, instr, {
                    state.reg.gpr[rd] = (i64)state.reg.gpr[rs] < (i64)state.reg.gpr[rt];
                })
                RType(SLTU, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] < state.reg.gpr[rt];
                })
                RType(SRA, instr, {
                    bool sign = (state.reg.gpr[rt] & (1lu << 31)) != 0;
                    // Right shift is logical for unsigned c types,
                    // we need to add the type manually.
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
                    if (sign) {
                        u64 mask = (1ul << (shamnt + 32)) - 1u;
                        state.reg.gpr[rd] |= mask << (32 - shamnt);
                    }
                })
                RType(SRAV, instr, {
                    bool sign = (state.reg.gpr[rt] & (1lu << 31)) != 0;
                    unsigned int shamnt = state.reg.gpr[rs] & 0x1flu;
                    // Right shift is logical for unsigned c types,
                    // we need to add the type manually.
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
                    if (sign) {
                        u64 mask = (1ul << (shamnt + 32)) - 1u;
                        state.reg.gpr[rd] |= mask << (32 - shamnt);
                    }
                    debugger.halt("SRAV instruction");
                })
                RType(SRL, instr, {
                    u64 res = (state.reg.gpr[rt] & 0xffffffffllu) >> shamnt;
                    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
                })
                RType(SRLV, instr, {
                    unsigned int shamnt = state.reg.gpr[rs] & 0x1flu;
                    u64 res = (state.reg.gpr[rt] & 0xfffffffflu) >> shamnt;
                    state.reg.gpr[rd] = sign_extend<u64, u32>(res);
                })
                RType(SUB, instr, {
                    int32_t res;
                    int32_t a = (int32_t)(uint32_t)state.reg.gpr[rs];
                    int32_t b = (int32_t)(uint32_t)state.reg.gpr[rt];
                    if (__builtin_sub_overflow(a, b, &res)) {
                        debugger.halt("SUB IntegerOverflow");
                    }
                    state.reg.gpr[rd] = sign_extend<u64, u32>((uint32_t)res);
                })
                RType(SUBU, instr, {
                    state.reg.gpr[rd] = sign_extend<u64, u32>(
                        state.reg.gpr[rs] - state.reg.gpr[rt]);
                })
                RType(SYNC, instr, { throw "Unsupported"; })
                RType(SYSCALL, instr, { throw "Unsupported"; })
                RType(TEQ, instr, { throw "Unsupported"; })
                RType(TGE, instr, { throw "Unsupported"; })
                RType(TGEU, instr, { throw "Unsupported"; })
                RType(TLT, instr, { throw "Unsupported"; })
                RType(TLTU, instr, { throw "Unsupported"; })
                RType(TNE, instr, { throw "Unsupported"; })
                RType(XOR, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] ^ state.reg.gpr[rt];
                })
                default:
                    throw "Unsupported Special";
            }
            break;

        case REGIMM:
            switch (Mips::getRt(instr)) {
                BType(BGEZ, instr, (i64)state.reg.gpr[rs] >= 0)
                BType(BLTZ, instr, (i64)state.reg.gpr[rs] < 0)
                IType(BGEZAL, instr, sign_extend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                IType(BGEZALL, instr, sign_extend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    } else
                        state.reg.pc += 4;
                })
                IType(BLTZAL, instr, sign_extend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                IType(BLTZALL, instr, sign_extend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    } else
                        state.reg.pc += 4;
                })
                IType(TEQI, instr, sign_extend, {})
                IType(TGEI, instr, sign_extend, {})
                IType(TGEIU, instr, sign_extend, {})
                IType(TLTI, instr, sign_extend, {})
                IType(TLTIU, instr, sign_extend, {})
                IType(TNEI, instr, sign_extend, {})
                default:
                    throw "Unupported Regimm";
                    break;
            }
            break;

        IType(ADDI, instr, sign_extend, {
            int32_t res;
            int32_t a = (int32_t)(uint32_t)state.reg.gpr[rs];
            int32_t b = (int32_t)(uint32_t)imm;
            if (__builtin_add_overflow(a, b, &res)) {
                debugger.halt("ADDI IntegerOverflow");
            }
            state.reg.gpr[rt] = sign_extend<u64, u32>((uint32_t)res);
        })
        IType(ADDIU, instr, sign_extend, {
            state.reg.gpr[rt] = sign_extend<u64, u32>(state.reg.gpr[rs] + imm);
        })
        IType(ANDI, instr, zero_extend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] & imm;
        })
        BType(BEQ, instr, state.reg.gpr[rt] == state.reg.gpr[rs])
        BType(BGTZ, instr, (i64)state.reg.gpr[rs] > 0)
        BType(BLEZ, instr, (i64)state.reg.gpr[rs] <= 0)
        BType(BNE, instr, state.reg.gpr[rt] != state.reg.gpr[rs])
        IType(CACHE, instr, sign_extend, {
            // @todo
        })
        case COP1:
            if (Cop1::eval(instr, delaySlot)) {
                return true;
            }
            break;
        case COP0:
        case COP2:
        case COP3:
            if (instr & Mips::COFUN) {
                cop[opcode & 0x3]->cofun(instr);
                break;
            }
            switch (Mips::getRs(instr)) {
                RType(MF, instr, {
                    state.reg.gpr[rt] = cop[opcode & 0x3]->read(4, rd, false);
                })
                RType(DMF, instr, { throw "Unsupported"; })
                RType(CF, instr, {
                    state.reg.gpr[rt] = cop[opcode & 0x3]->read(4, rd, true);
                })
                RType(MT, instr, {
                    cop[opcode & 0x3]->write(4, rd, state.reg.gpr[rt], false);
                })
                RType(DMT, instr, { throw "Unsupported"; })
                RType(CT, instr, {
                    cop[opcode & 0x3]->write(4, rd, state.reg.gpr[rt], true);
                })
                case BC:
                    switch (Mips::getRt(instr)) {
                        IType(BCF, instr, sign_extend, { throw "Unsupported"; })
                        IType(BCT, instr, sign_extend, { throw "Unsupported"; })
                        IType(BCFL, instr, sign_extend, { throw "Unsupported"; })
                        IType(BCTL, instr, sign_extend, { throw "Unsupported"; })
                        default:
                            throw "ReservedInstruction";
                    }
                    break;
                default:
                    throw "ReservedInstruction";
            }
            break;

        IType(DADDI, instr, sign_extend, { throw "Unsupported"; })
        IType(DADDIU, instr, sign_extend, { throw "Unsupported"; })
        JType(J, instr, {
            tg = (state.reg.pc & 0xfffffffff0000000llu) | (tg << 2);
            eval(state.reg.pc + 4, true);
            debugger.editStackFrame(tg, state.reg.gpr[29]);
            state.reg.pc = tg;
            state.branch = true;
        })
        JType(JAL, instr, {
            tg = (state.reg.pc & 0xfffffffff0000000llu) | (tg << 2);
            state.reg.gpr[31] = state.reg.pc + 8;
            eval(state.reg.pc + 4, true);
            debugger.newStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
            state.reg.pc = tg;
            state.branch = true;
        })
        IType(LB, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(1, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = sign_extend<u64, u8>(val);
        })
        IType(LBU, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(1, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = zero_extend<u64, u8>(val);
        })
        IType(LD, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 8, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(8, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = val;
        })
        IType(LDC1, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkCop1Usable();
            checkAddressAlignment(vAddr, 8, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(8, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.cp1reg.fpr_d[rt]->l = val;
        })
        IType(LDC2, instr, sign_extend, { throw "Unsupported"; })
        IType(LDL, instr, sign_extend, { throw "Unsupported"; })
        IType(LDR, instr, sign_extend, { throw "Unsupported"; })
        IType(LH, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 2, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(2, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = sign_extend<u64, u16>(val);
        })
        IType(LHU, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 2, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(2, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = zero_extend<u64, u16>(val);
        })
        IType(LL, instr, sign_extend, { throw "Unsupported"; })
        IType(LLD, instr, sign_extend, { throw "Unsupported"; })
        IType(LUI, instr, sign_extend, {
            state.reg.gpr[rt] = imm << 16;
        })
        IType(LW, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = sign_extend<u64, u32>(val);
        })
        IType(LWC1, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkCop1Usable();
            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.cp1reg.fpr_s[rt]->w = val;
        })
        IType(LWC2, instr, sign_extend, { throw "Unsupported"; })
        IType(LWC3, instr, sign_extend, { throw "Unsupported"; })
        IType(LWL, instr, sign_extend, {
            // debugger.halt("LWL instruction");
            // @todo only BigEndianMem & !ReverseEndian for now
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            // Not calling checkAddressAlignment:
            // this instruction specifically ignores the alignment
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);

            size_t count = 4 - (pAddr % 4);
            unsigned int shift = 28;
            u64 mask = (1llu << (32 - 8 * count)) - 1u;
            u64 val = 0;

            for (size_t nr = 0; nr < count; nr++, shift -= 8) {
                u64 byte = 0;
                if (!state.physmem.load(1, pAddr + nr, &byte)) {
                    returnException(BusError, vAddr, delaySlot, false, false);
                }
                val |= (byte << shift);
            }

            val = val | (state.reg.gpr[rt] & mask);
            state.reg.gpr[rt] = sign_extend<u64, u32>(val);
        })
        IType(LWR, instr, sign_extend, {
            // debugger.halt("LWR instruction");
            // @todo only BigEndianMem & !ReverseEndian for now
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            // Not calling checkAddressAlignment:
            // this instruction specifically ignores the alignment
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);

            size_t count = 1 + (pAddr % 4);
            unsigned int shift = 0;
            u64 mask = (1llu << (32 - 8 * count)) - 1u;
            mask <<= 8 * count;
            u64 val = 0;

            for (size_t nr = 0; nr < count; nr++, shift += 8) {
                u64 byte = 0;
                if (!state.physmem.load(1, pAddr - nr, &byte)) {
                    returnException(BusError, vAddr, delaySlot, false, false);
                }
                val |= (byte << shift);
            }

            val = val | (state.reg.gpr[rt] & mask);
            state.reg.gpr[rt] = sign_extend<u64, u32>(val);
        })
        IType(LWU, instr, sign_extend, { throw "Unsupported"; })
        IType(ORI, instr, zero_extend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] | imm;
        })
        IType(SB, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(1, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SC, instr, sign_extend, { throw "Unsupported"; })
        IType(SCD, instr, sign_extend, { throw "Unsupported"; })
        IType(SD, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 8, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(8, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SDC1, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkCop1Usable();
            checkAddressAlignment(vAddr, 8, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(8, pAddr, state.cp1reg.fpr_d[rt]->l))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SDC2, instr, sign_extend, { throw "Unsupported"; })
        IType(SDL, instr, sign_extend, { throw "Unsupported"; })
        IType(SDR, instr, sign_extend, { throw "Unsupported"; })
        IType(SH, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 2, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(2, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SLTI, instr, sign_extend, {
            state.reg.gpr[rt] = (i64)state.reg.gpr[rs] < (i64)imm;
        })
        IType(SLTIU, instr, sign_extend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] < imm;
        })
        IType(SW, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC1, instr, sign_extend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkCop1Usable();
            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, state.cp1reg.fpr_s[rt]->w))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC2, instr, sign_extend, { throw "Unsupported"; })
        IType(SWC3, instr, sign_extend, { throw "Unsupported"; })
        IType(SWL, instr, sign_extend, {
            // debugger.halt("SWL instruction");
            // @todo only BigEndianMem & !ReverseEndian for now
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            // Not calling checkAddressAlignment:
            // this instruction specifically ignores the alignment
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);

            size_t count = 4 - (pAddr % 4);
            u32 val = state.reg.gpr[rt];
            unsigned int shift = 24;
            for (size_t nr = 0; nr < count; nr++, shift -= 8) {
                u64 byte = (val >> shift) & 0xfflu;
                if (!state.physmem.store(1, pAddr + nr, byte)) {
                    returnException(BusError, vAddr, delaySlot, false, false);
                }
            }
        })
        IType(SWR, instr, sign_extend, {
            // debugger.halt("SWR instruction");
            // @todo only BigEndianMem & !ReverseEndian for now
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            // Not calling checkAddressAlignment:
            // this instruction specifically ignores the alignment
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);

            size_t count = 1 + (pAddr % 4);
            u32 val = state.reg.gpr[rt];
            unsigned int shift = 0;
            for (size_t nr = 0; nr < count; nr++, shift += 8) {
                u64 byte = (val >> shift) & 0xfflu;
                if (!state.physmem.store(1, pAddr - nr, byte)) {
                    returnException(BusError, vAddr, delaySlot, false, false);
                }
            }
        })
        IType(XORI, instr, zero_extend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] ^ imm;
        })

        default:
            throw "Unsupported Opcode";
            break;
    }
    return false;
}

void run()
{
    // while (1)
    for (int i = 0; i < 1000; i++)
        step();
}

}; /* namespace Eval */
}; /* namespace R4300 */
