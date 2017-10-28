
#include <r4300/cpu.h>
#include <memory.h>

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
            throw "32b now supported (KX)";
        if (vAddr >= U64_2GB && vAddr < U64_2_5GB)
            return vAddr - U64_2GB; // Unmapped access, cached.
        if (vAddr >= U64_2_5GB && vAddr < U64_3GB)
            return vAddr - U64_2_5GB; // Unmapped access, non cached.
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
    for (uint i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];
        // Check VPN against vAddr
        if ((entry.pageMask & vAddr) == entry.pageMask) {
            // Check global bit an AISD
            if (entry.global ||
                (entry.asid == (R4300::state.cp0reg.entryHi & 0xffllu)))
                break;;
        }
    }

    // No matching TLB entry, send for TLB refill.
    if (i >= R4300::tlbEntryCount)
        throw is32Bit ? "TLB refill" : "XTLB refill";

    R4300::tlbEntry &entry = R4300::state.tlb[i];

    // Check valid bit
    if (!entry.valid)
        throw "TLB invalid";

    // Check if trying to write dirty address.
    if (writeAccess && entry.dirty)
        throw "TLD mod";

    // Compute physical address + cache attributes.
    u64 maskShift = entry.pageMask >> 2;
    u64 offsetMask = maskShift ^ (maskShift - 1llu);
    u64 parityMask = offsetMask + 1llu;
    u64 pAddr = vAddr & offsetMask;
    if (vAddr & parityMask)
        pAddr |= (entry.entryLo1 << 6) & 0xffffff000llu;
    else
        pAddr |= (entry.entryLo0 << 6) & 0xffffff000llu;
    return pAddr;
}

};
