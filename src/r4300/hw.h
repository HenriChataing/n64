
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
    u32 DPC_CURRENT_REG;
    u32 DPC_STATUS_REG;
    u32 DPC_CLOCK_REG;
    u32 DPC_BUF_BUSY_REG;
    u32 DPC_PIPE_BUSY_REG;
    u32 DPC_TMEM_REG;

    // DPSPAN

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
    u32 RI_CURRENT_LOAD_REG;
    u32 RI_SELECT_REG;
    u32 RI_REFRESH_REG;
    u32 RI_LATENCY_REG;
    u32 RI_RERROR_REG;
    u32 RI_WERROR_REG;

    u32 SI_DRAM_ADDR_REG;
    u32 SI_PIF_ADDR_RD64B_REG;
    u32 SI_PIF_ADDR_WR64B_REG;
    u32 SI_STATUS_REG;
};

void set_MI_INTR_REG(u32 bits);
void clear_MI_INTR_REG(u32 bits);
void write_SP_RD_LEN_REG(u32 value);
void write_SP_WR_LEN_REG(u32 value);
void write_SP_STATUS_REG(u32 value);
u32 read_SP_SEMAPHORE_REG();
void write_DPC_STATUS_REG(u32 value);

#define SP_MEM_ADDR_IMEM        (UINT32_C(1) << 12)
#define SP_MEM_ADDR_MASK        (UINT32_C(0x1fff))

#define SP_RD_LEN_SKIP_SHIFT    (20)
#define SP_RD_LEN_SKIP_MASK     (UINT32_C(0xfff))
#define SP_RD_LEN_COUNT_SHIFT   (12)
#define SP_RD_LEN_COUNT_MASK    (UINT32_C(0xff))
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

#define PI_STATUS_ERROR         (UINT32_C(1) << 2)
#define PI_STATUS_IO_BUSY       (UINT32_C(1) << 1)
#define PI_STATUS_DMA_BUSY      (UINT32_C(1) << 0)

#define PI_STATUS_CLR_INTR      (UINT32_C(1) << 1)
#define PI_STATUS_RESET         (UINT32_C(1) << 0)

namespace RdRam {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace SP {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace DPCommand {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace DPSpan {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace MI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace VI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace AI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace PI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace RI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace SI {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace Cart_2_1 {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

namespace PIF {
bool read(uint bytes, u64 addr, u64 *value);
bool write(uint bytes, u64 addr, u64 value);
};

};

#endif /* __R4300_HW_INCLUDED__ */
