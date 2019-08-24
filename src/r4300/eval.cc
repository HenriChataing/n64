
#include <iomanip>
#include <iostream>
#include <vector>

#include <circular_buffer.h>
#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <debugger.h>

#include "eval.h"

namespace R4300 {
namespace Eval {

#define SignExtend(imm, size) ({ \
    u##size valu##size = (imm); \
    i##size vali##size = valu##size; \
    i64 vali64 = vali##size; \
    (u64)vali64; \
})

#define ZeroExtend(imm, size) ({ \
    (u64)(imm); \
})

/**
 * Check whether a virtual memory address is correctly aligned for a memory
 * access.
 */
#define checkAddressAlignment(vAddr, bytes, delay, instr, load) \
    if (((vAddr) & ((bytes) - 1)) != 0) { \
        takeException(AddressError, (vAddr), (delay), (instr), (load)); \
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
        u64 imm = extend(Mips::getImmediate(instr), 16); \
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
    IType(opcode, instr, SignExtend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4, true); \
            state.reg.pc += 4 + (i64)(imm << 2); \
            state.branch = true; \
        } \
    }) \
    IType(opcode##L, instr, SignExtend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4, true); \
            state.reg.pc += 4 + (i64)(imm << 2); \
            state.branch = true; \
        } else \
            state.reg.pc += 4; \
    })

/**
 * @brief Type of an entry in the interpreter log.
 */
typedef std::pair<u64, u32> LogEntry;

/**
 * @brief Circular buffer for storing the last instructions executed.
 */
