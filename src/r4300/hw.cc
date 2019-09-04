
#include <iostream>
#include <iomanip>

#include <r4300/hw.h>
#include <r4300/state.h>

namespace R4300 {

static inline void logWrite(const char *tag, u64 value)
{
#if 1
    std::cerr << std::left << std::setfill(' ') << std::setw(32);
    std::cerr << tag << " <- " << std::hex << value << std::endl;
#endif
}

static inline void logRead(const char *tag, u64 value)
{
#if 1
    std::cerr << std::left << std::setfill(' ') << std::setw(32);
    std::cerr << tag << " -> " << std::hex << value << std::endl;
#endif
}

namespace Eval {

/*
 * Defined in eval.cc
 */
void setInterruptPending(uint irq);
void clearInterruptPending(uint irq);

};

/**
 * Set bits in the MI_INTR_REG register.
 * Reevaluate the value of the Interrupt 2 pending bit afterwards.
 */
static void set_MI_INTR_REG(u32 bits) {
    state.hwreg.MI_INTR_REG |= bits;
    if (state.hwreg.MI_INTR_REG & state.hwreg.MI_INTR_MASK_REG) {
        Eval::setInterruptPending(2);
    } else {
        Eval::clearInterruptPending(2);
    }
}

/**
 * Clear bits in the MI_INTR_REG register.
 * Reevaluate the value of the Interrupt 2 pending bit afterwards.
 */
static void clear_MI_INTR_REG(u32 bits) {
    state.hwreg.MI_INTR_REG &= ~bits;
    if (state.hwreg.MI_INTR_REG & ~state.hwreg.MI_INTR_MASK_REG) {
        Eval::setInterruptPending(2);
    } else {
        Eval::clearInterruptPending(2);
    }
}

namespace RdRam {

enum Register {
    RDRAM_DEVICE_TYPE_REG = 0x0,
    RDRAM_DEVICE_ID_REG = 0x4,
    RDRAM_DELAY_REG = 0x8,
    RDRAM_MODE_REG = 0xc,
    RDRAM_REF_INTERVAL_REG = 0x10,
    RDRAM_REF_ROW_REG = 0x14,
    RDRAM_RAS_INTERVAL_REG = 0x18,
    RDRAM_MIN_INTERVAL_REG = 0x1c,
    RDRAM_ADDR_SELECT_REG = 0x20,
    RDRAM_DEVICE_MANUF_REG = 0x24,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RDRAM_DEVICE_TYPE_REG:
            logRead("RDRAM_DEVICE_TYPE_REG", state.hwreg.RDRAM_DEVICE_TYPE_REG);
            *value = state.hwreg.RDRAM_DEVICE_TYPE_REG;
            return true;
        case RDRAM_DEVICE_ID_REG:
            logRead("RDRAM_DEVICE_ID_REG", state.hwreg.RDRAM_DEVICE_ID_REG);
            *value = state.hwreg.RDRAM_DEVICE_ID_REG;
            return true;
        case RDRAM_DELAY_REG:
            logRead("RDRAM_DELAY_REG", state.hwreg.RDRAM_DELAY_REG);
            *value = state.hwreg.RDRAM_DELAY_REG;
            return true;
        case RDRAM_MODE_REG:
            logRead("RDRAM_MODE_REG", state.hwreg.RDRAM_MODE_REG);
            *value = state.hwreg.RDRAM_MODE_REG;
            return true;
        case RDRAM_REF_INTERVAL_REG:
            logRead("RDRAM_REF_INTERVAL_REG", state.hwreg.RDRAM_REF_INTERVAL_REG);
            *value = state.hwreg.RDRAM_REF_INTERVAL_REG;
            return true;
        case RDRAM_REF_ROW_REG:
            logRead("RDRAM_REF_ROW_REG", state.hwreg.RDRAM_REF_ROW_REG);
            *value = state.hwreg.RDRAM_REF_ROW_REG;
            return true;
        case RDRAM_RAS_INTERVAL_REG:
            logRead("RDRAM_RAS_INTERVAL_REG", state.hwreg.RDRAM_RAS_INTERVAL_REG);
            *value = state.hwreg.RDRAM_RAS_INTERVAL_REG;
            return true;
        case RDRAM_MIN_INTERVAL_REG:
            logRead("RDRAM_MIN_INTERVAL_REG", state.hwreg.RDRAM_MIN_INTERVAL_REG);
            *value = state.hwreg.RDRAM_MIN_INTERVAL_REG;
            return true;
        case RDRAM_ADDR_SELECT_REG:
            logRead("RDRAM_ADDR_SELECT_REG", state.hwreg.RDRAM_ADDR_SELECT_REG);
            *value = state.hwreg.RDRAM_ADDR_SELECT_REG;
            return true;
        case RDRAM_DEVICE_MANUF_REG:
            logRead("RDRAM_DEVICE_MANUF_REG", state.hwreg.RDRAM_DEVICE_MANUF_REG);
            *value = state.hwreg.RDRAM_DEVICE_MANUF_REG;
            return true;
        default:
            throw "RDRAM read unsupported";
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
            logWrite("RDRAM_DEVICE_TYPE_REG", value);
            state.hwreg.RDRAM_DEVICE_TYPE_REG = value;
            return true;
        case RDRAM_DEVICE_ID_REG:
            logWrite("RDRAM_DEVICE_ID_REG", value);
            state.hwreg.RDRAM_DEVICE_ID_REG = value;
            return true;
        case RDRAM_DELAY_REG:
            logWrite("RDRAM_DELAY_REG", value);
            state.hwreg.RDRAM_DELAY_REG = value;
            return true;
        case RDRAM_MODE_REG:
            logWrite("RDRAM_MODE_REG", value);
            state.hwreg.RDRAM_MODE_REG = value;
            return true;
        case RDRAM_REF_INTERVAL_REG:
            logWrite("RDRAM_REF_INTERVAL_REG", value);
            state.hwreg.RDRAM_REF_INTERVAL_REG = value;
            return true;
        case RDRAM_REF_ROW_REG:
            logWrite("RDRAM_REF_ROW_REG", value);
            state.hwreg.RDRAM_REF_ROW_REG = value;
            return true;
        case RDRAM_RAS_INTERVAL_REG:
            logWrite("RDRAM_RAS_INTERVAL_REG", value);
            state.hwreg.RDRAM_RAS_INTERVAL_REG = value;
            return true;
        case RDRAM_MIN_INTERVAL_REG:
            logWrite("RDRAM_MIN_INTERVAL_REG", value);
            state.hwreg.RDRAM_MIN_INTERVAL_REG = value;
            return true;
        case RDRAM_ADDR_SELECT_REG:
            logWrite("RDRAM_ADDR_SELECT_REG", value);
            state.hwreg.RDRAM_ADDR_SELECT_REG = value;
            return true;
        case RDRAM_DEVICE_MANUF_REG:
            logWrite("RDRAM_DEVICE_MANUF_REG", value);
            state.hwreg.RDRAM_DEVICE_MANUF_REG = value;
            return true;
        default:
            // throw "RDRAM write unsupported";
            break;
    }
    return true;
}

}; /* namespace RdRam */

