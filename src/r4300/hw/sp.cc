
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <core.h>
#include <debugger.h>

namespace R4300 {

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


/**
 * @brief Write the SP register SP_RD_LEN_REG.
 *  Writing the register starts a DMA tranfer from DRAM to DMEM/IMEM.
 */
void write_SP_RD_LEN_REG(u32 value) {
    debugger::info(Debugger::SP, "SP_RD_LEN_REG <- {:08x}", value);
    state.hwreg.SP_RD_LEN_REG = value;
    u32 len   = (value >> SP_RD_LEN_LEN_SHIFT)   & SP_RD_LEN_LEN_MASK;
    u32 count = (value >> SP_RD_LEN_COUNT_SHIFT) & SP_RD_LEN_COUNT_MASK;
    u32 skip  = (value >> SP_RD_LEN_SKIP_SHIFT)  & SP_RD_LEN_SKIP_MASK;
    u32 dst = state.hwreg.SP_MEM_ADDR_REG  & SP_MEM_ADDR_MASK;
    u64 src = state.hwreg.SP_DRAM_ADDR_REG & SP_DRAM_ADDR_MASK;
    u8 *dst_ptr =
        (state.hwreg.SP_MEM_ADDR_REG & SP_MEM_ADDR_IMEM) ?
            state.imem : state.dmem;

    // DMA length and line count are encoded as (value - 1).
    // The amount of data transferred must be a multiple of 8 bytes (64 bits),
    // hence the lower three bits of length are ignored and
    // assumed to be all 1’s.
    len   = 1 + (len | 0x7);
    count = 1 + count;

    // @todo clear/set DMA busy+full bits.
    // @todo wrapping over the end of dmem/imem ?
    for (; count > 0; count--, src += len + skip, dst += len) {
        // Check that the source range fits in the cartridge memory.
        if ((src + len) > 0x400000lu) {
            debugger::warn(Debugger::SP,
                "SP_RD_LEN_REG source range invalid: {:08x}+{:08x}",
                src, len);
            return;
        }
        // Check that the destination range fits in the dmem / imem memory.
        if ((dst + len) > 0x1000) {
            debugger::warn(Debugger::SP,
                "SP_RD_LEN_REG destination range invalid: {:08x}+{:08x}",
                dst, len);
            return;
        }
        // Perform the slice copy.
        memcpy(&dst_ptr[dst], &state.dram[src], len);
    }
}

/**
 * @brief Write the SP register SP_WR_LEN_REG.
 *  Writing the register starts a DMA tranfer from DMEM/IMEM to DRAM.
 */
void write_SP_WR_LEN_REG(u32 value) {
    debugger::info(Debugger::SP, "SP_WR_LEN_REG <- {:08x}", value);
    state.hwreg.SP_WR_LEN_REG = value;
    u32 len   = (value >> SP_RD_LEN_LEN_SHIFT)   & SP_RD_LEN_LEN_MASK;
    u32 count = (value >> SP_RD_LEN_COUNT_SHIFT) & SP_RD_LEN_COUNT_MASK;
    u32 skip  = (value >> SP_RD_LEN_SKIP_SHIFT)  & SP_RD_LEN_SKIP_MASK;
    u32 src = state.hwreg.SP_MEM_ADDR_REG  & SP_MEM_ADDR_MASK;
    u64 dst = state.hwreg.SP_DRAM_ADDR_REG & SP_DRAM_ADDR_MASK;
    u8 *src_ptr =
        (state.hwreg.SP_MEM_ADDR_REG & SP_MEM_ADDR_IMEM) ?
            state.imem : state.dmem;

    // DMA length and line count are encoded as (value - 1).
    // The amount of data transferred must be a multiple of 8 bytes (64 bits),
    // hence the lower three bits of length are ignored and
    // assumed to be all 1’s.
    len   = 1 + (len | 0x7);
    count = 1 + count;

    // @todo clear/set DMA busy+full bits.
    for (; count > 0; count--, src += len, dst += len + skip) {
        // Check that the destination range fits in the cartridge memory.
        if ((dst + len) > 0x400000lu) {
            debugger::warn(Debugger::SP,
                "SP_WR_LEN_REG destination range invalid: {:08x}+{:08x}",
                dst, len);
            return;
        }
        // Check that the source range fits in the dmem / imem memory.
        if ((src + len) > 0x1000) {
            debugger::warn(Debugger::SP,
                "SP_WR_LEN_REG source range invalid: {:08x}+{:08x}",
                src, len);
            return;
        }
        // Perform the slice copy.
        memcpy(&state.dram[dst], &src_ptr[src], len);
        core::invalidate_recompiler_cache(dst, dst + len);
    }
}

/**
 * @brief Write the SP register SP_STATUS_REG.
 *  This function is used for both the CPU (SP_STATUS_REG) and
 *  RSP (Coprocessor 0 register 4) view of the register.
 */
void write_SP_STATUS_REG(u32 value) {
    debugger::info(Debugger::SP, "SP_STATUS_REG <- {:08x}", value);
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
        // Expected behaviour not clearly known.
        core::halt("SP_STATUS_SET_INTR");
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
    debugger::info(Debugger::SP, "SP_SEMAPHORE_REG -> {:08x}",
        state.hwreg.SP_SEMAPHORE_REG);
    u32 value = state.hwreg.SP_SEMAPHORE_REG;
    state.hwreg.SP_SEMAPHORE_REG = 1;
    return value;
}

bool read_SP_REG(uint bytes, u64 addr, u64 *value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case SP_MEM_ADDR_REG:
        debugger::info(Debugger::SP, "SP_MEM_ADDR_REG -> {:08x}",
            state.hwreg.SP_MEM_ADDR_REG);
        *value = state.hwreg.SP_MEM_ADDR_REG;
        return true;
    case SP_DRAM_ADDR_REG:
        debugger::info(Debugger::SP, "SP_DRAM_ADDR_REG -> {:08x}",
            state.hwreg.SP_DRAM_ADDR_REG);
        *value = state.hwreg.SP_DRAM_ADDR_REG;
        return true;
    case SP_RD_LEN_REG:
        debugger::info(Debugger::SP, "SP_RD_LEN_REG -> {:08x}",
            state.hwreg.SP_RD_LEN_REG);
        *value = state.hwreg.SP_RD_LEN_REG;
        return true;
    case SP_WR_LEN_REG:
        debugger::info(Debugger::SP, "SP_WR_LEN_REG -> {:08x}",
            state.hwreg.SP_WR_LEN_REG);
        *value = state.hwreg.SP_WR_LEN_REG;
        return true;
    case SP_STATUS_REG:
        debugger::info(Debugger::SP, "SP_STATUS_REG -> {:08x}",
            state.hwreg.SP_STATUS_REG);
        *value = state.hwreg.SP_STATUS_REG;
        return true;
    case SP_DMA_FULL_REG:
        debugger::info(Debugger::SP, "SP_DMA_FULL_REG -> 0");
        *value = 0;
        return true;
    case SP_DMA_BUSY_REG:
        debugger::info(Debugger::SP, "SP_DMA_BUSY_REG -> 0");
        *value = 0;
        return true;
    case SP_SEMAPHORE_REG:
        *value = read_SP_SEMAPHORE_REG();
        return true;
    case SP_PC_REG:
        debugger::info(Debugger::SP, "SP_PC_REG -> {:08x}", state.rspreg.pc);
        *value = state.rspreg.pc;
        return true;
    case SP_IBIST_REG:
        debugger::info(Debugger::SP, "SP_IBIST_REG -> {:08x}",
            state.hwreg.SP_IBIST_REG);
        *value = state.hwreg.SP_IBIST_REG;
        return true;
    default:
        debugger::warn(Debugger::SP,
            "Read of unknown SP register: {:08x}", addr);
        core::halt("SP read unknown");
        break;
    }
    *value = 0;
    return true;
}

