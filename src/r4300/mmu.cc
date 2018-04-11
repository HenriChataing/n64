
#include <r4300/cpu.h>
#include <memory.h>

#define CKSEG3  UINT64_C(0xffffffffe0000000)
#define CKSSEG  UINT64_C(0xffffffffc0000000)
#define CKSEG1  UINT64_C(0xffffffffa0000000)
#define CKSEG0  UINT64_C(0xffffffff80000000)
#define XKSEG   UINT64_C(0xc000000000000000)
#define XKPHYS  UINT64_C(0x8000000000000000)
#define XKSSEG  UINT64_C(0x4000000000000000)
#define XKUSEG  UINT64_C(0x0000000000000000)

namespace Memory {

u64 translateAddress(u64 vAddr, bool writeAccess)
{
    // Select 32 vs 64 bit address.
    bool is32Bit = true;
    u8 ksu = R4300::KSU();

    // Step 1: check virtual address range.
    if (ksu == 0x0 || R4300::ERL() || R4300::EXL()) {
        // Kernel mode
        // also entered when ERL=1 || EXL=1
        if (R4300::KX())
            throw "KX not supported";
        if (vAddr >= CKSEG0 && vAddr < CKSEG1)
            return vAddr - CKSEG0; // Unmapped access, cached.
        if (vAddr >= CKSEG1 && vAddr < CKSSEG)
            return vAddr - CKSEG1; // Unmapped access, non cached.
    }
    else if (ksu == 0x1) {
        // Supervisor mode
        throw "Supervisor mode not supported";
    }
    else if (ksu == 0x2) {
        // User mode
        // Check valid address. The user address space is 2GiB when UX=0,
        // 1To when UX=1.
        u64 limit = R4300::UX() ? U64_2GB : U64_1TB;
        if (vAddr >= limit) {
            throw "Address error";
        }
    }
    else {
        throw "Undetermined execution mode";
    }

    // Step 2: lookup matching TLB entry
    uint i = 0;
    for (i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];
        // Check VPN against vAddr
        u64 pageMask = ~entry.pageMask & 0xfffffe000llu;
        if ((vAddr & pageMask) == (entry.entryHi & pageMask)) {
            // Check global bit and ASID
            if (entry.global ||
                (entry.asid == (R4300::state.cp0reg.entryHi & 0xffllu)))
                break;
        }
    }

    // No matching TLB entry, send for TLB refill.
    if (i >= R4300::tlbEntryCount)
        throw is32Bit ? "TLB refill" : "XTLB refill";

    R4300::tlbEntry &entry = R4300::state.tlb[i];

    // Compute physical address + cache attributes.
    u64 maskShift = entry.pageMask >> 2;
    u64 offsetMask = maskShift ^ (maskShift - 1llu);
    u64 parityMask = offsetMask + 1llu;
    u64 pAddr = vAddr & offsetMask;

    if (vAddr & parityMask) {
        // Check valid bit
        if ((entry.entryLo1 & 2) == 0)
            throw "TLB invalid";
        // Check if trying to write dirty address.
        if (writeAccess && (entry.entryLo1 & 4) == 0)
            throw "TLD mod";

        pAddr |= (entry.entryLo1 << 6) & 0xffffff000llu;
    } else {
        // Check valid bit
        if ((entry.entryLo0 & 2) == 0)
            throw "TLB invalid";
        // Check if trying to write dirty address.
        if (writeAccess && (entry.entryLo0 & 4) == 0)
            throw "TLD mod";

        pAddr |= (entry.entryLo0 << 6) & 0xffffff000llu;
    }
    return pAddr;
}

};