namespace SP {

enum Register {
    // Master, SP memory address
    // (RW): [11:0] DMEM/IMEM address
    //       [12] 0=DMEM,1=IMEM
    SP_MEM_ADDR_REG = 0x0,
    // Slave, SP DRAM DMA address
    // (RW): [23:0] RDRAM address
    SP_DRAM_ADDR_REG = 0x4,
    // SP read DMA length
    // direction: I/DMEM <- RDRAM
    // (RW): [11:0] length
    //       [19:12] count
    //       [31:20] skip
    SP_RD_LEN_REG = 0x8,
    // SP write DMA length
    // direction: I/DMEM -> RDRAM
    // (RW): [11:0] length
    //       [19:12] count
    //       [31:20] skip
    SP_WR_LEN_REG = 0xc,
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
    SP_STATUS_REG = 0x10,
    // SP DMA full
    // (R): [0] valid bit, dma full
    SP_DMA_FULL_REG = 0x14,
    // SP DMA busy
    // (R): [0] valid bit, dma busy
    SP_DMA_BUSY_REG = 0x18,
    // SP semaphore
    // (R): [0] semaphore flag (set on read)
    // (W): [] clear semaphore flag
    SP_SEMAPHORE_REG = 0x1c,
    // SP PC
    // (RW): [11:0] program counter
    SP_PC_REG = 0x40000,
    // SP IMEM BIST REG
    // (W): [0] BIST check           (R): [0] BIST check
    //      [1] BIST go                   [1] BIST go
    //      [2] BIST clear                [2] BIST done
    //                                    [6:3] BIST fail
    SP_IBIST_REG = 0x40004,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SP_MEM_ADDR_REG:
            logRead("SP_MEM_ADDR_REG", state.hwreg.SP_MEM_ADDR_REG);
            *value = state.hwreg.SP_MEM_ADDR_REG;
            return true;
        case SP_DRAM_ADDR_REG:
            logRead("SP_DRAM_ADDR_REG", state.hwreg.SP_DRAM_ADDR_REG);
            *value = state.hwreg.SP_DRAM_ADDR_REG;
            return true;
        case SP_RD_LEN_REG:
            logRead("SP_RD_LEN_REG", state.hwreg.SP_RD_LEN_REG);
            *value = state.hwreg.SP_RD_LEN_REG;
            return true;
        case SP_WR_LEN_REG:
            logRead("SP_WR_LEN_REG", state.hwreg.SP_WR_LEN_REG);
            *value = state.hwreg.SP_WR_LEN_REG;
            return true;
        case SP_STATUS_REG:
            logRead("SP_STATUS_REG", state.hwreg.SP_STATUS_REG);
            *value = state.hwreg.SP_STATUS_REG;
            return true;
        case SP_DMA_FULL_REG:
            logRead("SP_DMA_FULL_REG", state.hwreg.SP_DMA_FULL_REG);
            *value = 0;
            return true;
        case SP_DMA_BUSY_REG:
            logRead("SP_DMA_BUSY_REG", state.hwreg.SP_DMA_BUSY_REG);
            *value = 0;
            return true;
        case SP_SEMAPHORE_REG:
            logRead("SP_SEMAPHORE_REG", state.hwreg.SP_SEMAPHORE_REG);
            *value = state.hwreg.SP_SEMAPHORE_REG;
            state.hwreg.SP_SEMAPHORE_REG = 1;
            return true;
        case SP_PC_REG:
            logRead("SP_PC_REG", state.hwreg.SP_PC_REG);
            *value = state.hwreg.SP_PC_REG;
            return true;
        case SP_IBIST_REG:
            logRead("SP_IBIST_REG", state.hwreg.SP_IBIST_REG);
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
            logWrite("SP_MEM_ADDR_REG", value);
            state.hwreg.SP_MEM_ADDR_REG = value;
            return true;
        case SP_DRAM_ADDR_REG:
            logWrite("SP_DRAM_ADDR_REG", value);
            state.hwreg.SP_DRAM_ADDR_REG = value;
            return true;
        case SP_RD_LEN_REG:
            logWrite("SP_RD_LEN_REG", value);
            state.hwreg.SP_RD_LEN_REG = value;
            throw "unsupported_rd";
            return true;
        case SP_WR_LEN_REG:
            logWrite("SP_WR_LEN_REG", value);
            state.hwreg.SP_WR_LEN_REG = value;
            throw "unsupported_wr";
            return true;
        case SP_STATUS_REG:
            logWrite("SP_STATUS_REG", value);
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
            if (value & SP_STATUS_CLR_IOB) {
                state.hwreg.SP_STATUS_REG &= ~SP_STATUS_IOB;
            }
            if (value & SP_STATUS_SET_IOB) {
                state.hwreg.SP_STATUS_REG |= SP_STATUS_IOB;
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
            return true;
        case SP_DMA_FULL_REG:
            logWrite("SP_DMA_FULL_REG", value);
            return true;
        case SP_DMA_BUSY_REG:
            logWrite("SP_DMA_BUSY_REG", value);
            return true;
        case SP_SEMAPHORE_REG:
            logWrite("SP_SEMAPHORE_REG", value);
            state.hwreg.SP_SEMAPHORE_REG = 0;
            return true;
        case SP_PC_REG:
            logWrite("SP_PC_REG", value);
            state.hwreg.SP_PC_REG = value;
            return true;
        case SP_IBIST_REG:
            logWrite("SP_IBIST_REG", value);
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

bool read(uint bytes, u64 addr, u64 *value)
{
    std::cerr << "DPCommand::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "DPCommand::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return true;
}

}; /* namespace DPCommand */

namespace DPSpan {

bool read(uint bytes, u64 addr, u64 *value)
{
    std::cerr << "DPSpan::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "DPSpan::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return true;
}

}; /* namespace DPSpan */

namespace MI {

enum Register {
    // MI init mode
    // (W): [6:0] init length        (R): [6:0] init length
    //      [7] clear init mode           [7] init mode
    //      [8] set init mode             [8] ebus test mode
    //      [9/10] clr/set ebus test mode [9] RDRAM reg mode
    //      [11] clear DP interrupt
    //      [12] clear RDRAM reg
    //      [13] set RDRAM reg mode
    MI_MODE_REG = 0x0,
    // MI version
    // (R): [7:0] io
    //      [15:8] rac
    //      [23:16] rdp
    //      [31:24] rsp
    MI_VERSION_REG = 0x4,
    // MI interrupt
    // (R): [0] SP intr
    //      [1] SI intr
    //      [2] AI intr
    //      [3] VI intr
    //      [4] PI intr
    //      [5] DP intr
    MI_INTR_REG = 0x8,
    // MI interrupt mask
    // (W): [0/1] clear/set SP mask  (R): [0] SP intr mask
    //      [2/3] clear/set SI mask       [1] SI intr mask
    //      [4/5] clear/set AI mask       [2] AI intr mask
    //      [6/7] clear/set VI mask       [3] VI intr mask
    //      [8/9] clear/set PI mask       [4] PI intr mask
    //      [10/11] clear/set DP mask     [5] DP intr mask
    MI_INTR_MASK_REG = 0xc,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case MI_MODE_REG:
            logRead("MI_MODE_REG", state.hwreg.MI_MODE_REG);
            *value = state.hwreg.MI_MODE_REG;
            return true;
        case MI_VERSION_REG:
            logRead("MI_VERSION_REG", state.hwreg.MI_VERSION_REG);
            *value = state.hwreg.MI_VERSION_REG;
            return true;
        case MI_INTR_REG:
            logRead("MI_INTR_REG", state.hwreg.MI_INTR_REG);
            *value = state.hwreg.MI_INTR_REG;
            return true;
        case MI_INTR_MASK_REG:
            logRead("MI_INTR_MASK_REG", state.hwreg.MI_INTR_MASK_REG);
            *value = state.hwreg.MI_INTR_MASK_REG;
            return true;
        default:
            break;
    }
    logRead("MI_??", addr);
    throw "Unsupported";
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case MI_MODE_REG:
            logWrite("MI_MODE_REG", value);
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
            logWrite("MI_VERSION_REG", value);
            return true;
        case MI_INTR_REG:
            logWrite("MI_INTR_REG", value);
            return true;
        case MI_INTR_MASK_REG:
            logWrite("MI_INTR_MASK_REG", value);
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
            logWrite("MI_??", addr);
            throw "Unsupported";
            break;
    }
    return true;
}

}; /* namespace MI */

