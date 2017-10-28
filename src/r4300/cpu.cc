
#include <cstring>

#include <memory.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {
State state;
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
    reg.gpr[29]   = 0xa4001ff0;
    memset(&cp0reg, 0, sizeof(cp0reg));
    cp0reg.random = 0x0000001f;
    cp0reg.sr     = 0x70400004;
    cp0reg.prId   = 0x00000b00;
    cp0reg.config = 0x0006e463;

    Memory::store(0, 4, 0x04300004, 0, 0x01010101);
    Memory::dma(0x04000000, 0x10000000, 0x1000);
    reg.pc = 0xa4000040;
}
