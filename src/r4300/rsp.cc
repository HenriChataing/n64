
#include <cstring>
#include <iomanip>
#include <iostream>

#include <circular_buffer.h>
#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <debugger.h>
#include <types.h>

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
        debugger.halt("Invalid address alignment");
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
            state.rsp.nextAction = State::Action::Delay; \
            state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2); \
        } \
    })

/**
 * @brief Write the DPC_START_REG register.
 * This action is emulated as writing to DPC_CURRENT_REG at the same time,
 * which is only an approximation.
 */
void write_DPC_START_REG(u32 value) {
    state.hwreg.DPC_START_REG = value;
    state.hwreg.DPC_CURRENT_REG = value;
    // state.hwreg.DPC_STATUS_REG |= DPC_STATUS_START_VALID;
}

static bool DPC_hasNext(unsigned count) {
    return state.hwreg.DPC_CURRENT_REG + (count * 8) <= state.hwreg.DPC_END_REG;
}

static u64 DPC_peekNext(void) {
    u64 value;
    if (state.hwreg.DPC_STATUS_REG & DPC_STATUS_XBUS_DMEM_DMA) {
        u64 offset = state.hwreg.DPC_CURRENT_REG & UINT64_C(0xfff);
        memcpy(&value, &state.dmem[offset], sizeof(value));
        value = __builtin_bswap64(value);
    } else {
        // state.hwreg.DPC_CURRENT_REG contains a virtual memory address;
        // convert it first.
        R4300::Exception exn;
        u64 vAddr = state.hwreg.DPC_CURRENT_REG;
        u64 pAddr;
        value = 0;

        exn = translateAddress(vAddr, &pAddr, false);
        if (exn == R4300::None) {
            state.physmem.load(8, pAddr, &value);
        } else {
            debugger.halt("DPC_CURRENT_REG invalid");
        }
    }
    return value;
}

/**
 * @brief Write the DPC_END_REG register, which kickstarts the process of
 * loading commands from memory.
 * Commands are read from the DPC_CURRENT_REG until the DPC_END_REG excluded,
 * updating DPC_CURRENT_REG at the same time.
 */
