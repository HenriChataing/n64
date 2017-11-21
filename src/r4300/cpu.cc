
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"

using namespace R4300;

class Cop0 : public Coprocessor
{
public:
    Cop0() {}
    ~Cop0() {}

    virtual void COFUN(u32 instr) {
        std::cerr << "COP0 " << instr << std::endl;
    }

    virtual void MF(u32 rt, u32 rd) {
    }

    virtual void DMF(u32 rt, u32 rd) {
    }

    virtual void CF(u32 rt, u32 rd) {
    }

    virtual void MT(u32 rt, u32 rd) {
    }

    virtual void DMT(u32 rt, u32 rd) {
    }

    virtual void CT(u32 rt, u32 rd) {
    }

    virtual void BCF(u32 rd, u64 imm) {
    }

    virtual void BCT(u32 rd, u64 imm) {
    }

    virtual void BCFL(u32 rd, u64 imm) {
    }

    virtual void BCTL(u32 rd, u64 imm) {
    }
};

class Cop1 : public Coprocessor
{
public:
    Cop1() {}
    ~Cop1() {}

    virtual void COFUN(u32 instr) {
        std::cerr << "COP1 " << instr << std::endl;
    }

    virtual void MF(u32 rt, u32 fs) {
    }

    virtual void DMF(u32 rt, u32 fs) {
        if (FR())
            state.reg.gpr[rt] = state.reg.fgr[fs];
        else if ((fs % 2) == 0)
            state.reg.gpr[rt] = state.reg.fgr[fs / 2];
        else
            state.reg.gpr[rt] = 0; // undefined
    }

    virtual void CF(u32 rt, u32 fs) {
    }

    virtual void MT(u32 rt, u32 fs) {
        if (FR())
            state.reg.fgr[fs] = state.reg.gpr[rt]; // semi undefined
        else if (fs % 2)
            state.reg.fgr[fs / 2] =
                (state.reg.fgr[fs / 2] & 0xffffffff) |
                (state.reg.gpr[rt] << 32);
        else
            state.reg.fgr[fs / 2] =
                (state.reg.fgr[fs / 2] & ~0xffffffff) |
                (state.reg.gpr[rt] & 0xffffffff);
    }

    virtual void DMT(u32 rt, u32 fs) {
        if (FR())
            state.reg.fgr[fs] = state.reg.gpr[rt];
        else if ((fs % 2) == 0)
            state.reg.fgr[fs / 2] = state.reg.gpr[rt];
        // undefined otherwise
    }

    virtual void CT(u32 rt, u32 fs) {
    }

    virtual void BCF(u32 rd, u64 imm) {
    }

    virtual void BCT(u32 rd, u64 imm) {
    }

    virtual void BCFL(u32 rd, u64 imm) {
    }

    virtual void BCTL(u32 rd, u64 imm) {
    }
};

namespace R4300 {
State state;
Coprocessor *cop[4] = {
    new Cop0(),
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
    reg.gpr[29]   = 0xa4001ff0;
    memset(&cp0reg, 0, sizeof(cp0reg));
    cp0reg.random = 0x0000001f;
    cp0reg.sr     = 0x70400004;
    cp0reg.prId   = 0x00000b00;
    cp0reg.config = 0x0006e463;

    R4300::physmem.store(4, 0x04300004, 0x01010101);
    R4300::physmem.copy(0x04000000, 0x10000000, 0x1000);
    reg.pc = 0xa4000040;
}

std::ostream &operator<<(std::ostream &os, const State &state)
{
    os << "pc  : $" << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
