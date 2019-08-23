
#include <iostream>
#include <cstddef>

#include "Memory.h"
#include "M6502State.h"

using namespace M6502;

namespace M6502 {
    State *currentState = NULL;
};

State::State()
{
    regs.pc = 0;
    regs.sp = 0xfd;
    regs.a = 0;
    regs.x = 0;
    regs.y = 0;
    regs.p = 0x24;
    stack = Memory::ram + 0x1fd;
    cycles = 0LL;
    nmi = false;
    irq = false;
}

State::~State()
{
}

void State::clear()
{
    regs.pc = 0;
    regs.sp = 0xfd;
    regs.a = 0;
    regs.x = 0;
    regs.y = 0;
    regs.p = 0x24;
    stack = Memory::ram + 0x1fd;
    cycles = 0LL;
    nmi = false;
    irq = false;
}

void State::reset()
{
    regs.pc = Memory::loadw(Memory::RST_ADDR);
}
