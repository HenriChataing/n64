
#include <iomanip>
#include <iostream>

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
 * as rd, rs, rt, shamt in a new scope.
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
            eval(state.reg.pc + 4); \
            state.reg.pc += 4 + ((u64)imm << 2); \
        } \
    }) \
    IType(opcode##L, instr, SignExtend, { \
        if (__VA_ARGS__) { \
            eval(state.reg.pc + 4); \
            state.reg.pc += 4 + ((u64)imm << 2); \
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
circular_buffer<LogEntry> _log(32);

/**
 * @brief Fetch and interpret a single instruction from memory.
 */
void step()
{
    u64 pc = R4300::state.reg.pc;
    eval(pc);
    if (R4300::state.reg.pc == pc)
        R4300::state.reg.pc += 4;
}

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 */
void eval(u64 vAddr)
{
    u64 pAddr = Memory::translateAddress(vAddr, 0);
    u32 instr = R4300::physmem.load(4, pAddr);
    u32 opcode;

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
                    u64 r = state.reg.gpr[rs] + state.reg.gpr[rt];
                    if (r < state.reg.gpr[rs])
                        throw "Integer overflow";
                    state.reg.gpr[rd] = r;
                })
                RType(ADDU, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] + state.reg.gpr[rt];
                })
                RType(AND, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] & state.reg.gpr[rt];
                })
                case BREAK:
                    throw "BREAK trap";
                RType(DADD, instr, {
                    // @todo raise Reserved instruction exception in 32 bit mode
                    u64 r = state.reg.gpr[rs] + state.reg.gpr[rt];
                    if (r < state.reg.gpr[rs])
                        throw "Integer overflow";
                    state.reg.gpr[rd] = r;
                })
                RType(DADDU, instr, {
                    // @todo raise Reserved instruction exception in 32 bit mode
                    state.reg.gpr[rd] = state.reg.gpr[rs] + state.reg.gpr[rt];
                })
                RType(DDIV, instr, {
                    // @todo raise Reserved instruction exception in 32 bit mode
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                    state.reg.gpr[rd] = state.reg.multLo;
                })
                RType(DDIVU, instr, {
                    // @todo raise Reserved instruction exception in 32 bit mode
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                    state.reg.gpr[rd] = state.reg.multLo;
                })
                RType(DIV, instr, {
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                    state.reg.gpr[rd] = state.reg.multLo;
                })
                RType(DIVU, instr, {
                    state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
                    state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
                    state.reg.gpr[rd] = state.reg.multLo;
                })
                RType(DMULT, instr, { throw "Unsupported"; })
                RType(DMULTU, instr, { throw "Unsupported"; })
                RType(DSLL, instr, { throw "Unsupported"; })
                RType(DSLL32, instr, { throw "Unsupported"; })
                RType(DSLLV, instr, { throw "Unsupported"; })
                RType(DSRA, instr, { throw "Unsupported"; })
                RType(DSRA32, instr, { throw "Unsupported"; })
                RType(DSRAV, instr, { throw "Unsupported"; })
                RType(DSRL, instr, { throw "Unsupported"; })
                RType(DSRL32, instr, { throw "Unsupported"; })
                RType(DSRLV, instr, { throw "Unsupported"; })
                RType(DSUB, instr, { throw "Unsupported"; })
                RType(DSUBU, instr, { throw "Unsupported"; })
                RType(JALR, instr, { throw "Unsupported"; })
                RType(JR, instr, {
                    // @todo instruction takes 2 cycles
                    state.reg.pc = state.reg.gpr[rs];
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
                RType(MTHI, instr, { throw "Unsupported"; })
                RType(MTLO, instr, { throw "Unsupported"; })
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
                    state.reg.gpr[rd] = state.reg.gpr[rt] << shamnt;
                })
                RType(SLLV, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rt] << state.reg.gpr[rs];
                })
                RType(SLT, instr, {
                    state.reg.gpr[rd] = (i64)state.reg.gpr[rs] < (i64)state.reg.gpr[rt];
                })
                RType(SLTU, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] < state.reg.gpr[rt];
                })
                RType(SRA, instr, { throw "Unsupported"; })
                RType(SRAV, instr, { throw "Unsupported"; })
                RType(SRL, instr, {
                    // @todo undefined if rt is not a signed extended 32bit val
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> shamnt;
                })
                RType(SRLV, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rt] >> state.reg.gpr[rs];
                })
                RType(SUB, instr, { throw "Unsupported"; })
                RType(SUBU, instr, {
                    state.reg.gpr[rd] = state.reg.gpr[rs] - state.reg.gpr[rt];
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
                        eval(state.reg.pc + 4);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    }
                })
                IType(BGEZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r >= 0) {
                        eval(state.reg.pc + 4);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    } else
                        state.reg.pc += 4;
                })
                IType(BLTZAL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4);
                        state.reg.pc += 4 + (i64)(imm << 2);
                    }
                })
                IType(BLTZALL, instr, SignExtend, {
                    i64 r = state.reg.gpr[rs];
                    state.reg.gpr[31] = state.reg.pc + 8;
                    if (r < 0) {
                        eval(state.reg.pc + 4);
                        state.reg.pc += 4 + (i64)(imm << 2);
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
            u64 r = SignExtend(state.reg.gpr[rs] + imm, 32);
            // @todo detect integer overflows
            state.reg.gpr[rt] = r;
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
                    state.reg.gpr[rt] = cop[opcode & 0x3]->read(4, rd);
                })
                RType(DMF, instr, {
                    state.reg.gpr[rt] = cop[opcode & 0x3]->read(8, rd);
                })
                RType(CF, instr, {
                    throw "Unsupported";
                })
                RType(MT, instr, {
                    cop[opcode & 0x3]->write(4, rd, state.reg.gpr[rt]);
                })
                RType(DMT, instr, {
                    cop[opcode & 0x3]->write(8, rd, state.reg.gpr[rt]);
                })
                RType(CT, instr, {
                    throw "Unsupported";
                })
                case BC:
                    switch (Mips::getRt(instr)) {
                        IType(BCF, instr, SignExtend, {
                            throw "Unsupported";
                        })
                        IType(BCT, instr, SignExtend, {
                            throw "Unsupported";
                        })
                        IType(BCFL, instr, SignExtend, {
                            throw "Unsupported";
                        })
                        IType(BCTL, instr, SignExtend, {
                            throw "Unsupported";
                        })
                        default:
                            throw "Reserved instruction";
                    }
                    break;
                default:
                    throw "Reserved instruction";
            }
            break;

        IType(DADDI, instr, SignExtend, {
            // @todo raise Reserved instruction exception in 32 bit mode
            u64 r = state.reg.gpr[rs] + imm;
            if (r < state.reg.gpr[rs])
                throw "Integer overflow";
            state.reg.gpr[rt] = r;
        })
        IType(DADDIU, instr, SignExtend, {
            // @todo raise Reserved instruction exception in 32 bit mode
            state.reg.gpr[rt] = state.reg.gpr[rs] + imm;
        })
        JType(J, instr, { throw "Unsupported"; })
        JType(JAL, instr, {
            tg = (state.reg.pc & 0xfffffffff0000000) | (tg << 2);
            state.reg.gpr[31] = state.reg.pc + 8;
            state.reg.pc = tg;
        })
        IType(LB, instr, SignExtend, { throw "Unsupported"; })
        IType(LBU, instr, SignExtend, { throw "Unsupported"; })
        IType(LD, instr, SignExtend, { throw "Unsupported"; })
        IType(LDC1, instr, SignExtend, { throw "Unsupported"; })
        IType(LDC2, instr, SignExtend, { throw "Unsupported"; })
        IType(LDL, instr, SignExtend, { throw "Unsupported"; })
        IType(LDR, instr, SignExtend, { throw "Unsupported"; })
        IType(LH, instr, SignExtend, { throw "Unsupported"; })
        IType(LHU, instr, SignExtend, { throw "Unsupported"; })
        IType(LL, instr, SignExtend, { throw "Unsupported"; })
        IType(LLD, instr, SignExtend, { throw "Unsupported"; })
        IType(LUI, instr, SignExtend, {
            state.reg.gpr[rt] = imm << 16;
        })
        IType(LW, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            state.reg.gpr[rt] = SignExtend(R4300::physmem.load(4, pAddr), 32);
        })
        IType(LWC1, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            cop[1]->write(4, rt, SignExtend(R4300::physmem.load(4, pAddr), 32));
        })
        IType(LWC2, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            cop[2]->write(4, rt, SignExtend(R4300::physmem.load(4, pAddr), 32));
        })
        IType(LWC3, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            cop[3]->write(4, rt, SignExtend(R4300::physmem.load(4, pAddr), 32));
        })
        IType(LWL, instr, SignExtend, { throw "Unsupported"; })
        IType(LWR, instr, SignExtend, { throw "Unsupported"; })
        IType(LWU, instr, SignExtend, { throw "Unsupported"; })
        IType(ORI, instr, ZeroExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] | imm;
        })
        IType(SB, instr, SignExtend, { throw "Unsupported"; })
        IType(SC, instr, SignExtend, { throw "Unsupported"; })
        IType(SCD, instr, SignExtend, { throw "Unsupported"; })
        IType(SD, instr, SignExtend, { throw "Unsupported"; })
        IType(SDC1, instr, SignExtend, { throw "Unsupported"; })
        IType(SDC2, instr, SignExtend, { throw "Unsupported"; })
        IType(SDL, instr, SignExtend, { throw "Unsupported"; })
        IType(SDR, instr, SignExtend, { throw "Unsupported"; })
        IType(SH, instr, SignExtend, { throw "Unsupported"; })
        IType(SLTI, instr, SignExtend, {
            state.reg.gpr[rt] = state.reg.gpr[rs] < imm;
        })
        IType(SLTIU, instr, SignExtend, { throw "Unsupported"; })
        IType(SW, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            R4300::physmem.store(4, pAddr, state.reg.gpr[rt]);
        })
        IType(SWC1, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            R4300::physmem.store(4, pAddr, cop[1]->read(4, rt));
        })
        IType(SWC2, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            R4300::physmem.store(4, pAddr, cop[1]->read(4, rt));
        })
        IType(SWC3, instr, SignExtend, {
            u64 vAddr = state.reg.gpr[rs] + imm;
            u64 pAddr = Memory::translateAddress(vAddr, 0);
            R4300::physmem.store(4, pAddr, cop[1]->read(4, rt));
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
        Mips::disas(entry.second);
        std::cout << std::endl;
    }
}

}; /* namespace Eval */
}; /* namespace R4300 */
