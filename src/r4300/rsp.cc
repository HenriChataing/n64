
#include <cstring>
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
namespace RSP {

static inline void logWrite(bool flag, const char *tag, u64 value)
{
    if (flag) {
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << " <- " << std::hex << value << std::endl;
    }
}

/**
 * Check whether a virtual memory address is correctly aligned for a memory
 * access. The RSP does not implement exceptions but the alignment is checked
 * for the sake of catching suspicious states for debugging purposes.
 *
 * @param addr          Checked DMEM/IMEM address.
 * @param bytes         Required alignment, must be a power of two.
 */
static inline bool checkAddressAlignment(u64 addr, u64 bytes) {
    if ((addr & (bytes - 1u)) != 0) {
        std::cerr << "RSP: detected unaligned DMEM/IMEM access of ";
        std::cerr << std::dec << bytes << " bytes from address ";
        std::cerr << std::hex << addr << ", at pc: ";
        std::cerr << std::hex << state.rspreg.pc << std::endl;
        debugger.stop = true;
        return false;
    } else {
        return true;
    }
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
 * The macro generates the code for the "normal" instructions.
 * "branch likely" instructions are not implemented in the RSP processor.
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
            eval(state.rspreg.pc + 4, true); \
            state.rspreg.pc += 4 + (i64)(imm << 2); \
            state.branch = true; \
        } \
    })

/**
 * @brief Type of an entry in the interpreter log.
 */
typedef std::pair<u64, u32> LogEntry;

/**
 * @brief Circular buffer for storing the last instructions executed.
 */
circular_buffer<LogEntry> _log(64);

/** @brief Write a COP0 register value. */
static u32 readCop0Register(u32 r) {
    switch (r) {
        case 0: return state.hwreg.SP_MEM_ADDR_REG;
        case 1: return state.hwreg.SP_DRAM_ADDR_REG;
        case 2: return state.hwreg.SP_RD_LEN_REG;
        case 3: return state.hwreg.SP_WR_LEN_REG;
        case 4: return state.hwreg.SP_STATUS_REG;
        case 5: return (state.hwreg.SP_STATUS_REG & SP_STATUS_DMA_FULL) != 0;
        case 6: return (state.hwreg.SP_STATUS_REG & SP_STATUS_DMA_BUSY) != 0;
        case 7: return read_SP_SEMAPHORE_REG();
        case 8: return state.hwreg.DPC_START_REG;
        case 9: return state.hwreg.DPC_END_REG;
        case 10: return state.hwreg.DPC_CURRENT_REG;
        case 11: return state.hwreg.DPC_STATUS_REG;
        case 12: return state.hwreg.DPC_CLOCK_REG;
        case 13: return state.hwreg.DPC_BUF_BUSY_REG;
        case 14: return state.hwreg.DPC_PIPE_BUSY_REG;
        case 15: return state.hwreg.DPC_TMEM_REG;
        default:
            /* unknown register access */
            std::cerr << "RSP: writing unknown Cop0 register ";
            std::cerr << std::dec << r << std::endl;
            debugger.stop = true;
            break;
    }
    return 0;
}

