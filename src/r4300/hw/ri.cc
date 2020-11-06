
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <core.h>
#include <debugger.h>

namespace R4300 {

/*
 * Read only field describing the characteristics of the device.
 * The rambus exist in 18M and 64M format, with device type:
 *
 * [31:27]    col - number of column address bits (11(18M) or 11(64M))
 * [25]       bonus - specifies 8(0) or 9(1) byte length
 * [23:20]    bnk - number of bank address bits (1(18M) or 2(64M))
 * [19:16]    row - number of row address bits (9(18M) or 10(64M))
 * [11:8]     version - always 0010
 * [7:0]      type - always 0000
 */
const u32 RDRAM_DEVICE_TYPE_REG =   UINT32_C(0x03f00000);
const u32 RDRAM_DEVICE_ID_REG =     UINT32_C(0x03f00004);
const u32 RDRAM_DELAY_REG =         UINT32_C(0x03f00008);

/*
 * Read/Write register with fields that control the operating mode of
 * the RDRAM.
 *
 * [28]       AS     - specifies manual (0) or auto (1) t TR control. Set to 1.
 * [27]       SK     - specifies Skip value for manual t TR control. Set to 0.
 * [26]       SV     - skip value for auto t TR control. Read-only.
 * [25]       DE     - device Enable. Used during initialization.
 *            C(0:5) - specifies I OL output current. 111111b min, 000000b max.
 * [23:22]    C5,C2
 * [20]       FR     - force RXCLK,TXCLK on. FR = 1 => RDRAM Enable.
 * [18]       BASE   - set to 1 if Base RDRAMs with acknowledge are present.
 * [15:14]    C4,C1
 * [9]        CCAsym - current Control-Asymmetry adjustment.
 * [7:6]      C3,C0
 */
const u32 RDRAM_MODE_REG =          UINT32_C(0x03f0000c);
const u32 RDRAM_REF_INTERVAL_REG =  UINT32_C(0x03f00010);
const u32 RDRAM_REF_ROW_REG =       UINT32_C(0x03f00014);
const u32 RDRAM_RAS_INTERVAL_REG =  UINT32_C(0x03f00018);
const u32 RDRAM_MIN_INTERVAL_REG =  UINT32_C(0x03f0001c);
const u32 RDRAM_ADDR_SELECT_REG =   UINT32_C(0x03f00020);
const u32 RDRAM_DEVICE_MANUF_REG =  UINT32_C(0x03f00024);


bool read_RDRAM_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case RDRAM_DEVICE_TYPE_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_TYPE_REG -> {:08x}",
            state.hwreg.RDRAM_DEVICE_TYPE_REG);
        *value = state.hwreg.RDRAM_DEVICE_TYPE_REG;
        return true;
    case RDRAM_DEVICE_ID_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_ID_REG -> {:08x}",
            state.hwreg.RDRAM_DEVICE_ID_REG);
        *value = state.hwreg.RDRAM_DEVICE_ID_REG;
        return true;
    case RDRAM_DELAY_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DELAY_REG -> {:08x}",
            state.hwreg.RDRAM_DELAY_REG);
        *value = state.hwreg.RDRAM_DELAY_REG;
        return true;
    case RDRAM_MODE_REG:
        debugger::info(Debugger::RdRam, "RDRAM_MODE_REG -> {:08x}",
            state.hwreg.RDRAM_MODE_REG);
        *value = state.hwreg.RDRAM_MODE_REG;
        return true;
    case RDRAM_REF_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_REF_INTERVAL_REG -> {:08x}",
            state.hwreg.RDRAM_REF_INTERVAL_REG);
        *value = state.hwreg.RDRAM_REF_INTERVAL_REG;
        return true;
    case RDRAM_REF_ROW_REG:
        debugger::info(Debugger::RdRam, "RDRAM_REF_ROW_REG -> {:08x}",
            state.hwreg.RDRAM_REF_ROW_REG);
        *value = state.hwreg.RDRAM_REF_ROW_REG;
        return true;
    case RDRAM_RAS_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_RAS_INTERVAL_REG -> {:08x}",
            state.hwreg.RDRAM_RAS_INTERVAL_REG);
        *value = state.hwreg.RDRAM_RAS_INTERVAL_REG;
        return true;
    case RDRAM_MIN_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_MIN_INTERVAL_REG -> {:08x}",
            state.hwreg.RDRAM_MIN_INTERVAL_REG);
        *value = state.hwreg.RDRAM_MIN_INTERVAL_REG;
        return true;
    case RDRAM_ADDR_SELECT_REG:
        debugger::info(Debugger::RdRam, "RDRAM_ADDR_SELECT_REG -> {:08x}",
            state.hwreg.RDRAM_ADDR_SELECT_REG);
        *value = state.hwreg.RDRAM_ADDR_SELECT_REG;
        return true;
    case RDRAM_DEVICE_MANUF_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_MANUF_REG -> {:08x}",
            state.hwreg.RDRAM_DEVICE_MANUF_REG);
        *value = state.hwreg.RDRAM_DEVICE_MANUF_REG;
        return true;
    default:
        debugger::warn(Debugger::RdRam,
            "Read of unknown RdRam register: {:08x}", addr);
        core::halt("RdRam read unknown");
        break;
    }
    return true;
}

