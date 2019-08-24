
#ifndef __R4300_HW_INCLUDED__
#define __R4300_HW_INCLUDED__

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
    u32 SP_DMA_FULL_REG;
    u32 SP_DMA_BUSY_REG;
    u32 SP_SEMAPHORE_REG;
    u32 SP_PC_REG;
    u32 SP_IBIST_REG;

    // DPCOMMAND
    // DPSPAN

    u32 MI_MODE_REG;
    u32 MI_VERSION_REG;
    u32 MI_INTR_REG;
    u32 MI_INTR_MASK_REG;

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

#define MI_INTR_SP (1U << 0)
#define MI_INTR_SI (1U << 1)
#define MI_INTR_AI (1U << 2)
#define MI_INTR_VI (1U << 3)
#define MI_INTR_PI (1U << 4)
#define MI_INTR_DP (1U << 5)

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