/** @brief Read a COP0 register value. */
static void writeCop0Register(u32 r, u32 value) {
    switch (r) {
        case 0:
            logWrite(debugger.verbose.SP, "SP_MEM_ADDR_REG(rsp)", value);
            state.hwreg.SP_MEM_ADDR_REG = value;
            break;
        case 1:
            logWrite(debugger.verbose.SP, "SP_DRAM_ADDR_REG(rsp)", value);
            state.hwreg.SP_DRAM_ADDR_REG = value;
            break;
        case 2: write_SP_RD_LEN_REG(value); break;
        case 3: write_SP_WR_LEN_REG(value); break;
        case 4: write_SP_STATUS_REG(value); break;
        case 5: /* DMA_FULL, read only */ break;
        case 6: /* DMA_BUSY, read only */ break;
        case 7: state.hwreg.SP_SEMAPHORE_REG = 0; break;
        case 8:
            state.hwreg.DPC_START_REG = value;
            state.hwreg.DPC_CURRENT_REG = value;
            // state.hwreg.DPC_STATUS_REG |= DPC_STATUS_START_VALID;
            break;
        case 9:
            state.hwreg.DPC_END_REG = value;
            state.hwreg.DPC_CURRENT_REG = value;
            // state.hwreg.DPC_STATUS_REG |= DPC_STATUS_END_VALID;
            break;
        case 10:
            throw "RSP::RDP_command_current";
            break;
        case 11: write_DPC_STATUS_REG(value); break;
        case 12:
            throw "RSP::RDP_clock_counter";
            break;
        case 13:
            throw "RSP::RDP_command_busy";
            break;
        case 14:
            throw "RSP::RDP_pipe_busy_counter";
            break;
        case 15:
            throw "RSP::RDP_TMEM_load_counter";
            break;
        default:
            /* unknown register access */
            std::cerr << "RSP: writing unknown Cop0 register ";
            std::cerr << std::dec << r << std::endl;
            debugger.stop = true;
            break;
    }
}

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
bool step()
{
    // Nothing done is RSP is halted.
    if (state.hwreg.SP_STATUS_REG & SP_STATUS_HALT)
        return false;

    u64 pc = R4300::state.rspreg.pc;
    R4300::state.branch = false;
    bool exn = eval(pc, false);

    if (!R4300::state.branch && !exn)
        R4300::state.rspreg.pc += 4;

    return exn;
}

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 *
 * @param addr         Virtual address of the instruction to execute.
 * @param delaySlot     Whether the instruction executed is in a
 *                      branch delay slot.
 * @return true if the instruction caused an exception
 */
