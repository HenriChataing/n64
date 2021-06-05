
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
    vr_t hi;
    vr_t md;
    vr_t lo;

    uint64_t read(int i) const {
        return ((uint64_t)hi.h[i] << 32) |
               ((uint64_t)md.h[i] << 16) |
               ((uint64_t)lo.h[i] <<  0);
    }
    void write(int i, uint64_t val) {
        hi.h[i] = (uint16_t)(val >> 32);
        md.h[i] = (uint16_t)(val >> 16);
        lo.h[i] = (uint16_t)(val >>  0);
    }

    uint16_t read_md_clamp_signed(int i) const {
        uint32_t acc_u = (uint32_t)hi.h[i] << 16 | (uint32_t)md.h[i];
        int32_t acc = (int32_t)acc_u;
        return acc < INT16_MIN ? INT16_MIN :
               acc > INT16_MAX ? INT16_MAX : md.h[i];
    }
    uint16_t read_md_clamp_unsigned(int i) const {
        uint32_t acc_u = (uint32_t)hi.h[i] << 16 | (uint32_t)md.h[i];
        int32_t acc = (int32_t)acc_u;
        return acc < 0 ? 0 :
               acc > INT16_MAX ? UINT16_MAX : md.h[i];
    }
    uint16_t read_lo_clamp_unsigned(int i) const {
        uint32_t acc_u = (uint32_t)hi.h[i] << 16 | (uint32_t)md.h[i];
        int32_t acc = (int32_t)acc_u;
        return acc < INT16_MIN ? 0 :
               acc > INT16_MAX ? UINT16_MAX : lo.h[i];
    }

    void add(int i, uint64_t val) {
        write(i, read(i) + val);
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
    /* The accumulator is split into three vectors of high, middle, and low
     * words. This data representation is easier to manimulate with vector
     * instructions. */
    vacc_t vacc;

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
