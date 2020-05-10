
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

    virtual bool load(uint bytes, u64 addr, u64 *value);
    virtual bool store(uint bytes, u64 addr, u64 value);
    // virtual bool accept(uint bytes, u64 addr);

    void print();
    void insert(Region *region);
    void insertRam(u64 addr, u64 size, u8 *mem);
    void insertRom(u64 addr, u64 size, u8 *mem);
    void insertIOmem(u64 addr, u64 size,
                     bool (*read)(uint bytes, u64 addr, u64 *value),
                     bool (*write)(uint bytes, u64 addr, u64 value));

protected:
    void adjustEndianness(uint bytes, u64 *value);
    Region *lookup(u64 addr);
};

class AddressSpace
{
public:
    AddressSpace(u64 address, u64 size);
    ~AddressSpace();

    Region root;

    bool load(uint bytes, u64 addr, u64 *value);
    bool store(uint bytes, u64 addr, u64 value);
    bool copy(u64 dst, u64 src, uint bytes);
};

};

#endif /* _MEMORY_H_INCLUDED_ */
