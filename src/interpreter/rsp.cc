
#include <cstring>
#include <iomanip>
#include <iostream>

#include <core.h>
#include <circular_buffer.h>
#include <assembly/disassembler.h>
#include <assembly/opcodes.h>
#include <assembly/registers.h>
#include <r4300/cpu.h>
#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <debugger.h>
#include <interpreter.h>
#include <types.h>

using namespace R4300;
using namespace n64;

namespace interpreter::rsp {

void eval_Reserved(u32 instr) {
    core::halt("RSP reserved instruction");
}

/*
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
        core::halt("RSP invalid address alignment");
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


static void loadVectorBytesAt(unsigned vr, unsigned element, u8 *addr,
                              unsigned count) {
    for (unsigned i = 0; i < count && element < 16; i++, element++, addr++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        state.rspreg.vr[vr].b[element ^ le] = *addr;
    }
}

static void loadVectorBytes(unsigned vr, unsigned element, unsigned addr,
                            unsigned count) {
    for (unsigned i = 0; i < count && element < 16; i++, element++, addr++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        state.rspreg.vr[vr].b[element ^ le] = state.dmem[addr & 0xfffu];
    }
}

void eval_LTV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at this base address.
    // The load starts at the element slice offset, and wraps to the
    // start address.
    addr = addr + (offset << 4);
    unsigned slice = element;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned vs = (vt & 0x18u) | ((i + (element >> 1)) & 0x7u);

        state.rspreg.vr[vs].b[(2 * i) ^ le] =
            state.dmem[(addr + (slice % 16)) & 0xfffu];
        slice++;
        state.rspreg.vr[vs].b[(2 * i + 1) ^ le] =
            state.dmem[(addr + (slice % 16)) & 0xfffu];
        slice++;
    }
}

void eval_LWV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base] + (offset << 4);

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (i + (element >> 1)) & 0x7u;
        loadVectorBytes(vt, 2 * slice, addr + 2 * slice, 2);
    }
}

void eval_LRV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("LRV with non-zero element");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address aligned
    // to 16byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff0u;
    u32 addr_offset  = addr & 0xfu;

    loadVectorBytes(vt, element + 16 - addr_offset,
                    addr_aligned, addr_offset);
}

void eval_LPV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("LPV with non-zero element");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address
    // aligned to 8byte boundary.
    addr = addr + (offset << 3);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (addr_offset + i - element) % 16;
        state.rspreg.vr[vt].h[i] =
            (u16)state.dmem[(addr_aligned + slice) & 0xfffu] << 8;
    }
}

void eval_LUV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("LUV with non-zero element");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address
    // aligned to 8byte boundary.
    addr = addr + (offset << 3);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (addr_offset + i - element) % 16;
        state.rspreg.vr[vt].h[i] =
            (u16)state.dmem[(addr_aligned + slice) & 0xfffu] << 7;
    }
}

void eval_LHV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("LHV with non-zero element");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address
    // aligned to 8byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 8; i++) {
        unsigned slice = (addr_offset + 2 * i - element) % 16;
        state.rspreg.vr[vt].h[i] =
            (u16)state.dmem[(addr_aligned + slice) & 0xfffu] << 7;
    }
}

void eval_LFV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("LFV with non-zero element");

    core::halt("LFV TODO");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address
    // aligned to 8byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 4; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned slice = (addr_offset + 4 * i - element) % 16;
        unsigned index_hi = (element + 2 * i) % 16;
        unsigned index_lo = (element + 2 * i + 1) % 16;

        u16 val = (u16)state.dmem[(addr_aligned + slice) & 0xfffu] << 7;
        state.rspreg.vr[vt].b[index_hi ^ le] = val >> 8;
        state.rspreg.vr[vt].b[index_lo ^ le] = val;
    }
}

void eval_LWC2(u32 instr) {
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
        case UINT32_C(0x5): eval_LRV(instr); break;
        case UINT32_C(0x6): eval_LPV(instr); break;
        case UINT32_C(0x7): eval_LUV(instr); break;
        case UINT32_C(0x8): eval_LHV(instr); break;
        case UINT32_C(0x9): eval_LFV(instr); break;
        case UINT32_C(0xa): eval_LWV(instr); break;
        case UINT32_C(0xb): eval_LTV(instr); break;
        default:
            core::halt("RSP::LWC2 invalid operation");
            break;
    }
}

static void storeVectorBytesAt(unsigned vr, unsigned element, u8 *addr,
                               unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index = element % 16;
        *addr = state.rspreg.vr[vr].b[index ^ le];
    }
}

static void storeVectorBytes(unsigned vr, unsigned element, unsigned addr,
                             unsigned count) {
    for (unsigned i = 0; i < count; i++, element++, addr++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index = element % 16;
        state.dmem[addr & 0xfffu] = state.rspreg.vr[vr].b[index ^ le];
    }
}

void eval_SRV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("SRV with non-zero element");

    // Compose base address with offset. The bytes are loaded
    // from the 16byte window starting at the base address aligned
    // to 16byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff0u;
    u32 addr_offset  = addr & 0xfu;

    storeVectorBytes(vt, element + 16 - addr_offset,
                     addr_aligned, addr_offset);
}

void eval_SPV_SUV(u32 instr, bool suv) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0) {
        debugger::undefined(
            suv ? "SUV with non-zero element"
                : "SPV with non-zero element");
    }

    // Compose base address with offset.
    addr = addr + (offset << 3);

    // This instruction is implemented conjointly with LUV;
    // when using the byte element 8 the SUV behaviour
    // with element 0 is observed instead.
    if (suv) element += 8;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index = (element + i) % 16;
        unsigned index_hi = (2 * index) % 16;
        unsigned index_lo = (2 * index + 1) % 16;
        unsigned rshift = index >= 8 ? 7 : 8;

        u16 val = ((u16)state.rspreg.vr[vt].b[index_hi ^ le] << 8) |
                   (u16)state.rspreg.vr[vt].b[index_lo ^ le];

        state.dmem[(addr + i) & 0xfffu] = val >> rshift;
    }
}

void eval_SPV(u32 instr) {
    eval_SPV_SUV(instr, false);
}

void eval_SUV(u32 instr) {
    eval_SPV_SUV(instr, true);
}

void eval_SHV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("SHV with non-zero element");

    // Compose base address with offset. The bytes are stored
    // into the 16byte window starting at the base address aligned
    // to 16byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index_hi = (element + 2 * i) % 16;
        unsigned index_lo = (element + 2 * i + 1) % 16;
        unsigned slice = (addr_offset + 2 * i) % 16;

        u16 val = ((u16)state.rspreg.vr[vt].b[index_hi ^ le] << 8) |
                   (u16)state.rspreg.vr[vt].b[index_lo ^ le];

        state.dmem[(addr_aligned + slice) & 0xfffu] = val >> 7;
    }
}

void eval_SFV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    if (element != 0)
        debugger::undefined("SFV with non-zero element");

    core::halt("SFV TODO");

    // Compose base address with offset. The bytes are stored
    // into the 16byte window starting at the base address aligned
    // to 16byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index_hi = (element + 2 * i) % 16;
        unsigned index_lo = (element + 2 * i + 1) % 16;
        unsigned slice = (addr_offset + 4 * i) % 16;

        u16 val = ((u16)state.rspreg.vr[vt].b[index_hi ^ le] << 8) |
                   (u16)state.rspreg.vr[vt].b[index_lo ^ le];

        state.dmem[(addr_aligned + slice) & 0xfffu] = val >> 7;
    }
}

void eval_STV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    // Compose base address with offset. The bytes are stored
    // into the 16byte window starting at the base address aligned
    // to 8byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;
    unsigned slice = addr_offset;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned vs = (vt & 0x18u) | ((i + (element >> 1)) & 0x7u);

        state.dmem[(addr_aligned + (slice % 16)) & 0xfffu] =
            state.rspreg.vr[vs].b[(2 * i) ^ le];
        slice++;
        state.dmem[(addr_aligned + (slice % 16)) & 0xfffu] =
            state.rspreg.vr[vs].b[(2 * i + 1) ^ le];
        slice++;
    }
}

void eval_SWV(u32 instr) {
    u32 base = (instr >> 21) & 0x1flu;
    u32 vt = (instr >> 16) & 0x1flu;
    u32 element = (instr >> 7) & 0xflu;
    u32 offset = instr & 0x7flu;
    u32 addr = state.rspreg.gpr[base];

    // Compose base address with offset. The bytes are stored
    // into the 16byte window starting at the base address aligned
    // to 8byte boundary.
    addr = addr + (offset << 4);
    u32 addr_aligned = addr & 0xff8u;
    u32 addr_offset  = addr & 0x7u;
    unsigned slice = addr_offset;

    for (unsigned i = 0; i < 8; i++) {
        unsigned le = __BYTE_ORDER__ == __LITTLE_ENDIAN;
        unsigned index_hi = (2 * i + element) % 16;
        unsigned index_lo = (2 * i + 1 + element) % 16;

        state.dmem[(addr_aligned + (slice % 16)) & 0xfffu] =
            state.rspreg.vr[vt].b[index_hi ^ le];
        slice++;
        state.dmem[(addr_aligned + (slice % 16)) & 0xfffu] =
            state.rspreg.vr[vt].b[index_lo ^ le];
        slice++;
    }
}

void eval_SWC2(u32 instr) {
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
        case UINT32_C(0x5): eval_SRV(instr); break;
        case UINT32_C(0x6): eval_SPV(instr); break;
        case UINT32_C(0x7): eval_SUV(instr); break;
        case UINT32_C(0x8): eval_SHV(instr); break;
        case UINT32_C(0x9): eval_SFV(instr); break;
        case UINT32_C(0xa): eval_SWV(instr); break;
        case UINT32_C(0xb): eval_STV(instr); break;
        default:
            core::halt("RSP::SWC2 invalid operation");
            break;
    }
}

static const unsigned selectElementTable[16][8] = {
    // Vector Operand
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 1, 2, 3, 4, 5, 6, 7 },

    // Scalar Quarter
    { 0, 0, 2, 2, 4, 4, 6, 6 },
    { 1, 1, 3, 3, 5, 5, 7, 7 },

    // Scalar Half
    { 0, 0, 0, 0, 4, 4, 4, 4 },
    { 1, 1, 1, 1, 5, 5, 5, 5 },
    { 2, 2, 2, 2, 6, 6, 6, 6 },
    { 3, 3, 3, 3, 7, 7, 7, 7 },

    // Scalar Whole
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 2, 2, 2, 2, 2, 2, 2, 2 },
    { 3, 3, 3, 3, 3, 3, 3, 3 },
    { 4, 4, 4, 4, 4, 4, 4, 4 },
    { 5, 5, 5, 5, 5, 5, 5, 5 },
    { 6, 6, 6, 6, 6, 6, 6, 6 },
    { 7, 7, 7, 7, 7, 7, 7, 7 },
};

static inline unsigned selectElementIndex(unsigned i, u32 e) {
    return selectElementTable[e][i];
}

void eval_VABS(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        i16 res = svs > 0 ? svt :
                  svs < 0 ? -svt : 0;

        state.rspreg.vacc.lo.h[i] = (u16)res;
        out.h[i] = (u16)res;
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VADD(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] +
                  (i16)state.rspreg.vr[vt].h[j] +
                  state.rspreg.carry(i);

        state.rspreg.vacc.lo.h[i] = (u32)res;
        out.h[i] = (u16)clamp<i16, i32>(res);
    }

    state.rspreg.vr[vd] = out;
    state.rspreg.vco = 0;
}

void eval_VADDC(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] +
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vco |= res > UINT16_MAX ? 1u << i : 0;
        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VAND(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] &
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

void eval_VCH(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
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
        bool neq;

        if (sign) {
            tmp = s + t;
            ge = (i16)t < 0;
            le = (i16)tmp <= 0;
            vce = (i16)tmp == -1;
            neq = (i16)tmp != 0 && (i16)tmp != -1;
            di = le ? -t : s;
        } else {
            tmp = s - t;
            le = (i16)t < 0;
            ge = (i16)tmp >= 0;
            vce = 0;
            neq = (i16)tmp != 0;
            di = ge ? t : s;
        }

        state.rspreg.vacc.lo.h[i] = di;
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        state.rspreg.vco |= (u16)neq << (i + 8);
        state.rspreg.vco |= (u16)sign << i;
        state.rspreg.vce |= (u8)vce << i;
        out.h[i] = di;
    }

    state.rspreg.vr[vd] = out;
}

void eval_VCL(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);
        u32 s = state.rspreg.vr[vs].h[i];
        u32 t = state.rspreg.vr[vt].h[j];
        u32 tmp;
        u16 di;

        bool neq = (state.rspreg.vco >> (i + 8)) & 1;
        bool sign = (state.rspreg.vco >> i) & 1;
        bool ge = (state.rspreg.vcc >> (i + 8)) & 1;
        bool le = (state.rspreg.vcc >> i) & 1;
        bool vce = (state.rspreg.vce >> i) & 1;

        if (sign) {
            tmp = s + t;
            bool carry = tmp > UINT16_MAX;
            if (!neq) {
                le = (!vce && (tmp & 0xffffu) == 0 && !carry) ||
                     (vce && ((tmp & 0xffffu) == 0 || !carry));
            }
            di = le ? -t : s;
        } else {
            tmp = s - t;
            if (!neq) {
                ge = (i32)tmp >= 0;
            }
            di = ge ? t : s;
        }

        state.rspreg.vacc.lo.h[i] = di;
        state.rspreg.vcc &= ~(0x101u << i);
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        out.h[i] = di;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce = 0;
    state.rspreg.vr[vd] = out;
}

void eval_VCR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
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

        state.rspreg.vacc.lo.h[i] = di;
        state.rspreg.vcc |= (u16)ge << (i + 8);
        state.rspreg.vcc |= (u16)le << i;
        out.h[i] = di;
    }

    state.rspreg.vr[vd] = out;
}

void eval_VEQ(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 svs = state.rspreg.vr[vs].h[i];
        u16 svt = state.rspreg.vr[vt].h[j];
        bool neq = state.rspreg.neq(i);
        u16 res;

        if (svs == svt && !neq) {
            state.rspreg.set_compare(i);
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce &= 0xff;
    state.rspreg.vr[vd] = out;
}

void eval_VGE(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        bool carry = state.rspreg.carry(i);
        bool neq = state.rspreg.neq(i);
        u16 res;

        if (svs > svt || (svs == svt && !(carry && neq))) {
            state.rspreg.set_compare(i);
            res = (i16)svs;
        } else {
            res = (i16)svt;
        }

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce &= 0xff;
    state.rspreg.vr[vd] = out;
}

void eval_VLT(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i16 svs = (i16)state.rspreg.vr[vs].h[i];
        i16 svt = (i16)state.rspreg.vr[vt].h[j];
        bool carry = state.rspreg.carry(i);
        bool neq = state.rspreg.neq(i);
        u16 res;

        if (svs < svt || (svs == svt && neq && carry)) {
            state.rspreg.set_compare(i);
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce &= 0xff;
    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VMACF(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, (u64)res << 1);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMACQ(u32 instr) {
    core::halt("VMACQ unsupported");
}

__attribute__((weak))
void eval_VMACU(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, (u64)res << 1);
        out.h[i] = state.rspreg.vacc.read_md_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VMADH(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, (u64)res << 16);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VMADL(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] *
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, res >> 16);
        out.h[i] = state.rspreg.vacc.read_lo_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VMADM(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VMADN(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i32)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.add(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_lo_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMOV(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    state.rspreg.vacc.lo.h[de] = state.rspreg.vr[vt].h[e];
    state.rspreg.vr[vd].h[de] = state.rspreg.vr[vt].h[e];
}

void eval_VMRG(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        bool compare = state.rspreg.compare(i);
        u16 res = compare ? state.rspreg.vr[vs].h[i]
                          : state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vr[vd] = out;
}

void eval_VMUDH(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.write(i, (u64)res << 16);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMUDL(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u32 res = (u32)state.rspreg.vr[vs].h[i] *
                  (u32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.write(i, res >> 16);
        out.h[i] = state.rspreg.vacc.read_lo_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMUDM(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.write(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMUDN(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i32)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.write(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_lo_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMULF(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];
        res = (res << 1) + 0x8000;
        state.rspreg.vacc.write(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_md_clamp_signed(i);
    }

    state.rspreg.vr[vd] = out;
}

void eval_VMULQ(u32 instr) {
    core::halt("RSP::VMULQ unsupported");
}

void eval_VMULU(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i64 res = (i16)state.rspreg.vr[vs].h[i] *
                  (i16)state.rspreg.vr[vt].h[j];
        res = (res << 1) + 0x8000;
        state.rspreg.vacc.write(i, (u64)res);
        out.h[i] = state.rspreg.vacc.read_md_clamp_unsigned(i);
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VNAND(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] &
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

void eval_VNE(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vcc = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 svs = state.rspreg.vr[vs].h[i];
        u16 svt = state.rspreg.vr[vt].h[j];
        bool neq = state.rspreg.neq(i);
        u16 res;

        if (svs != svt || neq) {
            state.rspreg.set_compare(i);
            res = svs;
        } else {
            res = svt;
        }

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vco = 0;
    state.rspreg.vce &= 0xff;
    state.rspreg.vr[vd] = out;
}

void eval_VNOP(u32 instr) {
    (void)instr;
}

void eval_VNULL(u32 instr) {
    (void)instr;
}

__attribute__((weak))
void eval_VNOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] |
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VNXOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = ~(state.rspreg.vr[vs].h[i] ^
                    state.rspreg.vr[vt].h[j]);

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] |
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.lo.h[i] = res;
        out.h[i] = res;
    }

    state.rspreg.vr[vd] = out;
}

static inline u32 load_RCP_ROM(u32 divin) {
    int rshift, lshift;
    for (rshift = 31; rshift > 0; rshift--) {
        if (divin & (1u << rshift)) {
            break;
        }
    }
    lshift = 32 - rshift;
    u32 offset = (divin << lshift) >> 23;
    u32 rom = (u32)RCP_ROM[offset];
    return ((rom | 0x10000lu) << 14) >> rshift;
}

/**
 * @brief Implement the Reciprocal instruction.
 *  Inputs a signed i16 integer and outputs the reciprocal in 32bit fixed point
 *  format (the radix point is irrelevant).
 *
 *  Note that the machine instruction is implemented using a table lookup
 *  with the 10 most significant bits, i.e. there is precision loss.
 */
void eval_VRCP(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    i16 in = state.rspreg.vr[vt].h[e];
    u32 divin = in < 0 ? (u16)(i16)-in : (u16)in;
    u32 divout;

    state.rspreg.divin = divin;

    if (in == 0) {
        divout = INT32_MAX;
    } else {
        divout = load_RCP_ROM(divin);
        divout = in < 0 ? ~divout : divout;
    }

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divout = divout;
    state.rspreg.vr[vd].h[de] = divout;
    state.rspreg.divin_loaded = false;
}

void eval_VRCPH(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divin = (u32)state.rspreg.vr[vt].h[e] << 16;
    state.rspreg.vr[vd].h[de] = state.rspreg.divout >> 16;
    state.rspreg.divin_loaded = true;
}

void eval_VRCPL(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    if (state.rspreg.divin_loaded) {
        state.rspreg.divin &= ~UINT32_C(0xffff);
        state.rspreg.divin |= state.rspreg.vr[vt].h[e];
    } else {
        // core::halt("RCPL divin not loaded");
        state.rspreg.divin = sign_extend<u32, u16>(state.rspreg.vr[vt].h[e]);
    }

    i32 in = (i32)state.rspreg.divin;
    u32 divin = in < 0 ? (u32)(i32)-in : (u32)in;
    u32 divout;

    if (in == 0) {
        divout = INT32_MAX;
    } else {
        divout = load_RCP_ROM(divin);
        divout = in < 0 ? ~divout : divout;
    }

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divout = divout;
    state.rspreg.vr[vd].h[de] = divout;
    state.rspreg.divin_loaded = false;
}

void eval_VRNDN(u32 instr) {
    core::halt("RSP::VRNDN unsupported");
}

void eval_VRNDP(u32 instr) {
    core::halt("RSP::VRNDP unsupported");
}

static inline u32 load_RSQ_ROM(u32 divin) {
    int rshift, lshift;
    for (rshift = 31; rshift > 0; rshift--) {
        if (divin & (1u << rshift)) {
            break;
        }
    }
    lshift = 32 - rshift;
    u32 offset = (divin << lshift) >> 23;
    offset = (offset >> 1) | ((lshift & 0x1u) << 8);
    u32 rom = (u32)RSQ_ROM[offset];
    return ((rom | 0x10000lu) << 14) >> (rshift / 2);
}

void eval_VRSQ(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    i16 in = state.rspreg.vr[vt].h[e];
    u32 divin = in < 0 ? (u16)(i16)-in : (u16)in;
    u32 divout;

    state.rspreg.divin = divin;

    if ((u16)in == UINT16_C(0x8000)) {
        divout = 0xffff0000lu;
    } else if (in == 0) {
        divout = INT32_MAX;
    } else {
        divout = load_RSQ_ROM(divin);
        divout = in < 0 ? ~divout : divout;
    }

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divout = divout;
    state.rspreg.vr[vd].h[de] = divout;
    state.rspreg.divin_loaded = false;
}

void eval_VRSQH(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divin = (u32)state.rspreg.vr[vt].h[e] << 16;
    state.rspreg.vr[vd].h[de] = state.rspreg.divout >> 16;
    state.rspreg.divin_loaded = true;
}

void eval_VRSQL(u32 instr) {
    u32 e = assembly::getElement(instr) & 0x7u;
    u32 vt = assembly::getVt(instr);
    u32 de = assembly::getVs(instr) & 0x7u;
    u32 vd = assembly::getVd(instr);

    if (state.rspreg.divin_loaded) {
        state.rspreg.divin &= ~UINT32_C(0xffff);
        state.rspreg.divin |= state.rspreg.vr[vt].h[e];
    } else {
        state.rspreg.divin = sign_extend<u32, u16>(state.rspreg.vr[vt].h[e]);
    }

    i32 in = (i32)state.rspreg.divin;
    u32 divin = in < 0 ? (u32)(i32)-in : (u32)in;
    u32 divout;

    if ((u32)in == UINT32_C(0xffff8000)) {
        divout = 0xffff0000lu;
    } else if (in == 0) {
        divout = INT32_MAX;
    } else {
        divout = load_RSQ_ROM(divin);
        divout = in < 0 ? ~divout : divout;
    }

    for (unsigned i = 0; i < 8; i++) {
        state.rspreg.vacc.lo.h[i] = state.rspreg.vr[vt].h[i];
    }
    state.rspreg.divout = divout;
    state.rspreg.vr[vd].h[de] = divout;
    state.rspreg.divin_loaded = false;
}

void eval_VSAR(u32 instr) {
    u32 e = assembly::getElement(instr);
    // u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);

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
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc.hi.h[i];
                // state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 32);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i] << 32;
                break;
            case 9:
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc.md.h[i];
                // state.rspreg.vacc[i] &= ~(UINT64_C(0xffff) << 16);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i] << 16;
                break;
            case 10:
                state.rspreg.vr[vd].h[i] = state.rspreg.vacc.lo.h[i];
                // state.rspreg.vacc[i] &= ~UINT64_C(0xffff);
                // state.rspreg.vacc[i] |= (u64)state.rspreg.vr[vs].h[i];
                break;
        }
    }
}

