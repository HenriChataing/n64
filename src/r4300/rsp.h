
#ifndef __R4300_RSP_INCLUDED__
#define __R4300_RSP_INCLUDED__

#include <cstdint>
#include <memory.h>

namespace R4300 {

typedef union {
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
    /* The accumulator is saved in host cpu format (typically little endian).
     * Technically, the accumulator is only 48-bits wide. */
    u64 vacc[8];
    u16 vcc;
    u16 vco;
    u8 vce;

    /** Registers for holding the inputs/results of VRCP, to be fetched
     * by VRCPH, VRCPL in a second time. Because it is not otherwise
     * accessible, the value is stored in host cpu format. */
    u32 divin;
    u32 divout;
};

namespace RSP {
/** @brief Move the RSP one step, if not halted. */
bool step();
}; /* namespace RSP */

};

#endif /* __R4300_RSP_INCLUDED__ */
