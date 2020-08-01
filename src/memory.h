
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

    virtual bool load (uint bytes, u64 addr, u64 *value);
    virtual bool store(uint bytes, u64 addr, u64  value);

    void print();
    void insert(Region *region);
    void insertRam(u64 addr, u64 size, u8 *mem);
    void insertRom(u64 addr, u64 size, u8 *mem);
    void insertIOmem(u64 addr, u64 size,
                     bool (*read)(uint bytes, u64 addr, u64 *value),
                     bool (*write)(uint bytes, u64 addr, u64 value));

protected:
    void adjustEndianness(uint bytes, u64 *value);
};

class Bus
{
public:
    Bus(unsigned bits) : root(0, 1llu << bits) {}
    ~Bus() {}

    Region root;

    virtual bool load(unsigned bytes, u64 addr, u64 *val) {
        return root.load(bytes, addr, val);
    }
    virtual bool store(unsigned bytes, u64 addr, u64 val) {
        return root.store(bytes, addr, val);
    }

    inline bool load_u8(u64 addr, u8 *val) {
        u64 val64; bool res = load(1, addr, &val64);
        *val = val64; return res;
    }
    inline bool load_u16(u64 addr, u16 *val) {
        u64 val64; bool res = load(2, addr, &val64);
        *val = val64; return res;
    }
    inline bool load_u32(u64 addr, u32 *val) {
        u64 val64; bool res = load(4, addr, &val64);
        *val = val64; return res;
    }
    inline bool load_u64(u64 addr, u64 *val) {
        return load(8, addr, val);
    }

    inline bool store_u8(u64 addr, u8 val) {
        return store(1, addr, val);
    }
    inline bool store_u16(u64 addr, u16 val) {
        return store(2, addr, val);
    }
    inline bool store_u32(u64 addr, u32 val) {
        return store(4, addr, val);
    }
    inline bool store_u64(u64 addr, u64 val) {
        return store(8, addr, val);
    }
};

}; /* namespace Memory */

#endif /* _MEMORY_H_INCLUDED_ */