void write_DPC_END_REG(u32 value) {
    state.hwreg.DPC_END_REG = value;
    while (DPC_hasNext(1)) {
        u64 command = DPC_peekNext();
        u64 opcode = (command >> 56) & 0x3flu;
        std::cerr << std::hex << state.hwreg.DPC_CURRENT_REG << " ";
        unsigned skip_dwords = 1;
        switch (opcode) {
            case UINT64_C(0x08):
                std::cerr << "DPC non-shaded triangle " << std::hex << command << std::endl;
                skip_dwords = 4;
                break;
            case UINT64_C(0x0c):
                std::cerr << "DPC shade triangle " << std::hex << command << std::endl;
                skip_dwords = 8;
                break;
            case UINT64_C(0x0a):
                std::cerr << "DPC texture triangle " << std::hex << command << std::endl;
                skip_dwords = 8;
                break;
            case UINT64_C(0x0e):
                std::cerr << "DPC shade texture triangle " << std::hex << command << std::endl;
                skip_dwords = 12;
                break;
            case UINT64_C(0x09):
                std::cerr << "DPC non-shaded Zbuff triangle " << std::hex << command << std::endl;
                skip_dwords = 8;
                break;
            case UINT64_C(0x0d):
                std::cerr << "DPC shade Zbuff triangle " << std::hex << command << std::endl;
                skip_dwords = 12;
                break;
            case UINT64_C(0x0b):
                std::cerr << "DPC texture Zbuff triangle " << std::hex << command << std::endl;
                skip_dwords = 12;
                break;
            case UINT64_C(0x0f):
                std::cerr << "DPC shade texture Zbuff triangle " << std::hex << command << std::endl;
                skip_dwords = 16;
                break;

            case UINT64_C(0x3f):
                std::cerr << "DPC set color image " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x3d):
                std::cerr << "DPC set texture image " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x3e):
                std::cerr << "DPC set z image " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x2d):
                std::cerr << "DPC set scissor " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x3c):
                std::cerr << "DPC set combine mode " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x2f):
                std::cerr << "DPC set other modes " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x35):
                std::cerr << "DPC set tile " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x34):
                std::cerr << "DPC load tile " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x30):
                std::cerr << "DPC load tlut " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x37):
                std::cerr << "DPC set fill color " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x38):
                std::cerr << "DPC set fog color " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x39):
                std::cerr << "DPC set blend color " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x3a):
                std::cerr << "DPC set prim color " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x36):
                std::cerr << "DPC fill rectangle " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x24):
                std::cerr << "DPC texture rectangle " << std::hex << command << std::endl;
                skip_dwords = 2;
                break;
            case UINT64_C(0x31):
                std::cerr << "DPC sync load " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x28):
                std::cerr << "DPC sync tile " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x27):
                std::cerr << "DPC sync pipe " << std::hex << command << std::endl;
                break;
            case UINT64_C(0x29):
                std::cerr << "DPC sync full " << std::hex << command << std::endl;
                set_MI_INTR_REG(MI_INTR_DP);
                // MI_INTR_VI
                break;
            default:
                std::cerr << "DPC unknown opcode (" << std::hex << opcode;
                std::cerr << "): " << command << std::endl;
                break;
        }

        if (!DPC_hasNext(skip_dwords)) {
            std::cerr << "### incomplete command" << std::endl;
        }

        state.hwreg.DPC_CURRENT_REG += 8 * skip_dwords;
    }
}

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
        case 12:
            debugger.halt("DPC_CLOCK_REG read access");
            return state.hwreg.DPC_CLOCK_REG;
        case 13:
            debugger.halt("DPC_BUF_BUSY_REG read access");
            return state.hwreg.DPC_BUF_BUSY_REG;
        case 14:
            debugger.halt("DPC_PIPE_BUSY_REG read access");
            return state.hwreg.DPC_PIPE_BUSY_REG;
        case 15:
            debugger.halt("DPC_TMEM_REG read access");
            return state.hwreg.DPC_TMEM_REG;
        default:
            /* unknown register access */
            std::cerr << "RSP: writing unknown Cop0 register ";
            std::cerr << std::dec << r << std::endl;
            debugger.halt("Unknown Cop0 register read access");
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
        case 8: write_DPC_START_REG(value); break;
        case 9: write_DPC_END_REG(value); break;
        case 10:
            debugger.halt("RSP::RDP_command_current");
            break;
        case 11: write_DPC_STATUS_REG(value); break;
        case 12:
            debugger.halt("RSP::RDP_clock_counter");
            break;
        case 13:
            debugger.halt("RSP::RDP_command_busy");
            break;
        case 14:
            debugger.halt("RSP::RDP_pipe_busy_counter");
            break;
        case 15:
            debugger.halt("RSP::RDP_TMEM_load_counter");
            break;
        default:
            /* unknown register access */
            std::cerr << "RSP: writing unknown Cop0 register ";
            std::cerr << std::dec << r << std::endl;
            debugger.halt("Unknown Cop0 register write access");
            break;
    }
}

static inline u32 getVt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

