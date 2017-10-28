
#include <iostream>
#include <iomanip>

#include "memory.h"

using namespace Memory;

namespace Memory {

/**
 * Representation of a generic physical memory region. The regions can be
 * further differentiated into ram, iomem, rom and rom devices.
 */
class MemoryRegion
{
public:
    MemoryRegion();
    ~MemoryRegion();

    /**
     * @brief Read a value from memory.
     */
    virtual u64 load(MemoryAttr attr, int width, u64 pAddr, u64 vAddr);

    /**
     * @brief Write a value to memory.
     */
    virtual void store(MemoryAttr attr, int width, u64 pAddr, u64 vAddr);

private:
};

static u8 ram[0x3f00000];
static u8 dmem[0x1000];
static u8 imem[0x1000];
static u8 *rom = NULL;
static size_t romSize = 0;

u64 load(MemoryAttr memAttr, uint bytes, u64 pAddr, u64 vAddr)
{
    /* Raise Address Error Exception on un-aligned accesses. */
    if (pAddr % bytes)
        throw "Address error";

    u64 ret = 0;
    if (pAddr + bytes <= 0x3f00000) {
        memcpy((u8*)&ret, &ram[pAddr], bytes);
    }
    else if (pAddr >= 0x04000000 && pAddr + bytes <= 0x04001000) {
        memcpy((u8*)&ret, &dmem[pAddr - 0x04000000], bytes);
    }
    else if (pAddr >= 0x04001000 && pAddr + bytes <= 0x04002000) {
        memcpy((u8*)&ret, &imem[pAddr - 0x04001000], bytes);
    }
    else if (pAddr >= 0x10000000) {
        memcpy((u8*)&ret, &rom[pAddr - 0x10000000], bytes);
    }

    /*
     * ugly fix for reverse endian setting.
     * @todo support endiannes
     */
    if (memAttr & BigEndian) {
        u8 *ptr = (u8*)&ret;
        for (uint i = 0; i < bytes / 2; i++) {
            u8 tmp = ptr[i];
            ptr[i] = ptr[bytes - i - 1];
            ptr[bytes - i - 1] = tmp;
        }
    }
    return ret;
}

void store(MemoryAttr memAttr, uint bytes, u64 pAddr, u64 vAddr, u64 value)
{
    /* Raise Address Error Exception on un-aligned accesses. */
    if (pAddr % bytes)
        throw "Address error";

    /*
     * ugly fix for reverse endian setting.
     * @todo support endiannes
     */
    if (memAttr & BigEndian) {
        u8 *ptr = (u8*)&value;
        for (uint i = 0; i < bytes / 2; i++) {
            u8 tmp = ptr[i];
            ptr[i] = ptr[bytes - i - 1];
            ptr[bytes - i - 1] = tmp;
        }
    }

    if (pAddr + bytes <= 0x3f00000)
        memcpy(&ram[pAddr], (u8 *)&value, bytes);
    else if (pAddr >= 0x04000000 && pAddr + bytes <= 0x04001000) {
        memcpy(&dmem[pAddr - 0x04000000], (u8*)&value, bytes);
    }
    else if (pAddr >= 0x04001000 && pAddr + bytes <= 0x04002000) {
        memcpy(&imem[pAddr - 0x04001000], (u8*)&value, bytes);
    }
        // ram[pAddr] = value;
}

void loadRom(const char *path)
{
    FILE *romFile = fopen(path, "r");
    if (romFile == NULL)
        throw std::invalid_argument("Cannot read from file");

    // obtain file size:
    fseek (romFile , 0 , SEEK_END);
    romSize = ftell (romFile);
    rewind (romFile);

    // allocate memory to contain the whole file:
    rom = (u8 *)malloc (sizeof(u8) * romSize);
    if (rom == NULL) throw "malloc error";

    // copy the file into the buffer:
    int result = fread (rom, 1, romSize, romFile);
    if (result != (int)romSize) throw "fread error";

    // the whole file is now loaded in the memory buffer.
    fclose (romFile);
}

void dma(u64 dest, u64 source, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        store(0, 1, dest + i, 0, load(0, 1, source + i, 0));
    }
}

void dump(u8 *start, size_t size)
{
    std::cout << std::hex << std::setfill('0');
    std::cout << "== " << (uintptr_t)start << std::endl;
    for (uint i = 0; i < size; i++) {
        if (i && !(i % 32))
            std::cout << std::endl;
        std::cout << " " << std::setfill('0') << std::setw(2);
        std::cout << (uint)start[i];
    }
    std::cout << std::endl;
}

};
