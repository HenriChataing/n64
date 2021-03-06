
#ifndef _CPU_H_INCLUDED_
#define _CPU_H_INCLUDED_

#include <iostream>

#include "types.h"

namespace R4300 {

/**
 * The TLB entry count is specified as 48 in the MIPS R4000 reference,
 * downsized to 32 by MIPS R4300.
 */
const static uint tlbEntryCount = 32;

struct cpureg {
    u64 pc;             /**< Program counter */
    u64 gpr[32];        /**< General purpose registers */
    u64 multHi, multLo; /**< Multiply / Divide registers */
    bool llbit;
};

struct cp0reg {
    u32 index;          /**< Programmable pointer into TLB array */
    u32 random;         /**< Random pointer into TLB array */
    u64 entrylo0;       /**< Low half of the TLB entry for even VPN */
    u64 entrylo1;       /**< Low half of the TLB entry for odd VPN */
    u64 context;        /**< Pointer to kernel PTE table */
    u32 pagemask;       /**< TLB page mask */
    u32 wired;          /**< Number of wire TLB entries */
    u32 c7;             /**< Unused */
    u64 badvaddr;       /**< Bad virtual address */
    u32 count;          /**< Timer count */
    u64 entryhi;        /**< High half of TLB entry */
    u32 compare;        /**< Timer compare */
    u32 sr;             /**< Status register */
    u32 cause;          /**< Cause register */
    u64 epc;            /**< Exception program counter */
    u32 prid;           /**< Processor revision identifier */
    u32 config;         /**< Configuration register */
    u32 lladdr;         /**< Load linked address */
    u32 watchlo;        /**< Memory reference trap address lower bits */
    u32 watchhi;        /**< Memory reference trap address upper bits */
    u64 xcontext;       /**< Context register for MIPS-III addressing */
    u32 c21;            /**< Unused */
    u32 c22;            /**< Unused */
    u32 c23;            /**< Unused */
    u32 c24;            /**< Unused */
    u32 c25;            /**< Unused */
    u32 perr;           /**< Not Used */
    u32 cacheerr;       /**< Not Used */
    u32 taglo;          /**< Cache tag register */
    u32 taghi;          /**< Cache tag register (reserved) */
    u64 errorepc;       /**< Error exception program counter */
    u32 c31;            /**< Unused */

    /** Cycle count at the last Count register update. */
    ulong lastCounterUpdate;

#define INDEX_P                 (UINT32_C(0x80000000))

#define CONTEXT_PTEBASE_SHIFT   23
#define CONTEXT_PTEBASE_MASK    (UINT64_C(0x1ffffffffff))
#define CONTEXT_BADVPN2_SHIFT   4
#define CONTEXT_BADVPN2_MASK    (UINT64_C(0x7ffff))

#define STATUS_CU3              (UINT32_C(1) << 31)
#define STATUS_CU2              (UINT32_C(1) << 30)
#define STATUS_CU1              (UINT32_C(1) << 29)
#define STATUS_CU0              (UINT32_C(1) << 28)
#define STATUS_RP               (UINT32_C(1) << 27)
#define STATUS_FR               (UINT32_C(1) << 26)
#define STATUS_RE               (UINT32_C(1) << 25)
#define STATUS_BEV              (UINT32_C(1) << 22)
#define STATUS_KX               (UINT32_C(1) << 7)
#define STATUS_SX               (UINT32_C(1) << 6)
#define STATUS_UX               (UINT32_C(1) << 5)
#define STATUS_ERL              (UINT32_C(1) << 2)
#define STATUS_EXL              (UINT32_C(1) << 1)
#define STATUS_IE               (UINT32_C(1) << 0)