circular_buffer<LogEntry> _log(64);

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
                   bool delaySlot, bool instr, bool load)
{
    std::cerr << "Taking exception " << std::dec << exn << std::endl;
    std::cerr << "delay:" << delaySlot << " instr:" << instr;
    std::cerr << " load:" << load << std::endl;
    u32 exccode = 0;
    // Default vector valid for all general exceptions.
    u64 vector = 0x180llu;
    // TODO
    u32 ce = 0;

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
            state.cp0reg.badVAddr = vAddr;
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
            state.cp0reg.badVAddr = vAddr;
            // TODO : Context, XContext, EntryHi
            break;
        // TLB Modified occurs when a store operation virtual address
        // reference to memory matches a TLB entry which is marked
        // valid but is not dirty (the entry is not writable).
        case TLBModified:
            exccode = 1; // Mod
            state.cp0reg.badVAddr = vAddr;
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
            state.cp0reg.badVAddr = vAddr;
            break;
        // A Bus Error exception is raised by board-level circuitry for events
        // such as bus time-out, backplane bus parity errors, and invalid
        // physical memory addresses or access types.
        case BusError:
            exccode = instr ? 6 : 7; // IBE : DBE
            break;
        // An Integer Overflow exception occurs when an ADD, ADDI, SUB, DADD,
        // DADDI or DSUB instruction results in a 2’s complement overflow
        case IntegerOverflow:
            exccode = 12; // Ov
            break;
        // The Trap exception occurs when a TGE, TGEU, TLT, TLTU, TEQ, TNE,
        // TGEI, TGEUI, TLTI, TLTUI, TEQI, or TNEI instruction results in a TRUE
        // condition.
        case Trap:
            exccode = 13; // Tr
            break;
        // A System Call exception occurs during an attempt to execute the
        // SYSCALL instruction.
        case SystemCall:
            exccode = 8; // Sys
            break;
        // A Breakpoint exception occurs when an attempt is made to execute the
        // BREAK instruction.
        case Breakpoint:
            exccode = 9; // Bp
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
            // TODO: Set FP Control Status Register
            break;
        // A Watch exception occurs when a load or store instruction references
        // the  physical address specified in the WatchLo/WatchHi System Control
        // Coprocessor (CP0) registers. The WatchLo register specifies whether a
        // load or store initiated this exception.
        case Watch:
            exccode = 23; // WATCH
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
 * @brief Raise an exception and update the state of the processor.
 *
 * @param irq           Interrupt number.
 *                      INT0 = CAUSE_SW1
 *                      INT1 = CAUSE_SW2
 *                      INT2 = CAUSE_IP3 = RCP
 *                      INT3 = CAUSE_IP4 = cartridge = peripherals
 *                      INT4 = CAUSE_IP5 = pre-nmi = reset button
 *                      INT5 = CAUSE_IP6 = RDB read
 *                      INT6 = CAUSE_IP7 = RDB write
 *                      INT7 = CAUSE_IP8 = counter
 * @param vAddr         Virtual address being accessed.
 * @param delaySlot     Whether the exception occured in a branch delay
 *                      instruction.
 * @return              True iff the interrupt is taken (i.e. not masked and
 *                      interrupts are enabled)
 */
bool takeInterrupt(uint irq, u64 vAddr, bool delaySlot)
{
    std::cerr << "Taking interrupt " << std::dec << irq << std::endl;
    std::cerr << "delay:" << delaySlot << std::endl;

    u32 ip = 1lu << irq;
    state.cp0reg.cause = (state.cp0reg.cause & ~0xff00lu) | (ip << 8);

    /*
     * For the interrupt to be taken, the interrupts must globally enabled
     * (IE = 1) and the particular interrupt must be unmasked (IM[irq] = 1).
     */
    if (!IE() || !(ip & IM()))
        return false;

    takeException(Interrupt, vAddr, delaySlot, false, false);
    return true;
}

/**
 * @brief Contains the interrupt status bits if an interrupt is to be taken.
 */
static int _irq = -1;

/**
 * @brief Called to indicate an interrupt should be taken at the next
 * instruction.
 */
void scheduleInterrupt(uint irq)
{
    _irq = irq;
}

/**
 * @brief Check if an interrupt is pending and trigger the interrupt exception
 * if it is.
 */
bool takeScheduledInterrupt(u64 vAddr, bool delaySlot)
{
    if (_irq < 0)
        return false;

    uint irq = _irq;
    _irq = -1;
    return takeInterrupt(irq, vAddr, delaySlot);
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

    if (takeScheduledInterrupt(vAddr, delaySlot)) {
        return true;
    }

    exn = translateAddress(vAddr, &pAddr, false);

    if (exn != R4300::None)
        returnException(exn, vAddr, delaySlot, true, true);
    if (!state.physmem.load(4, pAddr, &instr))
        returnException(BusError, vAddr, delaySlot, true, true);

    _log.put(LogEntry(vAddr, instr));

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
                    u64 r0 =
                        (state.reg.gpr[rs] & 0xffffffff) +
                        (state.reg.gpr[rt] & 0xffffffff);
                    u64 r1 =
                        (state.reg.gpr[rs] & 0x7fffffff) +
                        (state.reg.gpr[rt] & 0x7fffffff);
                    if (((r0 >> 1) ^ r1) & 0x80000000)
                        throw "IntegerOverflow";
                    state.reg.gpr[rd] = SignExtend(r0, 32);
                })
                RType(ADDU, instr, {
                    u64 r = state.reg.gpr[rs] + state.reg.gpr[rt];
                    state.reg.gpr[rd] = SignExtend(r, 32);
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
                    state.reg.multLo = SignExtend(state.reg.gpr[rs] / state.reg.gpr[rt], 32);
                    state.reg.multHi = SignExtend(state.reg.gpr[rs] % state.reg.gpr[rt], 32);
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
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> (shamnt + 32);
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
                    // @todo undefined if an instruction that follows modify
                    // the special registers LO / HI
                    state.reg.gpr[rd] = state.reg.multHi;
                })
                RType(MFLO, instr, {
                    // @todo undefined if an instruction that follows modify
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
                    u64 m = state.reg.gpr[rs] * state.reg.gpr[rt];
                    state.reg.multLo = SignExtend(m & 0xffffffffllu, 32);
                    state.reg.multHi = SignExtend((m >> 32) & 0xffffffffllu, 32);
                })
                RType(NOR, instr, { throw "Unsupported"; })
                RType(OR, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] | state.reg.gpr[rt];
                })
                RType(SLL, instr, {
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] << shamnt, 32);
                })
                RType(SLLV, instr, {
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] << state.reg.gpr[rs], 32);
                })
                RType(SLT, instr, {
                    state.reg.gpr[rd] = (i64)state.reg.gpr[rs] < (i64)state.reg.gpr[rt];
                })
                RType(SLTU, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] < state.reg.gpr[rt];
                })
                RType(SRA, instr, {
                    // @todo undefined if rt is not a valid sign extended value
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] >> shamnt, 32);
                })
                RType(SRAV, instr, {
                    // @todo undefined if rt is not a valid sign extended value
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] >> state.reg.gpr[rs], 32);
                })
                RType(SRL, instr, {
                    u64 r = (state.reg.gpr[rt] & 0xffffffffllu) >> shamnt;
                    state.reg.gpr[rd] = SignExtend(r, 32);
                })
                RType(SRLV, instr, {
                    u64 r = (state.reg.gpr[rt] & 0xffffffffllu) >> state.reg.gpr[rs];
                    state.reg.gpr[rd] = SignExtend(r, 32);
                })
                RType(SUB, instr, { throw "Unsupported"; })
                RType(SUBU, instr, {
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rs] - state.reg.gpr[rt], 32);
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
                IType(BGEZAL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                IType(BGEZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    } else
                        state.reg.pc += 4;
                })
                IType(BLTZAL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                IType(BLTZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    } else
                        state.reg.pc += 4;
                })
                IType(TEQI, instr, SignExtend, {})
                IType(TGEI, instr, SignExtend, {})
                IType(TGEIU, instr, SignExtend, {})
                IType(TLTI, instr, SignExtend, {})
                IType(TLTIU, instr, SignExtend, {})
                IType(TNEI, instr, SignExtend, {})
                default:
                    throw "Unupported Regimm";
                    break;
            }
            break;

        IType(ADDI, instr, SignExtend, {
            u64 r0 = (state.reg.gpr[rs] & 0xffffffffllu) + (imm & 0xffffffffllu);
            u64 r1 = (state.reg.gpr[rs] & 0x7fffffffllu) + (imm & 0x7fffffffllu);
            if (((r0 >> 1) ^ r1) & 0x80000000llu)
                throw "IntegerOverflow";
            state.reg.gpr[rt] = SignExtend(r0, 32);
        })
        IType(ADDIU, instr, SignExtend, {
            state.reg.gpr[rt] = SignExtend(state.reg.gpr[rs] + imm, 32);
        })
        IType(ANDI, instr, ZeroExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] & imm;
        })
        BType(BEQ, instr, state.reg.gpr[rt] == state.reg.gpr[rs])
        BType(BGTZ, instr, (i64)state.reg.gpr[rs] > 0)
        BType(BLEZ, instr, (i64)state.reg.gpr[rs] <= 0)
        BType(BNE, instr, state.reg.gpr[rt] != state.reg.gpr[rs])
        IType(CACHE, instr, SignExtend, {
            // @todo
        })
        case COP0:
        case COP1:
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
                        IType(BCF, instr, SignExtend, { throw "Unsupported"; })
                        IType(BCT, instr, SignExtend, { throw "Unsupported"; })
                        IType(BCFL, instr, SignExtend, { throw "Unsupported"; })
                        IType(BCTL, instr, SignExtend, { throw "Unsupported"; })
                        default:
                            throw "ReservedInstruction";
                    }
                    break;
                default:
                    throw "ReservedInstruction";
            }
            break;

        IType(DADDI, instr, SignExtend, { throw "Unsupported"; })
        IType(DADDIU, instr, SignExtend, { throw "Unsupported"; })
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
        IType(LB, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(1, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = SignExtend(val, 8);
        })
        IType(LBU, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(1, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = ZeroExtend(val, 8);
        })
        IType(LD, instr, SignExtend, {
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
        IType(LDC1, instr, SignExtend, { throw "Unsupported"; })
        IType(LDC2, instr, SignExtend, { throw "Unsupported"; })
        IType(LDL, instr, SignExtend, { throw "Unsupported"; })
        IType(LDR, instr, SignExtend, { throw "Unsupported"; })
        IType(LH, instr, SignExtend, { throw "Unsupported"; })
        IType(LHU, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 2, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(2, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = ZeroExtend(val, 16);
        })
        IType(LL, instr, SignExtend, { throw "Unsupported"; })
        IType(LLD, instr, SignExtend, { throw "Unsupported"; })
        IType(LUI, instr, SignExtend, {
            state.reg.gpr[rt] = imm << 16;
        })
        IType(LW, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = SignExtend(val, 32);
        })
        IType(LWC1, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            cop[1]->write(4, rt, SignExtend(val, 32), false);
        })
        IType(LWC2, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            cop[2]->write(4, rt, SignExtend(val, 32), false);
        })
        IType(LWC3, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            checkAddressAlignment(vAddr, 4, delaySlot, false, true);
            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!state.physmem.load(4, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            cop[3]->write(4, rt, SignExtend(val, 32), false);
        })
        IType(LWL, instr, SignExtend, { throw "Unsupported"; })
        IType(LWR, instr, SignExtend, { throw "Unsupported"; })
        IType(LWU, instr, SignExtend, { throw "Unsupported"; })
        IType(ORI, instr, ZeroExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] | imm;
        })
        IType(SB, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(1, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SC, instr, SignExtend, { throw "Unsupported"; })
        IType(SCD, instr, SignExtend, { throw "Unsupported"; })
        IType(SD, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 8, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(8, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SDC1, instr, SignExtend, { throw "Unsupported"; })
        IType(SDC2, instr, SignExtend, { throw "Unsupported"; })
        IType(SDL, instr, SignExtend, { throw "Unsupported"; })
        IType(SDR, instr, SignExtend, { throw "Unsupported"; })
        IType(SH, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 2, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(2, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SLTI, instr, SignExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] < imm;
        })
        IType(SLTIU, instr, SignExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] < imm;
        })
        IType(SW, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC1, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC2, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC3, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!state.physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWL, instr, SignExtend, { throw "Unsupported"; })
        IType(SWR, instr, SignExtend, { throw "Unsupported"; })
        IType(XORI, instr, ZeroExtend, {
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

void hist()
{
    while (!_log.empty()) {
        LogEntry entry = _log.get();
        std::cout << std::hex << std::setfill(' ') << std::right;
        std::cout << std::setw(16) << entry.first << "    ";
        std::cout << std::hex << std::setfill('0');
        std::cout << std::setw(8) << entry.second << "    ";
        std::cout << std::setfill(' ');
        Mips::disas(entry.first, entry.second);
        std::cout << std::endl;
    }
}

}; /* namespace Eval */
}; /* namespace R4300 */