static inline u32 getVs(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

static inline u32 getVd(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

static void eval_LWC2(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 funct = (instr >> 11) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base];

    switch (funct) {
        case UINT32_C(0x0): /* LBV */
            state.rspreg.vr[vt].b[element] =
                state.dmem[(addr + offset) & 0xffflu];
            break;
        case UINT32_C(0x1): /* LSV */
            memcpy(
                &state.rspreg.vr[vt].b[element],
                &state.dmem[(addr + (offset << 1)) & 0xffflu],
                2);
            break;
        case UINT32_C(0x2): /* LLV */
            memcpy(
                &state.rspreg.vr[vt].b[element],
                &state.dmem[(addr + (offset << 2)) & 0xffflu],
                4);
            break;
        case UINT32_C(0x3): /* LDV */
            memcpy(
                &state.rspreg.vr[vt].b[element],
                &state.dmem[(addr + (offset << 3)) & 0xffflu],
                8);
            break;
        case UINT32_C(0x4): { /* LQV */
            u32 start = addr + (offset << 4);
            u32 end = (start & ~UINT32_C(15)) + UINT32_C(16);
            memcpy(
                &state.rspreg.vr[vt].b[0],
                &state.dmem[start & 0xffflu],
                end - start);
            debugger.warn("RSP::LQV offset shift uncertain");
            break;
        }
        case UINT32_C(0x5): { /* LRV */
            u32 end = addr + (offset << 4);
            u32 start = (end & ~UINT32_C(15));
            unsigned elt = 16 - (end & UINT32_C(15));
            memcpy(
                &state.rspreg.vr[vt].b[elt],
                &state.dmem[start & 0xffflu],
                end - start);
            debugger.warn("RSP::LRV offset shift uncertain");
            break;
        }
        case UINT32_C(0x6): /* LPV */
            debugger.halt("RSP::LPV not supported");
            break;
        case UINT32_C(0x7): /* LUV */
            debugger.halt("RSP::LUV not supported");
            break;
        case UINT32_C(0x8): /* LHV */
            debugger.halt("RSP::LHV not supported");
            break;
        case UINT32_C(0x9): /* LFV */
            debugger.halt("RSP::LFV not supported");
            break;
        case UINT32_C(0xb): /* LTV */
            debugger.halt("RSP::LTV not supported");
            break;
        default:
            debugger.halt("RSP::LWC2 invalid operation");
            break;
    }
}

static void eval_SWC2(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 funct = (instr >> 11) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base];

    switch (funct) {
        case UINT32_C(0x0): /* SBV */
            state.rspreg.vr[vt].b[element] =
                state.dmem[(addr + offset) & 0xffflu];
            break;
        case UINT32_C(0x1): /* SSV */
            memcpy(
                &state.dmem[(addr + (offset << 1)) & 0xffflu],
                &state.rspreg.vr[vt].b[element],
                2);
            break;
        case UINT32_C(0x2): /* SLV */
            memcpy(
                &state.dmem[(addr + (offset << 2)) & 0xffflu],
                &state.rspreg.vr[vt].b[element],
                4);
            break;
        case UINT32_C(0x3): /* SDV */
            memcpy(
                &state.dmem[(addr + (offset << 3)) & 0xffflu],
                &state.rspreg.vr[vt].b[element],
                8);
            break;
        case UINT32_C(0x4): { /* SQV */
            u32 start = addr + (offset << 4);
            u32 end = (start & ~UINT32_C(15)) + UINT32_C(16);
            memcpy(
                &state.dmem[start & 0xffflu],
                &state.rspreg.vr[vt].b[0],
                end - start);
            debugger.warn("RSP::SQV offset shift uncertain");
            break;
        }
        case UINT32_C(0x5): { /* SRV */
            u32 end = addr + (offset << 4);
            u32 start = (end & ~UINT32_C(15));
            unsigned elt = 16 - (end & UINT32_C(15));
            memcpy(
                &state.dmem[start & 0xffflu],
                &state.rspreg.vr[vt].b[elt],
                end - start);
            debugger.warn("RSP::SRV offset shift uncertain");
            break;
        }
        case UINT32_C(0x6): /* SPV */
            debugger.halt("RSP::SPV not supported");
            break;
        case UINT32_C(0x7): /* SUV */
            debugger.halt("RSP::SUV not supported");
            break;
        case UINT32_C(0x8): /* SHV */
            debugger.halt("RSP::SHV not supported");
            break;
        case UINT32_C(0x9): /* SFV */
            debugger.halt("RSP::SFV not supported");
            break;
        case UINT32_C(0xb): /* STV */
            debugger.halt("RSP::STV not supported");
            break;
        default:
            debugger.halt("RSP::SWC2 invalid operation");
            break;
    }
}

