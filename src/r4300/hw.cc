
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <graphics.h>
#include <debugger.h>

namespace R4300 {

static inline void logWrite(bool flag, const char *tag, u64 value)
{
    if (flag) {
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << " <- " << std::hex << value << std::endl;
    }
}

static inline void logWriteAtAddr(bool flag, const char *tag, u64 addr, u64 value)
{
    if (flag) {
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << ":" << std::hex << addr;
        std::cerr << " <- " << std::hex << value << std::endl;
    }
}

static inline void logRead(bool flag, const char *tag, u64 value)
{
    if (flag) {
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << " -> " << std::hex << value << std::endl;
    }
}

static inline void logReadAtAddr(bool flag, const char *tag, u64 addr, u64 value)
{
    if (flag) {
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << ":" << std::hex << addr;
        std::cerr << " -> " << std::hex << value << std::endl;
    }
}

/**
 * Set bits in the MI_INTR_REG register.
 * Reevaluate the value of the Interrupt 2 pending bit afterwards.
 */
void set_MI_INTR_REG(u32 bits) {
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
    state.hwreg.MI_INTR_REG &= ~bits;
    if (state.hwreg.MI_INTR_REG & ~state.hwreg.MI_INTR_MASK_REG) {
        setInterruptPending(2);
    } else {
        clearInterruptPending(2);
    }
}

/** @brief Called for VI interrupts. */
void raise_VI_INTR(void) {
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
 * @brief Write the SP register SP_RD_LEN_REG.
 *  Writing the register starts a DMA tranfer from DRAM to DMEM/IMEM.
 */
void write_SP_RD_LEN_REG(u32 value) {
    logWrite(debugger.verbose.SP, "SP_RD_LEN_REG", value);
    state.hwreg.SP_RD_LEN_REG = value;
    u32 len = 1u + (value & SP_RD_LEN_LEN_MASK);
    u32 count = 1u + ((value >> SP_RD_LEN_COUNT_SHIFT) & SP_RD_LEN_COUNT_MASK);
    u32 skip = (value >> SP_RD_LEN_SKIP_SHIFT) & SP_RD_LEN_SKIP_MASK;
    u32 offset = state.hwreg.SP_MEM_ADDR_REG & SP_MEM_ADDR_MASK;
    u64 dst_base = 0x04000000llu + offset;
    u64 src_base = state.hwreg.SP_DRAM_ADDR_REG & SP_DRAM_ADDR_MASK;
    // @todo skip+len must be aligned to 8
    // @todo clear/set DMA busy+full bits.
    for (; count > 0; count--, src_base += len + skip, dst_base += len) {
        // @todo access memory buffers directly
        //  it is an error if DRAM_ADDR is not inside the Ram.
        state.physmem.copy(dst_base, src_base, len);
    }
    set_MI_INTR_REG(MI_INTR_SP);
}

/**
 * @brief Write the SP register SP_WR_LEN_REG.
 *  Writing the register starts a DMA tranfer from DMEM/IMEM to DRAM.
 */
void write_SP_WR_LEN_REG(u32 value) {
    logWrite(debugger.verbose.SP, "SP_RD_LEN_REG", value);
    state.hwreg.SP_RD_LEN_REG = value;
    u32 len = 1u + (value & SP_RD_LEN_LEN_MASK);
    u32 count = 1u + ((value >> SP_RD_LEN_COUNT_SHIFT) & SP_RD_LEN_COUNT_MASK);
    u32 skip = (value >> SP_RD_LEN_SKIP_SHIFT) & SP_RD_LEN_SKIP_MASK;
    u32 offset = state.hwreg.SP_MEM_ADDR_REG & SP_MEM_ADDR_MASK;
    u64 dst_base = state.hwreg.SP_DRAM_ADDR_REG & SP_DRAM_ADDR_MASK;
    u64 src_base = 0x04000000llu + offset;
    // @todo skip+len must be aligned to 8
    // @todo clear/set DMA busy+full bits.
    for (; count > 0; count--, src_base += len, dst_base += len + skip) {
        // @todo access memory buffers directly
        //  it is an error if DRAM_ADDR is not inside the Ram.
        state.physmem.copy(dst_base, src_base, len);
    }
    set_MI_INTR_REG(MI_INTR_SP);
}

/**
 * @brief Write the SP register SP_STATUS_REG.
 *  This function is used for both the CPU (SP_STATUS_REG) and
 *  RSP (Coprocessor 0 register 4) view of the register.
 */
void write_SP_STATUS_REG(u32 value) {
    logWrite(debugger.verbose.SP, "SP_STATUS_REG", value);
    if (value & SP_STATUS_CLR_HALT) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_HALT;
    }
    if (value & SP_STATUS_SET_HALT) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_HALT;
    }
    if (value & SP_STATUS_CLR_BROKE) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_BROKE;
    }
    if (value & SP_STATUS_CLR_INTR) {
        clear_MI_INTR_REG(MI_INTR_SP);
    }
    if (value & SP_STATUS_SET_INTR) {
        throw "set_intr";
    }
    if (value & SP_STATUS_CLR_SSTEP) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SSTEP;
    }
    if (value & SP_STATUS_SET_SSTEP) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SSTEP;
    }
    if (value & SP_STATUS_CLR_INTR_BREAK) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_INTR_BREAK;
    }
    if (value & SP_STATUS_SET_INTR_BREAK) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_INTR_BREAK;
    }
    if (value & SP_STATUS_CLR_SIGNAL0) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL0;
    }
    if (value & SP_STATUS_SET_SIGNAL0) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL0;
    }
    if (value & SP_STATUS_CLR_SIGNAL1) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL1;
    }
    if (value & SP_STATUS_SET_SIGNAL1) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL1;
    }
    if (value & SP_STATUS_CLR_SIGNAL2) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL2;
    }
    if (value & SP_STATUS_SET_SIGNAL2) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL2;
    }
    if (value & SP_STATUS_CLR_SIGNAL3) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL3;
    }
    if (value & SP_STATUS_SET_SIGNAL3) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL3;
    }
    if (value & SP_STATUS_CLR_SIGNAL4) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL4;
    }
    if (value & SP_STATUS_SET_SIGNAL4) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL4;
    }
    if (value & SP_STATUS_CLR_SIGNAL5) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL5;
    }
    if (value & SP_STATUS_SET_SIGNAL5) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL5;
    }
    if (value & SP_STATUS_CLR_SIGNAL6) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL6;
    }
    if (value & SP_STATUS_SET_SIGNAL6) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL6;
    }
    if (value & SP_STATUS_CLR_SIGNAL7) {
        state.hwreg.SP_STATUS_REG &= ~SP_STATUS_SIGNAL7;
    }
    if (value & SP_STATUS_SET_SIGNAL7) {
        state.hwreg.SP_STATUS_REG |= SP_STATUS_SIGNAL7;
    }
}

