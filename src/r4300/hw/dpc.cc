
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <core.h>
#include <debugger.h>

namespace R4300 {

// (RW): [23:0] DMEM/RDRam start address
const u32 DPC_START_REG =           UINT32_C(0x04100000);
// (RW): [23:0] DMEM/RDRam end address
const u32 DPC_END_REG =             UINT32_C(0x04100004);
// (R):  [23:0] DMEM/RDRam current address
const u32 DPC_CURRENT_REG =         UINT32_C(0x04100008);
// (W): [0]  clear xbus_dmem_dma (R): [0]  xbus_dmem_dma
//      [1]  set xbus_dmem_dma        [1]  freeze
//      [2]  clear freeze             [2]  flush
//      [3]  set freeze               [3]  start gclk
//      [4]  clear flush              [4]  tmem busy
//      [5]  set flush                [5]  pipe busy
//      [6]  clear tmem ctr           [6]  cmd busy
//      [7]  clear pipe ctr           [7]  cbuf ready
//      [8]  clear cmd ctr            [8]  dma busy
//      [9]  clear clock ctr          [9]  end valid
//                                    [10] start valid
const u32 DPC_STATUS_REG =          UINT32_C(0x0410000c);
// (R): [23:0] clock counter
const u32 DPC_CLOCK_REG =           UINT32_C(0x04100010);
// (R): [23:0] buf busy counter
const u32 DPC_BUF_BUSY_REG =        UINT32_C(0x04100014);
// (R): [23:0] pipe busy counter
const u32 DPC_PIPE_BUSY_REG =       UINT32_C(0x04100018);
// (R): [23:0] tmem counter
const u32 DPC_TMEM_REG =            UINT32_C(0x0410001c);


bool read_DPC_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case DPC_START_REG:
        debugger::info(Debugger::DPCommand, "DPC_START_REG -> {:08x}",
            state.hwreg.DPC_START_REG);
        *value = state.hwreg.DPC_START_REG;
        break;
    case DPC_END_REG:
        debugger::info(Debugger::DPCommand, "DPC_END_REG -> {:08x}",
            state.hwreg.DPC_END_REG);
        *value = state.hwreg.DPC_END_REG;
        break;
    case DPC_STATUS_REG:
        *value = rdp::interface->read_DPC_STATUS_REG();
        debugger::info(Debugger::DPCommand, "DPC_STATUS_REG -> {:08x}", *value);
        break;
    case DPC_CURRENT_REG:
        *value = rdp::interface->read_DPC_CURRENT_REG();
        debugger::info(Debugger::DPCommand, "DPC_CURRENT_REG -> {:08x}", *value);
        break;
    case DPC_CLOCK_REG:
    case DPC_BUF_BUSY_REG:
    case DPC_PIPE_BUSY_REG:
    case DPC_TMEM_REG:
        *value = 0;
        return true;
    default:
        debugger::warn(Debugger::DPCommand,
            "Read of unknown DPCommand register: {:08x}", addr);
        core::halt("DPCommand read unknown");
        return false;
    }
    return true;
}

bool write_DPC_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case DPC_START_REG:     rdp::interface->write_DPC_START_REG(value); break;
    case DPC_END_REG:       rdp::interface->write_DPC_END_REG(value); break;
    case DPC_STATUS_REG:    rdp::interface->write_DPC_STATUS_REG(value); break;
    case DPC_CURRENT_REG:
    case DPC_CLOCK_REG:
    case DPC_BUF_BUSY_REG:
    case DPC_PIPE_BUSY_REG:
    case DPC_TMEM_REG:
        return true;
    default:
        debugger::warn(Debugger::DPCommand,
            "Write of unknown DPCommand register: {:08x} <- {:08x}",
            addr, value);
        core::halt("DPCommand write unknown");
        return false;
    }
    return true;
}

// DP tmem bist
//  (W): [0] BIST check           (R): [0] BIST check
//       [1] BIST go                   [1] BIST go
//       [2] BIST clear                [2] BIST done
//                                    [10:3] BIST fail
const u32 DPS_TBIST_REG = UINT32_C(0x0420000);

// DP span test mode
// (RW): [0] Span buffer test access enable
const u32 DPS_TEST_MODE_REG = UINT32_C(0x0420004);

// DP span buffer test address
// (RW): [6:0] bits
const u32 DPS_BUFTEST_ADDR_REG = UINT32_C(0x0420008);

// DP span buffer test data
// (RW): [31:0] span buffer data
const u32 DPS_BUFTEST_DATA_REG = UINT32_C(0x042000c);


bool read_DPS_REG(uint bytes, u64 addr, u64 *value)
{
    debugger::warn(Debugger::DPSpan, "Read of DPSpan register: {:08x}", addr);
    core::halt("DPSpan unsupported");
    *value = 0;
    return true;
}

bool write_DPS_REG(uint bytes, u64 addr, u64 value)
{
    debugger::warn(Debugger::DPSpan,
        "Write of DPSpan register: {:08x} <- {:08x}", addr, value);
    core::halt("DPSpan unsupported");
    return true;
}

}; /* namespace R4300 */
