
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <debugger.h>

namespace R4300 {

// AI DRAM address
// (W): [23:0] starting RDRAM address (8B-aligned)
const u32 AI_DRAM_ADDR_REG = UINT32_C(0x04500000);
// AI length
// (RW): [14:0] transfer length (v1.0) - Bottom 3 bits are ignored
//       [17:0] transfer length (v2.0) - Bottom 3 bits are ignored
const u32 AI_LEN_REG = UINT32_C(0x04500004);
// AI control
// (W): [0] DMA enable - if LSB == 1, DMA is enabled
const u32 AI_CONTROL_REG = UINT32_C(0x04500008);
// AI status
// (R): [31]/[0] ai_full (addr & len buffer full)
//      [30] ai_busy
//      Note that a 1to0 transition in ai_full will set interrupt
// (W): clear audio interrupt
const u32 AI_STATUS_REG = UINT32_C(0x0450000c);
// AI DAC sample period register
// (W): [13:0] dac rate
//          - vid_clock/(dperiod + 1) is the DAC sample rate
//          - (dperiod + 1) >= 66 * (aclockhp + 1) must be true
const u32 AI_DACRATE_REG = UINT32_C(0x04500010);
// AI bit rate
// (W): [3:0] bit rate (abus clock half period register - aclockhp)
//          - vid_clock/(2*(aclockhp + 1)) is the DAC clock rate
//          - The abus clock stops if aclockhp is zero
const u32 AI_BITRATE_REG = UINT32_C(0x04500014);


/**
 * @brief Write the AI register AI_LEN_REG.
 *  Writing the register starts a DMA tranfer from DRAM to DAC.
 */
static void write_AI_LEN_REG(u32 value) {
    debugger::info(Debugger::AI, "AI_LEN_REG <- {:08x}", value);
    state.hwreg.AI_LEN_REG = value & AI_LEN_V2_MASK;
    if (state.hwreg.AI_CONTROL_REG & AI_CONTROL_DMA_EN) {
        // state.hwreg.AI_LEN_REG = 0;
        set_MI_INTR_REG(MI_INTR_AI);
    }
}

bool read_AI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4 && addr != AI_STATUS_REG)
        return false;

    switch (addr) {
    case AI_DRAM_ADDR_REG:
        debugger::info(Debugger::AI, "AI_DRAM_ADDR_REG -> {:08x}",
            state.hwreg.AI_DRAM_ADDR_REG);
        *value = state.hwreg.AI_DRAM_ADDR_REG;
        return true;
    case AI_LEN_REG:
        debugger::info(Debugger::AI, "AI_LEN_REG -> {:08x}",
            state.hwreg.AI_LEN_REG);
        *value = state.hwreg.AI_LEN_REG;
        return true;
    case AI_CONTROL_REG:
        debugger::info(Debugger::AI, "AI_CONTROL_REG -> {:08x}",
            state.hwreg.AI_CONTROL_REG);
        *value = state.hwreg.AI_CONTROL_REG;
        return true;
    case AI_STATUS_REG:
        debugger::info(Debugger::AI, "AI_STATUS_REG -> {:08x}",
            state.hwreg.AI_STATUS_REG);
        *value = state.hwreg.AI_STATUS_REG >> (32 - 8 * bytes);
        return true;
    case AI_DACRATE_REG:
        debugger::info(Debugger::AI, "AI_DACRATE_REG -> {:08x}",
            state.hwreg.AI_DACRATE_REG);
        *value = state.hwreg.AI_DACRATE_REG;
        return true;
    case AI_BITRATE_REG:
        debugger::info(Debugger::AI, "AI_BITRATE_REG -> {:08x}",
            state.hwreg.AI_BITRATE_REG);
        *value = state.hwreg.AI_BITRATE_REG;
        return true;
    default:
        debugger::warn(Debugger::AI,
            "Read of unknown AI register: {:08x}", addr);
        debugger::halt("AI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_AI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case AI_DRAM_ADDR_REG:
        debugger::info(Debugger::AI, "AI_DRAM_ADDR_REG <- {:08x}", value);
        state.hwreg.AI_DRAM_ADDR_REG = value & AI_DRAM_ADDR_MASK;
        return true;
    case AI_LEN_REG:
        write_AI_LEN_REG(value);
        return true;
    case AI_CONTROL_REG:
        debugger::info(Debugger::AI, "AI_CONTROL_REG <- {:08x}", value);
        state.hwreg.AI_CONTROL_REG = value & AI_CONTROL_DMA_EN;
        return true;
    case AI_STATUS_REG:
        debugger::info(Debugger::AI, "AI_STATUS_REG <- {:08x}", value);
        clear_MI_INTR_REG(MI_INTR_AI);
        return true;
    case AI_DACRATE_REG:
        debugger::info(Debugger::AI, "AI_DACRATE_REG <- {:08x}", value);
        state.hwreg.AI_DACRATE_REG = value & AI_DACRATE_MASK;
        return true;
    case AI_BITRATE_REG:
        debugger::info(Debugger::AI, "AI_BITRATE_REG <- {:08x}", value);
        state.hwreg.AI_BITRATE_REG = value & AI_BITRATE_MASK;
        return true;
    default:
        debugger::warn(Debugger::AI,
            "Write of unknown AI register: {:08x} <- {:08x}",
            addr, value);
        debugger::halt("AI write unknown");
        break;
    }
    return true;
}

}; /* namespace R4300 */
