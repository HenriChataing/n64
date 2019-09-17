
#ifndef __R4300_RSP_INCLUDED__
#define __R4300_RSP_INCLUDED__

#include <cstdint>
#include <memory.h>

namespace R4300 {

typedef union {
    u64 d[2];
    u32 w[4];
    u16 h[8];
    u8  b[16];
} vr_t;

static_assert(sizeof(vr_t) == 16, "invalid vr_t type representation");

struct rspreg {
    u64 pc;             /**< Program counter */
    u64 gpr[32];        /**< General purpose registers */

    /* SP interface registers are accessed as registers of the Coprocessor 0
     * of the RSP, they are not reproduced here. */

    vr_t vr[32];
};

namespace RSP {
/** @brief Interpret the RSP instruction at the specified address. */
bool eval(u64 addr, bool delaySlot);

/** @brief Move the RSP one step, if not halted. */
bool step();
}; /* namespace RSP */

};

#endif /* __R4300_RSP_INCLUDED__ */