bool write_RDRAM_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case RDRAM_DEVICE_TYPE_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_TYPE_REG <- {:08x}", value);
        state.hwreg.RDRAM_DEVICE_TYPE_REG = value;
        return true;
    case RDRAM_DEVICE_ID_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_ID_REG <- {:08x}", value);
        state.hwreg.RDRAM_DEVICE_ID_REG = value;
        return true;
    case RDRAM_DELAY_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DELAY_REG <- {:08x}", value);
        state.hwreg.RDRAM_DELAY_REG = value;
        return true;
    case RDRAM_MODE_REG:
        debugger::info(Debugger::RdRam, "RDRAM_MODE_REG <- {:08x}", value);
        state.hwreg.RDRAM_MODE_REG = value;
        return true;
    case RDRAM_REF_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_REF_INTERVAL_REG <- {:08x}", value);
        state.hwreg.RDRAM_REF_INTERVAL_REG = value;
        return true;
    case RDRAM_REF_ROW_REG:
        debugger::info(Debugger::RdRam, "RDRAM_REF_ROW_REG <- {:08x}", value);
        state.hwreg.RDRAM_REF_ROW_REG = value;
        return true;
    case RDRAM_RAS_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_RAS_INTERVAL_REG <- {:08x}", value);
        state.hwreg.RDRAM_RAS_INTERVAL_REG = value;
        return true;
    case RDRAM_MIN_INTERVAL_REG:
        debugger::info(Debugger::RdRam, "RDRAM_MIN_INTERVAL_REG <- {:08x}", value);
        state.hwreg.RDRAM_MIN_INTERVAL_REG = value;
        return true;
    case RDRAM_ADDR_SELECT_REG:
        debugger::info(Debugger::RdRam, "RDRAM_ADDR_SELECT_REG <- {:08x}", value);
        state.hwreg.RDRAM_ADDR_SELECT_REG = value;
        return true;
    case RDRAM_DEVICE_MANUF_REG:
        debugger::info(Debugger::RdRam, "RDRAM_DEVICE_MANUF_REG <- {:08x}", value);
        state.hwreg.RDRAM_DEVICE_MANUF_REG = value;
        return true;

    /* Unknown registers accessed by CIC-NUS-6102 bootcode */
    case UINT32_C(0x03f80004):
    case UINT32_C(0x03f80008):
    case UINT32_C(0x03f8000c):
    case UINT32_C(0x03f80014):
    case UINT32_C(0x03f04004):
        return true;

    default:
        debugger::warn(Debugger::RdRam,
            "Write of unknown RdRam register: {:08x} <- {:08x}",
            addr, value);
        core::halt("RdRam write unknown");
        break;
    }
    return true;
}

// (RW): [1:0] operating mode
//       [2] stop T active
//       [3] stop R active
const u32 RI_MODE_REG =         UINT32_C(0x04700000);
// (RW): [5:0] current control input
//       [6] current control enable
const u32 RI_CONFIG_REG =       UINT32_C(0x04700004);
// (W): [] any write updates current control register
const u32 RI_CURRENT_LOAD_REG = UINT32_C(0x04700008);
// (RW): [2:0] receive select
//       [2:0] transmit select
const u32 RI_SELECT_REG =       UINT32_C(0x0470000c);
// (RW): [7:0] clean refresh delay
//       [15:8] dirty refresh delay
//       [16] refresh bank
//       [17] refresh enable
//       [18] refresh optimize
const u32 RI_REFRESH_REG =      UINT32_C(0x04700010);
// (RW): [3:0] DMA latency/overlap
const u32 RI_LATENCY_REG =      UINT32_C(0x04700014);
// (R): [0] nack error
//      [1] ack error
const u32 RI_RERROR_REG  =      UINT32_C(0x04700018);
// (W): [] any write clears all error bits
const u32 RI_WERROR_REG =       UINT32_C(0x0470001c);


