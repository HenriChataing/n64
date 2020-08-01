
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <debugger.h>

namespace R4300 {

// PI DRAM address
// (RW): [23:0] starting RDRAM address
const u32 PI_DRAM_ADDR_REG =        UINT32_C(0x04600000);
// PI pbus (cartridge) address
// (RW): [31:0] starting AD16 address
const u32 PI_CART_ADDR_REG =        UINT32_C(0x04600004);
// PI read length
// (RW): [23:0] read data length
const u32 PI_RD_LEN_REG =           UINT32_C(0x04600008);
// PI write length
// (RW): [23:0] write data length
const u32 PI_WR_LEN_REG =           UINT32_C(0x0460000c);
// PI status
// (R): [0] DMA busy             (W): [0] reset controller
//      [1] IO busy                       (and abort current op)
//      [2] error                     [1] clear intr
const u32 PI_STATUS_REG =           UINT32_C(0x04600010);
// PI dom1 latency
// (RW): [7:0] domain 1 device latency
const u32 PI_BSD_DOM1_LAT_REG =     UINT32_C(0x04600014);
// PI dom1 pulse width
// (RW): [7:0] domain 1 device R/W strobe pulse width
const u32 PI_BSD_DOM1_PWD_REG =     UINT32_C(0x04600018);
// PI dom1 page size
// (RW): [3:0] domain 1 device page size
const u32 PI_BSD_DOM1_PGS_REG =     UINT32_C(0x0460001c);
// PI dom1 release
// (RW): [1:0] domain 1 device R/W release duration
const u32 PI_BSD_DOM1_RLS_REG =     UINT32_C(0x04600020);
// PI dom2 latency
// (RW): [7:0] domain 2 device latency
const u32 PI_BSD_DOM2_LAT_REG =     UINT32_C(0x04600024);
// PI dom2 pulse width
// (RW): [7:0] domain 2 device R/W strobe pulse width
const u32 PI_BSD_DOM2_PWD_REG =     UINT32_C(0x04600028);
// PI dom2 page size
// (RW): [3:0] domain 2 device page size
const u32 PI_BSD_DOM2_PGS_REG =     UINT32_C(0x0460002c);
// PI dom2 release
// (RW): [1:0] domain 2 device R/W release duration
const u32 PI_BSD_DOM2_RLS_REG =     UINT32_C(0x04600030);


/**
 * @brief Write the PI register PI_RD_LEN_REG.
 *  Writing the register starts a DMA tranfer from DRAM to cartridge memory.
 */
static void write_PI_RD_LEN_REG(u32 value) {
    debugger::info(Debugger::PI, "PI_RD_LEN_REG <- {:08x}", value);
    state.hwreg.PI_RD_LEN_REG = value;
    u32 len = value + 1;
    u32 dst = state.hwreg.PI_CART_ADDR_REG;
    u32 src = state.hwreg.PI_DRAM_ADDR_REG;

    // Check that the destination range fits in the cartridge memory, and in
    // particular does not overflow.
    if ((dst + len) <= dst ||
        dst < 0x10000000llu ||
        (dst + len) > 0x1fc00000llu) {
        debugger::warn(Debugger::PI,
            "PI_RD_LEN_REG destination range invalid: {:08x}+{:08x}",
            dst, len);
        return;
    }

    // Check that the source range fits in the dram memory, and in
    // particular does not overflow.
    if ((src + len) <= src ||
        (src + len) > 0x400000llu) {
        debugger::warn(Debugger::PI,
            "PI_RD_LEN_REG source range invalid: {:08x}+{:08x}",
            src, len);
        return;
    }

    // Perform the actual copy.
    // memcpy(&state.rom[dst - 0x10000000llu], &state.dram[src], len);
    state.hwreg.PI_STATUS_REG = 0;
    set_MI_INTR_REG(MI_INTR_PI);
}

/**
 * @brief Write the PI register PI_WR_LEN_REG.
 *  Writing the register starts a DMA tranfer from cartridge memory to DRAM.
 */
static void write_PI_WR_LEN_REG(u32 value) {
    debugger::info(Debugger::PI, "PI_WR_LEN_REG <- {:08x}", value);
    state.hwreg.PI_WR_LEN_REG = value;
    u32 len = value + 1;
    u32 dst = state.hwreg.PI_DRAM_ADDR_REG;
    u32 src = state.hwreg.PI_CART_ADDR_REG;

    // Check that the destination range fits in the dram memory, and in
    // particular does not overflow.
    if ((u32)(dst + len) <= dst ||
        (dst + len) > 0x400000llu) {
        debugger::warn(Debugger::PI,
            "PI_WR_LEN_REG destination range invalid: {:08x}+{:08x}",
            dst, len);
        return;
    }

    // Check that the source range fits in the cartridge memory, and in
    // particular does not overflow.
    if ((u32)(src + len) <= src ||
        src < 0x10000000llu ||
        (src + len) > 0x1fc00000llu) {
        debugger::warn(Debugger::PI,
            "PI_WR_LEN_REG source range invalid: {:08x}+{:08x}",
            src, len);
        return;
    }

    // Perform the actual copy.
    memcpy(&state.dram[dst], &state.rom[src - 0x10000000llu], len);
    state.hwreg.PI_STATUS_REG = 0;
    set_MI_INTR_REG(MI_INTR_PI);
}

bool read_PI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case PI_DRAM_ADDR_REG:
        debugger::info(Debugger::PI, "PI_DRAM_ADDR_REG -> {:08x}",
            state.hwreg.PI_DRAM_ADDR_REG);
        *value = state.hwreg.PI_DRAM_ADDR_REG;
        return true;
    case PI_CART_ADDR_REG:
        debugger::info(Debugger::PI, "PI_CART_ADDR_REG -> {:08x}",
            state.hwreg.PI_CART_ADDR_REG);
        *value = state.hwreg.PI_CART_ADDR_REG;
        return true;
    case PI_RD_LEN_REG:
        debugger::info(Debugger::PI, "PI_RD_LEN_REG -> {:08x}",
            state.hwreg.PI_RD_LEN_REG);
        *value = state.hwreg.PI_RD_LEN_REG;
        return true;
    case PI_WR_LEN_REG:
        debugger::info(Debugger::PI, "PI_WR_LEN_REG -> {:08x}",
            state.hwreg.PI_WR_LEN_REG);
        *value = state.hwreg.PI_WR_LEN_REG;
        return true;
    case PI_STATUS_REG:
        debugger::info(Debugger::PI, "PI_STATUS_REG -> {:08x}",
            state.hwreg.PI_STATUS_REG);
        *value = state.hwreg.PI_STATUS_REG;
        return true;
    case PI_BSD_DOM1_LAT_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_LAT_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM1_LAT_REG);
        *value = state.hwreg.PI_BSD_DOM1_LAT_REG;
        return true;
    case PI_BSD_DOM1_PWD_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_PWD_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM1_PWD_REG);
        *value = state.hwreg.PI_BSD_DOM1_PWD_REG;
        return true;
    case PI_BSD_DOM1_PGS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_PGS_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM1_PGS_REG);
        *value = state.hwreg.PI_BSD_DOM1_PGS_REG;
        return true;
    case PI_BSD_DOM1_RLS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_RLS_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM1_RLS_REG);
        *value = state.hwreg.PI_BSD_DOM1_RLS_REG;
        return true;
    case PI_BSD_DOM2_LAT_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_LAT_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM2_LAT_REG);
        *value = state.hwreg.PI_BSD_DOM2_LAT_REG;
        return true;
    case PI_BSD_DOM2_PWD_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_PWD_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM2_PWD_REG);
        *value = state.hwreg.PI_BSD_DOM2_PWD_REG;
        return true;
    case PI_BSD_DOM2_PGS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_PGS_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM2_PGS_REG);
        *value = state.hwreg.PI_BSD_DOM2_PGS_REG;
        return true;
    case PI_BSD_DOM2_RLS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_RLS_REG -> {:08x}",
            state.hwreg.PI_BSD_DOM2_RLS_REG);
        *value = state.hwreg.PI_BSD_DOM2_RLS_REG;
        return true;
    default:
        debugger::warn(Debugger::PI,
            "Read of unknown PI register: {:08x}", addr);
        debugger::halt("PI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_PI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case PI_DRAM_ADDR_REG:
        debugger::info(Debugger::PI, "PI_DRAM_ADDR_REG <- {:08x}", value);
        state.hwreg.PI_DRAM_ADDR_REG = value & PI_DRAM_ADDR_MASK;
        return true;
    case PI_CART_ADDR_REG:
        debugger::info(Debugger::PI, "PI_CART_ADDR_REG <- {:08x}", value);
        state.hwreg.PI_CART_ADDR_REG = value;
        return true;

    case PI_RD_LEN_REG:
        write_PI_RD_LEN_REG(value);
        return true;
    case PI_WR_LEN_REG:
        write_PI_WR_LEN_REG(value);
        return true;

    case PI_STATUS_REG:
        debugger::info(Debugger::PI, "PI_STATUS_REG <- {:08x}", value);
        state.hwreg.PI_STATUS_REG = 0;
        if (value & PI_STATUS_RESET) {
            // Expected behaviour not clearly known.
            debugger::halt("PI_STATUS_RESET");
        }
        if (value & PI_STATUS_CLR_INTR) {
            clear_MI_INTR_REG(MI_INTR_PI);
        }
        return true;
    case PI_BSD_DOM1_LAT_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_LAT_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM1_LAT_REG = value;
        return true;
    case PI_BSD_DOM1_PWD_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_PWD_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM1_PWD_REG = value;
        return true;
    case PI_BSD_DOM1_PGS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_PGS_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM1_PGS_REG = value;
        return true;
    case PI_BSD_DOM1_RLS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM1_RLS_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM1_RLS_REG = value;
        return true;
    case PI_BSD_DOM2_LAT_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_LAT_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM2_LAT_REG = value;
        return true;
    case PI_BSD_DOM2_PWD_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_PWD_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM2_PWD_REG = value;
        return true;
    case PI_BSD_DOM2_PGS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_PGS_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM2_PGS_REG = value;
        return true;
    case PI_BSD_DOM2_RLS_REG:
        debugger::info(Debugger::PI, "PI_BSD_DOM2_RLS_REG <- {:08x}", value);
        state.hwreg.PI_BSD_DOM2_RLS_REG = value;
        return true;
    default:
        debugger::warn(Debugger::PI,
            "Write of unknown PI register: {:08x} <- {:08x}",
            addr, value);
        debugger::halt("PI write unknown");
        break;
    }
    return true;
}

}; /* namespace R4300 */
