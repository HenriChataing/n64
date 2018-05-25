
#include <iomanip>
#include <iostream>
#include <vector>

#include <circular_buffer.h>
#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>

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
        } \
    }) \
    IType(opcode##L, instr, SignExtend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4, true); \
            state.reg.pc += 4 + (i64)(imm << 2); \
        } else \
            state.reg.pc += 8; \
    })

/**
 * @brief Type of an entry in the interpreter log.
 */
typedef std::pair<u64, u32> LogEntry;

/**
 * @brief Type of a stack frame entry.
 */
struct StackFrame {
    u64 functionAddr;   /**< Address of the function. */
    u64 callerAddr;     /**< Address of the call point. */
    u64 stackPointer;   /**< Value of the stack pointer on function entry. */
};

/**
 * @brief Circular buffer for storing the last instructions executed.
 */
circular_buffer<LogEntry> _log(64);

/**
 * @brief Circular buffer for storing the last instructions executed.
 */
std::vector<StackFrame> _backtrace;

/**
 * @brief Push a new stack frame to the backtrace.
 */
static void newStackFrame(u64 functionAddr, u64 callerAddr, u64 stackPointer)
{
    StackFrame sf = { functionAddr, callerAddr, stackPointer };
    _backtrace.push_back(sf);
}

/**
 * @brief Delete the top stack frame(s).
 *
 * Check that the return address and stack pointer match the values recorded
 * in the stack frame.
 */
static void deleteStackFrame(u64 returnAddr, u64 stackPointer)
{
    uint i;

    if (_backtrace.size() == 0)
        return;

    for (i = _backtrace.size(); i > 0; i--) {
        StackFrame &sf = _backtrace[i - 1];
        if ((returnAddr == sf.callerAddr + 8) &&
            (stackPointer == sf.stackPointer))
            break;
    }

    if (i == 0)
        throw "UnknownStackFrame";

    if (i != _backtrace.size())
        throw "DiscardedStackFrames";

    _backtrace.resize(i - 1);
}

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
    u64 code = 0, vector;

    // Compute the exception code to add to the cause register.
    switch (exn) {
        case AddressError:
            code = load ? 4 : 5; // AdEL : AdES
            state.cp0reg.badVAddr = vAddr;
            break;
        case TLBRefill:
        case TLBInvalid:
        case XTLBRefill:
            code = load ? 2 : 3; // TLBL : TLBS
            break;
        case TLBModified:
            code = 1; // Mod
            break;
        case VirtualCoherency:
            code = instr ? 14 : 31; // VCEI : VCED
            break;
        case BusError:
            code = instr ? 6 : 7; // IBE : DBE
            break;
        case IntegerOverflow:
            code = 12; // Ov
            break;
        case Trap:
            code = 13; // Tr
            break;
        case SystemCall:
            code = 8; // Sys
            break;
        case Breakpoint:
            code = 9; // Bp
            break;
        case ReservedInstruction:
            code = 10; // RI
            break;
        case CoprocessorUnusable:
            code = 11; // CpU
            break;
        case FloatingPoint:
            code = 15; // FPE
            break;
        case Watch:
            code = 23; // WATCH
            break;
        case Interrupt:
            code = 0;
            break;
        case CacheError:
        default:
            throw "UnsupportedException";
    }

    // Update the cause and status registers.
    if (!EXL()) {
        state.cp0reg.epc = state.reg.pc;
        state.cp0reg.cause = (state.cp0reg.cause & 0xff00lu) | (code << 2);
        if (delaySlot)
            state.cp0reg.cause |= 1lu << 31;

        if (exn == TLBRefill)
            vector = 0x000;
        else if (exn == XTLBRefill)
            vector = 0x080;
        else /* not a miss */
            vector = 0x180;

    } else {
        state.cp0reg.cause = (state.cp0reg.cause & 0x1000ff00lu) | (code << 2);
        vector = 0x180;
    }

    state.cp0reg.sr |= 1lu << 1; // EXL = 1

    if (BEV())
        state.reg.pc = 0xffffffffbfc00200llu + vector;
    else
        state.reg.pc = 0xffffffff80000000llu + vector;
}

