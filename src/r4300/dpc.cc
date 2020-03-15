
#include <cstring>
#include <iostream>

#include <debugger.h>
#include <r4300/dpc.h>
#include <r4300/hw.h>
#include <r4300/state.h>

namespace R4300 {

using namespace R4300;

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

}; /* namespace R4300 */