namespace VI {

enum Register {
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
    VI_CONTROL_REG = 0x0, // VI_STATUS_REG
    // VI origin
    // (RW): [23:0] frame buffer origin in bytes
    VI_DRAM_ADDR_REG = 0x4, // VI_ORIGIN_REG
    // VI width
    // (RW): [11:0] frame buffer line width in pixels
    VI_WIDTH_REG = 0x8, // VI_H_WIDTH_REG
    // VI vertical intr
    // (RW): [9:0] interrupt when current half-line = V_INTR
    VI_INTR_REG = 0xc, // VI_V_INTR_REG
    // VI current vertical line
    // (RW): [9:0] current half line, sampled once per line (the lsb of
    //             V_CURRENT is constant within a field, and in
    //             interlaced modes gives the field number - which is
    //             constant for non-interlaced modes)
    //             - Writes clears interrupt line
    VI_CURRENT_REG = 0x10, // VI_V_CURRENT_LINE_REG
    // VI video timing
    // (RW): [7:0] horizontal sync width in pixels
    //       [15:8] color burst width in pixels
    //       [19:16] vertical sync width in half lines
    //       [29:20] start of color burst in pixels from h-sync
    VI_BURST_REG = 0x14, // VI_TIMING_REG
    // VI vertical sync
    // (RW): [9:0] number of half-lines per field
    VI_V_SYNC_REG = 0x18,
    // VI horizontal sync
    // (RW): [11:0] total duration of a line in 1/4 pixel
    //       [20:16] a 5-bit leap pattern used for PAL only (h_sync_period)
    VI_H_SYNC_REG = 0x1c,
    // VI horizontal sync leap
    // (RW): [11:0] identical to h_sync_period
    //       [27:16] identical to h_sync_period
    VI_LEAP_REG = 0x20, // VI_H_SYNC_LEAP_REG
    // VI horizontal video
    // (RW): [9:0] end of active video in screen pixels
    //       [25:16] start of active video in screen pixels
    VI_H_START_REG = 0x24, // VI_H_VIDEO_REG
    // VI vertical video
    // (RW): [9:0] end of active video in screen half-lines
    //       [25:16] start of active video in screen half-lines
    VI_V_START_REG = 0x28, // VI_V_VIDEO_REG
    // VI vertical burst
    // (RW): [9:0] end of color burst enable in half-lines
    //       [25:16] start of color burst enable in half-lines
    VI_V_BURST_REG = 0x2c,
    // VI x-scale
    // (RW): [11:0] 1/horizontal scale up factor (2.10 format)
    //       [27:16] horizontal subpixel offset (2.10 format)
    VI_X_SCALE_REG = 0x30,
    // VI y-scale
    // (RW): [11:0] 1/vertical scale up factor (2.10 format)
    //       [27:16] vertical subpixel offset (2.10 format)
    VI_Y_SCALE_REG = 0x34,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {

        case VI_CONTROL_REG:
            logRead("VI_CONTROL_REG", state.hwreg.VI_CONTROL_REG);
            *value = state.hwreg.VI_CONTROL_REG;
            return true;
        case VI_DRAM_ADDR_REG:
            logRead("VI_DRAM_ADDR_REG", state.hwreg.VI_DRAM_ADDR_REG);
            *value = state.hwreg.VI_DRAM_ADDR_REG;
            return true;
        case VI_WIDTH_REG:
            logRead("VI_WIDTH_REG", state.hwreg.VI_WIDTH_REG);
            *value = state.hwreg.VI_WIDTH_REG;
            return true;
        case VI_INTR_REG:
            logRead("VI_INTR_REG", state.hwreg.VI_INTR_REG);
            *value = state.hwreg.VI_INTR_REG;
            return true;
        case VI_CURRENT_REG:
            logRead("VI_CURRENT_REG", state.hwreg.VI_CURRENT_REG);
            *value = state.hwreg.VI_CURRENT_REG;
            return true;
        case VI_BURST_REG:
            logRead("VI_BURST_REG", state.hwreg.VI_BURST_REG);
            *value = state.hwreg.VI_BURST_REG;
            return true;
        case VI_V_SYNC_REG:
            logRead("VI_V_SYNC_REG", state.hwreg.VI_V_SYNC_REG);
            *value = state.hwreg.VI_V_SYNC_REG;
            return true;
        case VI_H_SYNC_REG:
            logRead("VI_H_SYNC_REG", state.hwreg.VI_H_SYNC_REG);
            *value = state.hwreg.VI_H_SYNC_REG;
            return true;
        case VI_LEAP_REG:
            logRead("VI_LEAP_REG", state.hwreg.VI_LEAP_REG);
            *value = state.hwreg.VI_LEAP_REG;
            return true;
        case VI_H_START_REG:
            logRead("VI_H_START_REG", state.hwreg.VI_H_START_REG);
            *value = state.hwreg.VI_H_START_REG;
            return true;
        case VI_V_START_REG:
            logRead("VI_V_START_REG", state.hwreg.VI_V_START_REG);
            *value = state.hwreg.VI_V_START_REG;
            return true;
        case VI_V_BURST_REG:
            logRead("VI_V_BURST_REG", state.hwreg.VI_V_BURST_REG);
            *value = state.hwreg.VI_V_BURST_REG;
            return true;
        case VI_X_SCALE_REG:
            logRead("VI_X_SCALE_REG", state.hwreg.VI_X_SCALE_REG);
            *value = state.hwreg.VI_X_SCALE_REG;
            return true;
        case VI_Y_SCALE_REG:
            logRead("VI_Y_SCALE_REG", state.hwreg.VI_Y_SCALE_REG);
            *value = state.hwreg.VI_Y_SCALE_REG;
            return true;
        default:
            throw "VI Unsupported";
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
            logWrite("VI_CONTROL_REG", value);
            state.hwreg.VI_CONTROL_REG = value;
            return true;
        case VI_DRAM_ADDR_REG:
            logWrite("VI_DRAM_ADDR_REG", value);
            state.hwreg.VI_DRAM_ADDR_REG = value;
            return true;
        case VI_WIDTH_REG:
            logWrite("VI_WIDTH_REG", value);
            state.hwreg.VI_WIDTH_REG = value;
            return true;
        case VI_INTR_REG:
            logWrite("VI_INTR_REG", value);
            state.hwreg.VI_INTR_REG = value;
            return true;
        case VI_CURRENT_REG:
            logWrite("VI_CURRENT_REG", value);
            state.hwreg.VI_CURRENT_REG = value;
            return true;
        case VI_BURST_REG:
            logWrite("VI_BURST_REG", value);
            state.hwreg.VI_BURST_REG = value;
            return true;
        case VI_V_SYNC_REG:
            logWrite("VI_V_SYNC_REG", value);
            state.hwreg.VI_V_SYNC_REG = value;
            return true;
        case VI_H_SYNC_REG:
            logWrite("VI_H_SYNC_REG", value);
            state.hwreg.VI_H_SYNC_REG = value;
            return true;
        case VI_LEAP_REG:
            logWrite("VI_LEAP_REG", value);
            state.hwreg.VI_LEAP_REG = value;
            return true;
        case VI_H_START_REG:
            logWrite("VI_H_START_REG", value);
            state.hwreg.VI_H_START_REG = value;
            return true;
        case VI_V_START_REG:
            logWrite("VI_V_START_REG", value);
            state.hwreg.VI_V_START_REG = value;
            return true;
        case VI_V_BURST_REG:
            logWrite("VI_V_BURST_REG", value);
            state.hwreg.VI_V_BURST_REG = value;
            return true;
        case VI_X_SCALE_REG:
            logWrite("VI_X_SCALE_REG", value);
            state.hwreg.VI_X_SCALE_REG = value;
            return true;
        case VI_Y_SCALE_REG:
            logWrite("VI_Y_SCALE_REG", value);
            state.hwreg.VI_Y_SCALE_REG = value;
            return true;
        default:
            throw "VI Unsupported";
            break;
    }
    return true;
}

}; /* namespace VI */