bool read_RI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case RI_MODE_REG:
        debugger::info(Debugger::RI, "RI_MODE_REG -> {:08x}",
            state.hwreg.RI_MODE_REG);
        *value = state.hwreg.RI_MODE_REG;
        return true;
    case RI_CONFIG_REG:
        debugger::info(Debugger::RI, "RI_CONFIG_REG -> {:08x}",
            state.hwreg.RI_CONFIG_REG);
        *value = state.hwreg.RI_CONFIG_REG;
        return true;
    case RI_CURRENT_LOAD_REG:
        debugger::info(Debugger::RI, "RI_CURRENT_LOAD_REG -> NA");
        return true;
    case RI_SELECT_REG:
        debugger::info(Debugger::RI, "RI_SELECT_REG -> {:08x}",
            state.hwreg.RI_SELECT_REG);
        *value = state.hwreg.RI_SELECT_REG;
        return true;
    case RI_REFRESH_REG:
        debugger::info(Debugger::RI, "RI_REFRESH_REG -> {:08x}",
            state.hwreg.RI_REFRESH_REG);
        *value = state.hwreg.RI_REFRESH_REG;
        return true;
    case RI_LATENCY_REG:
        debugger::info(Debugger::RI, "RI_LATENCY_REG -> {:08x}",
            state.hwreg.RI_LATENCY_REG);
        *value = state.hwreg.RI_LATENCY_REG;
        return true;
    case RI_RERROR_REG:
        debugger::info(Debugger::RI, "RI_RERROR_REG -> {:08x}",
            state.hwreg.RI_RERROR_REG);
        *value = state.hwreg.RI_RERROR_REG;
        return true;
    case RI_WERROR_REG:
        debugger::info(Debugger::RI, "RI_WERROR_REG -> NA");
        return true;
    default:
        debugger::warn(Debugger::RI,
            "Read of unknown RI register: {:08x}", addr);
        core::halt("RI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_RI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case RI_MODE_REG:
        debugger::info(Debugger::RI, "RI_MODE_REG <- {:08x}", value);
        state.hwreg.RI_MODE_REG = value;
        return true;
    case RI_CONFIG_REG:
        debugger::info(Debugger::RI, "RI_CONFIG_REG <- {:08x}", value);
        state.hwreg.RI_CONFIG_REG = value;
        return true;
    case RI_CURRENT_LOAD_REG:
        debugger::info(Debugger::RI, "RI_CURRENT_LOAD_REG <- {:08x}", value);
        // TODO: any write updates the control register
        return true;
    case RI_SELECT_REG:
        debugger::info(Debugger::RI, "RI_SELECT_REG <- {:08x}", value);
        state.hwreg.RI_SELECT_REG = value;
        return true;
    case RI_REFRESH_REG:
        debugger::info(Debugger::RI, "RI_REFRESH_REG <- {:08x}", value);
        state.hwreg.RI_REFRESH_REG = value;
        return true;
    case RI_LATENCY_REG:
        debugger::info(Debugger::RI, "RI_LATENCY_REG <- {:08x}", value);
        state.hwreg.RI_LATENCY_REG = value;
        return true;
    case RI_RERROR_REG:
        debugger::info(Debugger::RI, "RI_RERROR_REG <- {:08x}", value);
        return true;
    case RI_WERROR_REG:
        debugger::info(Debugger::RI, "RI_WERROR_REG <- {:08x}", value);
        state.hwreg.RI_RERROR_REG = 0;
        return true;
    default:
        debugger::warn(Debugger::RI,
            "Write of unknown RI register: {:08x} <- {:08x}",
            addr, value);
        core::halt("RI write unknown");
        break;
    }
    return true;
}

}; /* namespace R4300 */
