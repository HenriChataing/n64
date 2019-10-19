
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>
#include <r4300/state.h>

using namespace R4300;

namespace R4300 {
State state;
};

State::State() {}
State::~State() {}

void State::init(std::string romFile) {
    // Create the physical memory address space for this machine
    // importing the rom bytes for the select file.
    physmem.root = new Memory::Region(0, 0x100000000llu);
    physmem.root->insertRam(  0x00000000llu, 0x400000, dram);   /* RDRAM ranges 0, 1 */
    physmem.root->insertIOmem(0x03f00000llu, 0x100000, RdRam::read, RdRam::write); /* RDRAM Registers */
    physmem.root->insertRam(  0x04000000llu, 0x1000, dmem);     /* SP DMEM */
    physmem.root->insertRam(  0x04001000llu, 0x1000, imem);     /* SP IMEM */
    physmem.root->insertIOmem(0x04040000llu, 0x80000, SP::read, SP::write); /* SP Registers */
    physmem.root->insertIOmem(0x04100000llu, 0x100000, DPCommand::read, DPCommand::write); /* DP Command Registers */
    physmem.root->insertIOmem(0x04200000llu, 0x100000, DPSpan::read, DPSpan::write); /* DP Span Registers */
    physmem.root->insertIOmem(0x04300000llu, 0x100000, MI::read, MI::write); /* Mips Interface */
    physmem.root->insertIOmem(0x04400000llu, 0x100000, VI::read, VI::write); /* Video Interface */
    physmem.root->insertIOmem(0x04500000llu, 0x100000, AI::read, AI::write); /* Audio Interface */
    physmem.root->insertIOmem(0x04600000llu, 0x100000, PI::read, PI::write); /* Peripheral Interface */
    physmem.root->insertIOmem(0x04700000llu, 0x100000, RI::read, RI::write); /* RDRAM Interface */
    physmem.root->insertIOmem(0x04800000llu, 0x100000, SI::read, SI::write); /* Serial Interface */
    physmem.root->insertIOmem(0x05000000llu, 0x1000000, Cart_2_1::read, Cart_2_1::write);
    physmem.root->insertRom(  0x10000000llu, 0xfc00000, romFile);
    physmem.root->insertIOmem(0x1fc00000llu, 0x100000, PIF::read, PIF::write);
}

void State::boot() {
    // Reproduce the pif ROM boot sequence.
    // Referenced from http://www.emulation64.com/ultra64/bootn64.html
    // @todo test with actual pif ROM and compare the operations
    memset(reg.gpr, 0, sizeof(reg.gpr));
    reg.gpr[20]   = 0x1;
    reg.gpr[22]   = 0x3f;
    reg.gpr[29]   = 0xffffffffa4001ff0llu;
    memset(&cp0reg, 0, sizeof(cp0reg));
    cp0reg.random = 0x0000001f;
    cp0reg.sr     = 0x70400004;
    cp0reg.prid   = 0x00000b00;
    cp0reg.config = 0x0006e463;
    memset(&cp1reg, 0, sizeof(cp1reg));
    memset(tlb, 0, sizeof(tlb));

    physmem.store(4, 0x04300004llu, 0x01010101);
    physmem.copy(0x04000000llu, 0x10000000llu, 0x1000);
    reg.pc = 0xffffffffa4000040llu;

    cp1reg.setFprAliases(false);

    // Set reset values for HW registers.
    hwreg.SP_STATUS_REG = SP_STATUS_HALT;
}

std::ostream &operator<<(std::ostream &os, const State &state)
{
    os << "pc  : $" << std::hex << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
