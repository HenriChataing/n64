
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <graphics.h>
#include <debugger.h>

namespace R4300 {

// VI status/control
// (RW): [1:0] type[1:0] (pixel size)
//              0: blank (no data, no sync)
//              1: reserved
//              2: 5/5/5/3 ("16" bit)
//              3: 8/8/8/8 (32 bit)
//       [2] gamma_dither_enable (normally on, unless "special effect")
//       [3] gamma_enable (normally on, unless MPEG/JPEG)
//       [4] divot_enable (normally on if antialiased,
//           unless decal lines)
//       [5] reserved - always off
//       [6] serrate (always on if interlaced, off if not)
//       [7] reserved - diagnostics only
//       [9:8] anti-alias (aa) mode[1:0]
//              0: aa & resamp (always fetch extra lines)
//              1: aa & resamp (fetch extra lines if needed)
//              2: resamp only (treat as all fully covered)
//              3: neither (replicate pixels, no interpolate)
//       [11] reserved - diagnostics only
//       [15:12] reserved
const u32 VI_CONTROL_REG = UINT32_C(0x04400000); // VI_STATUS_REG
// VI origin
// (RW): [23:0] frame buffer origin in bytes
const u32 VI_DRAM_ADDR_REG = UINT32_C(0x04400004); // VI_ORIGIN_REG
// VI width
// (RW): [11:0] frame buffer line width in pixels
const u32 VI_WIDTH_REG = UINT32_C(0x04400008); // VI_H_WIDTH_REG
// VI vertical intr
// (RW): [9:0] interrupt when current half-line = V_INTR
const u32 VI_INTR_REG = UINT32_C(0x0440000c); // VI_V_INTR_REG
// VI current vertical line
// (RW): [9:0] current half line, sampled once per line (the lsb of
//             V_CURRENT is constant within a field, and in
//             interlaced modes gives the field number - which is
//             constant for non-interlaced modes)
//             - Writes clears interrupt line
const u32 VI_CURRENT_REG = UINT32_C(0x04400010); // VI_V_CURRENT_LINE_REG
// VI video timing
// (RW): [7:0] horizontal sync width in pixels
//       [15:8] color burst width in pixels
//       [19:16] vertical sync width in half lines
//       [29:20] start of color burst in pixels from h-sync
const u32 VI_BURST_REG = UINT32_C(0x04400014); // VI_TIMING_REG
// VI vertical sync
// (RW): [9:0] number of half-lines per field
const u32 VI_V_SYNC_REG = UINT32_C(0x04400018);
// VI horizontal sync
// (RW): [11:0] total duration of a line in 1/4 pixel
//       [20:16] a 5-bit leap pattern used for PAL only (h_sync_period)
const u32 VI_H_SYNC_REG = UINT32_C(0x0440001c);
// VI horizontal sync leap
// (RW): [11:0] identical to h_sync_period
//       [27:16] identical to h_sync_period
const u32 VI_LEAP_REG = UINT32_C(0x04400020); // VI_H_SYNC_LEAP_REG
// VI horizontal video
// (RW): [9:0] end of active video in screen pixels
//       [25:16] start of active video in screen pixels
const u32 VI_H_START_REG = UINT32_C(0x04400024); // VI_H_VIDEO_REG
// VI vertical video
// (RW): [9:0] end of active video in screen half-lines
//       [25:16] start of active video in screen half-lines
const u32 VI_V_START_REG = UINT32_C(0x04400028); // VI_V_VIDEO_REG
// VI vertical burst
// (RW): [9:0] end of color burst enable in half-lines
//       [25:16] start of color burst enable in half-lines
const u32 VI_V_BURST_REG = UINT32_C(0x0440002c);
// VI x-scale
// (RW): [11:0] 1/horizontal scale up factor (2.10 format)
//       [27:16] horizontal subpixel offset (2.10 format)
const u32 VI_X_SCALE_REG = UINT32_C(0x04400030);
// VI y-scale
// (RW): [11:0] 1/vertical scale up factor (2.10 format)
//       [27:16] vertical subpixel offset (2.10 format)
const u32 VI_Y_SCALE_REG = UINT32_C(0x04400034);


/** @brief Called for VI interrupts. */
void raise_VI_INTR(void) {
    debugger::debug(Debugger::VI, "VI_INTR event");
    // Compute the next interrupt time.
    state.hwreg.vi_NextIntr += state.hwreg.vi_IntrInterval;
    // Set the pending interrupt bit.
    set_MI_INTR_REG(MI_INTR_VI);
    // Force a refresh of the screen, in case the framebuffer config
    // has not changed since the last frame.
    refreshVideoImage();
    // Finally, schedule the next vertical blank interrupt.
    state.scheduleEvent(state.hwreg.vi_NextIntr, raise_VI_INTR);
}

/**
 * @brief Rebuild the current framebuffer object with the configuration
 *  in the registers VI_MODE_REG, VI_DRAM_ADDR_REG, VI_WIDTH_REG.
 */
static void updateCurrentFramebuffer(void) {
    bool valid = true;
    unsigned pixelSize = 0;
    void *start = NULL;

    u32 colorDepth =
        (state.hwreg.VI_CONTROL_REG >> VI_CONTROL_COLOR_DEPTH_SHIFT) &
        VI_CONTROL_COLOR_DEPTH_MASK;

    switch (colorDepth) {
        case VI_CONTROL_COLOR_DEPTH_32BIT: pixelSize = 32; break;
        case VI_CONTROL_COLOR_DEPTH_16BIT: pixelSize = 16; break;
        case VI_CONTROL_COLOR_DEPTH_BLANK:
        default:
            debugger::warn(Debugger::VI,
                "invalid COLOR_DEPTH config: {}", colorDepth);
            valid = false;
            break;
    }

    /* PAL standard */
    // float screenFrameRate = 25;
    // float screenPixelAspectRatio = 1.09; /* Horizontally elongated */

    /* NTSC standard */
    // float screenFrameRate = 29.97;
    // float screenPixelAspectRatio = 0.91; /* Vertically elongated */

    /* Unless stated otherwise, all are integer values.
     * lineDuration: 10.2
     * horizontalScale: 2.10
     * verticalScale: 2.10 */
    u32 linesPerFrame = R4300::state.hwreg.VI_V_SYNC_REG;
    u32 lineDuration = R4300::state.hwreg.VI_H_SYNC_REG & 0xfffu;
    u32 horizontalStart = (R4300::state.hwreg.VI_H_START_REG >> 16) & 0x3ffu;
    u32 horizontalEnd = R4300::state.hwreg.VI_H_START_REG & 0x3ffu;
    u32 verticalStart = (R4300::state.hwreg.VI_V_START_REG >> 16) & 0x3ffu;
    u32 verticalEnd = R4300::state.hwreg.VI_V_START_REG & 0x3ffu;
    u32 horizontalScale = R4300::state.hwreg.VI_X_SCALE_REG & 0xfffu;
    u32 verticalScale = R4300::state.hwreg.VI_Y_SCALE_REG & 0xfffu;

    u32 framebufferWidth =
        ((u64)(horizontalEnd - horizontalStart) * (u64)horizontalScale) / 1024;
    u32 framebufferHeight =
        ((u64)(verticalEnd - verticalStart) * (u64)verticalScale) / 1024 / 2;

    debugger::debug(Debugger::VI, "lines per frame : {}", linesPerFrame);
    debugger::debug(Debugger::VI, "line duration : {}", (float)lineDuration / 4.);
    debugger::debug(Debugger::VI, "horizontal start : {}", horizontalStart);
    debugger::debug(Debugger::VI, "horizontal end : {}", horizontalEnd);
    debugger::debug(Debugger::VI, "vertical start : {}", verticalStart);
    debugger::debug(Debugger::VI, "vertical end : {}", verticalEnd);
    debugger::debug(Debugger::VI, "framebuffer width : {}", framebufferWidth);
    debugger::debug(Debugger::VI, "framebuffer height : {}", framebufferHeight);

    framebufferWidth = state.hwreg.VI_WIDTH_REG;

    unsigned framebufferSize = framebufferWidth * framebufferHeight * (pixelSize / 8);
    u64 addr = state.hwreg.VI_DRAM_ADDR_REG;

    if (addr + framebufferSize > sizeof(state.dram)) {
        debugger::warn(Debugger::VI,
            "invalid DRAM_ADDR config: {:06x}+{:06x}",
            addr, framebufferSize);
        valid = false;
    } else {
        start = &state.dram[addr];
    }

    setVideoImage(framebufferWidth, framebufferHeight, pixelSize,
        valid ? start : NULL);
}

/** @brief Write the value of the VI_INTR_REG register. */
static void write_VI_INTR_REG(u32 value) {
    debugger::info(Debugger::VI, "VI_INTR_REG <- {:08x}", value);
    state.hwreg.VI_INTR_REG = value;
}

/**
 * @brief Read the value of the VI_CURRENT_REG register.
 *  The value of the register is estimated from:
 *      1. the difference between the last vblank interrupt and the
 *          current time
 *      2. the current number of cycles per vertical line
 *          (depends on the cpu clock frequency, the screen refresh frequency,
 *           the number of vertical lines in VI_V_SYNC_REG)
 *  The read value is cached in hwreg->VI_CURRENT_REG.
 */
static u32 read_VI_CURRENT_REG(void) {
    ulong diff = state.cycles - state.hwreg.vi_LastCycleCount;
    u32 linesPerFrame = state.hwreg.VI_V_SYNC_REG;
    u32 current = state.hwreg.VI_CURRENT_REG;
    u32 count = (current + diff / state.hwreg.vi_CyclesPerLine) % linesPerFrame;

    state.hwreg.vi_LastCycleCount += diff - (diff % state.hwreg.vi_CyclesPerLine);
    state.hwreg.VI_CURRENT_REG = count;

    /* Bit 0 always indicates the current field in interlaced mode. */
    if (state.hwreg.VI_CONTROL_REG & VI_CONTROL_SERRATE) {
        count &= ~UINT32_C(1);
        count |= 0; // TODO track current field.
    }

    debugger::debug(Debugger::VI, "VI_CURRENT_REG -> {:08x}", count);
    return count;
}

/** @brief Write the value of the VI_V_SYNC_REG register. */
static void write_VI_V_SYNC_REG(u32 value) {
    // CPU freq is 93.75 MHz, refresh frequency is assumed 60Hz.
    debugger::info(Debugger::VI, "VI_V_SYNC_REG <- {:08x}", value);
    state.hwreg.VI_V_SYNC_REG = value;
    state.hwreg.vi_CyclesPerLine = 93750000lu / (60 * (value + 1));
    state.hwreg.vi_IntrInterval = state.hwreg.vi_CyclesPerLine * value;
}

bool read_VI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case VI_CONTROL_REG:
        debugger::info(Debugger::VI, "VI_CONTROL_REG -> {:08x}",
            state.hwreg.VI_CONTROL_REG);
        *value = state.hwreg.VI_CONTROL_REG;
        return true;
    case VI_DRAM_ADDR_REG:
        debugger::info(Debugger::VI, "VI_DRAM_ADDR_REG -> {:08x}",
            state.hwreg.VI_DRAM_ADDR_REG);
        *value = state.hwreg.VI_DRAM_ADDR_REG;
        return true;
    case VI_WIDTH_REG:
        debugger::info(Debugger::VI, "VI_WIDTH_REG -> {:08x}",
            state.hwreg.VI_WIDTH_REG);
        *value = state.hwreg.VI_WIDTH_REG;
        return true;
    case VI_INTR_REG:
        debugger::info(Debugger::VI, "VI_INTR_REG -> {:08x}",
            state.hwreg.VI_INTR_REG);
        *value = state.hwreg.VI_INTR_REG;
        return true;
    case VI_CURRENT_REG:
        *value = read_VI_CURRENT_REG();
        return true;
    case VI_BURST_REG:
        debugger::info(Debugger::VI, "VI_BURST_REG -> {:08x}",
            state.hwreg.VI_BURST_REG);
        *value = state.hwreg.VI_BURST_REG;
        return true;
    case VI_V_SYNC_REG:
        debugger::info(Debugger::VI, "VI_V_SYNC_REG -> {:08x}",
            state.hwreg.VI_V_SYNC_REG);
        *value = state.hwreg.VI_V_SYNC_REG;
        return true;
    case VI_H_SYNC_REG:
        debugger::info(Debugger::VI, "VI_H_SYNC_REG -> {:08x}",
            state.hwreg.VI_H_SYNC_REG);
        *value = state.hwreg.VI_H_SYNC_REG;
        return true;
    case VI_LEAP_REG:
        debugger::info(Debugger::VI, "VI_LEAP_REG -> {:08x}",
            state.hwreg.VI_LEAP_REG);
        *value = state.hwreg.VI_LEAP_REG;
        return true;
    case VI_H_START_REG:
        debugger::info(Debugger::VI, "VI_H_START_REG -> {:08x}",
            state.hwreg.VI_H_START_REG);
        *value = state.hwreg.VI_H_START_REG;
        return true;
    case VI_V_START_REG:
        debugger::info(Debugger::VI, "VI_V_START_REG -> {:08x}",
            state.hwreg.VI_V_START_REG);
        *value = state.hwreg.VI_V_START_REG;
        return true;
    case VI_V_BURST_REG:
        debugger::info(Debugger::VI, "VI_V_BURST_REG -> {:08x}",
            state.hwreg.VI_V_BURST_REG);
        *value = state.hwreg.VI_V_BURST_REG;
        return true;
    case VI_X_SCALE_REG:
        debugger::info(Debugger::VI, "VI_X_SCALE_REG -> {:08x}",
            state.hwreg.VI_X_SCALE_REG);
        *value = state.hwreg.VI_X_SCALE_REG;
        return true;
    case VI_Y_SCALE_REG:
        debugger::info(Debugger::VI, "VI_Y_SCALE_REG -> {:08x}",
            state.hwreg.VI_Y_SCALE_REG);
        *value = state.hwreg.VI_Y_SCALE_REG;
        return true;
    default:
        debugger::warn(Debugger::VI,
            "Read of unknown VI register: {:08x}", addr);
        debugger::halt("VI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_VI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case VI_CONTROL_REG:
        debugger::info(Debugger::VI, "VI_CONTROL_REG <- {:08x}", value);
        state.hwreg.VI_CONTROL_REG = value;
        updateCurrentFramebuffer();
        return true;
    case VI_DRAM_ADDR_REG:
        debugger::info(Debugger::VI, "VI_DRAM_ADDR_REG <- {:08x}", value);
        state.hwreg.VI_DRAM_ADDR_REG = value & VI_DRAM_ADDR_MASK;
        updateCurrentFramebuffer();
        return true;
    case VI_WIDTH_REG:
        debugger::info(Debugger::VI, "VI_WIDTH_REG <- {:08x}", value);
        state.hwreg.VI_WIDTH_REG = value;
        updateCurrentFramebuffer();
        return true;
    case VI_INTR_REG:
        write_VI_INTR_REG(value);
        return true;
    case VI_CURRENT_REG:
        debugger::info(Debugger::VI, "VI_CURRENT_REG <- {:08x}", value);
        clear_MI_INTR_REG(MI_INTR_VI);
        return true;
    case VI_BURST_REG:
        debugger::info(Debugger::VI, "VI_BURST_REG <- {:08x}", value);
        state.hwreg.VI_BURST_REG = value;
        return true;
    case VI_V_SYNC_REG:
        write_VI_V_SYNC_REG(value);
        return true;
    case VI_H_SYNC_REG:
        debugger::info(Debugger::VI, "VI_H_SYNC_REG <- {:08x}", value);
        state.hwreg.VI_H_SYNC_REG = value;
        return true;
    case VI_LEAP_REG:
        debugger::info(Debugger::VI, "VI_LEAP_REG <- {:08x}", value);
        state.hwreg.VI_LEAP_REG = value;
        return true;
    case VI_H_START_REG:
        debugger::info(Debugger::VI, "VI_H_START_REG <- {:08x}", value);
        state.hwreg.VI_H_START_REG = value;
        updateCurrentFramebuffer();
        return true;
    case VI_V_START_REG:
        debugger::info(Debugger::VI, "VI_V_START_REG <- {:08x}", value);
        state.hwreg.VI_V_START_REG = value;
        updateCurrentFramebuffer();
        return true;
    case VI_V_BURST_REG:
        debugger::info(Debugger::VI, "VI_V_BURST_REG <- {:08x}", value);
        state.hwreg.VI_V_BURST_REG = value;
        return true;
    case VI_X_SCALE_REG:
        debugger::info(Debugger::VI, "VI_X_SCALE_REG <- {:08x}", value);
        state.hwreg.VI_X_SCALE_REG = value;
        updateCurrentFramebuffer();
        return true;
    case VI_Y_SCALE_REG:
        debugger::info(Debugger::VI, "VI_Y_SCALE_REG <- {:08x}", value);
        state.hwreg.VI_Y_SCALE_REG = value;
        updateCurrentFramebuffer();
        return true;
    default:
        debugger::warn(Debugger::VI,
            "Write of unknown VI register: {:08x} <- {:08x}",
            addr, value);
        debugger::halt("VI write unknown");
        break;
    }
    return true;
}

};
