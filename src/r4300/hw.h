
#ifndef __R4300_HW_INCLUDED__
#define __R4300_HW_INCLUDED__

#include <cstdint>
#include <memory.h>

namespace R4300 {

struct hwreg {
    u32 RDRAM_DEVICE_TYPE_REG;
    u32 RDRAM_DEVICE_ID_REG;
    u32 RDRAM_DELAY_REG;
    u32 RDRAM_MODE_REG;
    u32 RDRAM_REF_INTERVAL_REG;
    u32 RDRAM_REF_ROW_REG;
    u32 RDRAM_RAS_INTERVAL_REG;
    u32 RDRAM_MIN_INTERVAL_REG;
    u32 RDRAM_ADDR_SELECT_REG;
    u32 RDRAM_DEVICE_MANUF_REG;

    u32 SP_MEM_ADDR_REG;
    u32 SP_DRAM_ADDR_REG;
    u32 SP_RD_LEN_REG;
    u32 SP_WR_LEN_REG;
    u32 SP_STATUS_REG;
    u32 SP_SEMAPHORE_REG;
    u32 SP_IBIST_REG;

    u32 DPC_START_REG;
    u32 DPC_END_REG;
    u32 DPC_STATUS_REG;
    u32 DPC_CLOCK_REG;
    u32 DPC_BUF_BUSY_REG;
    u32 DPC_PIPE_BUSY_REG;
    u32 DPC_TMEM_REG;

    u32 dpc_Start;
    u32 dpc_End;
    u32 dpc_Current;

    /* Command buffer to cache command words as they are read
     * from DRAM or DMEM. The longest command is shade_texture_zbuff_triangle,
     * while 22 total double words. */
    u64  dpc_CommandBuffer[22];
    uint dpc_CommandBufferIndex;
    uint dpc_CommandBufferLen;

    u32 DPS_TBIST_REG;
    u32 DPS_TEST_MODE_REG;
    u32 DPS_BUFTEST_ADDR_REG;
    u32 DPS_BUFTEST_DATA_REG;

    u32 MI_MODE_REG;
    u32 MI_VERSION_REG;
    u32 MI_INTR_REG;
    u32 MI_INTR_MASK_REG;

    ulong vi_NextIntr;
    ulong vi_IntrInterval;
    ulong vi_LastCycleCount;
    ulong vi_CyclesPerLine;

    u32 VI_CONTROL_REG; // VI_STATUS_REG
    u32 VI_DRAM_ADDR_REG; // VI_ORIGIN_REG
    u32 VI_WIDTH_REG; // VI_H_WIDTH_REG
    u32 VI_INTR_REG; // VI_V_INTR_REG
    u32 VI_CURRENT_REG; // VI_V_CURRENT_LINE_REG
    u32 VI_BURST_REG; // VI_TIMING_REG
    u32 VI_V_SYNC_REG;
    u32 VI_H_SYNC_REG;
    u32 VI_LEAP_REG; // VI_H_SYNC_LEAP_REG
    u32 VI_H_START_REG; // VI_H_VIDEO_REG
    u32 VI_V_START_REG; // VI_V_VIDEO_REG
    u32 VI_V_BURST_REG;
    u32 VI_X_SCALE_REG;
    u32 VI_Y_SCALE_REG;

    u32 AI_DRAM_ADDR_REG;
    u32 AI_LEN_REG;
    u32 AI_CONTROL_REG;
    u32 AI_STATUS_REG;
    u32 AI_DACRATE_REG;
    u32 AI_BITRATE_REG;

    u32 PI_DRAM_ADDR_REG;
    u32 PI_CART_ADDR_REG;
    u32 PI_RD_LEN_REG;
    u32 PI_WR_LEN_REG;
    u32 PI_STATUS_REG;
    u32 PI_BSD_DOM1_LAT_REG;
    u32 PI_BSD_DOM1_PWD_REG;
    u32 PI_BSD_DOM1_PGS_REG;
    u32 PI_BSD_DOM1_RLS_REG;
    u32 PI_BSD_DOM2_LAT_REG;
    u32 PI_BSD_DOM2_PWD_REG;
    u32 PI_BSD_DOM2_PGS_REG;
    u32 PI_BSD_DOM2_RLS_REG;

    u32 RI_MODE_REG;
    u32 RI_CONFIG_REG;
    u32 RI_SELECT_REG;
    u32 RI_REFRESH_REG;
    u32 RI_LATENCY_REG;
    u32 RI_RERROR_REG;

    u32 SI_DRAM_ADDR_REG;
    u32 SI_STATUS_REG;