static inline unsigned selectElementIndex(unsigned i, u32 e) {
    unsigned j;
    if (e == 0) {
        j = i;
    } else if ((e & 0b1110lu) == 0b0010lu) {
        j = (i & 0b1110lu) + (e & 0b0001lu);
    } else if ((e & 0b1100lu) == 0b0100lu) {
        j = (i & 0b1100lu) + (e & 0b0011lu);
    } else if ((e & 0b1000lu) == 0b1000lu) {
        j = (e & 0b0111lu);
    }
    return j;
}

static void eval_VABS(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i16 res = svs > 0 ? svt :
                  svs < 0 ? -svt : 0;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)res;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VADD(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 add = svs + svt + ((state.rspreg.vco >> i) & UINT16_C(1));

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= ((u32)add & UINT64_C(0xffff));

        i16 res = clamp<i16, i32>(add);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }

    state.rspreg.vco = 0;
    debugger.warn("RSP::VADD clamp_signed not implemented");
}

static void eval_VADDC(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 svs = __builtin_bswap16(state.rspreg.vr[vs].h[i]);
        u16 svt = __builtin_bswap16(state.rspreg.vr[vt].h[j]);
        u32 add = svs + svt;

        state.rspreg.vco |= ((add & UINT32_C(0x10000)) >> 16) << i;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)add;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)add);
    }
}

static void eval_VAND(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = state.rspreg.vr[vs].h[i] & state.rspreg.vr[vt].h[j];
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

static void eval_VMACF(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        u32 acc = state.rspreg.vacc[i] >> 16;
        acc += (u32)mul << 1;
        state.rspreg.vacc[i] &= UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)acc << 16;

        i16 res = clamp<i16, i32>((i32)acc);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VMADH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        u32 acc = state.rspreg.vacc[i] & UINT64_C(0xffffffff);
        acc += ((u32)mul & UINT32_C(0xffff)) << 16;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= acc;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16(acc >> 16);
    }
}

static void eval_VMADM(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        u32 acc = state.rspreg.vacc[i] & UINT64_C(0xffffffff);
        acc += (u32)mul;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= acc;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16(acc >> 16);
    }
}

static void eval_VMADN(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        u32 acc = state.rspreg.vacc[i] & UINT64_C(0xffffffff);
        acc += (u32)mul;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= acc;

        i16 res = clamp<i16, i32>((i32)acc);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VMOV(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 de = getVs(instr);
    u32 vd = getVd(instr);

    state.rspreg.vacc[de] &= ~UINT64_C(0xffff);
    state.rspreg.vacc[de] |= __builtin_bswap16(state.rspreg.vr[vt].h[e]);
    state.rspreg.vr[vd].h[de] = state.rspreg.vr[vt].h[e];
}

static void eval_VMUDH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= ((u32)mul & UINT32_C(0xffff)) << 16;

        i16 res = clamp<i16, i32>(mul);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VMUDL(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        i16 acc = (i16)(u16)((u32)mul >> 16);
        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= (u16)acc
                             | (acc < 0 ? UINT64_C(0xffff0000) : 0);

        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)acc);
    }
}

static void eval_VMUDM(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= (u32)mul;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u32)mul >> 16);
    }
}

static void eval_VMUDN(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffffffff);
        state.rspreg.vacc[i] |= (u32)mul;

        i16 res = clamp<i16, i32>(mul);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VMULF(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        i16 svt = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]);
        i32 mul = (i32)svs * (i32)svt;

        u64 acc = state.rspreg.vacc[i] & UINT64_C(0xffff);
        acc |= (u64)(u32)mul << 17;
        acc += UINT64_C(0x8000);

        i32 res = (i32)(u32)(acc >> 16);
        if (res < INT32_C(-32768))
            res = INT32_C(-32768);
        if (res > INT32_C(+32767))
            res = INT32_C(+32767);

        state.rspreg.vacc[i] = acc;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)(i16)res);
    }
}

