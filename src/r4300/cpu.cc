
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"
#include "cop0.h"

using namespace R4300;

class Cop0 : public Coprocessor
{
public:
    Cop0() {
        regs[0] = &R4300::Index;
        regs[1] = &R4300::Index;
        regs[2] = &R4300::Index;
        regs[3] = &R4300::Index;
        regs[4] = &R4300::Index;
        regs[5] = &R4300::Index;
        regs[6] = &R4300::Index;
        regs[7] = &R4300::Index;
        regs[8] = &R4300::Index;
        regs[9] = &R4300::Count;
        regs[10] = &R4300::Index;
        regs[11] = &R4300::Compare;
        regs[12] = &R4300::Index;
        regs[13] = &R4300::Cause;
        regs[14] = &R4300::Index;
        regs[15] = &R4300::Index;
        regs[16] = &R4300::Index;
        regs[17] = &R4300::Index;
        regs[18] = &R4300::Index;
        regs[19] = &R4300::Index;
        regs[20] = &R4300::Index;
        regs[21] = &R4300::Index;
        regs[22] = &R4300::Index;
        regs[23] = &R4300::Index;
        regs[24] = &R4300::Index;
        regs[25] = &R4300::Index;
        regs[26] = &R4300::Index;
        regs[27] = &R4300::Index;
        regs[28] = &R4300::Index;
        regs[29] = &R4300::Index;
        regs[30] = &R4300::Index;
        regs[31] = &R4300::Index;
    }
    ~Cop0() {}

    virtual void COFUN(u32 instr) {
        std::cerr << "COP0 " << instr << std::endl;
    }

    virtual void MF(u32 rt, u32 rd) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void DMF(u32 rt, u32 rd) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void CF(u32 rt, u32 rd) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void MT(u32 rt, u32 rd) {
        std::cerr << "MTC0 " << rd << std::endl;
        u32 data = state.reg.gpr[rt];
        regs[rd]->write(2, data);
    }

    virtual void DMT(u32 rt, u32 rd) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void CT(u32 rt, u32 rd) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void BCF(u32 rd, u64 imm) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void BCT(u32 rd, u64 imm) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void BCFL(u32 rd, u64 imm) {
        std::cerr << "COP0" << std::endl;
    }

    virtual void BCTL(u32 rd, u64 imm) {
        std::cerr << "COP0" << std::endl;
    }

private:
    const Register *regs[32];
};

class Cop1 : public Coprocessor
{
public:
    Cop1() {}
    ~Cop1() {}

    virtual void COFUN(u32 instr) {
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
    os << "pc  : $" << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