bool write_SP_REG(uint bytes, u64 addr, u64 value)
{
    if (bytes != 4)
        return false;

    switch (addr) {
    case SP_MEM_ADDR_REG:
        debugger::info(Debugger::SP, "SP_MEM_ADDR_REG <- {:08x}", value);
        state.hwreg.SP_MEM_ADDR_REG =
            value & (SP_MEM_ADDR_MASK | SP_MEM_ADDR_IMEM);
        return true;
    case SP_DRAM_ADDR_REG:
        debugger::info(Debugger::SP, "SP_DRAM_ADDR_REG <- {:08x}", value);
        state.hwreg.SP_DRAM_ADDR_REG = value & SP_DRAM_ADDR_MASK;
        return true;
    case SP_RD_LEN_REG:
        write_SP_RD_LEN_REG(value);
        return true;
    case SP_WR_LEN_REG:
        debugger::info(Debugger::SP, "SP_WR_LEN_REG <- {:08x}", value);
        state.hwreg.SP_WR_LEN_REG = value;
        core::halt("Write of SP_WR_LEN_LEG");
        return true;
    case SP_STATUS_REG:
        write_SP_STATUS_REG(value);
        return true;
    case SP_DMA_FULL_REG:
        debugger::info(Debugger::SP, "SP_DMA_FULL_REG <- {:08x}", value);
        return true;
    case SP_DMA_BUSY_REG:
        debugger::info(Debugger::SP, "SP_DMA_BUSY_REG <- {:08x}", value);
        return true;
    case SP_SEMAPHORE_REG:
        debugger::info(Debugger::SP, "SP_SEMAPHORE_REG <- {:08x}", value);
        state.hwreg.SP_SEMAPHORE_REG = 0;
        return true;
    case SP_PC_REG:
        debugger::info(Debugger::SP, "SP_PC_REG <- {:08x}", value);
        // Note: not too preoccupied with the behaviour when the RSP
        // is already running; it is probably not recommended to try that..
        state.rspreg.pc = value & 0xffflu;
        state.rsp.nextPc = state.rspreg.pc;
        state.rsp.nextAction = State::Action::Jump;
        return true;
    case SP_IBIST_REG:
        debugger::info(Debugger::SP, "SP_IBIST_REG <- {:08x}", value);
        state.hwreg.SP_IBIST_REG = value;
        return true;

    default:
        debugger::warn(Debugger::SP,
            "Write of unknown SP register: {:08x} <- {:08x}",
            addr, value);
        core::halt("SP write unknown");
        break;
    }
    return true;
}

}; /* namespace R4300 */