bool eval(u64 addr, bool delaySlot)
{
    u64 instr;
    u32 opcode;

    checkAddressAlignment(addr, 4);
    instr = __builtin_bswap32(*(u32 *)&state.imem[addr & 0xffflu]);

    _log.put(LogEntry(addr, instr));

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
                    u64 r = state.rspreg.gpr[rs] + state.rspreg.gpr[rt];
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(r);
                })
                RType(ADDU, instr, {
                    u64 r = state.rspreg.gpr[rs] + state.rspreg.gpr[rt];
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(r);
                })
                RType(AND, instr, {
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] & state.rspreg.gpr[rt];
                })
                case BREAK: {
                    if (state.hwreg.SP_STATUS_REG & SP_STATUS_INTR_BREAK) {
                        set_MI_INTR_REG(MI_INTR_SP);
                    }
                    break;
                }
                /* DADD not implemented */
                /* DADDU not implemented */
                /* DDIV not implemented */
                /* DDIVU not implemented */
                /* DIV not implemented */
                /* DIVU not implemented */
                /* DMULT not implemented */
                /* DMULTU not implemented */
                /* DSLL not implemented */
                /* DSLL32 not implemented */
                /* DSLLV not implemented */
                /* DSRA not implemented */
                /* DSRA32 not implemented */
                /* DSRAV not implemented */
                /* DSRL not implemented */
                /* DSRL32 not implemented */
                /* DSRLV not implemented */
                /* DSUB not implemented */
                /* DSUBU not implemented */
                RType(JALR, instr, {
                    u64 tg = state.rspreg.gpr[rs];
                    state.rspreg.gpr[rd] = state.rspreg.pc + 8;
                    eval(state.rspreg.pc + 4, true);
                    state.rspreg.pc = tg;
                    state.branch = true;
                })
                RType(JR, instr, {
                    u64 tg = state.rspreg.gpr[rs];
                    eval(state.rspreg.pc + 4, true);
                    state.rspreg.pc = tg;
                    state.branch = true;
                })
                /* MFHI not implemented */
                /* MFLO not implemented */
                RType(MOVN, instr, { throw "Unsupported"; })
                RType(MOVZ, instr, { throw "Unsupported"; })
                /* MTHI not implemented */
                /* MTLO not implemented */
                /* MULT not implemented */
                /* MULTU not implemented */
                RType(NOR, instr, {
                    state.rspreg.gpr[rd] = ~(state.rspreg.gpr[rs] | state.rspreg.gpr[rt]);
                })
                RType(OR, instr, {
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] | state.rspreg.gpr[rt];
                })
                RType(SLL, instr, {
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rt] << shamnt);
                })
                RType(SLLV, instr, {
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rt] << state.rspreg.gpr[rs]);
                })
                RType(SLT, instr, {
                    state.rspreg.gpr[rd] = (i64)state.rspreg.gpr[rs] < (i64)state.rspreg.gpr[rt];
                })
                RType(SLTU, instr, {
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] < state.rspreg.gpr[rt];
                })
                RType(SRA, instr, {
                    // Undefined if rt is not a valid sign extended value
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rt] >> shamnt);
                })
                RType(SRAV, instr, {
                    // Undefined if rt is not a valid sign extended value
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rt] >> state.rspreg.gpr[rs]);
                })
                RType(SRL, instr, {
                    u64 r = (state.rspreg.gpr[rt] & 0xffffffffllu) >> shamnt;
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(r);
                })
                RType(SRLV, instr, {
                    u64 r = (state.rspreg.gpr[rt] & 0xffffffffllu) >> state.rspreg.gpr[rs];
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(r);
                })
                RType(SUB, instr, {
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rs] - state.rspreg.gpr[rt]);
                })
                RType(SUBU, instr, {
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rs] - state.rspreg.gpr[rt]);
                })
                /* SYNC not implemented */
                /* SYSCALL not implemented */
                /* TEQ not implemented */
                /* TGE not implemented */
                /* TGEU not implemented */
                /* TLT not implemented */
                /* TLTU not implemented */
                /* TNE not implemented */
                RType(XOR, instr, {
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] ^ state.rspreg.gpr[rt];
                })
                default:
                    throw "Unsupported Special";
            }
            break;

        case REGIMM:
            switch (Mips::getRt(instr)) {
                BType(BGEZ, instr, (i64)state.rspreg.gpr[rs] >= 0)
                BType(BLTZ, instr, (i64)state.rspreg.gpr[rs] < 0)
                IType(BGEZAL, instr, sign_extend, {
                    i64 r = state.rspreg.gpr[rs];
                    state.rspreg.gpr[31] = state.rspreg.pc + 8;
                    if (r >= 0) {
                        eval(state.rspreg.pc + 4, true);
                        state.rspreg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                /* BGEZALL not implemented */
                IType(BLTZAL, instr, sign_extend, {
                    i64 r = state.rspreg.gpr[rs];
                    state.rspreg.gpr[31] = state.rspreg.pc + 8;
                    if (r < 0) {
                        eval(state.rspreg.pc + 4, true);
                        state.rspreg.pc += 4 + (i64)(imm << 2);
                        state.branch = true;
                    }
                })
                /* BLTZALL not implemented */
                /* TEQI not implemented */
                /* TGEI not implemented */
                /* TGEIU not implemented */
                /* TLTI not implemented */
                /* TLTIU not implemented */
                /* TNEI not implemented */
                default:
                    throw "Unupported Regimm";
                    break;
            }
            break;

        IType(ADDI, instr, sign_extend, {
            state.rspreg.gpr[rt] = sign_extend<u64, u32>(
                state.rspreg.gpr[rs] + imm);
        })
        IType(ADDIU, instr, sign_extend, {
            state.rspreg.gpr[rt] = sign_extend<u64, u32>(
                state.rspreg.gpr[rs] + imm);
        })
        IType(ANDI, instr, zero_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] & imm;
        })
        BType(BEQ, instr, state.rspreg.gpr[rt] == state.rspreg.gpr[rs])
        BType(BGTZ, instr, (i64)state.rspreg.gpr[rs] > 0)
        BType(BLEZ, instr, (i64)state.rspreg.gpr[rs] <= 0)
        BType(BNE, instr, state.rspreg.gpr[rt] != state.rspreg.gpr[rs])
        IType(CACHE, instr, sign_extend, {/* @todo */ })
        /* COP1 not implemented */
        case COP0:
            switch (Mips::getRs(instr)) {
                RType(MF, instr, {
                    state.rspreg.gpr[rt] = readCop0Register(rd);
                })
                /* DMFC0 not implemented */
                /* CFC0 not implemented */
                RType(MT, instr, {
                    writeCop0Register(rd, state.rspreg.gpr[rt]);
                })
                /* DMTC0 not implemented */
                /* CTC0 not implemented */
                default:
                    throw "UnsupportedCOP0Instruction";
            }
            break;
        case COP2:
            throw "COP2 unsupported";
#if 0
            if (instr & Mips::COFUN) {
                cop[opcode & 0x3]->cofun(instr);
                break;
            }
            switch (Mips::getRs(instr)) {
                RType(MF, instr, {
                    state.rspreg.gpr[rt] = cop[opcode & 0x3]->read(4, rd, false);
                })
                /* DMFC2 not implemented */
                RType(CF, instr, {
                    state.rspreg.gpr[rt] = cop[opcode & 0x3]->read(4, rd, true);
                })
                RType(MT, instr, {
                    cop[opcode & 0x3]->write(4, rd, state.rspreg.gpr[rt], false);
                })
                /* DMTC2 not implemented */
                RType(CT, instr, {
                    cop[opcode & 0x3]->write(4, rd, state.rspreg.gpr[rt], true);
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
#endif
            break;
        /* COP3 not implemented */
        /* DADDI not implemented */
        /* DADDIU not implemented */
        JType(J, instr, {
            tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
            eval(state.rspreg.pc + 4, true);
            state.rspreg.pc = tg;
            state.branch = true;
        })
        JType(JAL, instr, {
            tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
            state.rspreg.gpr[31] = state.rspreg.pc + 8;
            eval(state.rspreg.pc + 4, true);
            state.rspreg.pc = tg;
            state.branch = true;
        })
        IType(LB, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;
            u64 val = state.dmem[addr & 0xffflu];
            state.rspreg.gpr[rt] = sign_extend<u64, u8>(val);
        })
        IType(LBU, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;
            u64 val = state.dmem[addr & 0xffflu];
            state.rspreg.gpr[rt] = zero_extend<u64, u8>(val);
        })
        /* LD not implemented */
        /* LDC1 not implemented */
        /* LDC2 not implemented */
        /* LDL not implemented */
        /* LDR not implemented */
        IType(LH, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;

            if (checkAddressAlignment(addr, 2)) {
                u64 val = __builtin_bswap16(*(u16 *)&state.dmem[addr & 0xffflu]);
                state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
            }
        })
        IType(LHU, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;

            // if (checkAddressAlignment(addr, 2)) {
            // TODO unaligned access !
            if ((addr & 1u) != 0) {
                u64 val = __builtin_bswap16(*(u16 *)&state.dmem[addr & 0xffflu]);
                state.rspreg.gpr[rt] = zero_extend<u64, u16>(val);
            } else {
                u64 val =
                    ((u16)state.dmem[addr & 0xffflu] << 8) |
                    (u16)state.dmem[(addr + 1u) & 0xffflu];
                state.rspreg.gpr[rt] = zero_extend<u64, u16>(val);
            }
        })
        /* LL not implemented */
        /* LLD not implemented */
        IType(LUI, instr, sign_extend, {
            state.rspreg.gpr[rt] = imm << 16;
        })
        IType(LW, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;

            if (checkAddressAlignment(addr, 4)) {
                u64 val = __builtin_bswap32(*(u32 *)&state.dmem[addr & 0xffflu]);
                state.rspreg.gpr[rt] = sign_extend<u64, u32>(val);
            }
        })
        /* LWC1 not implemented */
        case LWC2: {
            u32 base = (instr >> 21) & 0x1flu;
            u32 vt = (instr >> 16) & 0x1flu;
            u32 funct = (instr >> 11) & 0x1flu;
            u32 element = (instr >> 7) & 0xflu;
            u32 offset = i7_to_i32(instr & 0x7flu);
            u32 addr = state.rspreg.gpr[base] + offset;

            switch (funct) {
                case 0x0: /* LBV */
                    state.rspreg.vr[vt].b[element] =
                        state.dmem[addr & 0xffflu];
                    break;
                case 0x1lu: /* LSV */
                    memcpy(
                        &state.rspreg.vr[vt].w[element / 2],
                        &state.dmem[(addr << 1) & 0xffflu],
                        2);
                    break;
                case 0x2lu: /* LLV */
                    memcpy(
                        &state.rspreg.vr[vt].w[element / 4],
                        &state.dmem[(addr << 2) & 0xffflu],
                        4);
                    break;
                case 0x3lu: /* LDV */
                    memcpy(
                        &state.rspreg.vr[vt].d[element / 8],
                        &state.dmem[(addr << 3) & 0xffflu],
                        8);
                    break;
                case 0x4lu: /* LQV */
                    memcpy(
                        &state.rspreg.vr[vt].b[element],
                        &state.dmem[addr & 0xffflu],
                        16lu - (offset & 15lu));
                    break;
                case 0x5lu: /* LRV */
                    memcpy(
                        &state.rspreg.vr[vt].b[element],
                        &state.dmem[(addr & 0xffflu) & ~15lu],
                        offset & 15lu);
                    break;
                case 0x6lu: /* LPV */
                    throw "RSP::LPV not supported";
                    break;
                case 0x7lu: /* LUV */
                    throw "RSP::LUV not supported";
                    break;
                case 0x8lu: /* LHV */
                    throw "RSP::LHV not supported";
                    break;
                case 0xblu: /* LTV */
                    throw "RSP::LTV not supported";
                    break;
                default:
                    throw "RSP::invalid LWC2 operation";
                    break;
            }
            break;
        }
        /* LWC3 not implemented */
        /* LWL not implemented */
        /* LWR not implemented */
        /* LWU not implemented */
        IType(ORI, instr, zero_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] | imm;
        })
        IType(SB, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;
            state.dmem[addr & 0xffflu] = state.rspreg.gpr[rt];
        })
        /* SC not implemented */
        /* SCD not implemented */
        /* SD not implemented */
        /* SDC1 not implemented */
        /* SDC2 not implemented */
        /* SDL not implemented */
        /* SDR not implemented */
        IType(SH, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;
            if (checkAddressAlignment(addr, 2)) {
                *(u16 *)&state.dmem[addr & 0xffflu] =
                    __builtin_bswap16(state.rspreg.gpr[rt]);
            }
        })
        IType(SLTI, instr, sign_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] < imm;
        })
        IType(SLTIU, instr, sign_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] < imm;
        })
        IType(SW, instr, sign_extend, {
            u64 addr = state.rspreg.gpr[rs] + imm;
            if (checkAddressAlignment(addr, 4)) {
                *(u32 *)&state.dmem[addr & 0xffflu] =
                    __builtin_bswap32(state.rspreg.gpr[rt]);
            }
        })
        /* SWC1 not implemented */
        IType(SWC2, instr, sign_extend, {
            // u64 addr = state.rspreg.gpr[rs] + imm;
            // if (checkAddressAlignment(addr, 4)) {
            //     *(u32 *)&state.dmem[addr & 0xffflu] =
            //         __builtin_bswap32(state.rspreg.gpr[rt]);
            // }
            throw "RSP::SWC2 not implemented";
        })
        /* SWC3 not implemented */
        /* SWL not implemented */
        /* SWR not implemented */
        IType(XORI, instr, zero_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] ^ imm;
        })

        default:
            throw "Unsupported Opcode";
            break;
    }

    return false;
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

}; /* namespace RSP */
}; /* namespace R4300 */
