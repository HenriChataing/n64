
#ifndef _MEMORY_H_INCLUDED_
#define _MEMORY_H_INCLUDED_

#include <cstdlib>
#include <cassert>
#include <iterator>
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
    virtual ~Bus() {}

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

/** Type of bus access. */
enum BusAccess {
    Load,
    Store,
};

/** Bus access log. */
struct BusLog {
    BusAccess access;
    unsigned bytes;
    u64 address;
    u64 value;
    bool result;

    BusLog() :
        access(Memory::Load), bytes(0), address(0), value(0), result(0) {}
    BusLog(BusAccess access, unsigned bytes, u64 address, u64 value, bool result) :
        access(access), bytes(bytes), address(address), value(value), result(result) {}
};

/** Derive the Bus class to log all load and store memory accesses. */
class LoggingBus: public Memory::Bus
{
public:
    LoggingBus(unsigned bits) : Bus(bits), _capture(false) {}
    virtual ~LoggingBus() {}

    std::vector<BusLog> log;

    void capture(bool enabled) { this->_capture = enabled; }
    void copy(std::vector<BusLog> &log) {
        std::copy(this->log.begin(), this->log.end(),
            std::back_inserter(log));
    }
    void clear() { log.clear(); }

    virtual bool load(unsigned bytes, u64 addr, u64 *val) {
        bool res = root.load(bytes, addr, val);
        if (_capture) log.push_back(BusLog(Load, bytes, addr, res ? *val : 0, res));
        return res;
    }
    virtual bool store(unsigned bytes, u64 addr, u64 val) {
        bool res = root.store(bytes, addr, val);
        if (_capture) log.push_back(BusLog(Store, bytes, addr, val, res));
        return res;
    }

private:
    bool _capture;
};

}; /* namespace Memory */

#endif /* _MEMORY_H_INCLUDED_ */
