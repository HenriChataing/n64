
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <core.h>
#include <debugger.h>

namespace R4300 {

// SI DRAM address
// (R/W): [23:0] starting RDRAM address
const u32 SI_DRAM_ADDR_REG = UINT32_C(0x04800000);
// SI address read 64B
// (W): [] any write causes a 64B DMA write
const u32 SI_PIF_ADDR_RD64B_REG = UINT32_C(0x04800004);
// SI address write 64B
// (W): [] any write causes a 64B DMA read
const u32 SI_PIF_ADDR_WR64B_REG = UINT32_C(0x04800010);
// SI status
// (W): [] any write clears interrupt
// (R): [0] DMA busy
//      [1] IO read busy
//      [2] reserved
//      [3] DMA error
//      [12] interrupt
const u32 SI_STATUS_REG = UINT32_C(0x04800018);

/**
 * Command Types:
 *
 * | Command |       Description        |t |r |
 * +---------+--------------------------+-----+
 * |   00    |   request info           |01|03|
 * |   01    |   read button values     |01|04|
 * |   02    |   read from mempack slot |03|21|
 * |   03    |   write to mempack slot  |23|01|
 * |   04    |   read eeprom            |02|08|
 * |   05    |   write eeprom           |10|01|
 * |   ff    |   reset + request info   |01|03|
 *      NOTE: values are in hex
 *
 * Error bits (written to r byte)
 *    0x00 - no error, operation successful.
 *    0x80 - error, device not present for specified command.
 *    0x40 - error, unable to send/recieve the number bytes for command type.
 */

enum JoybusCommand {
    /*
     * Receive:
     * - Device identifier (2B)
     *    + 0x0000 Unknown
     *    + 0x0500 Controller
     *    + 0x0001 Voice Recognition Unit
     * - Device status (1B)
     *    + Controller
     *        + 0x1 Something plugged into the port
     *        + 0x2 Nothing plugged into the port
     *        + 0x4 Controller Read/Write CRC had an error
     *    + Voice Recognition Unit
     *        + 0x0 Uninitialized
     *        + 0x1 Initialized and ready for voice recognition
     */
    JOYBUS_INFO = 0x00,
    /*
     * Receive:
     * - Button Status (4B)
     *    + [31] A
     *    + [30] B
     *    + [29] Z
     *    + [28] Start
     *    + [27] dU
     *    + [26] dD
     *    + [25] dL
     *    + [24] dR
     *    + [23] Rst (LT + RT + Start together)
     *    + [22] Reserved
     *    + [21] LT
     *    + [20] RT
     *    + [19] cU
     *    + [18] cD
     *    + [17] cL
     *    + [16] cR
     *    + [15:8] X (two's complement, signed)
     *    + [7:0] Y (two's complement, signed)
     */
    JOYBUS_CONTROLLER_STATUS = 0x01,
    JOYBUS_MEMPACK_READ = 0x02,
    JOYBUS_MEMPACK_WRITE = 0x03,
    JOYBUS_EEPROM_READ = 0x04,
    JOYBUS_EEPROM_WRITE = 0x05,
    JOYBUS_RESET = 0xff,
};

/**
 * Write the value \p val to the PIF ram index \p index.
 */
static inline void write_pifram_byte(size_t index, uint8_t val) {
    state.pifram[index] = val;
}

/**
 * Evaluate a controller command.
 * The behavior implemented is described here:
 * https://raw.githubusercontent.com/mikeryan/n64dev/master/docs/n64dox.txt
 * https://sites.google.com/site/consoleprotocols/home/nintendo-joy-bus-documentation
 */
static void eval_PIF_controller_command(size_t index, uint8_t t, uint8_t r) {
    switch (state.pifram[index + 2]) {
    case JOYBUS_INFO:
        if (t != 1 || r != 3) {
            debugger::warn(Debugger::SI,
                "JOYBUS_INFO invalid t/r {}/{}", t, r);
            state.pifram[index + 1] |= 0x40;
            return;
        }
        write_pifram_byte(index + 3, 0x00);
        write_pifram_byte(index + 4, 0x00);
        write_pifram_byte(index + 5, 0x1);
        break;

    case JOYBUS_CONTROLLER_STATUS:
        if (t != 1 || r != 4) {
            debugger::warn(Debugger::SI,
                "JOYBUS_CONTROLLER_STATUS invalid t/r {}/{}", t, r);
            state.pifram[index + 1] |= 0x40;
            return;
        }
        write_pifram_byte(index + 3,
            (state.hwreg.buttons.A << 7) |
            (state.hwreg.buttons.B << 6) |
            (state.hwreg.buttons.Z << 5) |
            (state.hwreg.buttons.start << 4) |
            (state.hwreg.buttons.up << 3) |
            (state.hwreg.buttons.down << 2) |
            (state.hwreg.buttons.left << 1) |
            (state.hwreg.buttons.right << 0));
        write_pifram_byte(index + 4,
            (state.hwreg.buttons.L << 5) |
            (state.hwreg.buttons.R << 4) |
            (state.hwreg.buttons.C_up << 3) |
            (state.hwreg.buttons.C_down << 2) |
            (state.hwreg.buttons.C_left << 1) |
            (state.hwreg.buttons.C_right << 0));
        write_pifram_byte(index + 5, (unsigned)state.hwreg.buttons.x);
        write_pifram_byte(index + 6, (unsigned)state.hwreg.buttons.y);
        break;

    default:
        debugger::warn(Debugger::SI,
            "unknown JOYBUS command {:x}", state.pifram[index + 2]);
        state.pifram[index + 1] |= 0x80;
        break;
    }
}

/**
 * Evaluate the commands stored in the PIF RAM.
 * The behavior implemented is described here:
 * https://raw.githubusercontent.com/mikeryan/n64dev/master/docs/n64dox.txt
 */
static void eval_PIF_commands()
{
    size_t index = 0;
    size_t channel = 0;

    while (index < 0x3e) {
        size_t t = state.pifram[index];
        size_t r = state.pifram[index + 1];

        if (t == 0xfe) {
            /* Break command */
            break;
        }
        if (t & 0x80) {
            /* Negative length, discard transmit byte. */
            index++;
            continue;
        }
        if (t == 0) {
            /* Null command, increment the channel. */
            index++;
            channel++;
            continue;
        }

        if ((index + 2 + t) >= 0x3f ||
            (index + 2 + r) >= 0x3f) {
            /* Out of bound access. */
            state.pifram[index + 1] |= 0x40;
            break;
        }

        /* Call the command handle corresponding to the channel. */
        switch (channel) {
        case 0x0: eval_PIF_controller_command(index, t, r); break;
        // case 0x1: eval_PIF_controller_command(index, t, r); break;
        // case 0x2: eval_PIF_controller_command(index, t, r); break;
        // case 0x3: eval_PIF_controller_command(index, t, r); break;
        // case 0x4: eval_PIF_eeprom_command(index, t, r); break;
        // case 0x5: eval_PIF_eeprom_command(index, t, r); break;
        default: state.pifram[index + 1] |= 0x80;
        }

        channel++;
        index += t + r + 1;
    }

    state.pifram[0x3f] = 0;
}

/**
 * @brief Write the SI register SI_PIF_ADDR_RD64B_REG.
 *  Writing the register starts a DMA transfer from PIF ram to DRAM.
 */
static void write_SI_PIF_ADDR_RD64B_REG(u32 value)
{
    debugger::info(Debugger::SI, "SI_PIF_ADDR_RD64B_REG <- {:08x}", value);
    u32 dst = state.hwreg.SI_DRAM_ADDR_REG;

    // Check that the destination range fits in the dram memory, and in
    // particular does not overflow.
    if ((dst + 64) <= dst ||
        (dst + 64) > 0x400000llu) {
        debugger::warn(Debugger::SI,
            "SI_PIF_ADDR_RD64B_REG destination range invalid: {:08x}+64", dst);
        state.hwreg.SI_STATUS_REG = SI_STATUS_INTR | SI_STATUS_DMA_ERROR;
        set_MI_INTR_REG(MI_INTR_SI);
        return;
    }

    // Run the commands stored in the PIF ram.
    eval_PIF_commands();

    // Copy the result to the designated DRAM address.
    memcpy(state.dram + dst, state.pifram, 64);
    core::invalidate_recompiler_cache(dst, dst + 64);
    state.hwreg.SI_STATUS_REG = SI_STATUS_INTR;
    set_MI_INTR_REG(MI_INTR_SI);

    debugger::debug(Debugger::SI, "PIF response buffer:");
    for (size_t n = 0; n < 64; n+=8) {
        debugger::debug(Debugger::SI,
            "    {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
            state.pifram[n + 0], state.pifram[n + 1],
            state.pifram[n + 2], state.pifram[n + 3],
            state.pifram[n + 4], state.pifram[n + 5],
            state.pifram[n + 6], state.pifram[n + 7]);
    }
}

/**
 * @brief Write the SI register SI_PIF_ADDR_WR64B_REG.
 *  Writing the register starts a DMA transfer from DRAM to pif ram.
 */
static void write_SI_PIF_ADDR_WR64B_REG(u32 value)
{
    debugger::info(Debugger::SI, "SI_PIF_ADDR_WR64B_REG <- {:08x}", value);
    u32 src = state.hwreg.SI_DRAM_ADDR_REG;

    // Check that the sourece range fits in the dram memory, and in
    // particular does not overflow.
    if ((src + 64) <= src ||
        (src + 64) > 0x400000llu) {
        debugger::warn(Debugger::SI,
            "SI_PIF_ADDR_WR64B_REG source range invalid: {:08x}+64", src);
        state.hwreg.SI_STATUS_REG = SI_STATUS_INTR | SI_STATUS_DMA_ERROR;
        set_MI_INTR_REG(MI_INTR_SI);
        return;
    }

    memcpy(state.pifram, state.dram + src, 64);
    state.hwreg.SI_STATUS_REG = SI_STATUS_INTR;
    set_MI_INTR_REG(MI_INTR_SI);

    debugger::debug(Debugger::SI, "PIF command buffer:");
    for (size_t n = 0; n < 64; n+=8) {
        debugger::debug(Debugger::SI,
            "    {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
            state.pifram[n + 0], state.pifram[n + 1],
            state.pifram[n + 2], state.pifram[n + 3],
            state.pifram[n + 4], state.pifram[n + 5],
            state.pifram[n + 6], state.pifram[n + 7]);
    }
}

bool read_SI_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case SI_DRAM_ADDR_REG:
        debugger::info(Debugger::SI, "SI_DRAM_ADDR_REG -> {:08x}",
            state.hwreg.SI_DRAM_ADDR_REG);
        *value = state.hwreg.SI_DRAM_ADDR_REG;
        return true;
    case SI_PIF_ADDR_RD64B_REG:
        debugger::info(Debugger::SI, "SI_PIF_ADDR_RD64B_REG -> 0");
        *value = 0;
        return true;
    case SI_PIF_ADDR_WR64B_REG:
        debugger::info(Debugger::SI, "SI_PIF_ADDR_WR64B_REG -> 0");
        *value = 0;
        return true;
    case SI_STATUS_REG:
        debugger::info(Debugger::SI, "SI_STATUS_REG -> {:08x}",
            state.hwreg.SI_STATUS_REG);
        *value = state.hwreg.SI_STATUS_REG;
        return true;
    default:
        debugger::warn(Debugger::SI,
            "Read of unknown SI register: {:08x}", addr);
        core::halt("SI read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_SI_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case SI_DRAM_ADDR_REG:
        debugger::info(Debugger::SI, "SI_DRAM_ADDR_REG <- {:08x}", value);
        state.hwreg.SI_DRAM_ADDR_REG = value & SI_DRAM_ADDR_MASK;
        return true;

    case SI_PIF_ADDR_RD64B_REG:
        write_SI_PIF_ADDR_RD64B_REG(value);
        return true;
    case SI_PIF_ADDR_WR64B_REG:
        write_SI_PIF_ADDR_WR64B_REG(value);
        return true;

    case SI_STATUS_REG:
        debugger::info(Debugger::SI, "SI_STATUS_REG <- {:08x}", value);
        clear_MI_INTR_REG(MI_INTR_SI);
        state.hwreg.SI_STATUS_REG &= ~SI_STATUS_INTR;
        return true;

    default:
        debugger::warn(Debugger::SI,
            "Write of unknown SI register: {:08x} <- {:08x}",
            addr, value);
        core::halt("SI write unknown");
        break;
    }
    return true;
}

bool read_PIF_RAM(uint bytes, u64 addr, u64 *value)
{
    u64 offset = addr - UINT32_C(0x1fc00000);
    if (offset < 0x7c0 || offset >= 0x800)
        return false;
    *value = state.pifram[offset - 0x7c0];
    debugger::info(Debugger::PIF, "{:08x} -> {:08x}", addr, *value);
    return true;
}

bool write_PIF_RAM(uint bytes, u64 addr, u64 value)
{
    debugger::info(Debugger::PIF, "{:08x} <- {:08x}", addr, value);
    u64 offset = addr - UINT32_C(0x1fc00000);
    if (offset < 0x7c0 || offset >= 0x800)
        return false;

    state.pifram[offset - 0x7c0] = value;
    if ((state.pifram[0x3f] & 0x1) != 0)
        eval_PIF_commands();
    return true;
}

}; /* namespace R4300 */
