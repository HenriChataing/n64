
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"
#include "cop0.h"

using namespace R4300;

Register Undefined;

class Cop0 : public Coprocessor
{
public:
    Cop0() {
        regs[0] = &R4300::Index;
        regs[1] = &R4300::Random;
        regs[2] = &R4300::EntryLo0;
        regs[3] = &R4300::EntryLo1;
        regs[4] = &R4300::Context;
        regs[5] = &R4300::PageMask;
        regs[6] = &R4300::Wired;
        regs[7] = &Undefined;
        regs[8] = &R4300::BadVAddr;
        regs[9] = &R4300::Count;
        regs[10] = &R4300::EntryHi;
        regs[11] = &R4300::Compare;
        regs[12] = &R4300::SR;
        regs[13] = &R4300::Cause;
        regs[14] = &R4300::EPC;
        regs[15] = &R4300::PrId;
        regs[16] = &R4300::Config;
        regs[17] = &R4300::LLAddr;
        regs[18] = &R4300::WatchLo;
        regs[19] = &R4300::WatchHi;
        regs[20] = &R4300::XContext;
        regs[21] = &Undefined;
        regs[22] = &Undefined;
        regs[23] = &Undefined;
        regs[24] = &Undefined;
        regs[25] = &Undefined;
        regs[26] = &R4300::PErr;
        regs[27] = &R4300::CacheErr;
        regs[28] = &R4300::TagLo;
        regs[29] = &R4300::TagHi;
        regs[30] = &R4300::ErrPC;
        regs[31] = &Undefined;
    }
    ~Cop0() {}

    virtual void cofun(u32 instr) {
        std::cerr << "COP0 COFUN " << instr << std::endl;
    }

    virtual u64 read(uint bytes, u32 rd) {
        return regs[rd]->read(bytes);
    }

    virtual void write(uint bytes, u32 rd, u64 imm) {
        regs[rd]->write(bytes, imm);
    }

private:
    const Register *regs[32];
};

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
    os << "pc  : $" << std::hex << state.reg.pc << std::endl;
    for (uint r = 0; r < 32; r++)
        os << "gpr[" << r << "]" << "  : $" << state.reg.gpr[r] << std::endl;
    return os;
}