/**
 * @brief Read the value of the SP_SEMAPHORE_REG register.
 *  The semaphore is set to 1 as a consequence.
 */
u32 read_SP_SEMAPHORE_REG() {
    logRead(debugger.verbose.SP, "SP_SEMAPHORE_REG",
            state.hwreg.SP_SEMAPHORE_REG);
    u32 value = state.hwreg.SP_SEMAPHORE_REG;
    state.hwreg.SP_SEMAPHORE_REG = 1;
    return value;
}

/**
 * @brief Write the PI register PI_RD_LEN_REG.
 *  Writing the register starts a DMA tranfer from DRAM to cartridge memory.
 */
void write_PI_RD_LEN_REG(u32 value) {
    logWrite(debugger.verbose.PI, "PI_RD_LEN_REG", value);
    state.hwreg.PI_RD_LEN_REG = value;
    u32 len = value + 1;
    u32 dst = state.hwreg.PI_CART_ADDR_REG;
    u32 src = state.hwreg.PI_DRAM_ADDR_REG;

    // Check that the destination range fits in the cartridge memory, and in
    // particular does not overflow.
    if ((dst + len) <= dst ||
        dst < 0x10000000llu ||
        (dst + len) > 0x1fc00000llu) {
        std::cerr << "write_PI_RD_LEN_REG() destination range invalid" << std::endl;
        return;
    }

    // Check that the source range fits in the dram memory, and in
    // particular does not overflow.
    if ((src + len) <= src ||
        (src + len) > 0x400000llu) {
        std::cerr << "write_PI_RD_LEN_REG() source range invalid" << std::endl;
        return;
    }

    // Perform the actual copy.
    state.physmem.copy(dst, src, len);
    state.hwreg.PI_STATUS_REG = 0;
    set_MI_INTR_REG(MI_INTR_PI);
}

/**
 * @brief Write the PI register PI_WR_LEN_REG.
 *  Writing the register starts a DMA tranfer from cartridge memory to DRAM.
 */
void write_PI_WR_LEN_REG(u32 value) {
    logWrite(debugger.verbose.PI, "PI_WR_LEN_REG", value);
    state.hwreg.PI_WR_LEN_REG = value;
    u32 len = value + 1;
    u32 dst = state.hwreg.PI_DRAM_ADDR_REG;
    u32 src = state.hwreg.PI_CART_ADDR_REG;

    // Check that the destination range fits in the dram memory, and in
    // particular does not overflow.
    if ((dst + len) <= dst ||
        (dst + len) > 0x400000llu) {
        std::cerr << "write_PI_RD_LEN_REG() destination range invalid" << std::endl;
        return;
    }

    // Check that the source range fits in the cartridge memory, and in
    // particular does not overflow.
    if ((src + len) <= src ||
        src < 0x10000000llu ||
        (src + len) > 0x1fc00000llu) {
        std::cerr << "write_PI_RD_LEN_REG() source range invalid" << std::endl;
        return;
    }

    // Perform the actual copy.
    state.physmem.copy(dst, src, len);
    state.hwreg.PI_STATUS_REG = 0;
    set_MI_INTR_REG(MI_INTR_PI);
}


/*

           Command Types:

         | Command |       Description        |t |r |
         +---------+--------------------------+-----+
         |   00    |   request info (status)  |01|03|
         |   01    |   read button values     |01|04|
         |   02    |   read from mempack slot |03|21|
         |   03    |   write to mempack slot  |23|01|
         |   04    |   read eeprom            |02|08|
         |   05    |   write eeprom           |10|01|
         |   ff    |   reset                  |01|03|
              NOTE: values are in hex

     Error bits (written to r byte)
      0x00 - no error, operation successful.
      0x80 - error, device not present for specified command.
      0x40 - error, unable to send/recieve the number bytes for command type.

     Button bits:
        unsigned A : 1;
        unsigned B : 1;
        unsigned Z : 1;
        unsigned start : 1;
        unsigned up : 1;
        unsigned down : 1;
        unsigned left : 1;
        unsigned right : 1;
        unsigned : 2;
        unsigned L : 1;
        unsigned R : 1;
        unsigned C_up : 1;
        unsigned C_down : 1;
        unsigned C_left : 1;
        unsigned C_right : 1;
        signed x : 8;
        signed y : 8;
 */

void eval_PIF_commands()
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
                    write_pifram_byte(0, 0x00);
                    write_pifram_byte(1, 0x00);
                    write_pifram_byte(2, 0x00);
                    write_pifram_byte(3, 0x00);
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
void write_SI_PIF_ADDR_RD64B_REG(u32 value)
{
    logWrite(debugger.verbose.SI, "SI_PIF_ADDR_RD64B_REG", value);
    u32 dst = state.hwreg.SI_DRAM_ADDR_REG;

    // Check that the destination range fits in the dram memory, and in
    // particular does not overflow.
    if ((dst + 64) <= dst ||
        (dst + 64) > 0x400000llu) {
        std::cerr << "write_SI_PIF_ADDR_RD64B_REG() destination range invalid: ";
        std::cerr << std::hex << dst << std::endl;
        state.hwreg.SI_STATUS_REG = SI_STATUS_INTR | SI_STATUS_DMA_ERROR;
        set_MI_INTR_REG(MI_INTR_SI);
        return;
    }

    memcpy(state.dram + dst, state.pifram, 64);
    state.hwreg.SI_STATUS_REG = SI_STATUS_INTR;
    set_MI_INTR_REG(MI_INTR_SI);

    if (debugger.verbose.SI) {
        std::cerr << "PIF response buffer:" << std::endl;
        for (size_t n = 0; n < 64; n++) {
            if (n && !(n % 16))
                std::cerr << std::endl;
            std::cerr << std::hex << " ";
            std::cerr << std::setw(2) << (unsigned)state.pifram[n];
        }
        std::cerr << std::endl;
    }
}

/**
 * @brief Write the SI register SI_PIF_ADDR_WR64B_REG.
 *  Writing the register starts a DMA transfer from DRAM to pif ram.
 */
