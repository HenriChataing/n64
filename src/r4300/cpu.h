
#ifndef _CPU_H_INCLUDED_
#define _CPU_H_INCLUDED_

#include <iostream>

#include "types.h"

namespace R4300 {

const static uint tlbEntryCount = 47;

struct cpureg {
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

    void incrCount();
};

#define STATUS_CU3              (UINT32_C(1) << 31)
#define STATUS_CU2              (UINT32_C(1) << 30)
#define STATUS_CU1              (UINT32_C(1) << 29)
#define STATUS_CU0              (UINT32_C(1) << 28)
#define STATUS_FR               (UINT32_C(1) << 26)
#define STATUS_BEV              (UINT32_C(1) << 22)
#define STATUS_ERL              (UINT32_C(1) << 2)
#define STATUS_EXL              (UINT32_C(1) << 1)
#define STATUS_IE               (UINT32_C(1) << 0)

#define CAUSE_BD                (UINT32_C(1) << 31)
#define CAUSE_CE_MASK           (UINT32_C(0x3) << 28)
#define CAUSE_CE(ce)            ((uint32_t)(ce) << 28)
#define CAUSE_IP_MASK           (UINT32_C(0xff) << 8)
#define CAUSE_IP(ip)            ((uint32_t)(ip) << 8)
#define CAUSE_IP7               (UINT32_C(1) << 15)
#define CAUSE_EXCCODE_MASK      (UINT32_C(0x1f) << 2)
#define CAUSE_EXCCODE(exccode)  ((uint32_t)(exccode) << 2)

typedef union {
    float s;
    u32 w;
} fpr_s_t;

typedef union {
    double d;
    u64 l;
} fpr_d_t;

static_assert(sizeof(fpr_s_t) == sizeof(u32), "float is not single precision");
static_assert(sizeof(fpr_d_t) == sizeof(u64), "double is not double precision");

struct cp1reg {
    u64 fpr[32];        /**< FP general purpose registers */
    fpr_s_t *fpr_s[32]; /**< Aliases for word access to FP general purpose registers. */
    fpr_d_t *fpr_d[32]; /**< Aliases for double word access to FP general purpose registers. */
    u32 fcr0;           /**< Implementation register */
    u32 fcr31;          /**< Control register */

    /**
     * @brief Configure the memory aliases for single and double word access
     *  to the floating point registers.
     */
    void setFprAliases(bool fr);
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

extern Coprocessor *cop[4];

/**
 * @brief Translate a virtual memory address into a physical memory address
 *  relying on the current TLB entries.
 *
 * @param vAddr         Virtual memory address
 * @param pAddr         Pointer to the physical memory address \p vAddr
 *                      is mapped to
 * @param writeAccess   Indicate if the address is written or read
 * @return translation status (0 on success)
 */
Exception translateAddress(u64 vAddr, u64 *pAddr, bool writeAccess);

/**
 * @brief Lookup a matching TLB entry.
 *
 * @param vAddr         Virtual memory address
 * @param index         Pointer to a buffer where to write the index of the
 *                      found entry.
 * @return true iff a matching entry was found
 */
bool probeTLB(u64 vAddr, uint *index);

};

#endif /* _CPU_H_INCLUDED_ */
