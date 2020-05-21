
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

const char *Cop0RegisterNames[32] = {
    "dma_cache",    "dma_dram",     "dma_rd_len",   "dma_wr_len",
    "sp_status",    "dma_full",     "dma_busy",     "sp_reserved",
    "cmd_start",    "cmd_end",      "cmd_current",  "cmd_status",
    "cmd_clock",    "cmd_busy",     "cmd_pipe_busy","cmd_tmem_busy",
    "$16",          "$17",          "$18",          "$19",
    "$20",          "$21",          "$22",          "$23",
    "$24",          "$25",          "$26",          "$27",
    "$28",          "$29",          "$30",          "$31",
};

/**
 * Check whether a virtual memory address is correctly aligned for a memory
 * access. The RSP does not implement exceptions but the alignment is checked
 * for the sake of catching suspicious states, for debugging purposes.
 *
 * @param addr          Checked DMEM/IMEM address.
 * @param bytes         Required alignment, must be a power of two.
 */
static inline bool checkAddressAlignment(u64 addr, u64 bytes) {
    if ((addr & (bytes - 1u)) != 0) {
        debugger::warn(Debugger::RSP,
            "detected unaligned DMEM/IMEM access of {} bytes"
            " from address {:08x}, at pc {:08x}",
            bytes, addr, state.rspreg.pc);
        debugger::halt("RSP invalid address alignment");
        return false;
    } else {
        return true;
    }
}

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

static inline u32 getElement(u32 instr) {
     return (instr >> 21) & 0xfu;
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
            debugger::halt("RSP::LPV not supported");
            break;
        case UINT32_C(0x7): /* LUV */
            debugger::halt("RSP::LUV not supported");
            break;
        case UINT32_C(0x8): /* LHV */
            debugger::halt("RSP::LHV not supported");
            break;
        case UINT32_C(0x9): /* LFV */
            debugger::halt("RSP::LFV not supported");
            break;
        case UINT32_C(0xa): eval_LWV(instr); break;
        case UINT32_C(0xb): eval_LTV(instr); break;
        default:
            debugger::halt("RSP::LWC2 invalid operation");
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
            debugger::halt("RSP::SPV not supported");
            break;
        case UINT32_C(0x7): eval_SUV(instr); break;
        case UINT32_C(0x8): /* SHV */
            debugger::halt("RSP::SHV not supported");
            break;
        case UINT32_C(0x9): /* SFV */
            debugger::halt("RSP::SFV not supported");
            break;
        case UINT32_C(0xa): eval_SWV(instr); break;
        case UINT32_C(0xb): eval_STV(instr); break;
        default:
            debugger::halt("RSP::SWC2 invalid operation");
            break;
    }
}

static inline unsigned selectElementIndex(unsigned i, u32 e) {
    unsigned j = 0;
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
    debugger::halt("RSP reserved instruction");
}

static void eval_VABS(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;
    state.rspreg.vco = 0;
    state.rspreg.vce = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 s = state.rspreg.vr[vs].h[i];
        u16 t = state.rspreg.vr[vt].h[j];
        u16 tmp;
        u16 di;

        bool sign = (i16)(u16)(s ^ t) < 0;
        bool ge;
        bool le;
        bool vce;

        if (sign) {
            tmp = s + t;
            ge = (i16)t < 0;
            le = (i16)tmp <= 0;
            vce = (i16)tmp == -1;
            di = le ? -t : s;
        } else {
            tmp = s - t;
            le = (i16)t < 0;
            ge = (i16)tmp >= 0;
            vce = 0;
            di = ge ? t : s;
        }

        bool neq = tmp != 0;
        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= di;
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        state.rspreg.vco |= (u16)neq << (i + 8);
        state.rspreg.vco |= (u16)sign << i;
        state.rspreg.vce |= (u8)vce << i;
        out.h[i] = di;
    }

    state.rspreg.vr[vd] = out;
}

