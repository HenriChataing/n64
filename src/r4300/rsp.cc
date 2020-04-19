
#include <cstring>
#include <iomanip>
#include <iostream>

#include <circular_buffer.h>
#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/rdp.h>
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

static void loadVectorBytesAt(unsigned vr, unsigned element, u8 *addr,
                              unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
#if __BYTE_ORDER__ == __BIG_ENDIAN
        state.rspreg.vr[vr].b[element] = *addr;
#else
        state.rspreg.vr[vr].b[element ^ 1u] = *addr;
#endif
    }
}

static void loadVectorBytes(unsigned vr, unsigned element, unsigned addr,
                            unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
#if __BYTE_ORDER__ == __BIG_ENDIAN
        state.rspreg.vr[vr].b[element] = state.dmem[addr & 0xfffu];
#else
        state.rspreg.vr[vr].b[element ^ 1u] = state.dmem[addr & 0xfffu];
#endif
    }
}

static void eval_LTV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base] + (offset << 4);

    for (unsigned i = 0; i < 8; i++) {
        unsigned vs = (vt & 0x18u) | ((i + (element >> 1)) & 0x7u);
        unsigned slice = (i + (element >> 1)) & 0x7u;
        loadVectorBytes(vs, 2 * i, addr + 2 * slice, 2);
    }
}

static void eval_LWV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base] + (offset << 4);

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (i + (element >> 1)) & 0x7u;
        loadVectorBytes(vt, 2 * slice, addr + 2 * slice, 2);
    }
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
            loadVectorBytes(vt, element, addr + offset, 1);
            break;
        case UINT32_C(0x1): /* LSV */
            loadVectorBytes(vt, element, addr + (offset << 1), 2);
            break;
        case UINT32_C(0x2): /* LLV */
            loadVectorBytes(vt, element, addr + (offset << 2), 4);
            break;
        case UINT32_C(0x3): /* LDV */
            loadVectorBytes(vt, element, addr + (offset << 3), 8);
            break;
        case UINT32_C(0x4): { /* LQV */
            u32 start = addr + (offset << 4);
            u32 end = (start & ~UINT32_C(15)) + UINT32_C(16);
            loadVectorBytes(vt, element, start, end - start);
            break;
        }
        case UINT32_C(0x5): { /* LRV */
            u32 end = addr + (offset << 4);
            u32 start = (end & ~UINT32_C(15));
            element = 16 - (end & UINT32_C(15));
            loadVectorBytes(vt, element, start, end - start);
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
        case UINT32_C(0xa): eval_LWV(instr); break;
        case UINT32_C(0xb): eval_LTV(instr); break;
        default:
            debugger.halt("RSP::LWC2 invalid operation");
            break;
    }
}

static void storeVectorBytesAt(unsigned vr, unsigned element, u8 *addr,
                               unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
#if __BYTE_ORDER__ == __BIG_ENDIAN
        *addr = state.rspreg.vr[vr].b[element];
#else
        *addr = state.rspreg.vr[vr].b[element ^ 1u];
#endif
    }
}

static void storeVectorBytes(unsigned vr, unsigned element, unsigned addr,
                             unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
#if __BYTE_ORDER__ == __BIG_ENDIAN
        state.dmem[addr & 0xfffu] = state.rspreg.vr[vr].b[element];
#else
        state.dmem[addr & 0xfffu] = state.rspreg.vr[vr].b[element ^ 1u];
#endif
    }
}

static void eval_SUV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base] + (offset << 3);

    for (unsigned i = 0; i < 8; i++, addr++) {
        u8 val = state.rspreg.vr[vt].h[i] >> 7;
        state.dmem[addr & 0xfffu] = val;
    }
}

static void eval_STV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base] + (offset << 4);

    for (unsigned i = 0; i < 8; i++, addr += 2) {
        unsigned vs = (vt & 0x18u) | ((i + (element >> 1)) & 0x7u);
        storeVectorBytes(vs, 2 * i, addr, 2);
    }
}

