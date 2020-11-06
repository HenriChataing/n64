
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
 * |   00    |   request info (status)  |01|03|
 * |   01    |   read button values     |01|04|
 * |   02    |   read from mempack slot |03|21|
 * |   03    |   write to mempack slot  |23|01|
 * |   04    |   read eeprom            |02|08|
 * |   05    |   write eeprom           |10|01|
 * |   ff    |   reset                  |01|03|
 *      NOTE: values are in hex
 *
 * Error bits (written to r byte)
 *    0x00 - no error, operation successful.
 *    0x80 - error, device not present for specified command.
 *    0x40 - error, unable to send/recieve the number bytes for command type.
 *
 * Button bits:
 *    unsigned A : 1;
 *    unsigned B : 1;
 *    unsigned Z : 1;
 *    unsigned start : 1;
 *    unsigned up : 1;
 *    unsigned down : 1;
 *    unsigned left : 1;
 *    unsigned right : 1;
 *    unsigned : 2;
 *    unsigned L : 1;
 *    unsigned R : 1;
 *    unsigned C_up : 1;
 *    unsigned C_down : 1;
 *    unsigned C_left : 1;
 *    unsigned C_right : 1;
 *    signed x : 8;
 *    signed y : 8;
 */

static void eval_PIF_commands()
{
    size_t i = 0;
    size_t channel = 0;

    while (i < 0x3e)
    {
        uint8_t t = state.pifram[i];
        size_t ir = i + 1;

        if (t == 0xfeu) {
            break; /* Break command */
        }
        if (t & 0x80u) {
            /* Negative length, discard transmit byte and retry */
            i = ir;
            continue;
        }
        if (t == 0) {
            /* Null command, increment the channel and retry */
            i = ir;
            channel++;
            continue;
        }

        uint8_t r = state.pifram[ir];
        size_t itbuf = ir + 1;
        size_t irbuf = itbuf + t;

        if (irbuf >= 0x40u) {
            state.pifram[ir] |= 0x40u;
            break; /* Transmit overflow */
        }

#define write_pifram_byte(nr, val) \
    ({  if ((nr) < r && (irbuf + (nr)) < 0x3fu) \
            state.pifram[irbuf + (nr)] = (val); \
        else \
            state.pifram[ir] |= 0x40u; \
    })

        /* Interpret command byte. */
        if (channel < 4) {
            switch (state.pifram[itbuf]) {
                case 0x0: /* Request info */
                    if (t != 0x1) {
                        state.pifram[ir] |= 0x40u;
                    }
                    if (r != 0x3) {
                        state.pifram[ir] |= 0x40u;
                    }
                    write_pifram_byte(0, 0x00); /* Model byte 0 */
                    write_pifram_byte(1, 0x00); /* Model byte 1 */
                    /* Status: 1= something plugged,
                     *         2= nothing plugged,
                     *         4= pad address CRC (mempack) */
                    write_pifram_byte(2, channel == 0 ? 0x1 : 0x2);
                    break;

                case 0x1: /* Read button values */
                    if (t != 0x1) {
                        state.pifram[ir] |= 0x40u;
                    }
                    if (r != 0x4) {
                        state.pifram[ir] |= 0x40u;
                    }
                    if (channel != 0) {
                        state.pifram[ir] |= 0x80u;
                        break;
                    }

                    write_pifram_byte(0,
                        (state.hwreg.buttons.A << 7) |
                        (state.hwreg.buttons.B << 6) |
                        (state.hwreg.buttons.Z << 5) |
                        (state.hwreg.buttons.start << 4) |
                        (state.hwreg.buttons.up << 3) |
                        (state.hwreg.buttons.down << 2) |
                        (state.hwreg.buttons.left << 1) |
                        (state.hwreg.buttons.right << 0));
                    write_pifram_byte(1,
                        (state.hwreg.buttons.L << 5) |
                        (state.hwreg.buttons.R << 4) |
                        (state.hwreg.buttons.C_up << 3) |
                        (state.hwreg.buttons.C_down << 2) |
                        (state.hwreg.buttons.C_left << 1) |
                        (state.hwreg.buttons.C_right << 0));
                    write_pifram_byte(2, (unsigned)state.hwreg.buttons.x);
                    write_pifram_byte(3, (unsigned)state.hwreg.buttons.y);
                    break;

                default:
                    state.pifram[ir] |= 0x80u;
                    break;
            }
        } else {
            switch (state.pifram[itbuf]) {
                case 0x0: /* Request info */
                    if (t != 0x1) {
                        state.pifram[ir] |= 0x40u;
                    }
                    if (r != 0x3) {
                        state.pifram[ir] |= 0x40u;
                    }
                    write_pifram_byte(0, 0x00); /* Model byte 0 */
                    write_pifram_byte(1, 0x00); /* Model byte 1 */
                    /* Status: 1= something plugged,
                     *         2= nothing plugged,
                     *         4= pad address CRC (mempack) */
                    write_pifram_byte(2, 0x2); // channel == 4 ? 0x1 : 0x2);
                    break;

                default:
                    state.pifram[ir] |= 0x80u;
                    break;
            }
        }

        channel++;
        i = irbuf + r;
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
