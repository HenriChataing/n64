
#include <r4300/cpu.h>
#include <r4300/state.h>

#include <debugger.h>
#include <memory.h>

#define CKSEG3  UINT64_C(0xffffffffe0000000)
#define CKSSEG  UINT64_C(0xffffffffc0000000)
#define CKSEG1  UINT64_C(0xffffffffa0000000)
#define CKSEG0  UINT64_C(0xffffffff80000000)
#define XKSEG   UINT64_C(0xc000000000000000)
#define XKPHYS  UINT64_C(0x8000000000000000)
#define XKSSEG  UINT64_C(0x4000000000000000)
#define XKUSEG  UINT64_C(0x0000000000000000)
#define USEG    UINT64_C(0x0000000080000000)
#define XUSEG   UINT64_C(0x0000010000000000)

namespace R4300 {

R4300::Exception translateAddress(u64 vAddr, u64 *pAddr, bool writeAccess)
{
    bool extendedAddressing = false;
    u8 ksu = R4300::state.cp0reg.KSU();
    // u64 region;

    // Step 1: check virtual address range.
    if (ksu == 0x0 || R4300::state.cp0reg.ERL() ||
        R4300::state.cp0reg.EXL()) {
        // Kernel mode
        // also entered when ERL=1 || EXL=1
        if (R4300::state.cp0reg.KX()) {
            extendedAddressing = true;
            throw "ExtendedAddressingUnsupported";
        }
        if (vAddr >= CKSEG0 && vAddr < CKSEG1) {
            *pAddr = vAddr - CKSEG0; // Unmapped access, cached.
            return R4300::None;
        }
        if (vAddr >= CKSEG1 && vAddr < CKSSEG) {
            *pAddr = vAddr - CKSEG1; // Unmapped access, non cached.
            return R4300::None;
        }
    }
    else if (ksu == 0x1) {
        // Supervisor mode
        throw "Supervisor mode not supported";
    }
    else if (ksu == 0x2) {
        // User mode
        if (R4300::state.cp0reg.UX()) {
            extendedAddressing = true;
            throw "ExtendedAddressingUnsupported";
        }
        // Check valid address. The user address space is 2GiB when UX=0,
        // 1To when UX=1.
        if ((vAddr & UINT64_C(0xffffffff)) >= USEG)
            return R4300::AddressError;
    }
    else {
        throw "Undetermined execution mode";
    }

    // Step 2: lookup matching TLB entry
    uint i = 0;
    for (i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];
        // Check VPN against vAddr
        u64 pageMask = ~entry.pageMask & 0xffffffe000llu;
        if ((vAddr & pageMask) == (entry.entryHi & pageMask)) {
            // Check global bit and ASID
            if (entry.global ||
                (entry.asid == (R4300::state.cp0reg.entryhi & 0xffllu)))
                break;
        }
    }

    // No matching TLB entry, send for TLB refill.
    if (i >= R4300::tlbEntryCount)  {
        return extendedAddressing ? R4300::XTLBRefill : R4300::TLBRefill;
    }

    R4300::tlbEntry &entry = R4300::state.tlb[i];

    // Compute physical address + cache attributes.
    u64 pageMask = entry.pageMask | 0x0000001fffllu;
    u64 offsetMask = pageMask >> 1;
    u64 parityMask = offsetMask + 1llu;
    u64 offset = vAddr & offsetMask;

    u64 entrylo = (vAddr & parityMask) ? entry.entryLo1 : entry.entryLo0;

    // Check valid bit
    if ((entrylo & 2) == 0) {
        return R4300::TLBInvalid;
    }
    // Check if trying to write dirty address.
    if (writeAccess && (entrylo & 4) == 0) {
        return R4300::TLBModified;
    }

    *pAddr = offset | ((entrylo << 6) & 0xffffff000llu);
    return R4300::None;
}

bool probeTLB(u64 vAddr, uint *index)
{
    // Lookup matching TLB entry
    for (uint i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];
        // Check VPN against vAddr
        u64 pageMask = ~entry.pageMask & 0xfffffe000llu;
        if ((vAddr & pageMask) == (entry.entryHi & pageMask)) {
            // Check global bit and ASID
            if (entry.global ||
                (entry.asid == (R4300::state.cp0reg.entryhi & 0xffllu))) {
                *index = i;
                return true;
            }
        }
    }

    // No matching TLB entry.
    return false;
}

};