static void eval_SWV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = i7_to_i32(instr & 0x7flu);
    u32 addr = state.rspreg.gpr[base] + (offset << 4);

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (i + 8u - (element >> 1)) & 0x7u;
        storeVectorBytes(vt, 2 * i, addr + 2 * slice, 2);
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
            storeVectorBytes(vt, element, addr + offset, 1);
            break;
        case UINT32_C(0x1): /* SSV */
            storeVectorBytes(vt, element, addr + (offset << 1), 2);
            break;
        case UINT32_C(0x2): /* SLV */
            storeVectorBytes(vt, element, addr + (offset << 2), 4);
            break;
        case UINT32_C(0x3): /* SDV */
            storeVectorBytes(vt, element, addr + (offset << 3), 8);
            break;
        case UINT32_C(0x4): { /* SQV */
            u32 start = addr + (offset << 4);
            u32 end = (start & ~UINT32_C(15)) + UINT32_C(16);
            storeVectorBytes(vt, element, start, end - start);
            break;
        }
        case UINT32_C(0x5): { /* SRV */
            u32 end = addr + (offset << 4);
            u32 start = (end & ~UINT32_C(15));
            element = 16 - (end & UINT32_C(15));
            storeVectorBytes(vt, element, start, end - start);
            break;
        }
        case UINT32_C(0x6): /* SPV */
            debugger.halt("RSP::SPV not supported");
            break;
        case UINT32_C(0x7): eval_SUV(instr); break;
        case UINT32_C(0x8): /* SHV */
            debugger.halt("RSP::SHV not supported");
            break;
        case UINT32_C(0x9): /* SFV */
            debugger.halt("RSP::SFV not supported");
            break;
        case UINT32_C(0xa): eval_SWV(instr); break;
        case UINT32_C(0xb): eval_STV(instr); break;
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

static void eval_Reserved(u32 instr) {
    debugger.halt("RSP reserved instruction");
}

static void eval_VABS(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        i16 res = svs > 0 ? svt :
                  svs < 0 ? -svt : 0;

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)res;
        out.h[i] = (u16)res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VADD(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] +
                  (i16)state.rspreg.vr[vt].h[j] +
                  ((state.rspreg.vco >> i) & UINT16_C(1));

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)(u32)res;
        out.h[i] = (u16)clamp<i16, i32>(res);
    }

    state.rspreg.vr[vd] = out;
    state.rspreg.vco = 0;
}

static void eval_VADDC(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] +
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vco |= ((res >> 16) & UINT32_C(1)) << i;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)res;
        out.h[i] = (u16)res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VAND(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] &
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VCH(u32 instr) {
    debugger.halt("VCH unsupported");
}

static void eval_VCL(u32 instr) {
    debugger.halt("VCL unsupported");
}

static void eval_VCR(u32 instr) {
    debugger.halt("VCR unsupported");
}

