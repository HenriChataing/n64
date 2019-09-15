
#include <cstring>
#include <iostream>
#include <iomanip>

#include "memory.h"

using namespace Memory;

namespace Memory {

Region::Region(u64 address, u64 size, Region *container)
    : bigendian(false), ram(false), readonly(false), device(false),
      address(address), size(size), block(NULL), subregions(),
      container(container)
{
#ifdef TARGET_BIGENDIAN
    bigendian = true;
#endif
}

Region::~Region()
{
    for (Region *sub : subregions)
        delete sub;
}

void Region::print()
{
    using namespace std;
    cerr << hex;
    cerr << "[" << setfill('0') << setw(8) << address;
    cerr << "-" << setfill('0') << setw(8) << address + size;
    cerr << "[ ";
    if (readonly) cerr << "RO"; else cerr << "RW";
    cerr << endl;
    for (Region *sub : subregions)
        sub->print();
}

bool Region::load(uint bytes, u64 addr, u64 *value)
{
    if (addr + bytes > size)
        return false;

    for (Region *sub : subregions) {
        if (addr < sub->address)
            break;
        if (addr + bytes > sub->address + sub->size)
            continue;
        return sub->load(bytes, addr, value);
    }

    std::cerr << "InvalidAddress(" << std::hex << addr << ")" << std::endl;
    return false;
}

bool Region::store(uint bytes, u64 addr, u64 value)
{
    if (addr + bytes > size)
        return false;

    for (Region *sub : subregions) {
        if (addr < sub->address)
            break;
        if (addr + bytes > sub->address + sub->size)
            continue;
        return sub->store(bytes, addr, value);
    }

    std::cerr << "InvalidAddress(" << std::hex << addr << ")" << std::endl;
    return false;
}

void Region::adjustEndianness(uint bytes, u64 *value)
{
    if (!bigendian)
        return;

    switch (bytes) {
        case 2:
            *value = __builtin_bswap16(*value);
            break;
        case 4:
            *value = __builtin_bswap32(*value);
            break;
        case 8:
            *value = __builtin_bswap64(*value);
            break;
        default:
            break;
    }
}

Region *Region::lookup(u64 addr)
{
    if (addr >= size)
        return NULL;
    for (Region *sub : subregions) {
        if (addr < sub->address)
            continue;
        if (addr >= sub->address + sub->size)
            break;
        return sub->lookup(addr);
    }
    return this;
}

void Region::insert(Region *region)
{
    std::vector<Region *>::iterator it = subregions.begin();
    for (; it != subregions.end(); it++) {
        Region *sub = *it;
        if (region->address < sub->address)
            break;
    }
    subregions.insert(it, region);
}

class RamRegion : public Region
{
public:
    RamRegion(u64 address, u64 size, Region *container = NULL);
    RamRegion(u64 address, u64 size, u8 *mem, Region *container = NULL);
    RamRegion(u64 address, u64 size, const std::string file,
              Region *container = NULL);
    virtual ~RamRegion();