void write_SI_PIF_ADDR_WR64B_REG(u32 value)
{
    logWrite(debugger.verbose.SI, "SI_PIF_ADDR_WR64B_REG", value);
    u32 src = state.hwreg.SI_DRAM_ADDR_REG;

    // Check that the sourece range fits in the dram memory, and in
    // particular does not overflow.
    if ((src + 64) <= src ||
        (src + 64) > 0x400000llu) {
        std::cerr << "write_SI_PIF_ADDR_RD64B_REG() source range invalid";
        std::cerr << std::endl;
        state.hwreg.SI_STATUS_REG = SI_STATUS_INTR | SI_STATUS_DMA_ERROR;
        set_MI_INTR_REG(MI_INTR_SI);
        return;
    }

    memcpy(state.pifram, state.dram + src, 64);
    state.hwreg.SI_STATUS_REG = SI_STATUS_INTR;
    set_MI_INTR_REG(MI_INTR_SI);

    if (debugger.verbose.SI) {
        std::cerr << "PIF command buffer:" << std::endl;
        for (size_t n = 0; n < 64; n++) {
            if (n && !(n % 16))
                std::cerr << std::endl;
            std::cerr << std::hex << " ";
            std::cerr << std::setw(2) << (unsigned)state.pifram[n];
        }
        std::cerr << std::endl;
    }

    // Run the commands encoded in the PIF ram.
    eval_PIF_commands();
}

namespace RdRam {
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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RDRAM_DEVICE_TYPE_REG:
            logRead(debugger.verbose.rdram, "RDRAM_DEVICE_TYPE_REG",
                    state.hwreg.RDRAM_DEVICE_TYPE_REG);
            *value = state.hwreg.RDRAM_DEVICE_TYPE_REG;
            return true;
        case RDRAM_DEVICE_ID_REG:
            logRead(debugger.verbose.rdram, "RDRAM_DEVICE_ID_REG",
                    state.hwreg.RDRAM_DEVICE_ID_REG);
            *value = state.hwreg.RDRAM_DEVICE_ID_REG;
            return true;
        case RDRAM_DELAY_REG:
            logRead(debugger.verbose.rdram, "RDRAM_DELAY_REG",
                    state.hwreg.RDRAM_DELAY_REG);
            *value = state.hwreg.RDRAM_DELAY_REG;
            return true;
        case RDRAM_MODE_REG:
            logRead(debugger.verbose.rdram, "RDRAM_MODE_REG",
                    state.hwreg.RDRAM_MODE_REG);
            *value = state.hwreg.RDRAM_MODE_REG;
            return true;
        case RDRAM_REF_INTERVAL_REG:
            logRead(debugger.verbose.rdram, "RDRAM_REF_INTERVAL_REG",
                    state.hwreg.RDRAM_REF_INTERVAL_REG);
            *value = state.hwreg.RDRAM_REF_INTERVAL_REG;
            return true;
        case RDRAM_REF_ROW_REG:
            logRead(debugger.verbose.rdram, "RDRAM_REF_ROW_REG",
                    state.hwreg.RDRAM_REF_ROW_REG);
            *value = state.hwreg.RDRAM_REF_ROW_REG;
            return true;
        case RDRAM_RAS_INTERVAL_REG:
            logRead(debugger.verbose.rdram, "RDRAM_RAS_INTERVAL_REG",
                    state.hwreg.RDRAM_RAS_INTERVAL_REG);
            *value = state.hwreg.RDRAM_RAS_INTERVAL_REG;
            return true;
        case RDRAM_MIN_INTERVAL_REG:
            logRead(debugger.verbose.rdram, "RDRAM_MIN_INTERVAL_REG",
                    state.hwreg.RDRAM_MIN_INTERVAL_REG);
            *value = state.hwreg.RDRAM_MIN_INTERVAL_REG;
            return true;
        case RDRAM_ADDR_SELECT_REG:
            logRead(debugger.verbose.rdram, "RDRAM_ADDR_SELECT_REG",
                    state.hwreg.RDRAM_ADDR_SELECT_REG);
            *value = state.hwreg.RDRAM_ADDR_SELECT_REG;
            return true;
        case RDRAM_DEVICE_MANUF_REG:
            logRead(debugger.verbose.rdram, "RDRAM_DEVICE_MANUF_REG",
                    state.hwreg.RDRAM_DEVICE_MANUF_REG);
            *value = state.hwreg.RDRAM_DEVICE_MANUF_REG;
            return true;
        default:
            logRead(debugger.verbose.rdram, "RDRAM_UNKNOWN", 0);
            debugger.halt("RDRAM read");
            break;
    }
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RDRAM_DEVICE_TYPE_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_DEVICE_TYPE_REG", value);
            state.hwreg.RDRAM_DEVICE_TYPE_REG = value;
            return true;
        case RDRAM_DEVICE_ID_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_DEVICE_ID_REG", value);
            state.hwreg.RDRAM_DEVICE_ID_REG = value;
            return true;
        case RDRAM_DELAY_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_DELAY_REG", value);
            state.hwreg.RDRAM_DELAY_REG = value;
            return true;
        case RDRAM_MODE_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_MODE_REG", value);
            state.hwreg.RDRAM_MODE_REG = value;
            return true;
        case RDRAM_REF_INTERVAL_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_REF_INTERVAL_REG", value);
            state.hwreg.RDRAM_REF_INTERVAL_REG = value;
            return true;
        case RDRAM_REF_ROW_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_REF_ROW_REG", value);
            state.hwreg.RDRAM_REF_ROW_REG = value;
            return true;
        case RDRAM_RAS_INTERVAL_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_RAS_INTERVAL_REG", value);
            state.hwreg.RDRAM_RAS_INTERVAL_REG = value;
            return true;
        case RDRAM_MIN_INTERVAL_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_MIN_INTERVAL_REG", value);
            state.hwreg.RDRAM_MIN_INTERVAL_REG = value;
            return true;
        case RDRAM_ADDR_SELECT_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_ADDR_SELECT_REG", value);
            state.hwreg.RDRAM_ADDR_SELECT_REG = value;
            return true;
        case RDRAM_DEVICE_MANUF_REG:
            logWrite(debugger.verbose.rdram, "RDRAM_DEVICE_MANUF_REG", value);
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
            logWrite(debugger.verbose.rdram, "RDRAM_UNKNOWN", value);
            debugger.halt("RDRAM write");
            break;
    }
    return true;
}

}; /* namespace RdRam */