static void eval_VEQ(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 svs = state.rspreg.vr[vs].h[i];
        u16 svt = state.rspreg.vr[vt].h[j];
        u16 res;

        if (svs == svt) {
            state.rspreg.vcc |= 1u << i;
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

static void eval_VGE(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        unsigned vcoi = (state.rspreg.vco >> i) & 1u;
        u16 res;

        if (svs > svt || (svs == svt && !vcoi)) {
            state.rspreg.vcc |= 1u << i;
            res = (i16)svs;
        } else {
            res = (i16)svt;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

static void eval_VLT(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        unsigned vcoi = (state.rspreg.vco >> i) & 1u;
        u16 res;

        if (svs < svt || (svs == svt && vcoi)) {
            state.rspreg.vcc |= 1u << i;
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

static void eval_VMACF(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += sign_extend<u64, u32>((u32)res << 1);
        out.h[i] = state.rspreg.vacc[i] >> 16;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMACQ(u32 instr) {
    debugger.halt("VMACQ unsupported");
}

static void eval_VMACU(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += sign_extend<u64, u32>((u32)res << 1);
        out.h[i] = clamp<u16, i32>((i32)(state.rspreg.vacc[i] >> 16));
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMADH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += (u64)(i64)res << 16;
        out.h[i] = (u16)clamp<i16, i32>((u32)(state.rspreg.vacc[i] >> 16));
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMADL(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] *
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += sign_extend<u64, u32>(res >> 16);
        out.h[i] = state.rspreg.vacc[i];
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMADM(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += (u64)(i64)res;
        out.h[i] = state.rspreg.vacc[i] >> 16;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMADN(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i32)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] += (u64)(i64)res;
        out.h[i] = state.rspreg.vacc[i];
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMOV(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0x7);
    u32 vt = getVt(instr);
    u32 de = getVs(instr) & 0x7;
    u32 vd = getVd(instr);

    state.rspreg.vacc[de] &= ~UINT64_C(0xffff);
    state.rspreg.vacc[de] |= state.rspreg.vr[vt].h[e];
    state.rspreg.vr[vd].h[de] = state.rspreg.vr[vt].h[e];
}

static void eval_VMRG(u32 instr) {
    debugger.halt("RSP::VMRG unsupported");
}

static void eval_VMUDH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = (u64)(i64)res << 16;
        out.h[i] = (u16)clamp<i16, i32>(res);
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMUDL(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] *
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = sign_extend<u64, u32>(res >> 16);
        out.h[i] = res >> 16;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMUDM(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = (u64)(i64)res;
        out.h[i] = (u32)res >> 16;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMUDN(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i32)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = (u64)(i64)res;
        out.h[i] = (u16)(u32)res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMULF(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = sign_extend<u64, u32>(((u32)res << 1) + 32768u);
        out.h[i] = state.rspreg.vacc[i] >> 16;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMULQ(u32 instr) {
    debugger.halt("RSP::VMULQ unsupported");
}

static void eval_VMULU(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] = sign_extend<u64, u32>(((u32)res << 1) + 32768u);
        out.h[i] = clamp<u16, i32>((i32)(state.rspreg.vacc[i] >> 16));
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VNAND(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] &
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VNE(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 svs = state.rspreg.vr[vs].h[i];
        u16 svt = state.rspreg.vr[vt].h[j];
        u16 res;

        if (svs != svt) {
            state.rspreg.vcc |= 1u << i;
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

static void eval_VNOP(u32 instr) {
    (void)instr;
}

static void eval_VNOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] |
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VNXOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] ^
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] |
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
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
    i16 in = (i16)state.rspreg.vr[vt].h[e];
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
    state.rspreg.vr[vd].h[de] = (u16)(u32)out;
}

static void eval_VRCPH(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 de = getVs(instr);
    u32 vd = getVd(instr);

    u16 in = state.rspreg.vr[vt].h[e];

    state.rspreg.divin = (u32)in << 16;
    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)in;
    }
    state.rspreg.vr[vd].h[de] = state.rspreg.divout >> 16;
}

static void eval_VRCPL(u32 instr) {
    debugger.halt("RSP::VRCPL unsupported");
}

static void eval_VRNDN(u32 instr) {
    debugger.halt("RSP::VRNDN unsupported");
}

static void eval_VRNDP(u32 instr) {
    debugger.halt("RSP::VRNDP unsupported");
}

static void eval_VRSQ(u32 instr) {
    debugger.halt("RSP::VRSQ unsupported");
}

static void eval_VRSQH(u32 instr) {
    debugger.halt("RSP::VRSQH unsupported");
}

static void eval_VRSQL(u32 instr) {
    debugger.halt("RSP::VRSQL unsupported");
}

static void eval_VSAR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    // u32 vs = getVs(instr);
    u32 vd = getVd(instr);

    // According to the reference specification, VSAR both reads _and_ writes
    // selected slices of the accumulator, following the value of e (0, 1 or 2).
    // However some tests point at a different behaviour:
    //  - using e=0,1,2 does not read or modify the accumulator but returns 0
    //  - using e=8,9,10 reads the accumulator but does not write any value

    for (unsigned i = 0; i < 8; i++) {
        switch (e) {
            case 0:
            case 1:
            case 2:
                state.rspreg.vr[vd].h[i] = 0;
                break;
            case 8:
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc[i] >> 32;
                // state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 32);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i] << 32;
                break;
            case 9:
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc[i] >> 16;
                // state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 16);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i] << 16;
                break;
            case 10:
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc[i];
                // state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i];
                break;
        }
    }
}

static void eval_VSUB(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] -
                  (i16)state.rspreg.vr[vt].h[j] -
                  ((state.rspreg.vco >> i) & UINT16_C(1));

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)(u32)res;
        out.h[i] = (u16)clamp<i16, i32>(res);
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VSUBC(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] -
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= (u16)res;
        out.h[i] = res;

        if ((res >> 16) & UINT32_C(1)) { /* res < 0 */
            state.rspreg.vco |= UINT16_C(1) << i;
            state.rspreg.vco |= UINT16_C(1) << (i + 8);
        } else if (res) {
            state.rspreg.vco |= UINT16_C(1) << (i + 8);
        }
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VXOR(u32 instr) {
    u32 e = (instr >> 21) & UINT32_C(0xf);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] ^
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void (*COP2_callbacks[64])(u32) = {
    /* Multiply group */
    eval_VMULF,         eval_VMULU,         eval_VRNDP,         eval_VMULQ,
    eval_VMUDL,         eval_VMUDM,         eval_VMUDN,         eval_VMUDH,
    eval_VMACF,         eval_VMACU,         eval_VRNDN,         eval_VMACQ,
    eval_VMADL,         eval_VMADM,         eval_VMADN,         eval_VMADH,

    /* Add group */
    eval_VADD,          eval_VSUB,          eval_Reserved,      eval_VABS,
    eval_VADDC,         eval_VSUBC,         eval_Reserved,      eval_Reserved,
    eval_Reserved,      eval_Reserved,      eval_Reserved,      eval_Reserved,
    eval_Reserved,      eval_VSAR,          eval_Reserved,      eval_Reserved,

    /* Select group */
    eval_VLT,           eval_VEQ,           eval_VNE,           eval_VGE,
    eval_VCL,           eval_VCH,           eval_VCR,           eval_VMRG,

    /* Logical group */
    eval_VAND,          eval_VNAND,         eval_VOR,           eval_VNOR,
    eval_VXOR,          eval_VNXOR,         eval_Reserved,      eval_Reserved,

    /* Divide group */
    eval_VRCP,          eval_VRCPL,         eval_VRCPH,         eval_VMOV,
    eval_VRSQ,          eval_VRSQL,         eval_VRSQH,         eval_VNOP,

    /* Invalid group */
    eval_Reserved,      eval_Reserved,      eval_Reserved,      eval_Reserved,
    eval_Reserved,      eval_Reserved,      eval_Reserved,      eval_Reserved,
};

static void eval_COP2(u32 instr) {
    COP2_callbacks[instr & UINT32_C(0x3f)](instr);
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
                    storeVectorBytesAt(rd, e, (u8 *)&val, 2);
                    val = __builtin_bswap16(val);
                    state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
                })
                RType(MT, instr, {
                    u32 e = (instr >> 7) & UINT32_C(0xf);
                    u16 val = __builtin_bswap16((u16)state.rspreg.gpr[rt]);
                    loadVectorBytesAt(rd, e, (u8 *)&val, 2);
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

}; /* namespace RSP */
}; /* namespace R4300 */