static void eval_VNAND(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = ~(state.rspreg.vr[vs].h[i] & state.rspreg.vr[vt].h[j]);
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

static void eval_VNOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = ~(state.rspreg.vr[vs].h[i] | state.rspreg.vr[vt].h[j]);
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

static void eval_VNXOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = ~(state.rspreg.vr[vs].h[i] ^ state.rspreg.vr[vt].h[j]);
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

static void eval_VOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = state.rspreg.vr[vs].h[i] | state.rspreg.vr[vt].h[j];
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

/**
 * @brief Implement the Reciprocal instruction.
 *  Inputs a signed i16 integer and outputs the reciprocal in 32bit fixed point
 *  format (the radix point is irrelevant).
 *
 *  Note that the machine instruction is implemented using a table lookup
 *  with the 10 most significant bits, i.e. there is precision loss.
 *
 *  Without the original table, VRCP is implemented using a floating
 *  point division, whose result is converted back to S0.31.
 */
static void eval_VRCP(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 de = getVs(instr);
    u32 vd = getVd(instr);

    // Compute the reciprocal of the input value interpreted as
    // in S15.0 format, in S0.31 format. The actual output radix depends
    // on the radix the caller has set for the input value:
    //      input: Sm.n => output: Sm':(n-1)
    i16 in = (i16)__builtin_bswap16(state.rspreg.vr[vt].h[e]);
    i32 out;

    if (in == 0) {
        out = INT32_MAX;
    } else {
        double dout = 1. / (double)(in > 0 ? in : -in);
        dout *= (double)(1lu << 31);
        i64 out_ = in > 0 ? dout : -dout;
        // Clamp the result to the INT32_MIN, INT32_MAX interval.
        out = out_ > INT32_MAX ? INT32_MAX :
              out_ < INT32_MIN ? INT32_MIN : out_;
    }

    state.rspreg.divout = (u32)out;
    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)in;
    }
    state.rspreg.vr[vd].h[de] = __builtin_bswap16((u16)(u32)out);
}

static void eval_VRCPH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 de = getVs(instr);
    u32 vd = getVd(instr);

    u16 in = __builtin_bswap16(state.rspreg.vr[vt].h[e]);

    state.rspreg.divin = (u32)in << 16;
    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)in;
    }
    state.rspreg.vr[vd].h[de] = __builtin_bswap16(state.rspreg.divout >> 16);
}

static void eval_VRCPL(u32 instr) {
    debugger.halt("VRCPL");
}

static void eval_VSAR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0x7);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        if (e == 0) {
            state.rspreg.vr[vd].h[i] =
                __builtin_bswap16(state.rspreg.vacc[i] >> 32);
            state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 32);
            state.rspreg.vacc[i] |=
                (u64)__builtin_bswap16(state.rspreg.vr[vs].h[i]) << 32;
        } else if (e == 1) {
            state.rspreg.vr[vd].h[i] =
                __builtin_bswap16(state.rspreg.vacc[i] >> 16);
            state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 16);
            state.rspreg.vacc[i] |=
                (u64)__builtin_bswap16(state.rspreg.vr[vs].h[i]) << 16;
        } else if (e == 2) {
            state.rspreg.vr[vd].h[i] =
                __builtin_bswap16(state.rspreg.vacc[i]);
            state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
            state.rspreg.vacc[i] |=
                (u64)__builtin_bswap16(state.rspreg.vr[vs].h[i]);
        }
    }
}

static void eval_VSUB(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        i32 sub =
            (i16)__builtin_bswap16(state.rspreg.vr[vs].h[i]) -
            (i16)__builtin_bswap16(state.rspreg.vr[vt].h[j]) -
            ((state.rspreg.vco >> i) & UINT16_C(1));
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)(u32)sub;

        i16 res = clamp<i16, i32>(sub);
        state.rspreg.vr[vd].h[i] = __builtin_bswap16((u16)res);
    }
}