namespace SP {

// Master, SP memory address
// (RW): [11:0] DMEM/IMEM address
//       [12] 0=DMEM,1=IMEM
const u32 SP_MEM_ADDR_REG =         UINT32_C(0x04040000);
// Slave, SP DRAM DMA address
// (RW): [23:0] RDRAM address
const u32 SP_DRAM_ADDR_REG =        UINT32_C(0x04040004);
// SP read DMA length
// direction: I/DMEM <- RDRAM
// (RW): [11:0] length
//       [19:12] count
//       [31:20] skip
const u32 SP_RD_LEN_REG =           UINT32_C(0x04040008);
// SP write DMA length
// direction: I/DMEM -> RDRAM
// (RW): [11:0] length
//       [19:12] count
//       [31:20] skip
const u32 SP_WR_LEN_REG =           UINT32_C(0x0404000c);
// SP status
// (W): [0]  clear halt          (R): [0]  halt
//      [1]  set halt                 [1]  broke
//      [2]  clear broke              [2]  dma busy
//      [3]  clear intr               [3]  dma full
//      [4]  set intr                 [4]  io full
//      [5]  clear sstep              [5]  single step
//      [6]  set sstep                [6]  interrupt on break
//      [7]  clear intr on break      [7]  signal 0 set
//      [8]  set intr on break        [8]  signal 1 set
//      [9]  clear signal 0           [9]  signal 2 set
//      [10] set signal 0             [10] signal 3 set
//      [11] clear signal 1           [11] signal 4 set
//      [12] set signal 1             [12] signal 5 set
//      [13] clear signal 2           [13] signal 6 set
//      [14] set signal 2             [14] signal 7 set
//      [15] clear signal 3
//      [16] set signal 3
//      [17] clear signal 4
//      [18] set signal 4
//      [19] clear signal 5
//      [20] set signal 5
//      [21] clear signal 6
//      [22] set signal 6
//      [23] clear signal 7
//      [24] set signal 7
const u32 SP_STATUS_REG =           UINT32_C(0x04040010);
// SP DMA full
// (R): [0] valid bit, dma full
const u32 SP_DMA_FULL_REG =         UINT32_C(0x04040014);
// SP DMA busy
// (R): [0] valid bit, dma busy
const u32 SP_DMA_BUSY_REG =         UINT32_C(0x04040018);
// SP semaphore
// (R): [0] semaphore flag (set on read)
// (W): [] clear semaphore flag
const u32 SP_SEMAPHORE_REG =        UINT32_C(0x0404001c);
// SP PC
// (RW): [11:0] program counter
const u32 SP_PC_REG =               UINT32_C(0x04080000);
// SP IMEM BIST REG
// (W): [0] BIST check           (R): [0] BIST check
//      [1] BIST go                   [1] BIST go
//      [2] BIST clear                [2] BIST done
//                                    [6:3] BIST fail
const u32 SP_IBIST_REG =            UINT32_C(0x04080004);

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SP_MEM_ADDR_REG:
            logRead(debugger.verbose.SP, "SP_MEM_ADDR_REG",
                    state.hwreg.SP_MEM_ADDR_REG);
            *value = state.hwreg.SP_MEM_ADDR_REG;
            return true;
        case SP_DRAM_ADDR_REG:
            logRead(debugger.verbose.SP, "SP_DRAM_ADDR_REG",
                    state.hwreg.SP_DRAM_ADDR_REG);
            *value = state.hwreg.SP_DRAM_ADDR_REG;
            return true;
        case SP_RD_LEN_REG:
            logRead(debugger.verbose.SP, "SP_RD_LEN_REG",
                    state.hwreg.SP_RD_LEN_REG);
            *value = state.hwreg.SP_RD_LEN_REG;
            return true;
        case SP_WR_LEN_REG:
            logRead(debugger.verbose.SP, "SP_WR_LEN_REG",
                    state.hwreg.SP_WR_LEN_REG);
            *value = state.hwreg.SP_WR_LEN_REG;
            return true;
        case SP_STATUS_REG:
            logRead(debugger.verbose.SP, "SP_STATUS_REG",
                    state.hwreg.SP_STATUS_REG);
            *value = state.hwreg.SP_STATUS_REG;
            return true;
        case SP_DMA_FULL_REG:
            logRead(debugger.verbose.SP, "SP_DMA_FULL_REG", 0);
            *value = 0;
            return true;
        case SP_DMA_BUSY_REG:
            logRead(debugger.verbose.SP, "SP_DMA_BUSY_REG", 0);
            *value = 0;
            return true;
        case SP_SEMAPHORE_REG:
            *value = read_SP_SEMAPHORE_REG();
            return true;
        case SP_PC_REG:
            logRead(debugger.verbose.SP, "SP_PC_REG", state.rspreg.pc);
            *value = state.rspreg.pc;
            return true;
        case SP_IBIST_REG:
            logRead(debugger.verbose.SP, "SP_IBIST_REG",
                    state.hwreg.SP_IBIST_REG);
            *value = state.hwreg.SP_IBIST_REG;
            return true;
        default:
            throw "SP read unsupported";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SP_MEM_ADDR_REG:
            logWrite(debugger.verbose.SP, "SP_MEM_ADDR_REG", value);
            state.hwreg.SP_MEM_ADDR_REG = value;
            return true;
        case SP_DRAM_ADDR_REG:
            logWrite(debugger.verbose.SP, "SP_DRAM_ADDR_REG", value);
            state.hwreg.SP_DRAM_ADDR_REG = value;
            return true;
        case SP_RD_LEN_REG:
            write_SP_RD_LEN_REG(value);
            return true;
        case SP_WR_LEN_REG:
            logWrite(debugger.verbose.SP, "SP_WR_LEN_REG", value);
            state.hwreg.SP_WR_LEN_REG = value;
            throw "unsupported_wr";
            return true;
        case SP_STATUS_REG:
            write_SP_STATUS_REG(value);
            return true;
        case SP_DMA_FULL_REG:
            logWrite(debugger.verbose.SP, "SP_DMA_FULL_REG", value);
            return true;
        case SP_DMA_BUSY_REG:
            logWrite(debugger.verbose.SP, "SP_DMA_BUSY_REG", value);
            return true;
        case SP_SEMAPHORE_REG:
            logWrite(debugger.verbose.SP, "SP_SEMAPHORE_REG", value);
            state.hwreg.SP_SEMAPHORE_REG = 0;
            return true;
        case SP_PC_REG:
            logWrite(debugger.verbose.SP, "SP_PC_REG", value);
            // Note: not too preoccupied with the behaviour when the RSP
            // is already running; it is probably not recommended to try that..
            state.rspreg.pc = value & 0xffflu;
            state.rsp.nextPc = state.rspreg.pc;
            state.rsp.nextAction = State::Action::Jump;
            return true;
        case SP_IBIST_REG:
            logWrite(debugger.verbose.SP, "SP_IBIST_REG", value);
            state.hwreg.SP_IBIST_REG = value;
            return true;
        default:
            throw "SP read unsupported";
            break;
    }
    return true;
}

}; /* namespace SP */