/// XXX TODO
/// failure for N64/RSPTest/CP2/VCL/RSPCP2VCL.N64
static void eval_VCL(u32 instr) {
    u32 e = getElement(instr);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 s = state.rspreg.vr[vs].h[i];
        u16 t = state.rspreg.vr[vt].h[j];
        u32 tmp;
        u16 di;

        bool neq = (state.rspreg.vco >> (i + 8)) & 1;
        bool sign = (state.rspreg.vco >> i) & 1;
        bool ge = (state.rspreg.vcc >> (i + 8)) & 1;
        bool le = (state.rspreg.vcc >> i) & 1;
        bool vce = (state.rspreg.vce >> i) & 1;

        if (sign) {
            tmp = (u32)s + (u32)t;
            bool carry = tmp > 0xffffu;
            if (!neq) {
                le = (!vce && (tmp & 0xffffu) == 0 && !carry) ||
                     (vce && ((tmp & 0xffffu) == 0 || !carry));
            }
            di = le ? -t : s;
        } else {
            tmp = (u32)s - (u32)t;
            if (!neq) {
                ge = (i32)tmp >= 0;
            }
            di = ge ? t : s;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= di;
        state.rspreg.vcc &= ~(0x101u << i);
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        out.h[i] = di;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

static void eval_VCR(u32 instr) {
    u32 e = getElement(instr);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;
    state.rspreg.vco = 0;
    state.rspreg.vce = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u16 s = state.rspreg.vr[vs].h[i];
        u16 t = state.rspreg.vr[vt].h[j];
        u16 tmp;
        u16 di;

        bool sign = (i16)(u16)(s ^ t) < 0;
        bool ge;
        bool le;

        if (sign) {
            tmp = s + t + 1;
            ge = (i16)t < 0;
            le = (i16)tmp <= 0;
            di = le ? ~t : s;
        } else {
            tmp = s - t;
            le = (i16)t < 0;
            ge = (i16)tmp >= 0;
            di = ge ? t : s;
        }

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= di;
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        out.h[i] = di;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VEQ(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    debugger::halt("VMACQ unsupported");
}

/// XXX TODO
/// failure for N64/RSPTest/CP2/VMACF/RSPCP2VMACF.N64
static void eval_VMACU(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
    u32 vt = getVt(instr);
    u32 vs = getVs(instr);
    u32 vd = getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        bool vcci = (state.rspreg.vcc >> i) & 1;
        u16 res = vcci ? state.rspreg.vr[vs].h[i]
                       : state.rspreg.vr[vt].h[j];

        state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
        state.rspreg.vacc[i] |= res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static void eval_VMUDH(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    debugger::halt("RSP::VMULQ unsupported");
}

static void eval_VMULU(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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

static void eval_VNULL(u32 instr) {
    (void)instr;
}

static void eval_VNOR(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    debugger::halt("RSP::VRCPL unsupported");
}

static void eval_VRNDN(u32 instr) {
    debugger::halt("RSP::VRNDN unsupported");
}

static void eval_VRNDP(u32 instr) {
    debugger::halt("RSP::VRNDP unsupported");
}

static void eval_VRSQ(u32 instr) {
    debugger::halt("RSP::VRSQ unsupported");
}

static void eval_VRSQH(u32 instr) {
    debugger::halt("RSP::VRSQH unsupported");
}

static void eval_VRSQL(u32 instr) {
    debugger::halt("RSP::VRSQL unsupported");
}

static void eval_VSAR(u32 instr) {
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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
    u32 e = getElement(instr);
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

/* SPECIAL opcodes */

void eval_ADD(u32 instr) {
    RType(instr);
    u32 res = state.rspreg.gpr[rs] + state.rspreg.gpr[rt];
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_ADDU(u32 instr) {
    RType(instr);
    u32 res = state.rspreg.gpr[rs] + state.rspreg.gpr[rt];
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_AND(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] & state.rspreg.gpr[rt];
}

void eval_BREAK(u32 instr) {
    if (state.hwreg.SP_STATUS_REG & SP_STATUS_INTR_BREAK) {
        set_MI_INTR_REG(MI_INTR_SP);
    }
    state.hwreg.SP_STATUS_REG |= SP_STATUS_BROKE | SP_STATUS_HALT;
}

void eval_JALR(u32 instr) {
    RType(instr);
    u64 tg = state.rspreg.gpr[rs];
    state.rspreg.gpr[rd] = state.rspreg.pc + 8;
    state.rsp.nextAction = State::Action::Delay;
    state.rsp.nextPc = tg;
}

void eval_JR(u32 instr) {
    RType(instr);
    u64 tg = state.rspreg.gpr[rs];
    state.rsp.nextAction = State::Action::Delay;
    state.rsp.nextPc = tg;
}

void eval_MOVN(u32 instr) {
    RType(instr);
    debugger::halt("MOVN");
}

void eval_MOVZ(u32 instr) {
    RType(instr);
    debugger::halt("MOVZ");
}

void eval_NOR(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = ~(state.rspreg.gpr[rs] | state.rspreg.gpr[rt]);
}

void eval_OR(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] | state.rspreg.gpr[rt];
}

void eval_SLL(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(state.rspreg.gpr[rt] << shamnt);
}

void eval_SLLV(u32 instr) {
    RType(instr);
    shamnt = state.rspreg.gpr[rs] & 0x1flu;
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(state.rspreg.gpr[rt] << shamnt);
}

void eval_SLT(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = (i64)state.rspreg.gpr[rs] < (i64)state.rspreg.gpr[rt];
}

void eval_SLTU(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] < state.rspreg.gpr[rt];
}

void eval_SRA(u32 instr) {
    RType(instr);
    bool sign = (state.rspreg.gpr[rt] & (1lu << 31)) != 0;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.rspreg.gpr[rd] = state.rspreg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (1ul << (shamnt + 32)) - 1u;
        state.rspreg.gpr[rd] |= mask << (32 - shamnt);
    }
}

void eval_SRAV(u32 instr) {
    RType(instr);
    bool sign = (state.rspreg.gpr[rt] & (1lu << 31)) != 0;
    shamnt = state.rspreg.gpr[rs] & 0x1flu;
    // Right shift is logical for unsigned c types,
    // we need to add the type manually.
    state.rspreg.gpr[rd] = state.rspreg.gpr[rt] >> shamnt;
    if (sign) {
        u64 mask = (1ul << (shamnt + 32)) - 1u;
        state.rspreg.gpr[rd] |= mask << (32 - shamnt);
    }
}

void eval_SRL(u32 instr) {
    RType(instr);
    u64 res = (state.rspreg.gpr[rt] & 0xffffffffllu) >> shamnt;
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_SRLV(u32 instr) {
    RType(instr);
    shamnt = state.rspreg.gpr[rs] & 0x1flu;
    u64 res = (state.rspreg.gpr[rt] & 0xfffffffflu) >> shamnt;
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(res);
}

void eval_SUB(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
        state.rspreg.gpr[rs] - state.rspreg.gpr[rt]);
}

void eval_SUBU(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = sign_extend<u64, u32>(
        state.rspreg.gpr[rs] - state.rspreg.gpr[rt]);
}

void eval_XOR(u32 instr) {
    RType(instr);
    state.rspreg.gpr[rd] = state.rspreg.gpr[rs] ^ state.rspreg.gpr[rt];
}

/* REGIMM opcodes */

void eval_BGEZ(u32 instr) {
    IType(instr, sign_extend);
    if ((i64)state.rspreg.gpr[rs] >= 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLTZ(u32 instr) {
    IType(instr, sign_extend);
    if ((i64)state.rspreg.gpr[rs] < 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BGEZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.rspreg.gpr[rs];
    state.rspreg.gpr[31] = state.rspreg.pc + 8;
    if (r >= 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLTZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.rspreg.gpr[rs];
    state.rspreg.gpr[31] = state.rspreg.pc + 8;
    if (r < 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

/* Other opcodes */

void eval_ADDI(u32 instr) {
    IType(instr, sign_extend);
    state.rspreg.gpr[rt] = sign_extend<u64, u32>(state.rspreg.gpr[rs] + imm);
}

void eval_ADDIU(u32 instr) {
    IType(instr, sign_extend);
    state.rspreg.gpr[rt] = sign_extend<u64, u32>(state.rspreg.gpr[rs] + imm);
}

void eval_ANDI(u32 instr) {
    IType(instr, zero_extend);
    state.rspreg.gpr[rt] = state.rspreg.gpr[rs] & imm;
}

void eval_BEQ(u32 instr) {
    IType(instr, sign_extend);
    if (state.rspreg.gpr[rt] == state.rspreg.gpr[rs]) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BGTZ(u32 instr) {
    IType(instr, sign_extend);
    if ((i64)state.rspreg.gpr[rs] > 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BLEZ(u32 instr) {
    IType(instr, sign_extend);
    if ((i64)state.rspreg.gpr[rs] <= 0) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_BNE(u32 instr) {
    IType(instr, sign_extend);
    if (state.rspreg.gpr[rt] != state.rspreg.gpr[rs]) {
        state.rsp.nextAction = State::Action::Delay;
        state.rsp.nextPc = state.rspreg.pc + 4 + (i64)(imm << 2);
    }
}

void eval_CACHE(u32 instr) {
    (void)instr;
}

void eval_MFC0(u32 instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 val;

    switch (rd) {
    case 0:     val = state.hwreg.SP_MEM_ADDR_REG; break;
    case 1:     val = state.hwreg.SP_DRAM_ADDR_REG; break;
    case 2:     val = state.hwreg.SP_RD_LEN_REG; break;
    case 3:     val = state.hwreg.SP_WR_LEN_REG; break;
    case 4:     val = state.hwreg.SP_STATUS_REG; break;
    case 5:     val = (state.hwreg.SP_STATUS_REG & SP_STATUS_DMA_FULL) != 0; break;
    case 6:     val = (state.hwreg.SP_STATUS_REG & SP_STATUS_DMA_BUSY) != 0; break;
    case 7:     val = read_SP_SEMAPHORE_REG(); break;
    case 8:     val = state.hwreg.DPC_START_REG; break;
    case 9:     val = state.hwreg.DPC_END_REG; break;
    case 10:    val = state.hwreg.DPC_CURRENT_REG; break;
    case 11:    val = state.hwreg.DPC_STATUS_REG; break;
    case 12:
        debugger::halt("DPC_CLOCK_REG read access");
        val = state.hwreg.DPC_CLOCK_REG;
        break;
    case 13:
        debugger::halt("DPC_BUF_BUSY_REG read access");
        val = state.hwreg.DPC_BUF_BUSY_REG;
        break;
    case 14:
        debugger::halt("DPC_PIPE_BUSY_REG read access");
        val = state.hwreg.DPC_PIPE_BUSY_REG;
        break;
    case 15:
        debugger::halt("DPC_TMEM_REG read access");
        val = state.hwreg.DPC_TMEM_REG;
        break;
    default:
        /* unknown register access */
        val = 0;
        std::string reason = "MFC0 ";
        debugger::halt(reason + Cop0RegisterNames[rd]);
        break;
    }

    debugger::info(Debugger::RSP, "{} -> {:08x}", Cop0RegisterNames[rd], val);
    state.rspreg.gpr[rt] = sign_extend<u64, u32>(val);
}

void eval_MTC0(u32 instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 val = state.rspreg.gpr[rt];

    debugger::info(Debugger::RSP, "{} <- {:08x}", Cop0RegisterNames[rd], val);

    switch (rd) {
    case 0:     state.hwreg.SP_MEM_ADDR_REG = val; break;
    case 1:     state.hwreg.SP_DRAM_ADDR_REG = val & 0xfffffflu; break;
    case 2:     write_SP_RD_LEN_REG(val); break;
    case 3:     write_SP_WR_LEN_REG(val); break;
    case 4:     write_SP_STATUS_REG(val); break;
    case 5:     /* DMA_FULL, read only */ break;
    case 6:     /* DMA_BUSY, read only */ break;
    case 7:     state.hwreg.SP_SEMAPHORE_REG = 0; break;
    case 8:     write_DPC_START_REG(val); break;
    case 9:     write_DPC_END_REG(val); break;
    case 10:
        debugger::halt("RSP::RDP_command_current");
        break;
    case 11:    write_DPC_STATUS_REG(val); break;
    case 12:
        debugger::halt("RSP::RDP_clock_counter");
        break;
    case 13:
        debugger::halt("RSP::RDP_command_busy");
        break;
    case 14:
        debugger::halt("RSP::RDP_pipe_busy_counter");
        break;
    case 15:
        debugger::halt("RSP::RDP_TMEM_load_counter");
        break;
    default:
        std::string reason = "MTC0 ";
        debugger::halt(reason + Cop0RegisterNames[rd]);
        break;
    }
}

void eval_COP0(u32 instr) {
    switch (Mips::getRs(instr)) {
    case Mips::Copz::MF: eval_MFC0(instr); break;
    case Mips::Copz::MT: eval_MTC0(instr); break;
    default:
        debugger::halt("invalid RSP::COP0 instruction");
        break;
    }
}

void eval_MFC2(u32 instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 e = (instr >> 7) & 0xfu;
    u16 val;
    storeVectorBytesAt(rd, e, (u8 *)&val, 2);
    val = __builtin_bswap16(val);
    state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
}

void eval_MTC2(u32 instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 e = (instr >> 7) & 0xfu;
    u16 val = __builtin_bswap16((u16)state.rspreg.gpr[rt]);
    loadVectorBytesAt(rd, e, (u8 *)&val, 2);
}

void eval_CFC2(u32 instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 val = 0;

    switch (rd) {
    case 0: val = state.rspreg.vco; break;
    case 1: val = state.rspreg.vcc; break;
    case 2: val = state.rspreg.vce; break;
    }
    state.rspreg.gpr[rt] = val;
}

void eval_CTC2(u32 instr) {
    debugger::halt("RSP::CTC2 unsupported");
}

void eval_J(u32 instr) {
    u64 tg = Mips::getTarget(instr);
    tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.rsp.nextAction = State::Action::Delay;
    state.rsp.nextPc = tg;
}

void eval_JAL(u32 instr) {
    u64 tg = Mips::getTarget(instr);
    tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.rspreg.gpr[31] = state.rspreg.pc + 8;
    state.rsp.nextAction = State::Action::Delay;
    state.rsp.nextPc = tg;
}

void eval_LB(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;
    u64 val = state.dmem[addr & 0xfffu];
    state.rspreg.gpr[rt] = sign_extend<u64, u8>(val);
}

void eval_LBU(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;
    u64 val = state.dmem[addr & 0xfffu];
    state.rspreg.gpr[rt] = zero_extend<u64, u8>(val);
}

void eval_LH(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;

    if (checkAddressAlignment(addr, 2)) {
        u64 val = __builtin_bswap16(*(u16 *)&state.dmem[addr & 0xfffu]);
        state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
    }
}

void eval_LHU(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;

    if ((addr & 0x1u) == 0) { // checkAddressAlignment(addr, 2)) {
        u64 val = __builtin_bswap16(*(u16 *)&state.dmem[addr & 0xfffu]);
        state.rspreg.gpr[rt] = zero_extend<u64, u16>(val);
    } else {
        u64 val =
            ((u16)state.dmem[addr & 0xfffu] << 8) |
            (u16)state.dmem[(addr + 1u) & 0xfffu];
        state.rspreg.gpr[rt] = zero_extend<u64, u16>(val);
    }
}

void eval_LUI(u32 instr) {
    IType(instr, sign_extend);
    state.rspreg.gpr[rt] = imm << 16;
}

void eval_LW(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;

    if ((addr & 0x3u) == 0) { //checkAddressAlignment(addr, 4)) {
        u64 val = __builtin_bswap32(*(u32 *)&state.dmem[addr & 0xfffu]);
        state.rspreg.gpr[rt] = sign_extend<u64, u32>(val);
    } else {
        u32 val =
            ((u32)state.dmem[(addr + 0u) & 0xfffu] << 24) |
            ((u32)state.dmem[(addr + 1u) & 0xfffu] << 16) |
            ((u32)state.dmem[(addr + 2u) & 0xfffu] << 8) |
             (u32)state.dmem[(addr + 3u) & 0xfffu];
        state.rspreg.gpr[rt] = sign_extend<u64, u32>(val);
    }
}

void eval_ORI(u32 instr) {
    IType(instr, zero_extend);
    state.rspreg.gpr[rt] = state.rspreg.gpr[rs] | imm;
}

void eval_SB(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;
    state.dmem[addr & 0xfffu] = state.rspreg.gpr[rt];
}

void eval_SH(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;

    if (checkAddressAlignment(addr, 2)) {
        *(u16 *)&state.dmem[addr & 0xfffu] =
            __builtin_bswap16(state.rspreg.gpr[rt]);
    }
}

void eval_SLTI(u32 instr) {
    IType(instr, sign_extend);
    state.rspreg.gpr[rt] = (i64)state.rspreg.gpr[rs] < (i64)imm;
}

void eval_SLTIU(u32 instr) {
    IType(instr, sign_extend);
    state.rspreg.gpr[rt] = state.rspreg.gpr[rs] < imm;
}

void eval_SW(u32 instr) {
    IType(instr, sign_extend);
    u64 addr = state.rspreg.gpr[rs] + imm;

    if ((addr & 0x3u) == 0) { // checkAddressAlignment(addr, 4)) {
        *(u32 *)&state.dmem[addr & 0xfffu] =
            __builtin_bswap32(state.rspreg.gpr[rt]);
    } else {
        u32 val = state.rspreg.gpr[rt];
        state.dmem[(addr + 0u) & 0xfffu] = val >> 24;
        state.dmem[(addr + 1u) & 0xfffu] = val >> 16;
        state.dmem[(addr + 2u) & 0xfffu] = val >> 8;
        state.dmem[(addr + 3u) & 0xfffu] = val;
    }
}

void eval_XORI(u32 instr) {
    IType(instr, zero_extend);
    state.rspreg.gpr[rt] = state.rspreg.gpr[rs] ^ imm;
}


void (*COP2_callbacks[64])(u32) = {
    /* Multiply group */
    eval_VMULF,     eval_VMULU,     eval_VRNDP,     eval_VMULQ,
    eval_VMUDL,     eval_VMUDM,     eval_VMUDN,     eval_VMUDH,
    eval_VMACF,     eval_VMACU,     eval_VRNDN,     eval_VMACQ,
    eval_VMADL,     eval_VMADM,     eval_VMADN,     eval_VMADH,
    /* Add group */
    eval_VADD,      eval_VSUB,      eval_Reserved,  eval_VABS,
    eval_VADDC,     eval_VSUBC,     eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_VSAR,      eval_Reserved,  eval_Reserved,
    /* Select group */
    eval_VLT,       eval_VEQ,       eval_VNE,       eval_VGE,
    eval_VCL,       eval_VCH,       eval_VCR,       eval_VMRG,
    /* Logical group */
    eval_VAND,      eval_VNAND,     eval_VOR,       eval_VNOR,
    eval_VXOR,      eval_VNXOR,     eval_Reserved,  eval_Reserved,
    /* Divide group */
    eval_VRCP,      eval_VRCPL,     eval_VRCPH,     eval_VMOV,
    eval_VRSQ,      eval_VRSQL,     eval_VRSQH,     eval_VNOP,
    /* Invalid group */
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_VNULL,
};

void eval_COP2(u32 instr) {
    switch (Mips::getRs(instr)) {
    case Mips::Copz::MF: eval_MFC2(instr); break;
    case Mips::Copz::MT: eval_MTC2(instr); break;
    case Mips::Copz::CF: eval_CFC2(instr); break;
    case Mips::Copz::CT: eval_CTC2(instr); break;
    default:
        if ((instr & (1lu << 25)) == 0) {
            debugger::halt("RSP::COP2 invalid operation");
        } else {
            COP2_callbacks[instr & UINT32_C(0x3f)](instr);
        }
        break;
    }
}

void (*SPECIAL_callbacks[64])(u32) = {
    eval_SLL,       eval_Reserved,  eval_SRL,       eval_SRA,
    eval_SLLV,      eval_Reserved,  eval_SRLV,      eval_SRAV,
    eval_JR,        eval_JALR,      eval_MOVZ,      eval_MOVN,
    eval_Reserved,  eval_BREAK,     eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_ADD,       eval_ADDU,      eval_SUB,       eval_SUBU,
    eval_AND,       eval_OR,        eval_XOR,       eval_NOR,
    eval_Reserved,  eval_Reserved,  eval_SLT,       eval_SLTU,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

void eval_SPECIAL(u32 instr) {
    SPECIAL_callbacks[Mips::getFunct(instr)](instr);
}

void (*REGIMM_callbacks[32])(u32) = {
    eval_BLTZ,      eval_BGEZ,      eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_BLTZAL,    eval_BGEZAL,    eval_Reserved,  eval_Reserved,
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
    eval_COP0,      eval_Reserved,  eval_COP2,      eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_LB,        eval_LH,        eval_Reserved,  eval_LW,
    eval_LBU,       eval_LHU,       eval_Reserved,  eval_Reserved,
    eval_SB,        eval_SH,        eval_Reserved,  eval_SW,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_CACHE,
    eval_Reserved,  eval_Reserved,  eval_LWC2,      eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_SWC2,      eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 *
 * @param addr         Virtual address of the instruction to execute.
 * @param delaySlot     Whether the instruction executed is in a
 *                      branch delay slot.
 */
static void eval(void)
{
    u64 addr = state.rspreg.pc;
    u64 instr;

    checkAddressAlignment(addr, 4);
    instr = __builtin_bswap32(*(u32 *)&state.imem[addr & 0xfffu]);

    debugger::debugger.rspTrace.put(Debugger::TraceEntry(addr, instr));

    // The null instruction is 'sll r0, r0, 0', i.e. a NOP.
    // It is one of the most used instructions (to fill in delay slots).
    if (instr) {
        CPU_callbacks[Mips::getOpcode(instr)](instr);
    }
}

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
void step()
{
    // Nothing done if RSP is halted.
    if (state.hwreg.SP_STATUS_REG & SP_STATUS_HALT)
        return;

    switch (state.rsp.nextAction) {
        case State::Action::Continue:
            state.rspreg.pc += 4;
            eval();
            break;

        case State::Action::Delay:
            state.rspreg.pc += 4;
            state.rsp.nextAction = State::Action::Jump;
            eval();
            break;

        case State::Action::Jump:
            state.rspreg.pc = state.rsp.nextPc;
            state.rsp.nextAction = State::Action::Continue;
            eval();
            break;
    }
}

}; /* namespace RSP */
}; /* namespace R4300 */
