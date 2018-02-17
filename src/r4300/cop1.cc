
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {

class Cop1 : public Coprocessor
{
public:
    Cop1() {}
    ~Cop1() {}

    virtual void cofun(u32 instr) {
        std::cerr << "COP1 COFUN " << instr << std::endl;
        throw "COP1 COFUN";
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

Cop1 cop1;

}; /* namespace R4300 */
