
#ifndef __R4300_RSP_INCLUDED__
#define __R4300_RSP_INCLUDED__

#include <cstdint>
#include <memory.h>

namespace R4300 {

typedef union {
    u16 h[8];
    u8  b[16];
} vr_t;

typedef struct {
    u64 acc; /* 48-bits wide. */

    u32 read_u32() { return acc; }
    u64 read_u64() { return acc; }

    inline void write_lo(u16 lo) {
        acc = (acc & ~UINT64_C(0xffff)) | lo;
    }

    inline u16 read_mid_clamp_signed() {
        i32 mid = (i32)(u32)(acc >> 16);
        i16 res = mid < INT16_MIN ? INT16_MIN :
                  mid > INT16_MAX ? INT16_MAX : (i16)mid;
        return (u16)res;
    }
    inline u16 read_mid_clamp_unsigned() {
        i32 mid = (i32)(u32)(acc >> 16);
        i16 res = mid < 0 ? 0 :
                  mid > INT16_MAX ? UINT16_MAX : (i16)mid;
        return (u16)res;
    }
    inline u16 read_lo_clamp_unsigned() {
        i32 mid = (i32)(u32)(acc >> 16);
        i16 res = mid < INT16_MIN ? 0 :
                  mid > INT16_MAX ? UINT16_MAX : (i16)(u16)acc;
        return (u16)res;
    }

} vacc_t;

static_assert(sizeof(vr_t) == 16, "invalid vr_t type representation");

struct rspreg {
    u64 pc;             /**< Program counter */
    u64 gpr[32];        /**< General purpose registers */

    /* SP interface registers are accessed as registers of the Coprocessor 0
     * of the RSP, they are not reproduced here. */

    /* Vector registers. Every 2-byte element of each vector is stored in
     * the host cpu format to minimize byte-swapping. */
    vr_t vr[32];
    /* The accumulator is saved in host cpu format (typically little endian).
     * Technically, the accumulator is only 48-bits wide. */
    vacc_t vacc[8];
    u16 vco;
    u16 vcc;
    u8 vce;

    /** Registers for holding the inputs/results of VRCP, to be fetched
     * by VRCPH, VRCPL in a second time. Because it is not otherwise
     * accessible, the value is stored in host cpu format. */
    u32 divin;
    u32 divout;
    bool divin_loaded;

    inline bool neq(unsigned i)         { return (vco >> (i + 8)) & 1; }
    inline bool carry(unsigned i)       { return (vco >> i) & 1; }
    inline bool compare(unsigned i)     { return (vcc >> i) & 1; }
    inline void set_compare(unsigned i) { vcc |= 1 << i; }
};

namespace RSP {
/** @brief Move the RSP one step, if not halted. */
void step();
}; /* namespace RSP */

};

#endif /* __R4300_RSP_INCLUDED__ */
