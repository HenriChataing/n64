#ifndef _CPU_H_INCLUDED_
#define _CPU_H_INCLUDED_

#include "type.h"

namespace M6502
{

struct Registers
{
    u8 a;
    u8 x;
    u8 y;
    u8 p;
    u8 sp;
    u16 pc;
};

class State
{
public:
    State();
    ~State();

    void clear();
    void reset();

    /** CPU registers. */
    Registers regs;

    /** Stack pointer (equal to Memory::ram + 0x100 + SP) */
    u8 *stack;

    /** Cycle count. */
    ulong cycles;

    /** Set to true if an NMI is pending. */
    bool nmi;

    /** Set to true if an IRQ is pending. */
    bool irq;
};

extern State *currentState;

};

#endif /* _CPU_H_INCLUDED_ */