namespace DPCommand {

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


bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case DPC_START_REG:     *value = state.hwreg.DPC_START_REG; break;
        case DPC_END_REG:       *value = state.hwreg.DPC_END_REG; break;
        case DPC_STATUS_REG:    *value = state.hwreg.DPC_STATUS_REG; break;
        case DPC_CURRENT_REG:   *value = state.hwreg.DPC_CURRENT_REG; break;
        case DPC_CLOCK_REG:
        case DPC_BUF_BUSY_REG:
        case DPC_PIPE_BUSY_REG:
        case DPC_TMEM_REG:
            *value = 0;
            return true;
        default:
            debugger.halt("DPCommand::read invalid");
            return false;
    }
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case DPC_START_REG:     write_DPC_START_REG(value); break;
        case DPC_END_REG:       write_DPC_END_REG(value); break;
        case DPC_STATUS_REG:    write_DPC_STATUS_REG(value); break;
        case DPC_CURRENT_REG:
        case DPC_CLOCK_REG:
        case DPC_BUF_BUSY_REG:
        case DPC_PIPE_BUSY_REG:
        case DPC_TMEM_REG:
            return true;
        default:
            debugger.halt("DPCommand::write invalid");
            return false;
    }
    return true;
}

}; /* namespace DPCommand */

namespace DPSpan {

bool read(uint bytes, u64 addr, u64 *value)
{
    std::cerr << "DPSpan::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported12";
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "DPSpan::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported11";
    return true;
}

}; /* namespace DPSpan */

namespace MI {

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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case MI_MODE_REG:
            logRead(debugger.verbose.MI, "MI_MODE_REG",
                    state.hwreg.MI_MODE_REG);
            *value = state.hwreg.MI_MODE_REG;
            return true;
        case MI_VERSION_REG:
            logRead(debugger.verbose.MI, "MI_VERSION_REG",
                    state.hwreg.MI_VERSION_REG);
            *value = state.hwreg.MI_VERSION_REG;
            return true;
        case MI_INTR_REG:
            logRead(debugger.verbose.MI, "MI_INTR_REG",
                    state.hwreg.MI_INTR_REG);
            *value = state.hwreg.MI_INTR_REG;
            return true;
        case MI_INTR_MASK_REG:
            logRead(debugger.verbose.MI, "MI_INTR_MASK_REG",
                    state.hwreg.MI_INTR_MASK_REG);
            *value = state.hwreg.MI_INTR_MASK_REG;
            return true;
        default:
            logReadAtAddr(debugger.verbose.MI, "MI_??", addr, 0);
            throw "Unsupported10";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case MI_MODE_REG:
            logWrite(debugger.verbose.MI, "MI_MODE_REG", value);
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
            return true;
        case MI_VERSION_REG:
            logWrite(debugger.verbose.MI, "MI_VERSION_REG", value);
            return true;
        case MI_INTR_REG:
            logWrite(debugger.verbose.MI, "MI_INTR_REG", value);
            return true;
        case MI_INTR_MASK_REG:
            logWrite(debugger.verbose.MI, "MI_INTR_MASK_REG", value);
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
            return true;
        default:
            logWriteAtAddr(debugger.verbose.MI, "MI_??", addr, value);
            throw "Unsupported9";
            break;
    }
    return true;
}

}; /* namespace MI */

