
#include <r4300/hw.h>

namespace R4300 {

/**
 * Physical address space.
 */
Memory::AddressSpace physmem;

/*
 * Description in header.
 */
void init(std::string romFile)
{
    /* 32-bit addressing */
    physmem.root = new Memory::Region(0, 0x100000000llu);
    physmem.root->insertRam(0x00000000llu, 0x200000);   /* RDRAM range 0 */
    physmem.root->insertRam(0x00200000llu, 0x200000);   /* RDRAM range 1 */
    physmem.root->insertRam(0x04000000llu, 0x1000);     /* dmem */
    physmem.root->insertRam(0x04001000llu, 0x1000);     /* imem */
    physmem.root->insertRom(0x10000000llu, 0xfc00000, romFile);
}

};
