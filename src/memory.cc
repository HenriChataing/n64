
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

u64 Region::load(uint bytes, u64 addr)
{
    if (addr + bytes > size)
        throw "InvalidAddress1";
    for (Region *sub : subregions) {
        if (addr < sub->address)
            break;
        if (addr + bytes > sub->address + sub->size)
            continue;
        return sub->load(bytes, addr - sub->address);
    }
    std::cerr << "InvalidAddress(" << std::hex << addr << ")" << std::endl;
    // throw "InvalidAddress2";
    return 0;
}

void Region::store(uint bytes, u64 addr, u64 value)
{
    if (addr + bytes > size)
        throw "InvalidAddress3";
    for (Region *sub : subregions) {
        if (addr < sub->address)
            break;
        if (addr + bytes > sub->address + sub->size)
            continue;
        sub->store(bytes, addr - sub->address, value);
        return;
    }
    std::cerr << "InvalidAddress(" << std::hex << addr << ")" << std::endl;
    // throw "InvalidAddress4";
}

void Region::adjustEndianness(uint bytes, u64 *value)
{
    if (!bigendian)
        return;
    switch (bytes) {
        case 1:
            break;
        case 2:
            *(u16 *)value = __builtin_bswap16(*value);
            break;
        case 4:
            *(u32 *)value = __builtin_bswap32(*value);
            break;
        case 8:
            *value = __builtin_bswap64(*value);
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
        return sub->lookup(addr - sub->address);
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
    RamRegion(u64 address, u64 size, const std::string file,
              Region *container = NULL);
    virtual ~RamRegion();

    virtual u64 load(uint bytes, u64 addr);
    virtual void store(uint bytes, u64 addr, u64 value);
};

RamRegion::RamRegion(u64 address, u64 size, Region *container)
    : Region(address, size, container)
{
    ram = true;
    block = new u8[size];
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
    delete block;
}

u64 RamRegion::load(uint bytes, u64 addr)
{
    u64 ret = 0;
    switch (bytes) {
        case 1:
            ret = block[addr];
            break;
        case 2:
            ret = *(u16 *)(&block[addr]);
            break;
        case 4:
            ret = *(u32 *)(&block[addr]);
            break;
        case 8:
            ret = *(u64 *)(&block[addr]);
            break;
    }
    adjustEndianness(bytes, &ret);
    return ret;
}

void RamRegion::store(uint bytes, u64 addr, u64 value)
{
    if (readonly)
        throw "ReadOnlyMemory";
    adjustEndianness(bytes, &value);
    switch (bytes) {
        case 1:
            block[addr] = (u8)value;
            break;
        case 2:
            *(u16 *)(&block[addr]) = (u16)value;
            break;
        case 4:
            *(u32 *)(&block[addr]) = (u32)value;
            break;
        case 8:
            *(u64 *)(&block[addr]) = value;
            break;
    }
}

void Region::insertRam(u64 addr, u64 size)
{
    insert(new RamRegion(addr, size, this));
}

void Region::insertRom(u64 addr, u64 size, const std::string file)
{
    insert(new RamRegion(addr, size, file, this));
}

AddressSpace::AddressSpace() : root(NULL)
{
}

AddressSpace::~AddressSpace()
{
    delete root;
}

u64 AddressSpace::load(uint bytes, u64 addr)
{
    if (!root)
        throw "EmptyAddressSpace";
    return root->load(bytes, addr);
}

void AddressSpace::store(uint bytes, u64 addr, u64 value)
{
    if (!root)
        throw "EmptyAddressSpace";
    root->store(bytes, addr, value);
}

void AddressSpace::copy(u64 dst, u64 src, uint bytes)
{
    if (!root)
        throw "EmptyAddressSpace";
    for (uint i = 0; i < bytes; i++)
        root->store(1, dst + i, root->load(1, src + i));
}

};