#define returnException(...) \
    { \
        takeException(__VA_ARGS__); \
        return true; \
    }

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
bool step()
{
    u64 pc = R4300::state.reg.pc;
    bool exn = eval(pc, false);
    if (R4300::state.reg.pc == pc)
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

    exn = translateAddress(vAddr, &pAddr, false);

    if (exn != R4300::None)
        returnException(exn, vAddr, delaySlot, true, true);
    if (!R4300::physmem.load(4, pAddr, &instr))
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
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
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
                    u64 a = x >> 32, b = x & 0xffffffff;
                    u64 c = y >> 32, d = y & 0xffffffff;

                    u64 ac = a * c;
                    u64 bc = b * c;
                    u64 ad = a * d;
                    u64 bd = b * d;

                    u64 mid34 = (bd >> 32) + (bc & 0xffffffff) + (ad & 0xffffffff);

                    state.reg.multHi = ac + (bc >> 32) + (ad >> 32) + (mid34 >> 32);
                    state.reg.multLo = (mid34 << 32) | (bd & 0xffffffff);
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
                    newStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
                    state.reg.pc = tg;
                })
                RType(JR, instr, {
                    u64 tg = state.reg.gpr[rs];
                    eval(state.reg.pc + 4, true);
                    deleteStackFrame(tg, state.reg.gpr[29]);
                    state.reg.pc = tg;
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
                    // @todo undefined if rs or rt is not a sign extended
                    // 32bit val
                    // @todo instruction tkaes 3 cycles
                    u64 m = state.reg.gpr[rs] * state.reg.gpr[rt];
                    state.reg.multLo = m & 0xffffffff;
                    state.reg.multHi = (m >> 32) & 0xffffffff;
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
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] >> shamnt, 32);
                })
                RType(SRAV, instr, {
                    state.reg.gpr[rd] = SignExtend(state.reg.gpr[rt] >> state.reg.gpr[rs], 32);
                })
                RType(SRL, instr, {
                    // @todo undefined if rt is not a signed extended 32bit val
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
                })
                RType(SRLV, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> state.reg.gpr[rs];
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
                    }
                })
                IType(BGEZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    } else
                        state.reg.pc += 8;
                })
                IType(BLTZAL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    }
                })
                IType(BLTZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    } else
                        state.reg.pc += 8;
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
            u64 r0 = (state.reg.gpr[rs] & 0xffffffff) + (imm & 0xffffffff);
            u64 r1 = (state.reg.gpr[rs] & 0x7fffffff) + (imm & 0x7fffffff);
            if (((r0 >> 1) ^ r1) & 0x80000000)
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
            tg = (state.reg.pc & 0xfffffffff0000000) | (tg << 2);
            eval(state.reg.pc + 4, true);
            newStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
            state.reg.pc = tg;
        })
        JType(JAL, instr, {
            tg = (state.reg.pc & 0xfffffffff0000000) | (tg << 2);
            state.reg.gpr[31] = state.reg.pc + 8;
            eval(state.reg.pc + 4, true);
            newStackFrame(tg, state.reg.pc, state.reg.gpr[29]);
            state.reg.pc = tg;
        })
        IType(LB, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!R4300::physmem.load(1, pAddr, &val))
                returnException(BusError, vAddr, delaySlot, false, true);
            state.reg.gpr[rt] = SignExtend(val, 8);
        })
        IType(LBU, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr, val;

            exn = translateAddress(vAddr, &pAddr, false);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, true);
            if (!R4300::physmem.load(1, pAddr, &val))
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
            if (!R4300::physmem.load(8, pAddr, &val))
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
            if (!R4300::physmem.load(2, pAddr, &val))
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
            if (!R4300::physmem.load(4, pAddr, &val))
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
            if (!R4300::physmem.load(4, pAddr, &val))
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
            if (!R4300::physmem.load(4, pAddr, &val))
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
            if (!R4300::physmem.load(4, pAddr, &val))
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
            if (!R4300::physmem.store(1, pAddr, state.reg.gpr[rt]))
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
            if (!R4300::physmem.store(8, pAddr, state.reg.gpr[rt]))
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
            if (!R4300::physmem.store(2, pAddr, state.reg.gpr[rt]))
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
            if (!R4300::physmem.store(4, pAddr, state.reg.gpr[rt]))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC1, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!R4300::physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC2, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!R4300::physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
                returnException(BusError, vAddr, delaySlot, false, false);
        })
        IType(SWC3, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr;

            checkAddressAlignment(vAddr, 4, delaySlot, false, false);
            exn = translateAddress(vAddr, &pAddr, true);
            if (exn != None)
                returnException(exn, vAddr, delaySlot, false, false);
            if (!R4300::physmem.store(4, pAddr, cop[1]->read(4, rt, false)))
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

void backtrace()
{
    std::cout << std::hex << std::setfill(' ') << std::right;

    StackFrame &top = _backtrace.back();
    std::cout << std::setw(16) << state.reg.pc << " (";
    std::cout << top.functionAddr << " + ";
    std::cout << (state.reg.pc - top.functionAddr) << " ; )" << std::endl;

    for (uint i = _backtrace.size(); i > 0; i--) {
        StackFrame &cur = _backtrace[i - 1];
        if (i > 1) {
            StackFrame &prev = _backtrace[i - 2];
            std::cout << std::setw(16) << cur.callerAddr << " (";
            std::cout << prev.functionAddr << " + ";
            std::cout << (cur.callerAddr - prev.functionAddr) << " ; ";
            std::cout << cur.stackPointer << ")" << std::endl;
        } else {
            std::cout << std::setw(16) << cur.callerAddr << " ( ; ";
            std::cout << cur.stackPointer << ")" << std::endl;
        }
    }
}

}; /* namespace Eval */
}; /* namespace R4300 */