void eval_VSUB(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i16)state.rspreg.vr[vs].h[i] -
                  (i16)state.rspreg.vr[vt].h[j] -
                  state.rspreg.carry(i);

        state.rspreg.vacc.lo.h[i] = (u32)res;
        out.h[i] = (u16)clamp<i16, i32>(res);
    }

    state.rspreg.vr[vd] = out;
    state.rspreg.vco = 0;
}

void eval_VSUBC(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    state.rspreg.vco = 0;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        i32 res = (i32)state.rspreg.vr[vs].h[i] -
                  (i32)state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.lo.h[i] = (u32)res;
        out.h[i] = res;

        if (res < 0) {
            state.rspreg.vco |= 1u << i;
            state.rspreg.vco |= 1u << (i + 8);
        } else if (res > 0) {
            state.rspreg.vco |= 1u << (i + 8);
        }
    }

    state.rspreg.vr[vd] = out;
}

__attribute__((weak))
void eval_VXOR(u32 instr) {
    u32 e = assembly::getElement(instr);
    u32 vt = assembly::getVt(instr);
    u32 vs = assembly::getVs(instr);
    u32 vd = assembly::getVd(instr);
    vr_t out;

    for (unsigned i = 0; i < 8; i++) {
        unsigned j = selectElementIndex(i, e);

        u16 res = state.rspreg.vr[vs].h[i] ^
                  state.rspreg.vr[vt].h[j];

        state.rspreg.vacc.lo.h[i] = res;
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
    core::halt("MOVN");
}

void eval_MOVZ(u32 instr) {
    RType(instr);
    core::halt("MOVZ");
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
    branch((i64)state.rspreg.gpr[rs] >= 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BLTZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.rspreg.gpr[rs] < 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BGEZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.rspreg.gpr[rs];
    state.rspreg.gpr[31] = state.rspreg.pc + 8;
    branch(r >= 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BLTZAL(u32 instr) {
    IType(instr, sign_extend);
    i64 r = state.rspreg.gpr[rs];
    state.rspreg.gpr[31] = state.rspreg.pc + 8;
    branch(r < 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
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
    branch(state.rspreg.gpr[rt] == state.rspreg.gpr[rs],
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BGTZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.rspreg.gpr[rs] > 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BLEZ(u32 instr) {
    IType(instr, sign_extend);
    branch((i64)state.rspreg.gpr[rs] <= 0,
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_BNE(u32 instr) {
    IType(instr, sign_extend);
    branch(state.rspreg.gpr[rt] != state.rspreg.gpr[rs],
        state.rspreg.pc + 4 + (i64)(imm << 2),
        state.rspreg.pc + 8);
}

void eval_CACHE(u32 instr) {
    (void)instr;
}

void eval_MFC0(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
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
    case 10:    val = state.hwreg.dpc_Current; break;
    case 11:    val = state.hwreg.DPC_STATUS_REG; break;
    case 12:
        core::halt("DPC_CLOCK_REG read access");
        val = state.hwreg.DPC_CLOCK_REG;
        break;
    case 13:
        core::halt("DPC_BUF_BUSY_REG read access");
        val = state.hwreg.DPC_BUF_BUSY_REG;
        break;
    case 14:
        core::halt("DPC_PIPE_BUSY_REG read access");
        val = state.hwreg.DPC_PIPE_BUSY_REG;
        break;
    case 15:
        core::halt("DPC_TMEM_REG read access");
        val = state.hwreg.DPC_TMEM_REG;
        break;
    default:
        /* unknown register access */
        val = 0;
        std::string reason = "MFC0 ";
        core::halt(reason + assembly::rsp::Cop0RegisterNames[rd]);
        break;
    }

    debugger::info(Debugger::RSP, "{} -> {:08x}",
        assembly::rsp::Cop0RegisterNames[rd], val);
    state.rspreg.gpr[rt] = sign_extend<u64, u32>(val);
}

void eval_MTC0(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 val = state.rspreg.gpr[rt];

    debugger::info(Debugger::RSP, "{} <- {:08x}",
        assembly::rsp::Cop0RegisterNames[rd], val);

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
    case 10:    /* DPC_CURRENT_REG, read only */ break;
    case 11:    write_DPC_STATUS_REG(val); break;
    case 12:
        core::halt("RSP::RDP_clock_counter");
        break;
    case 13:
        core::halt("RSP::RDP_command_busy");
        break;
    case 14:
        core::halt("RSP::RDP_pipe_busy_counter");
        break;
    case 15:
        core::halt("RSP::RDP_TMEM_load_counter");
        break;
    default:
        std::string reason = "MTC0 ";
        core::halt(reason + assembly::rsp::Cop0RegisterNames[rd]);
        break;
    }
}

void eval_COP0(u32 instr) {
    switch (assembly::getRs(instr)) {
    case assembly::MFCz: eval_MFC0(instr); break;
    case assembly::MTCz: eval_MTC0(instr); break;
    default:
        core::halt("invalid RSP::COP0 instruction");
        break;
    }
}

void eval_MFC2(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 e = (instr >> 7) & 0xfu;
    u16 val;
    storeVectorBytesAt(rd, e, (u8 *)&val, 2);
    val = __builtin_bswap16(val);
    state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
}

void eval_MTC2(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 e = (instr >> 7) & 0xfu;
    u16 val = __builtin_bswap16((u16)state.rspreg.gpr[rt]);
    loadVectorBytesAt(rd, e, (u8 *)&val, 2);
}

void eval_CFC2(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u16 val = 0;

    switch (rd) {
    case 0: val = state.rspreg.vco; break;
    case 1: val = state.rspreg.vcc; break;
    case 2: val = state.rspreg.vce; break;
    }
    state.rspreg.gpr[rt] = sign_extend<u64, u16>(val);
}

void eval_CTC2(u32 instr) {
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 val = state.rspreg.gpr[rt];

    switch (rd) {
    case 0: state.rspreg.vco = val; break;
    case 1: state.rspreg.vcc = val; break;
    case 2: state.rspreg.vce = val; break;
    }
}

void eval_J(u32 instr) {
    u64 tg = assembly::getTarget(instr);
    tg = (state.rspreg.pc & 0xfffffffff0000000llu) | (tg << 2);
    state.rsp.nextAction = State::Action::Delay;
    state.rsp.nextPc = tg;
}

void eval_JAL(u32 instr) {
    u64 tg = assembly::getTarget(instr);
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


static void (*COP2_callbacks[64])(u32) = {
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
    switch (assembly::getRs(instr)) {
    case assembly::MFCz: eval_MFC2(instr); break;
    case assembly::MTCz: eval_MTC2(instr); break;
    case assembly::CFCz: eval_CFC2(instr); break;
    case assembly::CTCz: eval_CTC2(instr); break;
    default:
        if ((instr & (1lu << 25)) == 0) {
            core::halt("RSP::COP2 invalid operation");
        } else {
            COP2_callbacks[instr & UINT32_C(0x3f)](instr);
        }
        break;
    }
}

static void (*SPECIAL_callbacks[64])(u32) = {
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
    SPECIAL_callbacks[assembly::getFunct(instr)](instr);
}

static void (*REGIMM_callbacks[32])(u32) = {
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
    REGIMM_callbacks[assembly::getRt(instr)](instr);
}

static void (*CPU_callbacks[64])(u32) = {
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

void eval_Instr(u32 instr) {
    // The null instruction is 'sll r0, r0, 0', i.e. a NOP.
    // As it is one of the most used instructions (to fill in delay slots),
    // perform a quick check to spare the instruction execution.
    if (instr) CPU_callbacks[assembly::getOpcode(instr)](instr);
}

void eval(void) {
    u64 addr = state.rspreg.pc;
    u64 instr;

    checkAddressAlignment(addr, 4);
    instr = __builtin_bswap32(*(u32 *)&state.imem[addr & UINT64_C(0xfff)]);

#if ENABLE_TRACE
    debugger::debugger.rspTrace.put(Debugger::TraceEntry(addr, instr));
#endif /* ENABLE_TRACE */

    eval_Instr(instr);
}

u16 RCP_ROM[512] = {
    0xffff, 0xff00, 0xfe01, 0xfd04, 0xfc07, 0xfb0c, 0xfa11, 0xf918,
    0xf81f, 0xf727, 0xf631, 0xf53b, 0xf446, 0xf352, 0xf25f, 0xf16d,
    0xf07c, 0xef8b, 0xee9c, 0xedae, 0xecc0, 0xebd3, 0xeae8, 0xe9fd,
    0xe913, 0xe829, 0xe741, 0xe65a, 0xe573, 0xe48d, 0xe3a9, 0xe2c5,
    0xe1e1, 0xe0ff, 0xe01e, 0xdf3d, 0xde5d, 0xdd7e, 0xdca0, 0xdbc2,
    0xdae6, 0xda0a, 0xd92f, 0xd854, 0xd77b, 0xd6a2, 0xd5ca, 0xd4f3,
    0xd41d, 0xd347, 0xd272, 0xd19e, 0xd0cb, 0xcff8, 0xcf26, 0xce55,
    0xcd85, 0xccb5, 0xcbe6, 0xcb18, 0xca4b, 0xc97e, 0xc8b2, 0xc7e7,
    0xc71c, 0xc652, 0xc589, 0xc4c0, 0xc3f8, 0xc331, 0xc26b, 0xc1a5,
    0xc0e0, 0xc01c, 0xbf58, 0xbe95, 0xbdd2, 0xbd10, 0xbc4f, 0xbb8f,
    0xbacf, 0xba10, 0xb951, 0xb894, 0xb7d6, 0xb71a, 0xb65e, 0xb5a2,
    0xb4e8, 0xb42e, 0xb374, 0xb2bb, 0xb203, 0xb14b, 0xb094, 0xafde,
    0xaf28, 0xae73, 0xadbe, 0xad0a, 0xac57, 0xaba4, 0xaaf1, 0xaa40,
    0xa98e, 0xa8de, 0xa82e, 0xa77e, 0xa6d0, 0xa621, 0xa574, 0xa4c6,
    0xa41a, 0xa36e, 0xa2c2, 0xa217, 0xa16d, 0xa0c3, 0xa01a, 0x9f71,
    0x9ec8, 0x9e21, 0x9d79, 0x9cd3, 0x9c2d, 0x9b87, 0x9ae2, 0x9a3d,
    0x9999, 0x98f6, 0x9852, 0x97b0, 0x970e, 0x966c, 0x95cb, 0x952b,
    0x948b, 0x93eb, 0x934c, 0x92ad, 0x920f, 0x9172, 0x90d4, 0x9038,
    0x8f9c, 0x8f00, 0x8e65, 0x8dca, 0x8d30, 0x8c96, 0x8bfc, 0x8b64,
    0x8acb, 0x8a33, 0x899c, 0x8904, 0x886e, 0x87d8, 0x8742, 0x86ad,
    0x8618, 0x8583, 0x84f0, 0x845c, 0x83c9, 0x8336, 0x82a4, 0x8212,
    0x8181, 0x80f0, 0x8060, 0x7fd0, 0x7f40, 0x7eb1, 0x7e22, 0x7d93,
    0x7d05, 0x7c78, 0x7beb, 0x7b5e, 0x7ad2, 0x7a46, 0x79ba, 0x792f,
    0x78a4, 0x781a, 0x7790, 0x7706, 0x767d, 0x75f5, 0x756c, 0x74e4,
    0x745d, 0x73d5, 0x734f, 0x72c8, 0x7242, 0x71bc, 0x7137, 0x70b2,
    0x702e, 0x6fa9, 0x6f26, 0x6ea2, 0x6e1f, 0x6d9c, 0x6d1a, 0x6c98,
    0x6c16, 0x6b95, 0x6b14, 0x6a94, 0x6a13, 0x6993, 0x6914, 0x6895,
    0x6816, 0x6798, 0x6719, 0x669c, 0x661e, 0x65a1, 0x6524, 0x64a8,
    0x642c, 0x63b0, 0x6335, 0x62ba, 0x623f, 0x61c5, 0x614b, 0x60d1,
    0x6058, 0x5fdf, 0x5f66, 0x5eed, 0x5e75, 0x5dfd, 0x5d86, 0x5d0f,
    0x5c98, 0x5c22, 0x5bab, 0x5b35, 0x5ac0, 0x5a4b, 0x59d6, 0x5961,
    0x58ed, 0x5879, 0x5805, 0x5791, 0x571e, 0x56ac, 0x5639, 0x55c7,
    0x5555, 0x54e3, 0x5472, 0x5401, 0x5390, 0x5320, 0x52af, 0x5240,
    0x51d0, 0x5161, 0x50f2, 0x5083, 0x5015, 0x4fa6, 0x4f38, 0x4ecb,
    0x4e5e, 0x4df1, 0x4d84, 0x4d17, 0x4cab, 0x4c3f, 0x4bd3, 0x4b68,
    0x4afd, 0x4a92, 0x4a27, 0x49bd, 0x4953, 0x48e9, 0x4880, 0x4817,
    0x47ae, 0x4745, 0x46dc, 0x4674, 0x460c, 0x45a5, 0x453d, 0x44d6,
    0x446f, 0x4408, 0x43a2, 0x433c, 0x42d6, 0x4270, 0x420b, 0x41a6,
    0x4141, 0x40dc, 0x4078, 0x4014, 0x3fb0, 0x3f4c, 0x3ee8, 0x3e85,
    0x3e22, 0x3dc0, 0x3d5d, 0x3cfb, 0x3c99, 0x3c37, 0x3bd6, 0x3b74,
    0x3b13, 0x3ab2, 0x3a52, 0x39f1, 0x3991, 0x3931, 0x38d2, 0x3872,
    0x3813, 0x37b4, 0x3755, 0x36f7, 0x3698, 0x363a, 0x35dc, 0x357f,
    0x3521, 0x34c4, 0x3467, 0x340a, 0x33ae, 0x3351, 0x32f5, 0x3299,
    0x323e, 0x31e2, 0x3187, 0x312c, 0x30d1, 0x3076, 0x301c, 0x2fc2,
    0x2f68, 0x2f0e, 0x2eb4, 0x2e5b, 0x2e02, 0x2da9, 0x2d50, 0x2cf8,
    0x2c9f, 0x2c47, 0x2bef, 0x2b97, 0x2b40, 0x2ae8, 0x2a91, 0x2a3a,
    0x29e4, 0x298d, 0x2937, 0x28e0, 0x288b, 0x2835, 0x27df, 0x278a,
    0x2735, 0x26e0, 0x268b, 0x2636, 0x25e2, 0x258d, 0x2539, 0x24e5,
    0x2492, 0x243e, 0x23eb, 0x2398, 0x2345, 0x22f2, 0x22a0, 0x224d,
    0x21fb, 0x21a9, 0x2157, 0x2105, 0x20b4, 0x2063, 0x2012, 0x1fc1,
    0x1f70, 0x1f1f, 0x1ecf, 0x1e7f, 0x1e2e, 0x1ddf, 0x1d8f, 0x1d3f,
    0x1cf0, 0x1ca1, 0x1c52, 0x1c03, 0x1bb4, 0x1b66, 0x1b17, 0x1ac9,
    0x1a7b, 0x1a2d, 0x19e0, 0x1992, 0x1945, 0x18f8, 0x18ab, 0x185e,
    0x1811, 0x17c4, 0x1778, 0x172c, 0x16e0, 0x1694, 0x1648, 0x15fd,
    0x15b1, 0x1566, 0x151b, 0x14d0, 0x1485, 0x143b, 0x13f0, 0x13a6,
    0x135c, 0x1312, 0x12c8, 0x127f, 0x1235, 0x11ec, 0x11a3, 0x1159,
    0x1111, 0x10c8, 0x107f, 0x1037, 0x0fef, 0x0fa6, 0x0f5e, 0x0f17,
    0x0ecf, 0x0e87, 0x0e40, 0x0df9, 0x0db2, 0x0d6b, 0x0d24, 0x0cdd,
    0x0c97, 0x0c50, 0x0c0a, 0x0bc4, 0x0b7e, 0x0b38, 0x0af2, 0x0aad,
    0x0a68, 0x0a22, 0x09dd, 0x0998, 0x0953, 0x090f, 0x08ca, 0x0886,
    0x0842, 0x07fd, 0x07b9, 0x0776, 0x0732, 0x06ee, 0x06ab, 0x0668,
    0x0624, 0x05e1, 0x059e, 0x055c, 0x0519, 0x04d6, 0x0494, 0x0452,
    0x0410, 0x03ce, 0x038c, 0x034a, 0x0309, 0x02c7, 0x0286, 0x0245,
    0x0204, 0x01c3, 0x0182, 0x0141, 0x0101, 0x00c0, 0x0080, 0x0040,
};

u16 RSQ_ROM[512] = {
    0xffff, 0xff00, 0xfe02, 0xfd06, 0xfc0b, 0xfb12, 0xfa1a, 0xf923,
    0xf82e, 0xf73b, 0xf648, 0xf557, 0xf467, 0xf379, 0xf28c, 0xf1a0,
    0xf0b6, 0xefcd, 0xeee5, 0xedff, 0xed19, 0xec35, 0xeb52, 0xea71,
    0xe990, 0xe8b1, 0xe7d3, 0xe6f6, 0xe61b, 0xe540, 0xe467, 0xe38e,
    0xe2b7, 0xe1e1, 0xe10d, 0xe039, 0xdf66, 0xde94, 0xddc4, 0xdcf4,
    0xdc26, 0xdb59, 0xda8c, 0xd9c1, 0xd8f7, 0xd82d, 0xd765, 0xd69e,
    0xd5d7, 0xd512, 0xd44e, 0xd38a, 0xd2c8, 0xd206, 0xd146, 0xd086,
    0xcfc7, 0xcf0a, 0xce4d, 0xcd91, 0xccd6, 0xcc1b, 0xcb62, 0xcaa9,
    0xc9f2, 0xc93b, 0xc885, 0xc7d0, 0xc71c, 0xc669, 0xc5b6, 0xc504,
    0xc453, 0xc3a3, 0xc2f4, 0xc245, 0xc198, 0xc0eb, 0xc03f, 0xbf93,
    0xbee9, 0xbe3f, 0xbd96, 0xbced, 0xbc46, 0xbb9f, 0xbaf8, 0xba53,
    0xb9ae, 0xb90a, 0xb867, 0xb7c5, 0xb723, 0xb681, 0xb5e1, 0xb541,
    0xb4a2, 0xb404, 0xb366, 0xb2c9, 0xb22c, 0xb191, 0xb0f5, 0xb05b,
    0xafc1, 0xaf28, 0xae8f, 0xadf7, 0xad60, 0xacc9, 0xac33, 0xab9e,
    0xab09, 0xaa75, 0xa9e1, 0xa94e, 0xa8bc, 0xa82a, 0xa799, 0xa708,
    0xa678, 0xa5e8, 0xa559, 0xa4cb, 0xa43d, 0xa3b0, 0xa323, 0xa297,
    0xa20b, 0xa180, 0xa0f6, 0xa06c, 0x9fe2, 0x9f59, 0x9ed1, 0x9e49,
    0x9dc2, 0x9d3b, 0x9cb4, 0x9c2f, 0x9ba9, 0x9b25, 0x9aa0, 0x9a1c,
    0x9999, 0x9916, 0x9894, 0x9812, 0x9791, 0x9710, 0x968f, 0x960f,
    0x9590, 0x9511, 0x9492, 0x9414, 0x9397, 0x931a, 0x929d, 0x9221,
    0x91a5, 0x9129, 0x90af, 0x9034, 0x8fba, 0x8f40, 0x8ec7, 0x8e4f,
    0x8dd6, 0x8d5e, 0x8ce7, 0x8c70, 0x8bf9, 0x8b83, 0x8b0d, 0x8a98,
    0x8a23, 0x89ae, 0x893a, 0x88c6, 0x8853, 0x87e0, 0x876d, 0x86fb,
    0x8689, 0x8618, 0x85a7, 0x8536, 0x84c6, 0x8456, 0x83e7, 0x8377,
    0x8309, 0x829a, 0x822c, 0x81bf, 0x8151, 0x80e4, 0x8078, 0x800c,
    0x7fa0, 0x7f34, 0x7ec9, 0x7e5e, 0x7df4, 0x7d8a, 0x7d20, 0x7cb6,
    0x7c4d, 0x7be5, 0x7b7c, 0x7b14, 0x7aac, 0x7a45, 0x79de, 0x7977,
    0x7911, 0x78ab, 0x7845, 0x77df, 0x777a, 0x7715, 0x76b1, 0x764d,
    0x75e9, 0x7585, 0x7522, 0x74bf, 0x745d, 0x73fa, 0x7398, 0x7337,
    0x72d5, 0x7274, 0x7213, 0x71b3, 0x7152, 0x70f2, 0x7093, 0x7033,
    0x6fd4, 0x6f76, 0x6f17, 0x6eb9, 0x6e5b, 0x6dfd, 0x6da0, 0x6d43,
    0x6ce6, 0x6c8a, 0x6c2d, 0x6bd1, 0x6b76, 0x6b1a, 0x6abf, 0x6a64,
    0x6a09, 0x6955, 0x68a1, 0x67ef, 0x673e, 0x668d, 0x65de, 0x6530,
    0x6482, 0x63d6, 0x632b, 0x6280, 0x61d7, 0x612e, 0x6087, 0x5fe0,
    0x5f3a, 0x5e95, 0x5df1, 0x5d4e, 0x5cac, 0x5c0b, 0x5b6b, 0x5acb,
    0x5a2c, 0x598f, 0x58f2, 0x5855, 0x57ba, 0x5720, 0x5686, 0x55ed,
    0x5555, 0x54be, 0x5427, 0x5391, 0x52fc, 0x5268, 0x51d5, 0x5142,
    0x50b0, 0x501f, 0x4f8e, 0x4efe, 0x4e6f, 0x4de1, 0x4d53, 0x4cc6,
    0x4c3a, 0x4baf, 0x4b24, 0x4a9a, 0x4a10, 0x4987, 0x48ff, 0x4878,
    0x47f1, 0x476b, 0x46e5, 0x4660, 0x45dc, 0x4558, 0x44d5, 0x4453,
    0x43d1, 0x434f, 0x42cf, 0x424f, 0x41cf, 0x4151, 0x40d2, 0x4055,
    0x3fd8, 0x3f5b, 0x3edf, 0x3e64, 0x3de9, 0x3d6e, 0x3cf5, 0x3c7c,
    0x3c03, 0x3b8b, 0x3b13, 0x3a9c, 0x3a26, 0x39b0, 0x393a, 0x38c5,
    0x3851, 0x37dd, 0x3769, 0x36f6, 0x3684, 0x3612, 0x35a0, 0x352f,
    0x34bf, 0x344f, 0x33df, 0x3370, 0x3302, 0x3293, 0x3226, 0x31b9,
    0x314c, 0x30df, 0x3074, 0x3008, 0x2f9d, 0x2f33, 0x2ec8, 0x2e5f,
    0x2df6, 0x2d8d, 0x2d24, 0x2cbc, 0x2c55, 0x2bee, 0x2b87, 0x2b21,
    0x2abb, 0x2a55, 0x29f0, 0x298b, 0x2927, 0x28c3, 0x2860, 0x27fd,
    0x279a, 0x2738, 0x26d6, 0x2674, 0x2613, 0x25b2, 0x2552, 0x24f2,
    0x2492, 0x2432, 0x23d3, 0x2375, 0x2317, 0x22b9, 0x225b, 0x21fe,
    0x21a1, 0x2145, 0x20e8, 0x208d, 0x2031, 0x1fd6, 0x1f7b, 0x1f21,
    0x1ec7, 0x1e6d, 0x1e13, 0x1dba, 0x1d61, 0x1d09, 0x1cb1, 0x1c59,
    0x1c01, 0x1baa, 0x1b53, 0x1afc, 0x1aa6, 0x1a50, 0x19fa, 0x19a5,
    0x1950, 0x18fb, 0x18a7, 0x1853, 0x17ff, 0x17ab, 0x1758, 0x1705,
    0x16b2, 0x1660, 0x160d, 0x15bc, 0x156a, 0x1519, 0x14c8, 0x1477,
    0x1426, 0x13d6, 0x1386, 0x1337, 0x12e7, 0x1298, 0x1249, 0x11fb,
    0x11ac, 0x115e, 0x1111, 0x10c3, 0x1076, 0x1029, 0x0fdc, 0x0f8f,
    0x0f43, 0x0ef7, 0x0eab, 0x0e60, 0x0e15, 0x0dca, 0x0d7f, 0x0d34,
    0x0cea, 0x0ca0, 0x0c56, 0x0c0c, 0x0bc3, 0x0b7a, 0x0b31, 0x0ae8,
    0x0aa0, 0x0a58, 0x0a10, 0x09c8, 0x0981, 0x0939, 0x08f2, 0x08ab,
    0x0865, 0x081e, 0x07d8, 0x0792, 0x074d, 0x0707, 0x06c2, 0x067d,
    0x0638, 0x05f3, 0x05af, 0x056a, 0x0526, 0x04e2, 0x049f, 0x045b,
    0x0418, 0x03d5, 0x0392, 0x0350, 0x030d, 0x02cb, 0x0289, 0x0247,
    0x0206, 0x01c4, 0x0183, 0x0142, 0x0101, 0x00c0, 0x0080, 0x0040,
};

}; /* namespace interpreter::rsp */
