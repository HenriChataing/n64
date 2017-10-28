
#ifndef _MEMORY_H_INCLUDED_
#define _MEMORY_H_INCLUDED_

#include <cstdlib>
#include <cassert>
#include <cstring>

#include "types.h"

namespace Memory
{

typedef int MemoryAttr;

static const MemoryAttr BigEndian = 1;

/**
 * @brief Translate a virtual memory address into a physical memory address
 *  relying on the current TLB entries.
 *
 * @param vAddr         Virtual memory address
 * @param writeAccess   Indicate if the address is written or read
 * @return the physical memory address \p vAddr is mapped to
 */
u64 translateAddress(u64 vAddr, bool writeAccess);

/**
 *
 */
u64 load(MemoryAttr memAttr, uint bytes, u64 pAddr, u64 vAddr);

/**
 *
 */
void store(MemoryAttr memAttr, uint bytes, u64 pAddr, u64 vAddr, u64 value);


void loadRom(const char *path);
void dma(u64 dest, u64 source, size_t size);
void dump(u8 *start, size_t size);

};

#endif /* _MEMORY_H_INCLUDED_ */