static void eval_VSUBC(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 svs = __builtin_bswap16(state.rspreg.vr[vs].h[i]);
        u16 svt = __builtin_bswap16(state.rspreg.vr[vt].h[j]);
        u32 res = (u32)svs - (u32)svt;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)res;
        state.rspreg.vr[vd].h[i] = __builtin_bswap16(res);

        if (res & (UINT32_C(1) << 16)) { /* res < 0 */
            state.rspreg.vco |= UINT16_C(1) << i;
            state.rspreg.vco |= UINT16_C(1) << (i + 8);
        } else if (res) {
            state.rspreg.vco |= UINT16_C(1) << (i + 8);
        }
    }
}

static void eval_VXOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 res = state.rspreg.vr[vs].h[i] ^ state.rspreg.vr[vt].h[j];
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u64)__builtin_bswap16(res);
        state.rspreg.vr[vd].h[i] = res;
    }
}

static void eval_COP2(u32 instr) {
    switch (instr & 0x3flu) {
        case UINT32_C(0x13): eval_VABS(instr); break;
        case UINT32_C(0x10): eval_VADD(instr); break;
        case UINT32_C(0x14): eval_VADDC(instr); break;
        case UINT32_C(0x28): eval_VAND(instr); break;
        case UINT32_C(0x25):
            debugger.halt("RSP::VCH unsupported");
            break;
        case UINT32_C(0x24):
            debugger.halt("RSP::VCL unsupported");
            break;
        case UINT32_C(0x26):
            debugger.halt("RSP::VCR unsupported");
            break;
        case UINT32_C(0x21):
            debugger.halt("RSP::VEQ unsupported");
            break;
        case UINT32_C(0x23):
            debugger.halt("RSP::VGE unsupported");
            break;
        case UINT32_C(0x20):
            debugger.halt("RSP::VLT unsupported");
            break;
        case UINT32_C(0x08): eval_VMACF(instr); break;
        case UINT32_C(0x0b):
            debugger.halt("RSP::VMACQ unsupported");
            break;
        case UINT32_C(0x09):
            debugger.halt("RSP::VMACU unsupported");
            break;
        case UINT32_C(0x0f): eval_VMADH(instr); break;
        case UINT32_C(0x0c):
            debugger.halt("RSP::VMADL unsupported");
            break;
        case UINT32_C(0x0d): eval_VMADM(instr); break;
        case UINT32_C(0x0e): eval_VMADN(instr); break;
        case UINT32_C(0x33): eval_VMOV(instr); break;
        case UINT32_C(0x27):
            debugger.halt("RSP::VMRG unsupported");
            break;
        case UINT32_C(0x07): eval_VMUDH(instr); break;
        case UINT32_C(0x04): eval_VMUDL(instr); break;
        case UINT32_C(0x05): eval_VMUDM(instr); break;
        case UINT32_C(0x06): eval_VMUDN(instr); break;
        case UINT32_C(0x00): eval_VMULF(instr); break;
        case UINT32_C(0x03):
            debugger.halt("RSP::VMULQ unsupported");
            break;
        case UINT32_C(0x01):
            debugger.halt("RSP::VMULU unsupported");
            break;
        case UINT32_C(0x29): eval_VNAND(instr); break;
        case UINT32_C(0x22):
            debugger.halt("RSP::VNE unsupported");
            break;
        case UINT32_C(0x37):
            debugger.halt("RSP::VNOP unsupported");
            break;
        case UINT32_C(0x2b): eval_VNOR(instr); break;
        case UINT32_C(0x2d): eval_VNXOR(instr); break;
        case UINT32_C(0x2a): eval_VOR(instr); break;
        case UINT32_C(0x30): eval_VRCP(instr); break;
        case UINT32_C(0x31): eval_VRCPL(instr); break;
        case UINT32_C(0x32): eval_VRCPH(instr); break;
        case UINT32_C(0x0a):
            debugger.halt("RSP::VRNDN unsupported");
            break;
        case UINT32_C(0x02):
            debugger.halt("RSP::VRNDP unsupported");
            break;
        case UINT32_C(0x34):
            debugger.halt("RSP::VRSQ unsupported");
            break;
        case UINT32_C(0x36):
            debugger.halt("RSP::VRSQH unsupported");
            break;
        case UINT32_C(0x35):
            debugger.halt("RSP::VRSQL unsupported");
            break;
        case UINT32_C(0x1d): eval_VSAR(instr); break;
        case UINT32_C(0x11): eval_VSUB(instr); break;
        case UINT32_C(0x15): eval_VSUBC(instr); break;
        case UINT32_C(0x2c): eval_VXOR(instr); break;
        default:
            debugger.halt("RSP::COP2 invalid operation");
            break;
    }
}

