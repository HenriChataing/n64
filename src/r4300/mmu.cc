
#include <r4300/cpu.h>
#include <r4300/state.h>

#include <debugger.h>
#include <memory.h>

#define CKSEG3      UINT64_C(0xffffffffe0000000)
#define CKSSEG      UINT64_C(0xffffffffc0000000)
#define CKSEG1      UINT64_C(0xffffffffa0000000)
#define CKSEG0      UINT64_C(0xffffffff80000000)
/* Address error */
#define XKSEG_END   UINT64_C(0xc00000ff80000000)
#define XKSEG       UINT64_C(0xc000000000000000)
#define XKPHYS      UINT64_C(0x8000000000000000)
/* Address error */
#define XKSSEG_END  UINT64_C(0x4000010000000000)
#define XKSSEG      UINT64_C(0x4000000000000000)
/* Address error */
#define XKUSEG_END  UINT64_C(0x0000010000000000)
#define XKUSEG      UINT64_C(0x0000000000000000)

#define USEG        UINT64_C(0x0000000080000000)
#define XUSEG       UINT64_C(0x0000010000000000)

namespace R4300 {

R4300::Exception translate_address(uint64_t virt_addr, uint64_t *phys_addr,
   bool write_access, uint64_t *virt_start, uint64_t *virt_end)
{
    bool extended_addressing = false;
    u8 ksu = R4300::state.cp0reg.KSU();
    bool erl = R4300::state.cp0reg.ERL();
    bool exl = R4300::state.cp0reg.EXL();

    // Step 1:
    // Match the virtual address against unmapped or invalid memory regions.
    // These regions depend on the current execution mode.
    if (ksu == 0x0 || erl || exl) {
        // Kernel mode. Exceptions (ERL=1 or EXL=1) are forced to
        // kernel mode.
        extended_addressing = R4300::state.cp0reg.KX();

        if (virt_addr >= CKSEG0 && virt_addr < CKSEG1) {
            // Unmapped access, cached.
            *phys_addr = virt_addr - CKSEG0;
            if (virt_start != NULL) *virt_start = CKSEG0;
            if (virt_end != NULL)   *virt_end   = CKSEG1 - 1;
            return R4300::None;
        }
        if (virt_addr >= CKSEG1 && virt_addr < CKSSEG) {
            // Unmapped access, non cached.
            *phys_addr = virt_addr - CKSEG1;
            if (virt_start != NULL) *virt_start = CKSEG1;
            if (virt_end != NULL)   *virt_end   = CKSSEG - 1;
            return R4300::None;
        }
        if (extended_addressing) {
            if (virt_addr >= XKSEG_END && virt_addr < CKSEG0) {
                // Invalid region.
                return R4300::AddressError;
            }
            if (virt_addr >= XKPHYS && virt_addr < XKSEG) {
                // Unmapped access.
                *phys_addr = virt_addr - XKPHYS;
                if (virt_start != NULL) *virt_start = XKPHYS;
                if (virt_end != NULL)   *virt_end   = XKSEG - 1;
                return R4300::None;
            }
            if (virt_addr >= XKSSEG_END && virt_addr < XKPHYS) {
                // Invalid region.
                return R4300::AddressError;
            }
            if (virt_addr >= XKUSEG_END && virt_addr < XKSSEG) {
                // Invalid region.
                return R4300::AddressError;
            }
        }
    }
    else if (ksu == 0x1) {
        // Supervisor mode.
        throw "Supervisor mode not supported";
    }
    else if (ksu == 0x2) {
        // User mode.
        extended_addressing = R4300::state.cp0reg.UX();

        // Check valid address.
        // The user address space is 2GiB when UX=0, 1To when UX=1.
        if ((virt_addr & UINT64_C(0xffffffff)) >= USEG) {
            // Invalid region.
            return R4300::AddressError;
        }
        if (extended_addressing) {
            throw "ExtendedAddressingUnsupported";
        }
    }
    else {
        throw "Undetermined execution mode";
    }

    // Step 2:
    // Fallthrough to mapped memory region.
    // Look for the first matching TLB entry.
    unsigned index = 0;
    unsigned asid = R4300::state.cp0reg.entryhi & UINT64_C(0xff);

    for (index = 0; index < R4300::tlbEntryCount; index++) {
        R4300::tlbEntry &entry = R4300::state.tlb[index];
        u64 page_mask = ~entry.pageMask & UINT64_C(0xffffffe000);
        // Check VPN against virt_addr, then check if the global bit
        // is set or the ASID matches.
        if ((virt_addr & page_mask) == (entry.entryHi & page_mask) &&
            (entry.global || entry.asid == asid)) {
            break;
        }
    }

    // No matching TLB entry, send for TLB refill.
    if (index >= R4300::tlbEntryCount)  {
        return extended_addressing ? R4300::XTLBRefill : R4300::TLBRefill;
    }

    R4300::tlbEntry &entry = R4300::state.tlb[index];

    // Compute physical address + cache attributes.
    u64 page_mask = entry.pageMask | UINT64_C(0x0000001fff);
    u64 offset_mask = page_mask >> 1;
    u64 parity_mask = offset_mask + 1llu;
    u64 offset = virt_addr & offset_mask;

    u64 entrylo = (virt_addr & parity_mask) ? entry.entryLo1 : entry.entryLo0;

    // Check valid bit
    if ((entrylo & 2) == 0) {
        return R4300::TLBInvalid;
    }
    // Check if trying to write dirty address.
    if (write_access && (entrylo & 4) == 0) {
        return R4300::TLBModified;
    }

    *phys_addr = offset | ((entrylo << 6) & UINT64_C(0xffffff000));
    if (virt_start != NULL) *virt_start = entry.entryHi & ~page_mask;
    if (virt_end != NULL)   *virt_end   = (entry.entryHi & ~page_mask) | page_mask;
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