namespace AI {

enum Register {
    // AI DRAM address
    // (W): [23:0] starting RDRAM address (8B-aligned)
    AI_DRAM_ADDR_REG = 0x0,
    // AI length
    // (RW): [14:0] transfer length (v1.0) - Bottom 3 bits are ignored
    //       [17:0] transfer length (v2.0) - Bottom 3 bits are ignored
    AI_LEN_REG = 0x4,
    // AI control
    // (W): [0] DMA enable - if LSB == 1, DMA is enabled
    AI_CONTROL_REG = 0x8,
    // AI status
    // (R): [31]/[0] ai_full (addr & len buffer full)
    //      [30] ai_busy
    //      Note that a 1to0 transition in ai_full will set interrupt
    // (W): clear audio interrupt
    AI_STATUS_REG = 0xc,
    // AI DAC sample period register
    // (W): [13:0] dac rate
    //          - vid_clock/(dperiod + 1) is the DAC sample rate
    //          - (dperiod + 1) >= 66 * (aclockhp + 1) must be true
    AI_DACRATE_REG = 0x10,
    // AI bit rate
    // (W): [3:0] bit rate (abus clock half period register - aclockhp)
    //          - vid_clock/(2*(aclockhp + 1)) is the DAC clock rate
    //          - The abus clock stops if aclockhp is zero
    AI_BITRATE_REG = 0x14,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case AI_DRAM_ADDR_REG:
            logRead("AI_DRAM_ADDR_REG", state.hwreg.AI_DRAM_ADDR_REG);
            *value = state.hwreg.AI_DRAM_ADDR_REG;
            return true;
        case AI_LEN_REG:
            logRead("AI_LEN_REG", state.hwreg.AI_LEN_REG);
            *value = state.hwreg.AI_LEN_REG;
            return true;
        case AI_CONTROL_REG:
            logRead("AI_CONTROL_REG", state.hwreg.AI_CONTROL_REG);
            *value = state.hwreg.AI_CONTROL_REG;
            return true;
        case AI_STATUS_REG:
            logRead("AI_STATUS_REG", state.hwreg.AI_STATUS_REG);
            *value = state.hwreg.AI_STATUS_REG;
            return true;
        case AI_DACRATE_REG:
            logRead("AI_DACRATE_REG", state.hwreg.AI_DACRATE_REG);
            *value = state.hwreg.AI_DACRATE_REG;
            return true;
        case AI_BITRATE_REG:
            logRead("AI_BITRATE_REG", state.hwreg.AI_BITRATE_REG);
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
            logWrite("AI_DRAM_ADDR_REG", value);
            state.hwreg.AI_DRAM_ADDR_REG = value;
            return true;
        case AI_LEN_REG:
            logWrite("AI_LEN_REG", value);
            state.hwreg.AI_LEN_REG = value;
            return true;
        case AI_CONTROL_REG:
            logWrite("AI_CONTROL_REG", value);
            state.hwreg.AI_CONTROL_REG = value;
            return true;
        case AI_STATUS_REG:
            logWrite("AI_STATUS_REG", value);
            state.hwreg.AI_STATUS_REG = value;
            return true;
        case AI_DACRATE_REG:
            logWrite("AI_DACRATE_REG", value);
            state.hwreg.AI_DACRATE_REG = value;
            return true;
        case AI_BITRATE_REG:
            logWrite("AI_BITRATE_REG", value);
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

enum Register {
    // PI DRAM address
    // (RW): [23:0] starting RDRAM address
    PI_DRAM_ADDR_REG = 0x0,
    // PI pbus (cartridge) address
    // (RW): [31:0] starting AD16 address
    PI_CART_ADDR_REG = 0x4,
    // PI read length
    // (RW): [23:0] read data length
    PI_RD_LEN_REG = 0x8,
    // PI write length
    // (RW): [23:0] write data length
    PI_WR_LEN_REG = 0xc,
    // PI status
    // (R): [0] DMA busy             (W): [0] reset controller
    //      [1] IO busy                       (and abort current op)
    //      [2] error                     [1] clear intr
    PI_STATUS_REG = 0x10,
    // PI dom1 latency
    // (RW): [7:0] domain 1 device latency
    PI_BSD_DOM1_LAT_REG = 0x14,
    // PI dom1 pulse width
    // (RW): [7:0] domain 1 device R/W strobe pulse width
    PI_BSD_DOM1_PWD_REG = 0x18,
    // PI dom1 page size
    // (RW): [3:0] domain 1 device page size
    PI_BSD_DOM1_PGS_REG = 0x1c,
    // PI dom1 release
    // (RW): [1:0] domain 1 device R/W release duration
    PI_BSD_DOM1_RLS_REG = 0x20,
    // PI dom2 latency
    // (RW): [7:0] domain 2 device latency
    PI_BSD_DOM2_LAT_REG = 0x24,
    // PI dom2 pulse width
    // (RW): [7:0] domain 2 device R/W strobe pulse width
    PI_BSD_DOM2_PWD_REG = 0x28,
    // PI dom2 page size
    // (RW): [3:0] domain 2 device page size
    PI_BSD_DOM2_PGS_REG = 0x2c,
    // PI dom2 release
    // (RW): [1:0] domain 2 device R/W release duration
    PI_BSD_DOM2_RLS_REG = 0x30,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case PI_DRAM_ADDR_REG:
            logRead("PI_DRAM_ADDR_REG", state.hwreg.PI_DRAM_ADDR_REG);
            *value = state.hwreg.PI_DRAM_ADDR_REG;
            return true;
        case PI_CART_ADDR_REG:
            logRead("PI_CART_ADDR_REG", state.hwreg.PI_CART_ADDR_REG);
            *value = state.hwreg.PI_CART_ADDR_REG;
            return true;
        case PI_RD_LEN_REG:
            logRead("PI_RD_LEN_REG", state.hwreg.PI_RD_LEN_REG);
            *value = state.hwreg.PI_RD_LEN_REG;
            return true;
        case PI_WR_LEN_REG:
            logRead("PI_WR_LEN_REG", state.hwreg.PI_WR_LEN_REG);
            *value = state.hwreg.PI_WR_LEN_REG;
            return true;
        case PI_STATUS_REG:
            logRead("PI_STATUS_REG", state.hwreg.PI_STATUS_REG);
            *value = state.hwreg.PI_STATUS_REG;
            return true;
        case PI_BSD_DOM1_LAT_REG:
            logRead("PI_BSD_DOM1_LAT_REG", state.hwreg.PI_BSD_DOM1_LAT_REG);
            *value = state.hwreg.PI_BSD_DOM1_LAT_REG;
            return true;
        case PI_BSD_DOM1_PWD_REG:
            logRead("PI_BSD_DOM1_PWD_REG", state.hwreg.PI_BSD_DOM1_PWD_REG);
            *value = state.hwreg.PI_BSD_DOM1_PWD_REG;
            return true;
        case PI_BSD_DOM1_PGS_REG:
            logRead("PI_BSD_DOM1_PGS_REG", state.hwreg.PI_BSD_DOM1_PGS_REG);
            *value = state.hwreg.PI_BSD_DOM1_PGS_REG;
            return true;
        case PI_BSD_DOM1_RLS_REG:
            logRead("PI_BSD_DOM1_RLS_REG", state.hwreg.PI_BSD_DOM1_RLS_REG);
            *value = state.hwreg.PI_BSD_DOM1_RLS_REG;
            return true;
        case PI_BSD_DOM2_LAT_REG:
            logRead("PI_BSD_DOM2_LAT_REG", state.hwreg.PI_BSD_DOM2_LAT_REG);
            *value = state.hwreg.PI_BSD_DOM2_LAT_REG;
            return true;
        case PI_BSD_DOM2_PWD_REG:
            logRead("PI_BSD_DOM2_PWD_REG", state.hwreg.PI_BSD_DOM2_PWD_REG);
            *value = state.hwreg.PI_BSD_DOM2_PWD_REG;
            return true;
        case PI_BSD_DOM2_PGS_REG:
            logRead("PI_BSD_DOM2_PGS_REG", state.hwreg.PI_BSD_DOM2_PGS_REG);
            *value = state.hwreg.PI_BSD_DOM2_PGS_REG;
            return true;
        case PI_BSD_DOM2_RLS_REG:
            logRead("PI_BSD_DOM2_RLS_REG", state.hwreg.PI_BSD_DOM2_RLS_REG);
            *value = state.hwreg.PI_BSD_DOM2_RLS_REG;
            return true;
        default:
            logRead("PI_??", addr);
            throw "Unsupported";
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
            logWrite("PI_DRAM_ADDR_REG", value);
            state.hwreg.PI_DRAM_ADDR_REG = value;
            return true;
        case PI_CART_ADDR_REG:
            logWrite("PI_CART_ADDR_REG", value);
            state.hwreg.PI_CART_ADDR_REG = value;
            return true;

