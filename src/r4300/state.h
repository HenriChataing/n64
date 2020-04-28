
#ifndef _R4300_STATE_H_INCLUDED_
#define _R4300_STATE_H_INCLUDED_

#include <iostream>

#include <r4300/cpu.h>
#include <r4300/rsp.h>
#include <r4300/hw.h>

namespace R4300 {

class State
{
public:
    State();
    ~State();

    void init(std::string romFile);
    void boot();

    struct cpureg reg;          /**< CPU registers */
    struct cp0reg cp0reg;       /**< Co-processor 0 registers */
    struct cp1reg cp1reg;       /**< Co-processor 1 registers */
    struct rspreg rspreg;
    struct hwreg hwreg;
    struct tlbEntry tlb[tlbEntryCount]; /**< Translation look-aside buffer */

    alignas(u64) u8 dram[0x400000];
    alignas(u64) u8 dmem[0x1000];
    alignas(u64) u8 imem[0x1000];
    alignas(u64) u8 tmem[0x1000];
    alignas(u64) u8 pifram[0x40];
    alignas(u64) u8 rom[0xfc00000];
    Memory::AddressSpace physmem;

    ulong cycles;

    enum Action {
        Continue,   /**< Evaluate the instruction at pc+4 */
        Delay,      /**< Evaluate the instruction at pc+4, then perform a jump */
        Jump,       /**< Jump to the specified address. */
        Interrupt,  /**< Take an interrupt at the next instruction. */
    };

    struct {
        Action nextAction;
        u64 nextPc;
    } cpu, rsp;
};

/** Current machine state. */
extern State state;

}; /* namespace R4300 */

std::ostream &operator<<(std::ostream &os, const R4300::State &state);

#endif /* _R4300_STATE_H_INCLUDED_ */
