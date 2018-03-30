
#include <iostream>
#include <iomanip>

#include <r4300/hw.h>

namespace R4300 {

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

static uint32_t DeviceType;
static uint32_t DeviceId;
static uint32_t Delay;
static uint32_t Mode;
static uint32_t RefInterval;
static uint32_t RefRow;
static uint32_t RasInterval;
static uint32_t MinInterval;
static uint32_t AddrSelect;
static uint32_t DeviceManuf;

u64 read(uint bytes, u64 addr)
{
    std::cerr << "RdRam::read(" << std::hex << addr << ")" << std::endl;
    if (bytes != 4)
        throw "RI::ReadInvalidWidth";
    switch (addr & ~0x7) {
        case RDRAM_DEVICE_TYPE_REG:
            std::cerr << "RDRAM_DEVICE_TYPE_REG" << std::endl;
            return DeviceType;
        case RDRAM_DEVICE_ID_REG:
            std::cerr << "RDRAM_DEVICE_ID_REG" << std::endl;
            return DeviceId;
        case RDRAM_DELAY_REG:
            std::cerr << "RDRAM_DELAY_REG" << std::endl;
            return Delay;
        case RDRAM_MODE_REG:
            std::cerr << "RDRAM_MODE_REG" << std::endl;
            return Mode;
        case RDRAM_REF_INTERVAL_REG:
            std::cerr << "RDRAM_REF_INTERVAL_REG" << std::endl;
            return RefInterval;
        case RDRAM_REF_ROW_REG:
            std::cerr << "RDRAM_REF_ROW_REG" << std::endl;
            return RefRow;
        case RDRAM_RAS_INTERVAL_REG:
            std::cerr << "RDRAM_RAS_INTERVAL_REG" << std::endl;
            return RasInterval;
        case RDRAM_MIN_INTERVAL_REG:
            std::cerr << "RDRAM_MIN_INTERVAL_REG" << std::endl;
            return MinInterval;
        case RDRAM_ADDR_SELECT_REG:
            std::cerr << "RDRAM_ADDR_SELECT_REG" << std::endl;
            return AddrSelect;
        case RDRAM_DEVICE_MANUF_REG:
            std::cerr << "RDRAM_DEVICE_MANUF_REG" << std::endl;
            return DeviceManuf;
        default:
            throw "RDRAM read unsupported";
            break;
    }
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "RdRam::write(" << std::hex << addr << "," << value << ")" << std::endl;
    if (bytes != 4)
        throw "RI::WriteInvalidWidth";
    switch (addr & ~0x7) {
        case RDRAM_DEVICE_TYPE_REG:
            std::cerr << "RDRAM_DEVICE_TYPE_REG" << std::endl;
            DeviceType = value;
            break;
        case RDRAM_DEVICE_ID_REG:
            std::cerr << "RDRAM_DEVICE_ID_REG" << std::endl;
            DeviceId = value;
            break;
        case RDRAM_DELAY_REG:
            std::cerr << "RDRAM_DELAY_REG" << std::endl;
            Delay = value;
            break;
        case RDRAM_MODE_REG:
            std::cerr << "RDRAM_MODE_REG" << std::endl;
            Mode = value;
            break;
        case RDRAM_REF_INTERVAL_REG:
            std::cerr << "RDRAM_REF_INTERVAL_REG" << std::endl;
            RefInterval = value;
            break;
        case RDRAM_REF_ROW_REG:
            std::cerr << "RDRAM_REF_ROW_REG" << std::endl;
            RefRow = value;
            break;
        case RDRAM_RAS_INTERVAL_REG:
            std::cerr << "RDRAM_RAS_INTERVAL_REG" << std::endl;
            RasInterval = value;
            break;
        case RDRAM_MIN_INTERVAL_REG:
            std::cerr << "RDRAM_MIN_INTERVAL_REG" << std::endl;
            MinInterval = value;
            break;
        case RDRAM_ADDR_SELECT_REG:
            std::cerr << "RDRAM_ADDR_SELECT_REG" << std::endl;
            AddrSelect = value;
            break;
        case RDRAM_DEVICE_MANUF_REG:
            std::cerr << "RDRAM_DEVICE_MANUF_REG" << std::endl;
            DeviceManuf = value;
            break;
        default:
            // throw "RDRAM write unsupported";
            break;
    }
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

static u32 MemAddr;
static u32 DramAddr;
static u32 ReadLen;
static u32 WriteLen;
static u32 Status;
static u32 Semaphore;
static u32 ProgramCounter;
static u32 Ibist;

u64 read(uint bytes, u64 addr)
{
    std::cerr << "SP::read(" << std::hex << addr << ")" << std::endl;

    if (bytes != 4)
        throw "RI::ReadInvalidWidth";

    switch (addr) {
        case SP_MEM_ADDR_REG:
            std::cerr << "SP_MEM_ADDR_REG" << std::endl;
            return MemAddr;
        case SP_DRAM_ADDR_REG:
            std::cerr << "SP_DRAM_ADDR_REG" << std::endl;
            return DramAddr;
        case SP_RD_LEN_REG:
            std::cerr << "SP_RD_LEN_REG" << std::endl;
            return ReadLen;
        case SP_WR_LEN_REG:
            std::cerr << "SP_WR_LEN_REG" << std::endl;
            return WriteLen;
        case SP_STATUS_REG:
            std::cerr << "SP_STATUS_REG" << std::endl;
            return Status;
        case SP_DMA_FULL_REG:
            std::cerr << "SP_DMA_FULL_REG" << std::endl;
            return 0;
        case SP_DMA_BUSY_REG:
            std::cerr << "SP_DMA_BUSY_REG" << std::endl;
            return 0;
        case SP_SEMAPHORE_REG: {
            std::cerr << "SP_SEMAPHORE_REG" << std::endl;
            u32 old = Semaphore;
            Semaphore = 1;
            return old;
        }
        case SP_PC_REG:
            std::cerr << "SP_PC_REG" << std::endl;
            return ProgramCounter;
        case SP_IBIST_REG:
            std::cerr << "SP_IBIST_REG" << std::endl;
            return Ibist;
        default:
            throw "SP read unsupported";
            break;
    }
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "SP::write(" << std::hex << addr << ")" << std::endl;

    if (bytes != 4)
        throw "RI::ReadInvalidWidth";

    switch (addr) {
        case SP_MEM_ADDR_REG:
            std::cerr << "SP_MEM_ADDR_REG" << std::endl;
            MemAddr = value;
            break;
        case SP_DRAM_ADDR_REG:
            std::cerr << "SP_DRAM_ADDR_REG" << std::endl;
            DramAddr = value;
            break;
        case SP_RD_LEN_REG:
            std::cerr << "SP_RD_LEN_REG" << std::endl;
            ReadLen = value;
            break;
        case SP_WR_LEN_REG:
            std::cerr << "SP_WR_LEN_REG" << std::endl;
            WriteLen = value;;
            break;
        case SP_STATUS_REG:
            std::cerr << "SP_STATUS_REG" << std::endl;
            break;
        case SP_DMA_FULL_REG:
            std::cerr << "SP_DMA_FULL_REG" << std::endl;
            break;
        case SP_DMA_BUSY_REG:
            std::cerr << "SP_DMA_BUSY_REG" << std::endl;
            break;
        case SP_SEMAPHORE_REG:
            std::cerr << "SP_SEMAPHORE_REG" << std::endl;
            Semaphore = 0;
            break;
        case SP_PC_REG:
            std::cerr << "SP_PC_REG" << std::endl;
            ProgramCounter = value;
            break;
        case SP_IBIST_REG:
            std::cerr << "SP_IBIST_REG" << std::endl;
            Ibist = value;
            break;
        default:
            throw "SP read unsupported";
            break;
    }
}

}; /* namespace SP */

namespace DPCommand {

u64 read(uint bytes, u64 addr)
{
    std::cerr << "DPCommand::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "DPCommand::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
}

}; /* namespace DPCommand */

namespace DPSpan {

u64 read(uint bytes, u64 addr)
{
    std::cerr << "DPSpan::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "DPSpan::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
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

static uint32_t Mode;
static uint32_t Version;
static uint32_t Intr;
static uint32_t IntrMask;

u64 read(uint bytes, u64 addr)
{
    std::cerr << "MI::read(" << std::hex << addr << ")" << std::endl;
    switch (addr) {
        case MI_MODE_REG:
            std::cerr << "MI_MODE_REG" << std::endl;
            return Mode;
        case MI_VERSION_REG:
            std::cerr << "MI_VERSION_REG" << std::endl;
            return Version;
        case MI_INTR_REG:
            std::cerr << "MI_INTR_REG" << std::endl;
            return Intr;
        case MI_INTR_MASK_REG:
            std::cerr << "MI_INTR_MASK_REG" << std::endl;
            return IntrMask;
        default:
            break;
    }
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "MI::write(" << std::hex << addr << ")" << std::endl;
    switch (addr) {
        case MI_MODE_REG:
            std::cerr << "MI_MODE_REG" << std::endl;
            Mode = value;
            break;
        case MI_VERSION_REG:
            break;
        case MI_INTR_MASK_REG:
            std::cerr << "MI_INTR_MASK_REG" << std::endl;
            IntrMask = value;
            break;
        default:
            throw "Unsupported";
            break;
    }
}

}; /* namespace MI */

namespace VI {

u64 read(uint bytes, u64 addr)
{
    std::cerr << "VI::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "VI::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
}

}; /* namespace VI */

namespace AI {

u64 read(uint bytes, u64 addr)
{
    std::cerr << "AI::read(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "AI::write(" << std::hex << addr << ")" << std::endl;
    throw "Unsupported";
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

u32 DramAddr;
u32 CartAddr;
u32 ReadLen;
u32 WriteLen;
u32 Status = 0;
u32 BsdDom1Lat;
u32 BsdDom1Pwd;
u32 BsdDom1Pgs;
u32 BsdDom1Rls;
u32 BsdDom2Lat;
u32 BsdDom2Pwd;
u32 BsdDom2Pgs;
u32 BsdDom2Rls;

u64 read(uint bytes, u64 addr)
{
    std::cerr << "PI::read(" << std::hex << addr << ")" << std::endl;

    switch (addr) {
        case PI_DRAM_ADDR_REG:
            std::cerr << "PI_DRAM_ADDR_REG" << std::endl;
            return DramAddr;
        case PI_CART_ADDR_REG:
            std::cerr << "PI_CART_ADDR_REG" << std::endl;
            return CartAddr;
        case PI_RD_LEN_REG:
            std::cerr << "PI_RD_LEN_REG" << std::endl;
            return ReadLen;
        case PI_WR_LEN_REG:
            std::cerr << "PI_WR_LEN_REG" << std::endl;
            return WriteLen;
        case PI_STATUS_REG:
            std::cerr << "PI_STATUS_REG" << std::endl;
            return Status;
        case PI_BSD_DOM1_LAT_REG:
            std::cerr << "PI_BSD_DOM1_LAT_REG" << std::endl;
            return BsdDom1Lat;
        case PI_BSD_DOM1_PWD_REG:
            std::cerr << "PI_BSD_DOM1_PWD_REG" << std::endl;
            return BsdDom1Pwd;
        case PI_BSD_DOM1_PGS_REG:
            std::cerr << "PI_BSD_DOM1_PGS_REG" << std::endl;
            return BsdDom1Pgs;
        case PI_BSD_DOM1_RLS_REG:
            std::cerr << "PI_BSD_DOM1_RLS_REG" << std::endl;
            return BsdDom1Rls;
        case PI_BSD_DOM2_LAT_REG:
            std::cerr << "PI_BSD_DOM2_LAT_REG" << std::endl;
            return BsdDom2Lat;
        case PI_BSD_DOM2_PWD_REG:
            std::cerr << "PI_BSD_DOM2_PWD_REG" << std::endl;
            return BsdDom2Pwd;
        case PI_BSD_DOM2_PGS_REG:
            std::cerr << "PI_BSD_DOM2_PGS_REG" << std::endl;
            return BsdDom2Pgs;
        case PI_BSD_DOM2_RLS_REG:
            std::cerr << "PI_BSD_DOM2_RLS_REG" << std::endl;
            return BsdDom2Rls;
        default:
            throw "Unsupported";
            break;
    }
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "PI::write(" << std::hex << addr << "," << value << ")" << std::endl;
    switch (addr) {
        case PI_DRAM_ADDR_REG:
            std::cerr << "PI_DRAM_ADDR_REG" << std::endl;
            DramAddr = value;
            break;
        case PI_CART_ADDR_REG:
            std::cerr << "PI_CART_ADDR_REG" << std::endl;
            CartAddr = value;
            break;
        case PI_RD_LEN_REG:
            std::cerr << "PI_RD_LEN_REG" << std::endl;
            ReadLen = value;
            physmem.copy(CartAddr, DramAddr, ReadLen);
            break;
        case PI_WR_LEN_REG:
            std::cerr << "PI_WR_LEN_REG" << std::endl;
            WriteLen = value;
            physmem.copy(DramAddr, CartAddr, WriteLen);
            break;
        case PI_STATUS_REG:
            std::cerr << "PI_STATUS_REG" << std::endl;
            Status = value;
            break;
        case PI_BSD_DOM1_LAT_REG:
            std::cerr << "PI_BSD_DOM1_LAT_REG" << std::endl;
            BsdDom1Lat = value;
            break;
        case PI_BSD_DOM1_PWD_REG:
            std::cerr << "PI_BSD_DOM1_PWD_REG" << std::endl;
            BsdDom1Pwd = value;
            break;
        case PI_BSD_DOM1_PGS_REG:
            std::cerr << "PI_BSD_DOM1_PGS_REG" << std::endl;
            BsdDom1Pgs = value;
            break;
        case PI_BSD_DOM1_RLS_REG:
            std::cerr << "PI_BSD_DOM1_RLS_REG" << std::endl;
            BsdDom1Rls = value;
            break;
        case PI_BSD_DOM2_LAT_REG:
            std::cerr << "PI_BSD_DOM2_LAT_REG" << std::endl;
            BsdDom2Lat = value;
            break;
        case PI_BSD_DOM2_PWD_REG:
            std::cerr << "PI_BSD_DOM2_PWD_REG" << std::endl;
            BsdDom2Pwd = value;
            break;
        case PI_BSD_DOM2_PGS_REG:
            std::cerr << "PI_BSD_DOM2_PGS_REG" << std::endl;
            BsdDom2Pgs = value;
            break;
        case PI_BSD_DOM2_RLS_REG:
            std::cerr << "PI_BSD_DOM2_RLS_REG" << std::endl;
            BsdDom2Rls = value;
            break;
        default:
            throw "Unsupported";
            break;
    }
}

}; /* namespace PI */

namespace RI {

static uint32_t Mode;
static uint32_t Config;
static uint32_t CurrentLoad;
static uint32_t Select;
static uint32_t Refresh;
static uint32_t Latency;
static uint32_t RError;
static uint32_t WError;

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

u64 read(uint bytes, u64 addr)
{
    std::cerr << "RI::read(" << std::hex << addr << ")" << std::endl;
    if (bytes != 4)
        throw "RI::ReadInvalidWidth";
    switch (addr) {
        case RI_MODE_REG:
            std::cerr << "RI_MODE_REG" << std::endl;
            return Mode;
        case RI_CONFIG_REG:
            std::cerr << "RI_CONFIG_REG" << std::endl;
            return Config;
        case RI_CURRENT_LOAD_REG:
            std::cerr << "RI_CURRENT_LOAD_REG" << std::endl;
            return CurrentLoad;
        case RI_SELECT_REG:
            std::cerr << "RI_SELECT_REG" << std::endl;
            return Select;
        case RI_REFRESH_REG:
            std::cerr << "RI_REFRESH_REG" << std::endl;
            return Refresh;
        case RI_LATENCY_REG:
            std::cerr << "RI_LATENCY_REG" << std::endl;
            return Latency;
        case RI_RERROR_REG:
            std::cerr << "RI_RERROR_REG" << std::endl;
            return RError;
        case RI_WERROR_REG:
            std::cerr << "RI_WERROR_REG" << std::endl;
            return WError;
        default:
            break;
    }
    throw "Unsupported";
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "RI::write(" << std::hex << addr << "," << value << ")" << std::endl;
    if (bytes != 4)
        throw "RI::WriteInvalidWidth";
    switch (addr) {
        case RI_MODE_REG:
            std::cerr << "RI_MODE_REG" << std::endl;
            Mode = value;
            break;
        case RI_CONFIG_REG:
            std::cerr << "RI_CONFIG_REG" << std::endl;
            Config = value;
            break;
        case RI_CURRENT_LOAD_REG:
            std::cerr << "RI_CURRENT_LOAD_REG" << std::endl;
            CurrentLoad = value;
            break;
        case RI_SELECT_REG:
            std::cerr << "RI_SELECT_REG" << std::endl;
            Select = value;
            break;
        case RI_REFRESH_REG:
            std::cerr << "RI_REFRESH_REG" << std::endl;
            Refresh = value;
            break;
        case RI_LATENCY_REG:
            std::cerr << "RI_LATENCY_REG" << std::endl;
            Latency = value;
            break;
        case RI_RERROR_REG:
            std::cerr << "RI_RERROR_REG" << std::endl;
            RError = value;
            break;
        case RI_WERROR_REG:
            std::cerr << "RI_WERROR_REG" << std::endl;
            WError = value;
            break;
        default:
            throw "Unsupported";
            break;
    }
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

static u32 DramAddr;
static u32 Status;

u64 read(uint bytes, u64 addr)
{
    std::cerr << "SI::read(" << std::hex << addr << ")" << std::endl;
    if (bytes != 4)
        throw "SI::ReadInvalidWidth";
    switch (addr) {
        case SI_DRAM_ADDR_REG:
            std::cerr << "SI_DRAM_ADDR_REG" << std::endl;
            return DramAddr;
        case SI_STATUS_REG:
            std::cerr << "SI_STATUS_REG" << std::endl;
            return Status;
        default:
            throw "SI Unsupported";
            break;
    }
    return 0;
}

void write(uint bytes, u64 addr, u64 value)
{
    std::cerr << "SI::write(" << std::hex << addr << ")" << std::endl;
    if (bytes != 4)
        throw "SI::ReadInvalidWidth";
    switch (addr) {
        case SI_DRAM_ADDR_REG:
            std::cerr << "SI_DRAM_ADDR_REG" << std::endl;
            DramAddr = value;
            break;
        case SI_PIF_ADDR_RD64B_REG:
            std::cerr << "SI_PIF_ADDR_RD64B_REG" << std::endl;
            throw "SI unsupported";
            break;
        case SI_PIF_ADDR_WR64B_REG:
            std::cerr << "SI_PIF_ADDR_WR64B_REG" << std::endl;
            throw "SI unsupported";
            break;
        case SI_STATUS_REG:
            std::cerr << "SI_STATUS_REG" << std::endl;
            Status = 0;
            break;
        default:
            throw "SI Unsupported";
            break;
    }
}

}; /* namespace SI */

/**
 * Physical address space.
 */
Memory::AddressSpace physmem;

/*
 * Description in header.
 */
void init(std::string romFile)
{
    /* 32-bit addressing */
    physmem.root = new Memory::Region(0, 0x100000000llu);
    physmem.root->insertRam(  0x00000000llu, 0x200000);   /* RDRAM range 0 */
    physmem.root->insertRam(  0x00200000llu, 0x200000);   /* RDRAM range 1 */
    physmem.root->insertIOmem(0x03f00000llu, 0x100000, RdRam::read, RdRam::write); /* RDRAM Registers */
    physmem.root->insertRam(  0x04000000llu, 0x1000);     /* SP DMEM */
    physmem.root->insertRam(  0x04001000llu, 0x1000);     /* SP IMEM */
    physmem.root->insertIOmem(0x04040000llu, 0x80000, SP::read, SP::write); /* SP Registers */
    physmem.root->insertIOmem(0x04100000llu, 0x100000, DPCommand::read, DPCommand::write); /* DP Command Registers */
    physmem.root->insertIOmem(0x04200000llu, 0x100000, DPSpan::read, DPSpan::write); /* DP Span Registers */
    physmem.root->insertIOmem(0x04300000llu, 0x100000, MI::read, MI::write); /* Mips Interface */
    physmem.root->insertIOmem(0x04400000llu, 0x100000, VI::read, VI::write); /* Video Interface */
    physmem.root->insertIOmem(0x04500000llu, 0x100000, AI::read, AI::write); /* Audio Interface */
    physmem.root->insertIOmem(0x04600000llu, 0x100000, PI::read, PI::write); /* Peripheral Interface */
    physmem.root->insertIOmem(0x04700000llu, 0x100000, RI::read, RI::write); /* RDRAM Interface */
    physmem.root->insertIOmem(0x04800000llu, 0x100000, SI::read, SI::write); /* Serial Interface */
    physmem.root->insertRam(0x04001000llu, 0x1000);     /* imem */
    physmem.root->insertRom(0x10000000llu, 0xfc00000, romFile);
}

};