        case PI_RD_LEN_REG:
            logWrite("PI_RD_LEN_REG", value);
            state.hwreg.PI_RD_LEN_REG = value;
            state.physmem.copy(
                state.hwreg.PI_CART_ADDR_REG,
                state.hwreg.PI_DRAM_ADDR_REG,
                state.hwreg.PI_RD_LEN_REG + 1U);
            state.hwreg.PI_STATUS_REG = 0;
            set_MI_INTR_REG(MI_INTR_PI);
            return true;

        case PI_WR_LEN_REG:
            logWrite("PI_WR_LEN_REG", value);
            state.hwreg.PI_WR_LEN_REG = value;
            state.physmem.copy(
                state.hwreg.PI_DRAM_ADDR_REG,
                state.hwreg.PI_CART_ADDR_REG,
                state.hwreg.PI_WR_LEN_REG + 1U);
            state.hwreg.PI_STATUS_REG = 0;
            set_MI_INTR_REG(MI_INTR_PI);
            return true;

        case PI_STATUS_REG:
            logWrite("PI_STATUS_REG", value);
            state.hwreg.PI_STATUS_REG = 0;
            if (value & PI_STATUS_RESET) {
                throw "Unsupported_PI_STATUS_REG__RESET";
            }
            if (value & PI_STATUS_CLR_INTR) {
                clear_MI_INTR_REG(MI_INTR_PI);
            }
            return true;
        case PI_BSD_DOM1_LAT_REG:
            logWrite("PI_BSD_DOM1_LAT_REG", value);
            state.hwreg.PI_BSD_DOM1_LAT_REG = value;
            return true;
        case PI_BSD_DOM1_PWD_REG:
            logWrite("PI_BSD_DOM1_PWD_REG", value);
            state.hwreg.PI_BSD_DOM1_PWD_REG = value;
            return true;
        case PI_BSD_DOM1_PGS_REG:
            logWrite("PI_BSD_DOM1_PGS_REG", value);
            state.hwreg.PI_BSD_DOM1_PGS_REG = value;
            return true;
        case PI_BSD_DOM1_RLS_REG:
            logWrite("PI_BSD_DOM1_RLS_REG", value);
            state.hwreg.PI_BSD_DOM1_RLS_REG = value;
            return true;
        case PI_BSD_DOM2_LAT_REG:
            logWrite("PI_BSD_DOM2_LAT_REG", value);
            state.hwreg.PI_BSD_DOM2_LAT_REG = value;
            return true;
        case PI_BSD_DOM2_PWD_REG:
            logWrite("PI_BSD_DOM2_PWD_REG", value);
            state.hwreg.PI_BSD_DOM2_PWD_REG = value;
            return true;
        case PI_BSD_DOM2_PGS_REG:
            logWrite("PI_BSD_DOM2_PGS_REG", value);
            state.hwreg.PI_BSD_DOM2_PGS_REG = value;
            return true;
        case PI_BSD_DOM2_RLS_REG:
            logWrite("PI_BSD_DOM2_RLS_REG", value);
            state.hwreg.PI_BSD_DOM2_RLS_REG = value;
            return true;
        default:
            logWrite("PI_??", addr);
            throw "Unsupported";
            break;
    }
    return true;
}

}; /* namespace PI */

