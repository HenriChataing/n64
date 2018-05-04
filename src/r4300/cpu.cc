
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {

extern Coprocessor cop0;
extern Coprocessor cop1;

State state;
Coprocessor *cop[4] = {
    &cop0,
    &cop1,
    new Coprocessor(),
    new Coprocessor(),
};
};

State::State() {}
State::~State() {}

void State::boot()
{
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
    cp0reg.prId   = 0x00000b00;
    cp0reg.config = 0x0006e463;
    memset(&cp1reg, 0, sizeof(cp1reg));
    memset(tlb, 0, sizeof(tlb));

    R4300::physmem.store(4, 0x04300004llu, 0x01010101);
    R4300::physmem.copy(0x04000000llu, 0x10000000llu, 0x1000);
    reg.pc = 0xffffffffa4000040llu;
}

std::ostream &operator<<(std::ostream &os, const State &state)
{
    os << "pc  : $" << std::hex << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