    inline bool CU3() { return (sr & STATUS_CU3) != 0; }
    inline bool CU2() { return (sr & STATUS_CU2) != 0; }
    inline bool CU1() { return (sr & STATUS_CU1) != 0; }
    inline bool CU0() { return (sr & STATUS_CU0) != 0; }
    inline bool RP()  { return (sr & STATUS_RP) != 0; }
    inline bool FR()  { return (sr & STATUS_FR) != 0; }
    inline bool RE()  { return (sr & STATUS_RE) != 0; }
    inline bool BEV() { return (sr & STATUS_BEV) != 0; }
    inline u32 DS()   { return (sr >> 16) & 0x1flu; }
    inline u32 IM()   { return (sr >> 8) & 0xfflu; }
    inline bool KX()  { return (sr & STATUS_KX) != 0; }
    inline bool SX()  { return (sr & STATUS_SX) != 0; }
    inline bool UX()  { return (sr & STATUS_UX) != 0; }
    inline u32 KSU()  { return (sr >> 3) & 0x3lu; }
    inline bool ERL() { return (sr & STATUS_ERL) != 0; }
    inline bool EXL() { return (sr & STATUS_EXL) != 0; }
    inline bool IE()  { return (sr & STATUS_IE) != 0; }

#define CAUSE_BD                (UINT32_C(1) << 31)
#define CAUSE_CE_MASK           (UINT32_C(0x3) << 28)
#define CAUSE_CE(ce)            ((uint32_t)(ce) << 28)
#define CAUSE_IP_MASK           (UINT32_C(0xff) << 8)
#define CAUSE_IP(ip)            ((uint32_t)(ip) << 8)
#define CAUSE_IP7               (UINT32_C(1) << 15)
#define CAUSE_EXCCODE_MASK      (UINT32_C(0x1f) << 2)
#define CAUSE_EXCCODE(exccode)  ((uint32_t)(exccode) << 2)

    inline u32 IP()   { return (cause >> 8) & 0xfflu; }
};

typedef union {
    float s;
    u32 w;
} fpr_s_t;

typedef union {
    double d;
    u64 l;
} fpr_d_t;

static_assert(sizeof(fpr_s_t) == sizeof(u32), "float is larger than u32");
static_assert(sizeof(fpr_d_t) == sizeof(u64), "double is larger than u64");

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

#define FCR31_C                 (UINT32_C(1) << 23)

struct tlbEntry {
    u64 entryHi;        /**< High half of TLB entry */
    u64 entryLo0;       /**< Low half of the TLB entry for even VPN */
    u64 entryLo1;       /**< Low half of the TLB entry for odd VPN */
    u32 pageMask;       /**< TLB page mask */

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

/**
 * @brief Translate a virtual memory address to a physical memory address.
 *  The boundaries of the virtual memory region \p virt_addr belong to
 *  are returned in \p virt_start, \p virt_end.
 *  If the function returns Exception::None, any address `v` inside this region
 *  can safely be translated as `phys_addr + (v - virt_addr)`.
 *
 * @param virt_addr     Virtual memory address.
 * @param phys_addr
 *      Pointer to a buffer where to write the physical address
 *      \p virt_addr is mapped to.
 * @param write_access
 *      Indicate if the address is being translated for a read (false)
 *      or write (true) access.
 * @param virt_start
 *      Optional pointer to a buffer where to write the start address
 *      of the valid virtual range.
 * @param virt_end
 *      Optional pointer to a buffer where to write the end address
 *      of the valid virtual range (inclusive).
 * @return
 *      - Exception::None on success
 *      - Exception::AddressError if the virtual address falls inside an
 *          invalid region
 *      - Exception::TLBRefill, Exception::XTLBRefill if the virtual address
 *          falls inside a mapped region, but no valid TLB entry matches
 *      - Exception::TLBInvalid, Exception::TLBModified if the virtual address
 *          falls inside a mapped ragion, but the first matching TLB entry is
 *          invalid or modified (with write_access true).
 */
Exception translate_address(uint64_t virt_addr, uint64_t *phys_addr,
    bool write_access, uint64_t *virt_start = NULL, uint64_t *virt_end = NULL);

/**
 * @brief Raise an exception and update the state of the processor.
 *
 * @param vAddr         Virtual address being accessed.
 * @param delaySlot     Whether the exception occured in a branch delay
 *                      instruction.
 * @param instr         Whether the exception was triggered by an instruction
 *                      fetch.
 * @param load          Whether the exception was triggered by a load or store
 *                      operation.
 */
void takeException(R4300::Exception exn, u64 vAddr,
                   bool instr, bool load, u32 ce = 0);

/**
 * @brief Lookup a matching TLB entry.
 *
 * @param vAddr         Virtual memory address
 * @param index         Pointer to a buffer where to write the index of the
 *                      found entry.
 * @return true iff a matching entry was found
 */
bool probeTLB(u64 vAddr, uint *index);

void setInterruptPending(uint irq);
void clearInterruptPending(uint irq);
void checkInterrupt(void);

void scheduleCounterEvent(void);
void handleCounterEvent(void);

void step(void);

};

#endif /* _CPU_H_INCLUDED_ */
