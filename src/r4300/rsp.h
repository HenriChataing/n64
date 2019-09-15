
#ifndef __R4300_RSP_INCLUDED__
#define __R4300_RSP_INCLUDED__

#include <cstdint>
#include <memory.h>

namespace R4300 {

struct rspreg {
    u64 pc;             /**< Program counter */
    u64 gpr[32];        /**< General purpose registers */

    /* SP interface registers are accessed as egisters of the Coprocessor 0
     * of the RSP, they are not reproduced here. */

    // TODO VU registers
};

namespace RSP {
/** @brief Interpret the RSP instruction at the specified address. */
bool eval(u64 addr, bool delaySlot);

/** @brief Move the RSP one step, if not halted. */
bool step();
}; /* namespace RSP */

};

#endif /* __R4300_RSP_INCLUDED__ */