namespace RI {

enum Register {
    // (RW): [1:0] operating mode
    //       [2] stop T active
    //       [3] stop R active
    RI_MODE_REG = 0x0,
    // (RW): [5:0] current control input
    //       [6] current control enable
    RI_CONFIG_REG = 0x4,
    // (W): [] any write updates current control register
    RI_CURRENT_LOAD_REG = 0x8,
    // (RW): [2:0] receive select
    //       [2:0] transmit select
    RI_SELECT_REG = 0xc,
    // (RW): [7:0] clean refresh delay
    //       [15:8] dirty refresh delay
    //       [16] refresh bank
    //       [17] refresh enable
    //       [18] refresh optimize
    RI_REFRESH_REG = 0x10,
    // (RW): [3:0] DMA latency/overlap
    RI_LATENCY_REG = 0x14,
    // (R): [0] nack error
    //      [1] ack error
    RI_RERROR_REG  = 0x18,
    // (W): [] any write clears all error bits
    RI_WERROR_REG = 0x1c,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RI_MODE_REG:
            logRead("RI_MODE_REG", state.hwreg.RI_MODE_REG);
            *value = state.hwreg.RI_MODE_REG;
            return true;
        case RI_CONFIG_REG:
            logRead("RI_CONFIG_REG", state.hwreg.RI_CONFIG_REG);
            *value = state.hwreg.RI_CONFIG_REG;
            return true;
        case RI_CURRENT_LOAD_REG:
            logRead("RI_CURRENT_LOAD_REG", state.hwreg.RI_CURRENT_LOAD_REG);
            *value = state.hwreg.RI_CURRENT_LOAD_REG;
            return true;
        case RI_SELECT_REG:
            logRead("RI_SELECT_REG", state.hwreg.RI_SELECT_REG);
            *value = state.hwreg.RI_SELECT_REG;
            return true;
        case RI_REFRESH_REG:
            logRead("RI_REFRESH_REG", state.hwreg.RI_REFRESH_REG);
            *value = state.hwreg.RI_REFRESH_REG;
            return true;
        case RI_LATENCY_REG:
            logRead("RI_LATENCY_REG", state.hwreg.RI_LATENCY_REG);
            *value = state.hwreg.RI_LATENCY_REG;
            return true;
        case RI_RERROR_REG:
            logRead("RI_RERROR_REG", state.hwreg.RI_RERROR_REG);
            *value = state.hwreg.RI_RERROR_REG;
            return true;
        case RI_WERROR_REG:
            logRead("RI_WERROR_REG", state.hwreg.RI_WERROR_REG);
            *value = state.hwreg.RI_WERROR_REG;
            return true;
        default:
            break;
    }
    logRead("RI_??", addr);
    throw "Unsupported";
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case RI_MODE_REG:
            logWrite("RI_MODE_REG", value);
            state.hwreg.RI_MODE_REG = value;
            return true;
        case RI_CONFIG_REG:
            logWrite("RI_CONFIG_REG", value);
            state.hwreg.RI_CONFIG_REG = value;
            return true;
        case RI_CURRENT_LOAD_REG:
            logWrite("RI_CURRENT_LOAD_REG", value);
            state.hwreg.RI_CURRENT_LOAD_REG = value;
            return true;
        case RI_SELECT_REG:
            logWrite("RI_SELECT_REG", value);
            state.hwreg.RI_SELECT_REG = value;
            return true;
        case RI_REFRESH_REG:
            logWrite("RI_REFRESH_REG", value);
            state.hwreg.RI_REFRESH_REG = value;
            return true;
        case RI_LATENCY_REG:
            logWrite("RI_LATENCY_REG", value);
            state.hwreg.RI_LATENCY_REG = value;
            return true;
        case RI_RERROR_REG:
            logWrite("RI_RERROR_REG", value);
            state.hwreg.RI_RERROR_REG = value;
            return true;
        case RI_WERROR_REG:
            logWrite("RI_WERROR_REG", value);
            state.hwreg.RI_WERROR_REG = value;
            return true;
        default:
            logWrite("RI_??", addr);
            throw "Unsupported";
            break;
    }
    return true;
}

}; /* namespace RI */