static bool eval(bool delaySlot);

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
bool step()
{
    // Nothing done is RSP is halted.
    if (state.hwreg.SP_STATUS_REG & SP_STATUS_HALT)
        return false;

    bool exn;
    switch (state.rsp.nextAction) {
        case State::Action::Continue:
            state.rspreg.pc += 4;
            exn = eval(false);
            break;

        case State::Action::Delay:
            state.rspreg.pc += 4;
            state.rsp.nextAction = State::Action::Jump;
            exn = eval(true);
            break;

        case State::Action::Jump:
            state.rspreg.pc = state.rsp.nextPc;
            state.rsp.nextAction = State::Action::Continue;
            exn = eval(false);
            break;
    }
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
static bool eval(bool delaySlot)
{
    u64 addr = state.rspreg.pc;
    u64 instr;
    u32 opcode;

    checkAddressAlignment(addr, 4);
    instr = __builtin_bswap32(*(u32 *)&state.imem[addr & 0xffflu]);

    debugger.rspTrace.put(TraceEntry(addr, instr));

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
                    state.hwreg.SP_STATUS_REG |=
                        SP_STATUS_BROKE | SP_STATUS_HALT;
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
                    state.rsp.nextAction = State::Action::Delay;
                    state.rsp.nextPc = tg;
                })
                RType(JR, instr, {
                    u64 tg = state.rspreg.gpr[rs];
                    state.rsp.nextAction = State::Action::Delay;
                    state.rsp.nextPc = tg;
                })
                /* MFHI not implemented */
                /* MFLO not implemented */
                RType(MOVN, instr, { debugger.halt("Unsupported"); })
                RType(MOVZ, instr, { debugger.halt("Unsupported"); })
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
                    unsigned int shamnt = state.rspreg.gpr[rs] & 0x1flu;
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
                        state.rspreg.gpr[rt] << shamnt);
                })
                RType(SLT, instr, {
                    state.rspreg.gpr[rd] = (i64)state.rspreg.gpr[rs] < (i64)state.rspreg.gpr[rt];
                })
                RType(SLTU, instr, {
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] < state.rspreg.gpr[rt];
                })
                RType(SRA, instr, {
                    bool sign = (state.rspreg.gpr[rt] & (1lu << 31)) != 0;
                    // Right shift is logical for unsigned c types,
                    // we need to add the type manually.
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rt] >> shamnt;
                    if (sign) {
                        u64 mask = (1ul << (shamnt + 32)) - 1u;
                        state.rspreg.gpr[rd] |= mask << (32 - shamnt);
                    }
                })
                RType(SRAV, instr, {
                    bool sign = (state.rspreg.gpr[rt] & (1lu << 31)) != 0;
                    unsigned int shamnt = state.rspreg.gpr[rs] & 0x1flu;
                    // Right shift is logical for unsigned c types,
                    // we need to add the type manually.
                    state.rspreg.gpr[rd] = state.rspreg.gpr[rt] >> shamnt;
                    if (sign) {
                        u64 mask = (1ul << (shamnt + 32)) - 1u;
                        state.rspreg.gpr[rd] |= mask << (32 - shamnt);
                    }
                })
                RType(SRL, instr, {
                    u64 r = (state.rspreg.gpr[rt] & 0xffffffffllu) >> shamnt;
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(r);
                })
                RType(SRLV, instr, {
                    unsigned int shamnt = state.rspreg.gpr[rs] & 0x1flu;
                    u64 res = (state.rspreg.gpr[rt] & 0xfffffffflu) >> shamnt;
                    state.rspreg.gpr[rd] = sign_extend<u64, u32>(res);
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
                    debugger.halt("Unsupported Special");
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
                        state.rsp.nextAction = State::Action::Delay;
                        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
                    }
                })
                /* BGEZALL not implemented */
                IType(BLTZAL, instr, sign_extend, {
                    i64 r = state.rspreg.gpr[rs];
                    state.rspreg.gpr[31] = state.rspreg.pc + 8;
                    if (r < 0) {
                        state.rsp.nextAction = State::Action::Delay;
                        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
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
                    debugger.halt("Unupported Regimm");
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
        IType(CACHE, instr, sign_extend, { /* @todo */ })
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
                    debugger.halt("UnsupportedCOP0Instruction");
            }
            break;
        /* COP1 not implemented */
        case COP2:
            switch (Mips::getRs(instr)) {
                RType(MF, instr, {
                    u32 e = (instr >> 7) & UINT32_C(0xf);
                    u16 val;
                    memcpy(&val, &state.rspreg.vr[rd].b[e], 2);
                    val = __builtin_bswap16(val);
                    state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
                })
                RType(MT, instr, {
                    u32 e = (instr >> 7) & UINT32_C(0xf);
                    u16 val = __builtin_bswap16((u16)state.rspreg.gpr[rt]);
                    memcpy(&state.rspreg.vr[rd].b[e], &val, 2);
                })
                RType(CF, instr, {
                    u32 out = 0;
                    switch (rd) {
                        case 0: out = state.rspreg.vco; break;
                        case 1: out = state.rspreg.vcc; break;
                        case 2: out = state.rspreg.vce; break;
                    }
                    state.rspreg.gpr[rt] = out;
                })
                RType(CT, instr, {
                    debugger.halt("RSP::CTC2 unsupported");
                })
                default:
                    if ((instr & (1lu << 25)) == 0) {
                        debugger.halt("RSP::COP2 invalid operation");
                    } else {
                        eval_COP2(instr);
                    }
                    break;
            }

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
                        IType(BCF, instr, sign_extend, { debugger.halt("Unsupported"); })
                        IType(BCT, instr, sign_extend, { debugger.halt("Unsupported"); })
                        IType(BCFL, instr, sign_extend, { debugger.halt("Unsupported"); })
                        IType(BCTL, instr, sign_extend, { debugger.halt("Unsupported"); })
                        default:
                            debugger.halt("ReservedInstruction");
                    }
                    break;
                default:
                    debugger.halt("ReservedInstruction");
            }
#endif
            break;
        /* COP3 not implemented */
        /* DADDI not implemented */
        /* DADDIU not implemented */
        JType(J, instr, {
            tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
            state.rsp.nextAction = State::Action::Delay;
            state.rsp.nextPc = tg;
        })
        JType(JAL, instr, {
            tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
            state.rspreg.gpr[31] = state.rspreg.pc + 8;
            state.rsp.nextAction = State::Action::Delay;
            state.rsp.nextPc = tg;
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
        case LWC2:
            eval_LWC2(instr);
            break;
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
            state.rspreg.gpr[rt] = (i64)state.rspreg.gpr[rs] < (i64)imm;
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
        case SWC2:
            eval_SWC2(instr);
            break;
        /* SWC3 not implemented */
        /* SWL not implemented */
        /* SWR not implemented */
        IType(XORI, instr, zero_extend, {
            state.rspreg.gpr[rt] = state.rspreg.gpr[rs] ^ imm;
        })

        default:
            debugger.halt("Unsupported Opcode");
            break;
    }

    return false;
}

}; /* namespace RSP */
}; /* namespace R4300 */