namespace VI {

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

/**
 * @brief Rebuild the current framebuffer object with the configuration
 *  in the registers VI_MODE_REG, VI_DRAM_ADDR_REG, VI_WIDTH_REG.
 */
void updateCurrentFramebuffer(void) {
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
            std::cerr << "Invalid COLOR_DEPTH config" << std::endl;
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

    if (debugger.verbose.VI) {
        std::cerr << std::dec;
        std::cerr << "horizontal start : " << horizontalStart << std::endl;
        std::cerr << "horizontal end : " << horizontalEnd << std::endl;
        std::cerr << "vertical start : " << verticalStart << std::endl;
        std::cerr << "vertical end : " << verticalEnd << std::endl;
        std::cerr << "framebuffer width : " << framebufferWidth << std::endl;
        std::cerr << "framebuffer height : " << framebufferHeight << std::endl;
    }

    framebufferWidth = state.hwreg.VI_WIDTH_REG;

    unsigned framebufferSize = framebufferWidth * framebufferHeight * (pixelSize / 8);
    u64 addr = sign_extend<u64, u32>(state.hwreg.VI_DRAM_ADDR_REG);
    u64 offset = 0;
    Exception exn;

    exn = translateAddress(addr, &offset, false);
    if (exn == Exception::None &&
        offset + framebufferSize <= sizeof(state.dram)) {
        start = &state.dram[offset];
    } else {
        std::cerr << "Invalid DRAM_ADDR config: " << std::hex;
        std::cerr << addr << "(" << offset << ")," << framebufferSize << std::endl;
        valid = false;
    }

    setVideoImage(framebufferWidth, framebufferHeight, pixelSize, valid ? start : NULL);
}

/** @brief Write the value of the VI_INTR_REG register. */
void write_VI_INTR_REG(u32 value) {
    logWrite(debugger.verbose.VI, "VI_INTR_REG", value);
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
u32 read_VI_CURRENT_REG(void) {
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
    logRead(debugger.verbose.VI, "VI_CURRENT_REG", count);
    return count;
}

/** @brief Write the value of the VI_V_SYNC_REG register. */
void write_VI_V_SYNC_REG(u32 value) {
    // CPU freq is 93.75 MHz, refresh frequency is assumed 60Hz.
    logWrite(debugger.verbose.VI, "VI_V_SYNC_REG", value);
    state.hwreg.VI_V_SYNC_REG = value;
    state.hwreg.vi_CyclesPerLine = 93750000lu / (60 * (value + 1));
    state.hwreg.vi_IntrInterval = state.hwreg.vi_CyclesPerLine * value;
}

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case VI_CONTROL_REG:
            logRead(debugger.verbose.VI, "VI_CONTROL_REG", state.hwreg.VI_CONTROL_REG);
            *value = state.hwreg.VI_CONTROL_REG;
            return true;
        case VI_DRAM_ADDR_REG:
            logRead(debugger.verbose.VI, "VI_DRAM_ADDR_REG", state.hwreg.VI_DRAM_ADDR_REG);
            *value = state.hwreg.VI_DRAM_ADDR_REG;
            return true;
        case VI_WIDTH_REG:
            logRead(debugger.verbose.VI, "VI_WIDTH_REG", state.hwreg.VI_WIDTH_REG);
            *value = state.hwreg.VI_WIDTH_REG;
            return true;
        case VI_INTR_REG:
            logRead(debugger.verbose.VI, "VI_INTR_REG", state.hwreg.VI_INTR_REG);
            *value = state.hwreg.VI_INTR_REG;
            return true;
        case VI_CURRENT_REG:
            *value = read_VI_CURRENT_REG();
            return true;
        case VI_BURST_REG:
            logRead(debugger.verbose.VI, "VI_BURST_REG", state.hwreg.VI_BURST_REG);
            *value = state.hwreg.VI_BURST_REG;
            return true;
        case VI_V_SYNC_REG:
            logRead(debugger.verbose.VI, "VI_V_SYNC_REG", state.hwreg.VI_V_SYNC_REG);
            *value = state.hwreg.VI_V_SYNC_REG;
            return true;
        case VI_H_SYNC_REG:
            logRead(debugger.verbose.VI, "VI_H_SYNC_REG", state.hwreg.VI_H_SYNC_REG);
            *value = state.hwreg.VI_H_SYNC_REG;
            return true;
        case VI_LEAP_REG:
            logRead(debugger.verbose.VI, "VI_LEAP_REG", state.hwreg.VI_LEAP_REG);
            *value = state.hwreg.VI_LEAP_REG;
            return true;
        case VI_H_START_REG:
            logRead(debugger.verbose.VI, "VI_H_START_REG", state.hwreg.VI_H_START_REG);
            *value = state.hwreg.VI_H_START_REG;
            return true;
        case VI_V_START_REG:
            logRead(debugger.verbose.VI, "VI_V_START_REG", state.hwreg.VI_V_START_REG);
            *value = state.hwreg.VI_V_START_REG;
            return true;
        case VI_V_BURST_REG:
            logRead(debugger.verbose.VI, "VI_V_BURST_REG", state.hwreg.VI_V_BURST_REG);
            *value = state.hwreg.VI_V_BURST_REG;
            return true;
        case VI_X_SCALE_REG:
            logRead(debugger.verbose.VI, "VI_X_SCALE_REG", state.hwreg.VI_X_SCALE_REG);
            *value = state.hwreg.VI_X_SCALE_REG;
            return true;
        case VI_Y_SCALE_REG:
            logRead(debugger.verbose.VI, "VI_Y_SCALE_REG", state.hwreg.VI_Y_SCALE_REG);
            *value = state.hwreg.VI_Y_SCALE_REG;
            return true;
        default:
            throw "VI Unsupported8";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case VI_CONTROL_REG:
            logWrite(debugger.verbose.VI, "VI_CONTROL_REG", value);
            state.hwreg.VI_CONTROL_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_DRAM_ADDR_REG:
            logWrite(debugger.verbose.VI, "VI_DRAM_ADDR_REG", value);
            state.hwreg.VI_DRAM_ADDR_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_WIDTH_REG:
            logWrite(debugger.verbose.VI, "VI_WIDTH_REG", value);
            state.hwreg.VI_WIDTH_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_INTR_REG:
            write_VI_INTR_REG(value);
            return true;
        case VI_CURRENT_REG:
            logWrite(debugger.verbose.VI, "VI_CURRENT_REG", value);
            clear_MI_INTR_REG(MI_INTR_VI);
            return true;
        case VI_BURST_REG:
            logWrite(debugger.verbose.VI, "VI_BURST_REG", value);
            state.hwreg.VI_BURST_REG = value;
            return true;
        case VI_V_SYNC_REG:
            write_VI_V_SYNC_REG(value);
            return true;
        case VI_H_SYNC_REG:
            logWrite(debugger.verbose.VI, "VI_H_SYNC_REG", value);
            state.hwreg.VI_H_SYNC_REG = value;
            return true;
        case VI_LEAP_REG:
            logWrite(debugger.verbose.VI, "VI_LEAP_REG", value);
            state.hwreg.VI_LEAP_REG = value;
            return true;
        case VI_H_START_REG:
            logWrite(debugger.verbose.VI, "VI_H_START_REG", value);
            state.hwreg.VI_H_START_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_V_START_REG:
            logWrite(debugger.verbose.VI, "VI_V_START_REG", value);
            state.hwreg.VI_V_START_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_V_BURST_REG:
            logWrite(debugger.verbose.VI, "VI_V_BURST_REG", value);
            state.hwreg.VI_V_BURST_REG = value;
            return true;
        case VI_X_SCALE_REG:
            logWrite(debugger.verbose.VI, "VI_X_SCALE_REG", value);
            state.hwreg.VI_X_SCALE_REG = value;
            updateCurrentFramebuffer();
            return true;
        case VI_Y_SCALE_REG:
            logWrite(debugger.verbose.VI, "VI_Y_SCALE_REG", value);
            state.hwreg.VI_Y_SCALE_REG = value;
            updateCurrentFramebuffer();
            return true;
        default:
            throw "VI Unsupported7";
            break;
    }
    return true;
}

}; /* namespace VI */

namespace AI {

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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case AI_DRAM_ADDR_REG:
            logRead(debugger.verbose.AI, "AI_DRAM_ADDR_REG",
                    state.hwreg.AI_DRAM_ADDR_REG);
            *value = state.hwreg.AI_DRAM_ADDR_REG;
            return true;
        case AI_LEN_REG:
            logRead(debugger.verbose.AI, "AI_LEN_REG",
                    state.hwreg.AI_LEN_REG);
            *value = state.hwreg.AI_LEN_REG;
            return true;
        case AI_CONTROL_REG:
            logRead(debugger.verbose.AI, "AI_CONTROL_REG",
                    state.hwreg.AI_CONTROL_REG);
            *value = state.hwreg.AI_CONTROL_REG;
            return true;
        case AI_STATUS_REG:
            logRead(debugger.verbose.AI, "AI_STATUS_REG",
                    state.hwreg.AI_STATUS_REG);
            *value = state.hwreg.AI_STATUS_REG;
            return true;
        case AI_DACRATE_REG:
            logRead(debugger.verbose.AI, "AI_DACRATE_REG",
                    state.hwreg.AI_DACRATE_REG);
            *value = state.hwreg.AI_DACRATE_REG;
            return true;
        case AI_BITRATE_REG:
            logRead(debugger.verbose.AI, "AI_BITRATE_REG",
                    state.hwreg.AI_BITRATE_REG);
            *value = state.hwreg.AI_BITRATE_REG;
            return true;
        default:
            throw "AI::unsupported";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case AI_DRAM_ADDR_REG:
            logWrite(debugger.verbose.AI, "AI_DRAM_ADDR_REG", value);
            state.hwreg.AI_DRAM_ADDR_REG = value;
            return true;
        case AI_LEN_REG:
            logWrite(debugger.verbose.AI, "AI_LEN_REG", value);
            state.hwreg.AI_LEN_REG = value;
            return true;
        case AI_CONTROL_REG:
            logWrite(debugger.verbose.AI, "AI_CONTROL_REG", value);
            state.hwreg.AI_CONTROL_REG = value;
            return true;
        case AI_STATUS_REG:
            logWrite(debugger.verbose.AI, "AI_STATUS_REG", value);
            state.hwreg.AI_STATUS_REG = value;
            return true;
        case AI_DACRATE_REG:
            logWrite(debugger.verbose.AI, "AI_DACRATE_REG", value);
            state.hwreg.AI_DACRATE_REG = value;
            return true;
        case AI_BITRATE_REG:
            logWrite(debugger.verbose.AI, "AI_BITRATE_REG", value);
            state.hwreg.AI_BITRATE_REG = value;
            return true;
        default:
            throw "AI::unsupported";
            break;
    }
    return true;
}

}; /* namespace AI */

