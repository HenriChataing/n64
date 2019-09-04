
#ifndef _R4300_STATE_H_INCLUDED_
#define _R4300_STATE_H_INCLUDED_

#include <iostream>

#include <r4300/cpu.h>
#include <r4300/hw.h>

namespace R4300 {

class State
{
public:
    State();
    ~State();

    void init(std::string romFile);
    void boot();

    struct cpureg reg;          /**< CPU registers */
    struct cp0reg cp0reg;       /**< Co-processor 0 registers */
    struct cp1reg cp1reg;       /**< Co-processor 0 registers */
    struct tlbEntry tlb[tlbEntryCount];    /**< Translation look-aside buffer */

    Memory::AddressSpace physmem;
    struct hwreg hwreg;

    ulong cycles;
    bool branch;
};

/** Current machine state. */
extern State state;

static inline bool CU3() { return (state.cp0reg.sr & STATUS_CU3) != 0; }
static inline bool CU2() { return (state.cp0reg.sr & STATUS_CU2) != 0; }
static inline bool CU1() { return (state.cp0reg.sr & STATUS_CU1) != 0; }
static inline bool CU0() { return (state.cp0reg.sr & STATUS_CU0) != 0; }
static inline bool RP()  { return (state.cp0reg.sr & (1lu << 27)) != 0; }
static inline bool FR()  { return (state.cp0reg.sr & (1lu << 26)) != 0; }
static inline bool RE()  { return (state.cp0reg.sr & (1lu << 25)) != 0; }
static inline bool BEV() { return (state.cp0reg.sr & STATUS_BEV) != 0; }
static inline u32 DS()   { return (state.cp0reg.sr >> 16) & 0x1flu; }
static inline u32 IM()   { return (state.cp0reg.sr >> 8) & 0xfflu; }
static inline bool KX()  { return (state.cp0reg.sr & (1lu << 7)) != 0; }
static inline bool SX()  { return (state.cp0reg.sr & (1lu << 6)) != 0; }
static inline bool UX()  { return (state.cp0reg.sr & (1lu << 5)) != 0; }
static inline u32 KSU()  { return (state.cp0reg.sr >> 3) & 0x3lu; }
static inline bool ERL() { return (state.cp0reg.sr & STATUS_ERL) != 0; }
static inline bool EXL() { return (state.cp0reg.sr & STATUS_EXL) != 0; }
static inline bool IE()  { return (state.cp0reg.sr & STATUS_IE) != 0; }

static inline u32 IP()   { return (state.cp0reg.cause >> 8) & 0xfflu; }

}; /* namespace R4300 */

std::ostream &operator<<(std::ostream &os, const R4300::State &state);

#endif /* _R4300_STATE_H_INCLUDED_ */