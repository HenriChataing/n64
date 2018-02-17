
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"
#include "cop0.h"

using namespace R4300;

extern
Register Undefined;

class Cop1 : public Coprocessor
{
public:
    Cop1() {}
    ~Cop1() {}

    virtual void cofun(u32 instr) {
        std::cerr << "COP1 COFUN " << instr << std::endl;
    }

    virtual u64 read(uint bytes, u32 fs) {
        if (bytes == 8) {
            if (FR())
                return state.reg.fgr[fs];
            else if ((fs % 2) == 0)
                return state.reg.fgr[fs / 2];
        }
        return 0; // undefined
    }

    virtual void write(uint bytes, u32 fs, u64 imm)
    {
        if (bytes == 8) {
            if (FR())
                state.reg.fgr[fs] = imm;
            else if ((fs % 2) == 0)
                state.reg.fgr[fs / 2] = imm;
            // undefined otherwise
        } else {
            if (FR())
                state.reg.fgr[fs] = imm; // semi undefined
            else if (fs % 2)
                state.reg.fgr[fs / 2] =
                    (state.reg.fgr[fs / 2] & 0xffffffff) |
                    (imm << 32);
            else
                state.reg.fgr[fs / 2] =
                    (state.reg.fgr[fs / 2] & ~0xffffffff) |
                    (imm & 0xffffffff);
        }
    }
};

namespace R4300 {

extern Coprocessor cop0;

State state;
Coprocessor *cop[4] = {
    &cop0,
    new Cop1(),
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
    reg.gpr[29]   = 0xffffffffa4001ff0;
    memset(&cp0reg, 0, sizeof(cp0reg));
    cp0reg.random = 0x0000001f;
    cp0reg.sr     = 0x70400004;
    cp0reg.prId   = 0x00000b00;
    cp0reg.config = 0x0006e463;

    R4300::physmem.store(4, 0x04300004, 0x01010101);
    R4300::physmem.copy(0x04000000, 0x10000000, 0x1000);
    reg.pc = 0xffffffffa4000040;
}

std::ostream &operator<<(std::ostream &os, const State &state)
{
    os << "pc  : $" << std::hex << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