namespace PI {

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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case PI_DRAM_ADDR_REG:
            logRead(debugger.verbose.PI, "PI_DRAM_ADDR_REG",
                    state.hwreg.PI_DRAM_ADDR_REG);
            *value = state.hwreg.PI_DRAM_ADDR_REG;
            return true;
        case PI_CART_ADDR_REG:
            logRead(debugger.verbose.PI, "PI_CART_ADDR_REG",
                    state.hwreg.PI_CART_ADDR_REG);
            *value = state.hwreg.PI_CART_ADDR_REG;
            return true;
        case PI_RD_LEN_REG:
            logRead(debugger.verbose.PI, "PI_RD_LEN_REG",
                    state.hwreg.PI_RD_LEN_REG);
            *value = state.hwreg.PI_RD_LEN_REG;
            return true;
        case PI_WR_LEN_REG:
            logRead(debugger.verbose.PI, "PI_WR_LEN_REG",
                    state.hwreg.PI_WR_LEN_REG);
            *value = state.hwreg.PI_WR_LEN_REG;
            return true;
        case PI_STATUS_REG:
            logRead(debugger.verbose.PI, "PI_STATUS_REG",
                    state.hwreg.PI_STATUS_REG);
            *value = state.hwreg.PI_STATUS_REG;
            return true;
        case PI_BSD_DOM1_LAT_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM1_LAT_REG",
                    state.hwreg.PI_BSD_DOM1_LAT_REG);
            *value = state.hwreg.PI_BSD_DOM1_LAT_REG;
            return true;
        case PI_BSD_DOM1_PWD_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM1_PWD_REG",
                    state.hwreg.PI_BSD_DOM1_PWD_REG);
            *value = state.hwreg.PI_BSD_DOM1_PWD_REG;
            return true;
        case PI_BSD_DOM1_PGS_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM1_PGS_REG",
                    state.hwreg.PI_BSD_DOM1_PGS_REG);
            *value = state.hwreg.PI_BSD_DOM1_PGS_REG;
            return true;
        case PI_BSD_DOM1_RLS_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM1_RLS_REG",
                    state.hwreg.PI_BSD_DOM1_RLS_REG);
            *value = state.hwreg.PI_BSD_DOM1_RLS_REG;
            return true;
        case PI_BSD_DOM2_LAT_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM2_LAT_REG",
                    state.hwreg.PI_BSD_DOM2_LAT_REG);
            *value = state.hwreg.PI_BSD_DOM2_LAT_REG;
            return true;
        case PI_BSD_DOM2_PWD_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM2_PWD_REG",
                    state.hwreg.PI_BSD_DOM2_PWD_REG);
            *value = state.hwreg.PI_BSD_DOM2_PWD_REG;
            return true;
        case PI_BSD_DOM2_PGS_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM2_PGS_REG",
                    state.hwreg.PI_BSD_DOM2_PGS_REG);
            *value = state.hwreg.PI_BSD_DOM2_PGS_REG;
            return true;
        case PI_BSD_DOM2_RLS_REG:
            logRead(debugger.verbose.PI, "PI_BSD_DOM2_RLS_REG",
                    state.hwreg.PI_BSD_DOM2_RLS_REG);
            *value = state.hwreg.PI_BSD_DOM2_RLS_REG;
            return true;
        default:
            logRead(debugger.verbose.PI, "PI_??", addr);
            throw "Unsupported6";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case PI_DRAM_ADDR_REG:
            logWrite(debugger.verbose.PI, "PI_DRAM_ADDR_REG", value);
            state.hwreg.PI_DRAM_ADDR_REG = value;
            return true;
        case PI_CART_ADDR_REG:
            logWrite(debugger.verbose.PI, "PI_CART_ADDR_REG", value);
            state.hwreg.PI_CART_ADDR_REG = value;
            return true;

        case PI_RD_LEN_REG:
            write_PI_RD_LEN_REG(value);
            return true;
        case PI_WR_LEN_REG:
            write_PI_WR_LEN_REG(value);
            return true;

        case PI_STATUS_REG:
            logWrite(debugger.verbose.PI, "PI_STATUS_REG", value);
            state.hwreg.PI_STATUS_REG = 0;
            if (value & PI_STATUS_RESET) {
                throw "Unsupported_PI_STATUS_REG__RESET";
            }
            if (value & PI_STATUS_CLR_INTR) {
                clear_MI_INTR_REG(MI_INTR_PI);
            }
            return true;
        case PI_BSD_DOM1_LAT_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM1_LAT_REG", value);
            state.hwreg.PI_BSD_DOM1_LAT_REG = value;
            return true;
        case PI_BSD_DOM1_PWD_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM1_PWD_REG", value);
            state.hwreg.PI_BSD_DOM1_PWD_REG = value;
            return true;
        case PI_BSD_DOM1_PGS_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM1_PGS_REG", value);
            state.hwreg.PI_BSD_DOM1_PGS_REG = value;
            return true;
        case PI_BSD_DOM1_RLS_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM1_RLS_REG", value);
            state.hwreg.PI_BSD_DOM1_RLS_REG = value;
            return true;
        case PI_BSD_DOM2_LAT_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM2_LAT_REG", value);
            state.hwreg.PI_BSD_DOM2_LAT_REG = value;
            return true;
        case PI_BSD_DOM2_PWD_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM2_PWD_REG", value);
            state.hwreg.PI_BSD_DOM2_PWD_REG = value;
            return true;
        case PI_BSD_DOM2_PGS_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM2_PGS_REG", value);
            state.hwreg.PI_BSD_DOM2_PGS_REG = value;
            return true;
        case PI_BSD_DOM2_RLS_REG:
            logWrite(debugger.verbose.PI, "PI_BSD_DOM2_RLS_REG", value);
            state.hwreg.PI_BSD_DOM2_RLS_REG = value;
            return true;
        default:
            logWrite(debugger.verbose.PI, "PI_??", addr);
            throw "Unsupported5";
            break;
    }
    return true;
}

}; /* namespace PI */

