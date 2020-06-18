
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <debugger.h>

namespace R4300 {

// MI init mode
// (W): [6:0] init length        (R): [6:0] init length
//      [7] clear init mode           [7] init mode
//      [8] set init mode             [8] ebus test mode
//      [9/10] clr/set ebus test mode [9] RDRAM reg mode
//      [11] clear DP interrupt
//      [12] clear RDRAM reg
//      [13] set RDRAM reg mode
const u32 MI_MODE_REG = UINT32_C(0x04300000);
// MI version
// (R): [7:0] io
//      [15:8] rac
//      [23:16] rdp
//      [31:24] rsp
const u32 MI_VERSION_REG = UINT32_C(0x04300004);
// MI interrupt
// (R): [0] SP intr
//      [1] SI intr
//      [2] AI intr
//      [3] VI intr
//      [4] PI intr
//      [5] DP intr
const u32 MI_INTR_REG = UINT32_C(0x04300008);
// MI interrupt mask
// (W): [0/1] clear/set SP mask  (R): [0] SP intr mask
//      [2/3] clear/set SI mask       [1] SI intr mask
//      [4/5] clear/set AI mask       [2] AI intr mask
//      [6/7] clear/set VI mask       [3] VI intr mask
//      [8/9] clear/set PI mask       [4] PI intr mask
//      [10/11] clear/set DP mask     [5] DP intr mask
const u32 MI_INTR_MASK_REG = UINT32_C(0x0430000c);


/**
 * Set bits in the MI_INTR_REG register.
 * Reevaluate the value of the Interrupt 2 pending bit afterwards.
 */
void set_MI_INTR_REG(u32 bits) {
    debugger::info(Debugger::MI, "MI_INTR_REG |= {:x}", bits);
    state.hwreg.MI_INTR_REG |= bits;
    if (state.hwreg.MI_INTR_REG & state.hwreg.MI_INTR_MASK_REG) {
        setInterruptPending(2);
    } else {
        clearInterruptPending(2);
    }
}

/**
 * Clear bits in the MI_INTR_REG register.
 * Reevaluate the value of the Interrupt 2 pending bit afterwards.
 */
void clear_MI_INTR_REG(u32 bits) {
    debugger::info(Debugger::MI, "MI_INTR_REG &= ~{:x}", bits);
    state.hwreg.MI_INTR_REG &= ~bits;
    if (state.hwreg.MI_INTR_REG & state.hwreg.MI_INTR_MASK_REG) {
        setInterruptPending(2);
    } else {
        clearInterruptPending(2);
    }
}

static void write_MI_MODE_REG(u32 value) {
    debugger::info(Debugger::MI, "MI_MODE_REG <- {:08x}", value);
    state.hwreg.MI_MODE_REG &= MI_MODE_INIT_LEN_MASK;
    state.hwreg.MI_MODE_REG |= (value & MI_MODE_INIT_LEN_MASK);
    if (value & MI_MODE_CLR_INIT) {
        state.hwreg.MI_MODE_REG &= ~MI_MODE_INIT;
    }
    if (value & MI_MODE_SET_INIT) {
        state.hwreg.MI_MODE_REG |= MI_MODE_INIT;
    }
    if (value & MI_MODE_CLR_EBUS_TEST) {
        state.hwreg.MI_MODE_REG &= ~MI_MODE_EBUS_TEST;
    }
    if (value & MI_MODE_SET_EBUS_TEST) {
        state.hwreg.MI_MODE_REG |= MI_MODE_EBUS_TEST;
    }
    if (value & MI_MODE_CLR_DP_INTR) {
        clear_MI_INTR_REG(MI_INTR_DP);
    }
    if (value & MI_MODE_CLR_RDRAM_REG) {
        state.hwreg.MI_MODE_REG &= ~MI_MODE_RDRAM_REG;
    }
    if (value & MI_MODE_SET_RDRAM_REG) {
        state.hwreg.MI_MODE_REG |= MI_MODE_RDRAM_REG;
    }
}

static void write_MI_INTR_MASK_REG(u32 value) {
    debugger::info(Debugger::MI, "MI_INTR_MASK_REG <- {:08x}", value);
    if (value & MI_INTR_MASK_CLR_SP) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_SP;
    }
    if (value & MI_INTR_MASK_SET_SP) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_SP;
    }
    if (value & MI_INTR_MASK_CLR_SI) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_SI;
    }
    if (value & MI_INTR_MASK_SET_SI) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_SI;
    }
    if (value & MI_INTR_MASK_CLR_AI) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_AI;
    }
    if (value & MI_INTR_MASK_SET_AI) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_AI;
    }
    if (value & MI_INTR_MASK_CLR_VI) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_VI;
    }
    if (value & MI_INTR_MASK_SET_VI) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_VI;
    }
    if (value & MI_INTR_MASK_CLR_PI) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_PI;
    }
    if (value & MI_INTR_MASK_SET_PI) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_PI;
    }
    if (value & MI_INTR_MASK_CLR_DP) {
        state.hwreg.MI_INTR_MASK_REG &= ~MI_INTR_MASK_DP;
    }
    if (value & MI_INTR_MASK_SET_DP) {
        state.hwreg.MI_INTR_MASK_REG |= MI_INTR_MASK_DP;
    }
    checkInterrupt();
}

bool read_MI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case MI_MODE_REG:
        debugger::info(Debugger::MI, "MI_MODE_REG -> {:08x}",
            state.hwreg.MI_MODE_REG);
        *value = state.hwreg.MI_MODE_REG;
        return true;
    case MI_VERSION_REG:
        debugger::info(Debugger::MI, "MI_VERSION_REG -> {:08x}",
            state.hwreg.MI_VERSION_REG);
        *value = state.hwreg.MI_VERSION_REG;
        return true;
    case MI_INTR_REG:
        debugger::info(Debugger::MI, "MI_INTR_REG -> {:08x}",
            state.hwreg.MI_INTR_REG);
        *value = state.hwreg.MI_INTR_REG;
        return true;
    case MI_INTR_MASK_REG:
        debugger::info(Debugger::MI, "MI_INTR_MASK_REG -> {:08x}",
            state.hwreg.MI_INTR_MASK_REG);
        *value = state.hwreg.MI_INTR_MASK_REG;
        return true;
    default:
        debugger::warn(Debugger::MI,
            "Read of unknown MI register: {:08x}", addr);
        debugger::halt("MI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_MI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case MI_MODE_REG:
        write_MI_MODE_REG(value);
        return true;
    case MI_VERSION_REG:
        debugger::info(Debugger::MI, "MI_VERSION_REG <- {:08x}", value);
        return true;
    case MI_INTR_REG:
        debugger::info(Debugger::MI, "MI_INTR_REG <- {:08x}", value);
        return true;
    case MI_INTR_MASK_REG:
        write_MI_INTR_MASK_REG(value);
        return true;
    default:
        debugger::warn(Debugger::MI,
            "Write of unknown MI register: {:08x} <- {:08x}",
            addr, value);
        debugger::halt("MI write unknown");
        break;
    }
    return true;
}

}; /* namespace R4300 */