namespace SI {

enum Register {
    // SI DRAM address
    // (R/W): [23:0] starting RDRAM address
    SI_DRAM_ADDR_REG = 0x0,
    // SI address read 64B
    // (W): [] any write causes a 64B DMA write
    SI_PIF_ADDR_RD64B_REG = 0x4,
    // SI address write 64B
    // (W): [] any write causes a 64B DMA read
    SI_PIF_ADDR_WR64B_REG = 0x10,
    // SI status
    // (W): [] any write clears interrupt
    // (R): [0] DMA busy
    //      [1] IO read busy
    //      [2] reserved
    //      [3] DMA error
    //      [12] interrupt
    SI_STATUS_REG = 0x18,
};

bool read(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
        case SI_DRAM_ADDR_REG:
            logRead("SI_DRAM_ADDR_REG", state.hwreg.SI_DRAM_ADDR_REG);
            *value = state.hwreg.SI_DRAM_ADDR_REG;
            return true;
        case SI_PIF_ADDR_RD64B_REG:
            logRead("SI_PIF_ADDR_RD64B_REG", state.hwreg.SI_PIF_ADDR_RD64B_REG);
            *value = state.hwreg.SI_PIF_ADDR_RD64B_REG;
            return true;
        case SI_PIF_ADDR_WR64B_REG:
            logRead("SI_PIF_ADDR_WR64B_REG", state.hwreg.SI_PIF_ADDR_WR64B_REG);
            *value = state.hwreg.SI_PIF_ADDR_WR64B_REG;
            return true;
        case SI_STATUS_REG:
            logRead("SI_STATUS_REG", state.hwreg.SI_STATUS_REG);
            *value = state.hwreg.SI_STATUS_REG;
            return true;
        default:
            throw "SI Unsupported";
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
            logWrite("SI_DRAM_ADDR_REG", value);
            state.hwreg.SI_DRAM_ADDR_REG = value;
            return true;

        case SI_PIF_ADDR_RD64B_REG:
            logWrite("SI_PIF_ADDR_RD64B_REG", value);
            state.hwreg.SI_PIF_ADDR_RD64B_REG = value;
            state.physmem.copy(
                state.hwreg.SI_DRAM_ADDR_REG,
                state.hwreg.SI_PIF_ADDR_RD64B_REG,
                64);
            state.hwreg.SI_STATUS_REG = UINT32_C(1) << 12;
            set_MI_INTR_REG(MI_INTR_SI);
            return true;

        case SI_PIF_ADDR_WR64B_REG:
            logWrite("SI_PIF_ADDR_WR64B_REG", value);
            state.hwreg.SI_PIF_ADDR_WR64B_REG = value;
            state.physmem.copy(
                state.hwreg.SI_PIF_ADDR_WR64B_REG,
                state.hwreg.SI_DRAM_ADDR_REG,
                64);
            state.hwreg.SI_STATUS_REG = UINT32_C(1) << 12;
            set_MI_INTR_REG(MI_INTR_SI);
            return true;

        case SI_STATUS_REG:
            logWrite("SI_STATUS_REG", value);
            clear_MI_INTR_REG(MI_INTR_SI);
            state.hwreg.SI_STATUS_REG &= ~(UINT32_C(1) << 12);
            return true;

        default:
            throw "SI Unsupported";
            break;
    }
    return true;
}

}; /* namespace SI */

namespace PIF {

bool read(uint bytes, u64 addr, u64 *value)
{
    std::cerr << "PIF::read(" << std::hex << addr << ")" << std::endl;
    if (addr > 0x800)
        return false;
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "PIF::write(" << std::hex << addr << ", " << value << ")" << std::endl;
    if (addr > 0x800)
        return false;
    return true;
}

}; /* namespace PIF */

namespace Cart_2_1 {

bool read(uint bytes, u64 addr, u64 *value)
{
    std::cerr << "Cart_2_1::read(" << std::hex << addr << ")" << std::endl;
    *value = 0;
    return true;
}

bool write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "Cart_2_1::write(" << std::hex << addr << ", " << value << ")" << std::endl;
    return true;
}

}; /* namespace PIF */

};