    struct {
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
    } buttons;
};

/* RdRam */
bool read_RDRAM_REG(uint bytes, u64 addr, u64 *value);
bool write_RDRAM_REG(uint bytes, u64 addr, u64 value);

/* SP */
void write_SP_RD_LEN_REG(u32 value);
void write_SP_WR_LEN_REG(u32 value);
void write_SP_STATUS_REG(u32 value);
u32 read_SP_SEMAPHORE_REG();
bool read_SP_REG(uint bytes, u64 addr, u64 *value);
bool write_SP_REG(uint bytes, u64 addr, u64 value);

/* DPCommand */
void write_DPC_STATUS_REG(u32 value);
void write_DPC_START_REG(u32 value);
void write_DPC_END_REG(u32 value);
bool read_DPC_REG(uint bytes, u64 addr, u64 *value);
bool write_DPC_REG(uint bytes, u64 addr, u64 value);

/* DPSpan */
bool read_DPS_REG(uint bytes, u64 addr, u64 *value);
bool write_DPS_REG(uint bytes, u64 addr, u64 value);

/* MI */
void set_MI_INTR_REG(u32 bits);
void clear_MI_INTR_REG(u32 bits);
bool read_MI_REG(uint bytes, u64 addr, u64 *value);
bool write_MI_REG(uint bytes, u64 addr, u64 value);

/* AI */
bool read_AI_REG(uint bytes, u64 addr, u64 *value);
bool write_AI_REG(uint bytes, u64 addr, u64 value);

/* PI */
bool read_PI_REG(uint bytes, u64 addr, u64 *value);
bool write_PI_REG(uint bytes, u64 addr, u64 value);

/* VI */
void raise_VI_INTR(void);
bool read_VI_REG(uint bytes, u64 addr, u64 *value);
bool write_VI_REG(uint bytes, u64 addr, u64 value);

/* RI */
bool read_RI_REG(uint bytes, u64 addr, u64 *value);
bool write_RI_REG(uint bytes, u64 addr, u64 value);

/* SI */
bool read_SI_REG(uint bytes, u64 addr, u64 *value);
bool write_SI_REG(uint bytes, u64 addr, u64 value);

/* Cartridge */
bool read_CART_1_1(uint bytes, u64 addr, u64 *value);
bool write_CART_1_1(uint bytes, u64 addr, u64 value);
bool read_CART_1_3(uint bytes, u64 addr, u64 *value);
bool write_CART_1_3(uint bytes, u64 addr, u64 value);
bool read_CART_2_1(uint bytes, u64 addr, u64 *value);
bool write_CART_2_1(uint bytes, u64 addr, u64 value);
bool read_CART_2_2(uint bytes, u64 addr, u64 *value);
bool write_CART_2_2(uint bytes, u64 addr, u64 value);

/* PIF */
bool read_PIF_RAM(uint bytes, u64 addr, u64 *value);
bool write_PIF_RAM(uint bytes, u64 addr, u64 value);


#define RDRAM_DEVICE_TYPE_18M   (UINT32_C(0xb2190020))
#define RDRAM_DEVICE_TYPE_64M   (UINT32_C(0xb02a0020))

#define SP_MEM_ADDR_IMEM        (UINT32_C(1) << 12)
#define SP_MEM_ADDR_MASK        (UINT32_C(0xfff))

#define SP_DRAM_ADDR_MASK       (UINT32_C(0xfffffff))

#define SP_RD_LEN_SKIP_SHIFT    (20)
#define SP_RD_LEN_SKIP_MASK     (UINT32_C(0xfff))
#define SP_RD_LEN_COUNT_SHIFT   (12)
#define SP_RD_LEN_COUNT_MASK    (UINT32_C(0xff))
#define SP_RD_LEN_LEN_SHIFT     (0)
#define SP_RD_LEN_LEN_MASK      (UINT32_C(0xfff))

#define SP_STATUS_SIGNAL7       (UINT32_C(1) << 14)
#define SP_STATUS_SIGNAL6       (UINT32_C(1) << 13)
#define SP_STATUS_SIGNAL5       (UINT32_C(1) << 12)
#define SP_STATUS_SIGNAL4       (UINT32_C(1) << 11)
#define SP_STATUS_SIGNAL3       (UINT32_C(1) << 10)
#define SP_STATUS_SIGNAL2       (UINT32_C(1) << 9)
#define SP_STATUS_SIGNAL1       (UINT32_C(1) << 8)
#define SP_STATUS_SIGNAL0       (UINT32_C(1) << 7)
#define SP_STATUS_INTR_BREAK    (UINT32_C(1) << 6) /* interrupt on break */
#define SP_STATUS_SSTEP         (UINT32_C(1) << 5) /* single step */
#define SP_STATUS_IO_FULL       (UINT32_C(1) << 4)
#define SP_STATUS_DMA_FULL      (UINT32_C(1) << 3)
#define SP_STATUS_DMA_BUSY      (UINT32_C(1) << 2)
#define SP_STATUS_BROKE         (UINT32_C(1) << 1)
#define SP_STATUS_HALT          (UINT32_C(1) << 0)

#define SP_STATUS_SET_SIGNAL7   (UINT32_C(1) << 24)
#define SP_STATUS_CLR_SIGNAL7   (UINT32_C(1) << 23)
#define SP_STATUS_SET_SIGNAL6   (UINT32_C(1) << 22)
#define SP_STATUS_CLR_SIGNAL6   (UINT32_C(1) << 21)
#define SP_STATUS_SET_SIGNAL5   (UINT32_C(1) << 20)
#define SP_STATUS_CLR_SIGNAL5   (UINT32_C(1) << 19)
#define SP_STATUS_SET_SIGNAL4   (UINT32_C(1) << 18)
#define SP_STATUS_CLR_SIGNAL4   (UINT32_C(1) << 17)
#define SP_STATUS_SET_SIGNAL3   (UINT32_C(1) << 16)
#define SP_STATUS_CLR_SIGNAL3   (UINT32_C(1) << 15)
#define SP_STATUS_SET_SIGNAL2   (UINT32_C(1) << 14)
#define SP_STATUS_CLR_SIGNAL2   (UINT32_C(1) << 13)
#define SP_STATUS_SET_SIGNAL1   (UINT32_C(1) << 12)
#define SP_STATUS_CLR_SIGNAL1   (UINT32_C(1) << 11)
#define SP_STATUS_SET_SIGNAL0   (UINT32_C(1) << 10)
#define SP_STATUS_CLR_SIGNAL0   (UINT32_C(1) << 9)
#define SP_STATUS_SET_INTR_BREAK    (UINT32_C(1) << 8)
#define SP_STATUS_CLR_INTR_BREAK    (UINT32_C(1) << 7)
#define SP_STATUS_SET_SSTEP     (UINT32_C(1) << 6)
#define SP_STATUS_CLR_SSTEP     (UINT32_C(1) << 5)
#define SP_STATUS_SET_INTR      (UINT32_C(1) << 4)
#define SP_STATUS_CLR_INTR      (UINT32_C(1) << 3)
#define SP_STATUS_CLR_BROKE     (UINT32_C(1) << 2)
#define SP_STATUS_SET_HALT      (UINT32_C(1) << 1)
#define SP_STATUS_CLR_HALT      (UINT32_C(1) << 0)

#define DPC_STATUS_CLR_CLOCK_CTR        (UINT32_C(1) << 9)
#define DPC_STATUS_CLR_CMD_CTR          (UINT32_C(1) << 8)
#define DPC_STATUS_CLR_PIPE_CTR         (UINT32_C(1) << 7)
#define DPC_STATUS_CLR_TMEM_CTR         (UINT32_C(1) << 6)
#define DPC_STATUS_SET_FLUSH    (UINT32_C(1) << 5)
#define DPC_STATUS_CLR_FLUSH    (UINT32_C(1) << 4)
#define DPC_STATUS_SET_FREEZE   (UINT32_C(1) << 3)
#define DPC_STATUS_CLR_FREEZE   (UINT32_C(1) << 2)
#define DPC_STATUS_SET_XBUS_DMEM_DMA    (UINT32_C(1) << 1)
#define DPC_STATUS_CLR_XBUS_DMEM_DMA    (UINT32_C(1) << 0)

#define DPC_STATUS_START_VALID  (UINT32_C(1) << 10)
#define DPC_STATUS_END_VALID    (UINT32_C(1) << 9)
#define DPC_STATUS_DMA_BUSY     (UINT32_C(1) << 8)
#define DPC_STATUS_CBUF_READY   (UINT32_C(1) << 7)
#define DPC_STATUS_CMD_BUSY     (UINT32_C(1) << 6)
#define DPC_STATUS_PIPE_BUSY    (UINT32_C(1) << 5)
#define DPC_STATUS_TMEM_BUSY    (UINT32_C(1) << 4)
#define DPC_STATUS_START_GCLK   (UINT32_C(1) << 3)
#define DPC_STATUS_FLUSH        (UINT32_C(1) << 2)
#define DPC_STATUS_FREEZE       (UINT32_C(1) << 1)
#define DPC_STATUS_XBUS_DMEM_DMA        (UINT32_C(1) << 0)

#define MI_MODE_RDRAM_REG       (UINT32_C(1) << 9)
#define MI_MODE_EBUS_TEST       (UINT32_C(1) << 8)
#define MI_MODE_INIT            (UINT32_C(1) << 7)

#define MI_MODE_SET_RDRAM_REG   (UINT32_C(1) << 13)
#define MI_MODE_CLR_RDRAM_REG   (UINT32_C(1) << 12)
#define MI_MODE_CLR_DP_INTR     (UINT32_C(1) << 11)
#define MI_MODE_SET_EBUS_TEST   (UINT32_C(1) << 10)
#define MI_MODE_CLR_EBUS_TEST   (UINT32_C(1) << 9)
#define MI_MODE_SET_INIT        (UINT32_C(1) << 8)
#define MI_MODE_CLR_INIT        (UINT32_C(1) << 7)
#define MI_MODE_INIT_LEN_MASK   (UINT32_C(0x7f))

#define MI_INTR_DP              (UINT32_C(1) << 5)
#define MI_INTR_PI              (UINT32_C(1) << 4)
#define MI_INTR_VI              (UINT32_C(1) << 3)
#define MI_INTR_AI              (UINT32_C(1) << 2)
#define MI_INTR_SI              (UINT32_C(1) << 1)
#define MI_INTR_SP              (UINT32_C(1) << 0)

#define MI_INTR_MASK_DP         (UINT32_C(1) << 5)
#define MI_INTR_MASK_PI         (UINT32_C(1) << 4)
#define MI_INTR_MASK_VI         (UINT32_C(1) << 3)
#define MI_INTR_MASK_AI         (UINT32_C(1) << 2)
#define MI_INTR_MASK_SI         (UINT32_C(1) << 1)
#define MI_INTR_MASK_SP         (UINT32_C(1) << 0)

#define MI_INTR_MASK_SET_DP     (UINT32_C(1) << 11)
#define MI_INTR_MASK_CLR_DP     (UINT32_C(1) << 10)
#define MI_INTR_MASK_SET_PI     (UINT32_C(1) << 9)
#define MI_INTR_MASK_CLR_PI     (UINT32_C(1) << 8)
#define MI_INTR_MASK_SET_VI     (UINT32_C(1) << 7)
#define MI_INTR_MASK_CLR_VI     (UINT32_C(1) << 6)
#define MI_INTR_MASK_SET_AI     (UINT32_C(1) << 5)
#define MI_INTR_MASK_CLR_AI     (UINT32_C(1) << 4)
#define MI_INTR_MASK_SET_SI     (UINT32_C(1) << 3)
#define MI_INTR_MASK_CLR_SI     (UINT32_C(1) << 2)
#define MI_INTR_MASK_SET_SP     (UINT32_C(1) << 1)
#define MI_INTR_MASK_CLR_SP     (UINT32_C(1) << 0)

#define VI_CONTROL_SERRATE      (UINT32_C(1) << 6)
#define VI_CONTROL_COLOR_DEPTH_SHIFT    0
#define VI_CONTROL_COLOR_DEPTH_MASK     UINT32_C(0x3)
#define VI_CONTROL_COLOR_DEPTH_BLANK    UINT32_C(0)
#define VI_CONTROL_COLOR_DEPTH_16BIT    UINT32_C(2)
#define VI_CONTROL_COLOR_DEPTH_32BIT    UINT32_C(3)

#define VI_DRAM_ADDR_MASK       (UINT32_C(0xffffff))

#define AI_DRAM_ADDR_MASK       (UINT32_C(0xffffff))

#define AI_LEN_V1_MASK          (UINT32_C(0x7fff))
#define AI_LEN_V2_MASK          (UINT32_C(0x3ffff))

#define AI_CONTROL_DMA_EN       (UINT32_C(1) << 0)

#define AI_STATUS_AI_FULL       (UINT32_C(1) << 31)
#define AI_STATUS_AI_BUSY       (UINT32_C(1) << 30)

#define AI_DACRATE_MASK         (UINT32_C(0x3fff))

#define AI_BITRATE_MASK         (UINT32_C(0xf))

#define PI_DRAM_ADDR_MASK       (UINT32_C(0xffffff))

#define PI_STATUS_ERROR         (UINT32_C(1) << 2)
#define PI_STATUS_IO_BUSY       (UINT32_C(1) << 1)
#define PI_STATUS_DMA_BUSY      (UINT32_C(1) << 0)

#define PI_STATUS_CLR_INTR      (UINT32_C(1) << 1)
#define PI_STATUS_RESET         (UINT32_C(1) << 0)

#define SI_DRAM_ADDR_MASK       (UINT32_C(0xffffff))

#define SI_STATUS_INTR          (UINT32_C(1) << 12)
#define SI_STATUS_DMA_ERROR     (UINT32_C(1) << 3)
#define SI_STATUS_IO_BUSY       (UINT32_C(1) << 2)
#define SI_STATUS_DMA_BUSY      (UINT32_C(1) << 0)

};

#endif /* __R4300_HW_INCLUDED__ */
