
#ifndef _CPU_H_INCLUDED_
#define _CPU_H_INCLUDED_

#include <iostream>

#include "types.h"

namespace R4300 {

const static uint tlbEntryCount = 47;

struct reg {
    u64 pc;             /**< Program counter */
    u64 gpr[32];        /**< General purpose registers */
    u64 multHi, multLo; /**< Multiply / Divide registers */
    bool llbit;
};

struct cp0reg {
    u32 index;          /**< Programmable pointer into TLB array */
    u32 random;         /**< Random pointer into TLB array */
    u64 entryLo0;       /**< Low half of the TLB entry for even VPN */
    u64 entryLo1;       /**< Low half of the TLB entry for odd VPN */
    u64 context;        /**< Pointer to kernel PTE table */
    u32 pageMask;       /**< TLB page mask */
    u32 wired;          /**< Number of wire TLB entries */
    u32 cpr7;           /**< Unused */
    u64 badVAddr;       /**< Bad virtual address */
    u32 count;          /**< Timer count */
    u64 entryHi;        /**< High half of TLB entry */
    u32 compare;        /**< Timer compare */
    u32 sr;             /**< Status register */
    u32 cause;          /**< Cause register */
    u64 epc;            /**< Exception program counter */
    u32 prId;           /**< Processor revision identifier */
    u32 config;         /**< Configuration register */
    u32 llAddr;         /**< Load linked address */
    u32 watchLo;        /**< Memory reference trap address lower bits */
    u32 watchHi;        /**< Memory reference trap address upper bits */
    u64 xContext;       /**< Context register for MIPS-III addressing */
    u32 cpr21;          /**< Unused */
    u32 cpr22;          /**< Unused */
    u32 cpr23;          /**< Unused */
    u32 cpr24;          /**< Unused */
    u32 cpr25;          /**< Unused */
    u32 pErr;           /**< Not Used */
    u32 cacheErr;       /**< Not Used */
    u32 tagLo;          /**< Cache tag register */
    u32 tagHi;          /**< Cache tag register (reserved) */
    u32 errorEpc;       /**< Error exception program counter */
    u32 cpr31;          /**< Unused */
};

struct cp1reg {
    u64 fgr[32];        /**< FP general purpose registers */
    u32 fcr0, fcr31;    /**< Implementation and control registers */
};

struct tlbEntry {
    u64 pageMask;       /**< TLB page mask */
    u64 entryHi;        /**< High half of TLB entry */
    u64 entryLo0;       /**< Low half of the TLB entry for even VPN */
    u64 entryLo1;       /**< Low half of the TLB entry for odd VPN */

    u8 asid;            /**< Extracted ASID */
    bool global;        /**< Extracted global bit */
};

enum Exception {
    None = 0,
    AddressError = 1,
    TLBRefill = 2,
    XTLBRefill = 3,
    TLBInvalid = 4,
    TLBModified = 5,
    CacheError = 6,
    VirtualCoherency = 7,
    BusError = 8,
    IntegerOverflow = 9,
    Trap = 10,
    SystemCall = 11,
    Breakpoint = 12,
    ReservedInstruction = 13,
    CoprocessorUnusable = 14,
    FloatingPoint = 15,
    Watch = 16,
    Interrupt = 17,
};

class State
{
public:
    State();
    ~State();
    void boot();

    struct reg reg;             /**< CPU registers */
    struct cp0reg cp0reg;       /**< Co-processor 0 registers */
    struct cp1reg cp1reg;       /**< Co-processor 0 registers */
    struct tlbEntry tlb[tlbEntryCount];    /**< Translation look-aside buffer */
    ulong cycles;
};

class Coprocessor
{
public:
    Coprocessor() {}
    ~Coprocessor() {}

    virtual void cofun(u32 instr) {}
    virtual u64 read(uint bytes, u32 rd, bool ctrl) { return 0; }
    virtual void write(uint bytes, u32 rd, u64 imm, bool ctrl) {}

    virtual void BCF(u32 rd, u64 imm) {}
    virtual void BCT(u32 rd, u64 imm) {}
    virtual void BCFL(u32 rd, u64 imm) {}
    virtual void BCTL(u32 rd, u64 imm) {}
};

class Register
{
public:
    virtual u64 read(uint bytes) const {
        throw "UndefinedRegister";
        return 0;
    }
    virtual void write(uint bytes, u64 val) const {
        throw "UndefinedRegister";
    }
};

extern State state;
extern Coprocessor *cop[4];

static inline bool CU3() { return (state.cp0reg.sr & (1lu << 31)) != 0; }
static inline bool CU2() { return (state.cp0reg.sr & (1lu << 30)) != 0; }
static inline bool CU1() { return (state.cp0reg.sr & (1lu << 29)) != 0; }
static inline bool CU0() { return (state.cp0reg.sr & (1lu << 28)) != 0; }
static inline bool RP()  { return (state.cp0reg.sr & (1lu << 27)) != 0; }
static inline bool FR()  { return (state.cp0reg.sr & (1lu << 26)) != 0; }
static inline bool RE()  { return (state.cp0reg.sr & (1lu << 25)) != 0; }
static inline u32 DS()   { return (state.cp0reg.sr >> 16) & 0x1flu; }
static inline bool KX()  { return (state.cp0reg.sr & (1lu << 7)) != 0; }
static inline bool SX()  { return (state.cp0reg.sr & (1lu << 6)) != 0; }
static inline bool UX()  { return (state.cp0reg.sr & (1lu << 5)) != 0; }
static inline u32 KSU()  { return (state.cp0reg.sr >> 3) & 0x3lu; }
static inline bool ERL() { return (state.cp0reg.sr & (1lu << 2)) != 0; }
static inline bool EXL() { return (state.cp0reg.sr & (1lu << 1)) != 0; }
static inline bool IE()  { return (state.cp0reg.sr & (1lu << 0)) != 0; }

};

std::ostream &operator<<(std::ostream &os, const R4300::State &state);

#endif /* _CPU_H_INCLUDED_ */