namespace RI {

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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RI_MODE_REG:
            logRead(debugger.verbose.RI, "RI_MODE_REG",
                    state.hwreg.RI_MODE_REG);
            *value = state.hwreg.RI_MODE_REG;
            return true;
        case RI_CONFIG_REG:
            logRead(debugger.verbose.RI, "RI_CONFIG_REG",
                    state.hwreg.RI_CONFIG_REG);
            *value = state.hwreg.RI_CONFIG_REG;
            return true;
        case RI_CURRENT_LOAD_REG:
            logRead(debugger.verbose.RI, "RI_CURRENT_LOAD_REG",
                    state.hwreg.RI_CURRENT_LOAD_REG);
            *value = state.hwreg.RI_CURRENT_LOAD_REG;
            return true;
        case RI_SELECT_REG:
            logRead(debugger.verbose.RI, "RI_SELECT_REG",
                    state.hwreg.RI_SELECT_REG);
            *value = state.hwreg.RI_SELECT_REG;
            return true;
        case RI_REFRESH_REG:
            logRead(debugger.verbose.RI, "RI_REFRESH_REG",
                    state.hwreg.RI_REFRESH_REG);
            *value = state.hwreg.RI_REFRESH_REG;
            return true;
        case RI_LATENCY_REG:
            logRead(debugger.verbose.RI, "RI_LATENCY_REG",
                    state.hwreg.RI_LATENCY_REG);
            *value = state.hwreg.RI_LATENCY_REG;
            return true;
        case RI_RERROR_REG:
            logRead(debugger.verbose.RI, "RI_RERROR_REG",
                    state.hwreg.RI_RERROR_REG);
            *value = state.hwreg.RI_RERROR_REG;
            return true;
        case RI_WERROR_REG:
            logRead(debugger.verbose.RI, "RI_WERROR_REG",
                    state.hwreg.RI_WERROR_REG);
            *value = state.hwreg.RI_WERROR_REG;
            return true;
        default:
            logRead(debugger.verbose.RI, "RI_??", addr);
            throw "Unsupported4";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RI_MODE_REG:
            logWrite(debugger.verbose.RI, "RI_MODE_REG", value);
            state.hwreg.RI_MODE_REG = value;
            return true;
        case RI_CONFIG_REG:
            logWrite(debugger.verbose.RI, "RI_CONFIG_REG", value);
            state.hwreg.RI_CONFIG_REG = value;
            return true;
        case RI_CURRENT_LOAD_REG:
            logWrite(debugger.verbose.RI, "RI_CURRENT_LOAD_REG", value);
            state.hwreg.RI_CURRENT_LOAD_REG = value;
            return true;
        case RI_SELECT_REG:
            logWrite(debugger.verbose.RI, "RI_SELECT_REG", value);
            state.hwreg.RI_SELECT_REG = value;
            return true;
        case RI_REFRESH_REG:
            logWrite(debugger.verbose.RI, "RI_REFRESH_REG", value);
            state.hwreg.RI_REFRESH_REG = value;
            return true;
        case RI_LATENCY_REG:
            logWrite(debugger.verbose.RI, "RI_LATENCY_REG", value);
            state.hwreg.RI_LATENCY_REG = value;
            return true;
        case RI_RERROR_REG:
            logWrite(debugger.verbose.RI, "RI_RERROR_REG", value);
            state.hwreg.RI_RERROR_REG = value;
            return true;
        case RI_WERROR_REG:
            logWrite(debugger.verbose.RI, "RI_WERROR_REG", value);
            state.hwreg.RI_WERROR_REG = value;
            return true;
        default:
            logWrite(debugger.verbose.RI, "RI_??", addr);
            throw "Unsupported3";
            break;
    }
    return true;
}

}; /* namespace RI */

namespace SI {

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

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SI_DRAM_ADDR_REG:
            logRead(debugger.verbose.SI, "SI_DRAM_ADDR_REG",
                    state.hwreg.SI_DRAM_ADDR_REG);
            *value = state.hwreg.SI_DRAM_ADDR_REG;
            return true;
        case SI_PIF_ADDR_RD64B_REG:
            logRead(debugger.verbose.SI, "SI_PIF_ADDR_RD64B_REG", 0);
            *value = 0;
            return true;
        case SI_PIF_ADDR_WR64B_REG:
            logRead(debugger.verbose.SI, "SI_PIF_ADDR_WR64B_REG", 0);
            *value = 0;
            return true;
        case SI_STATUS_REG:
            logRead(debugger.verbose.SI, "SI_STATUS_REG",
                    state.hwreg.SI_STATUS_REG);
            *value = state.hwreg.SI_STATUS_REG;
            return true;
        default:
            throw "SI Unsupported14";
            break;
    }
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SI_DRAM_ADDR_REG:
            logWrite(debugger.verbose.SI, "SI_DRAM_ADDR_REG", value);
            state.hwreg.SI_DRAM_ADDR_REG = value & 0xfffffflu;
            return true;

        case SI_PIF_ADDR_RD64B_REG:
            write_SI_PIF_ADDR_RD64B_REG(value);
            return true;
        case SI_PIF_ADDR_WR64B_REG:
            write_SI_PIF_ADDR_WR64B_REG(value);
            return true;

        case SI_STATUS_REG:
            logWrite(debugger.verbose.SI, "SI_STATUS_REG", value);
            clear_MI_INTR_REG(MI_INTR_SI);
            state.hwreg.SI_STATUS_REG &= ~SI_STATUS_INTR;
            return true;

        default:
            throw "SI Unsupported13";
            break;
    }
    return true;
}

}; /* namespace SI */

namespace PIF {

bool read(uint bytes, u64 addr, u64 *value)
{
    u64 offset = addr - UINT32_C(0x1fc00000);
    if (offset < 0x7c0 || offset >= 0x800)
        return false;
    *value = state.pifram[offset - 0x7c0];
    logReadAtAddr(debugger.verbose.PIF, "PIF", addr, *value);
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    logWriteAtAddr(debugger.verbose.PIF, "PIF", addr, value);
    u64 offset = addr - UINT32_C(0x1fc00000);
    if (offset < 0x7c0 || offset >= 0x800)
        return false;

    state.pifram[offset - 0x7c0] = value;
    if ((state.pifram[0x3f] & 0x1) != 0)
        eval_PIF_commands();
    return true;
}

}; /* namespace PIF */

namespace Cart_2_1 {

bool read(uint bytes, u64 addr, u64 *value)
{
    logReadAtAddr(debugger.verbose.cart_2_1, "Cart_2_1", addr, 0);
    debugger.halt("Cart_2_1 read access");
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    logWriteAtAddr(debugger.verbose.cart_2_1, "Cart_2_1", addr, value);
    debugger.halt("Cart_2_1 write access");
    return true;
}

}; /* namespace PIF */

};
