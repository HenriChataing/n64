
#ifndef _MEMORY_H_INCLUDED_
#define _MEMORY_H_INCLUDED_

#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>

#include "types.h"

namespace Memory
{

class Region
{
public:
    Region(u64 address, u64 size, Region *container = NULL);
    virtual ~Region();

    bool bigendian;
    bool ram;
    bool readonly;
    bool device;

    u64 address;
    u64 size;
    u8 *block;          /**< Optional ram block. */

    std::vector<Region *>subregions;
    Region *container;

    virtual u64 load(uint bytes, u64 addr);
    virtual void store(uint bytes, u64 addr, u64 value);
    // virtual bool accept(uint bytes, u64 addr);

    void print();
    void insert(Region *region);
    void insertRam(u64 addr, u64 size);
    void insertRom(u64 addr, u64 size, const std::string file);
    void insertIOmem(u64 addr, u64 size,
                     u64 (*read)(uint bytes, u64 addr),
                     void (*write)(uint bytes, u64 addr, u64 value));

protected:
    void adjustEndianness(uint bytes, u64 *value);
    Region *lookup(u64 addr);
};

class AddressSpace
{
public:
    AddressSpace();
    ~AddressSpace();

    Region *root;

    u64 load(uint bytes, u64 addr);
    void store(uint bytes, u64 addr, u64 value);
    void copy(u64 dst, u64 src, uint bytes);

protected:
    Region *lookup(u64 addr);
};

/**
 * @brief Translate a virtual memory address into a physical memory address
 *  relying on the current TLB entries.
 *
 * @param vAddr         Virtual memory address
 * @param writeAccess   Indicate if the address is written or read
 * @return the physical memory address \p vAddr is mapped to
 */
u64 translateAddress(u64 vAddr, bool writeAccess);

};

#endif /* _MEMORY_H_INCLUDED_ */