    virtual bool load(uint bytes, u64 addr, u64 *value);
    virtual bool store(uint bytes, u64 addr, u64 value);

private:
    bool _allocated;
};

RamRegion::RamRegion(u64 address, u64 size, Region *container)
    : Region(address, size, container), _allocated(true)
{
    ram = true;
    block = new u8[size];
    memset(block, 0, size);
}

RamRegion::RamRegion(u64 address, u64 size, u8 *mem, Region *container)
    : Region(address, size, container)
{
    ram = true;
    if (mem == NULL) {
        block = new u8[size];
        _allocated = true;
    } else {
        block = mem;
        _allocated = false;
    }
    memset(block, 0, size);
}

RamRegion::RamRegion(u64 address, u64 size, const std::string file,
                     Region *container)
    : Region(address, size, container)
{
    readonly = true;
    block = new u8[size];

    FILE *romFile = fopen(file.c_str(), "r");
    if (romFile == NULL)
        throw "CannotReadFile";

    /* Obtain file size */
    fseek(romFile, 0, SEEK_END);
    size_t romSize = ftell(romFile);
    rewind(romFile);

    if (romSize > size)
        throw "LargeFile";

    /* Copy the file into the buffer. */
    int result = fread(block, 1, romSize, romFile);
    if (result != (int)romSize)
        throw "fread error";

    /* The whole file is now loaded in the memory buffer. */
    fclose(romFile);
}

RamRegion::~RamRegion()
{
    if (_allocated)
        delete block;
}

bool RamRegion::load(uint bytes, u64 addr, u64 *value)
{
    u64 ret = 0;
    u64 offset = addr - this->address;
    switch (bytes) {
        case 1:
            ret = block[offset];
            break;
        case 2:
            ret = *(u16 *)(&block[offset]);
            break;
        case 4:
            ret = *(u32 *)(&block[offset]);
            break;
        case 8:
            ret = *(u64 *)(&block[offset]);
            break;
    }
    adjustEndianness(bytes, &ret);
    *value = ret;
    return true;
}

bool RamRegion::store(uint bytes, u64 addr, u64 value)
{
    if (readonly)
        return false;

    u64 offset = addr - this->address;
    adjustEndianness(bytes, &value);
    switch (bytes) {
        case 1:
            block[offset] = (u8)value;
            break;
        case 2:
            *(u16 *)(&block[offset]) = (u16)value;
            break;
        case 4:
            *(u32 *)(&block[offset]) = (u32)value;
            break;
        case 8:
            *(u64 *)(&block[offset]) = value;
            break;
    }
    return true;
}

class IOmemRegion : public Region
{
public:
    typedef bool (*Reader)(uint bytes, u64 addr, u64 *value);
    typedef bool (*Writer)(uint bytes, u64 addr, u64 value);

    IOmemRegion(u64 address, u64 size,
                Reader read, Writer write,
                Region *container = NULL);
    virtual ~IOmemRegion() {}

    virtual bool load(uint bytes, u64 addr, u64 *value) {
        return read(bytes, addr, value);
    }
    virtual bool store(uint bytes, u64 addr, u64 value) {
        return write(bytes, addr, value);
    }

private:
    Reader read;
    Writer write;
};

IOmemRegion::IOmemRegion(u64 address, u64 size,
                         Reader read, Writer write,
                         Region *container)
    : Region(address, size, container), read(read), write(write)
{
    bigendian = false;
    ram = false;
    readonly = false;
    device = true;
}

void Region::insertRam(u64 addr, u64 size, u8 *mem)
{
    insert(new RamRegion(addr, size, mem, this));
}

void Region::insertRom(u64 addr, u64 size, const std::string file)
{
    insert(new RamRegion(addr, size, file, this));
}

void Region::insertIOmem(u64 addr, u64 size,
                         IOmemRegion::Reader read,
                         IOmemRegion::Writer write)
{
    insert(new IOmemRegion(addr, size, read, write, this));
}

AddressSpace::AddressSpace() : root(NULL)
{
}

AddressSpace::~AddressSpace()
{
    delete root;
}

bool AddressSpace::load(uint bytes, u64 addr, u64 *value)
{
    if (!root)
        throw "EmptyAddressSpace";
    return root->load(bytes, addr, value);
}

bool AddressSpace::store(uint bytes, u64 addr, u64 value)
{
    if (!root)
        throw "EmptyAddressSpace";
    return root->store(bytes, addr, value);
}

bool AddressSpace::copy(u64 dst, u64 src, uint bytes)
{
    if (!root)
        throw "EmptyAddressSpace";

    for (uint i = 0; i < bytes; i++) {
        u64 val;
        if (!root->load(1, src + i, &val))
            return false;
        if (!root->store(1, dst + i, val))
            return false;
    }
    return true;
}

};
