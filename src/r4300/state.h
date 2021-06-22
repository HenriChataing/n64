
#ifndef _R4300_STATE_H_INCLUDED_
#define _R4300_STATE_H_INCLUDED_

#include <iostream>
#include <mutex>

#include <r4300/cpu.h>
#include <r4300/rsp.h>
#include <r4300/hw.h>
#include <r4300/controller.h>

namespace R4300 {

class State
{
public:
    State();
    ~State();

    int loadBios(std::istream &bios_contents);
    int load(std::istream &rom_contents);
    void reset();

    struct cpureg reg;          /**< CPU registers */
    struct cp0reg cp0reg;       /**< Co-processor 0 registers */
    struct cp1reg cp1reg;       /**< Co-processor 1 registers */
    struct rspreg rspreg;
    struct hwreg hwreg;
    struct tlbEntry tlb[tlbEntryCount]; /**< Translation look-aside buffer */

    alignas(u64) u8 dram[0x400000];
                 u8 dram_bit9[0x80000];

    alignas(u64) u8 dmem[0x1000];
    alignas(u64) u8 imem[0x1000];
    alignas(u64) u8 tmem[0x1000];
    alignas(u64) u8 pifram[0x40];
    alignas(u64) u8 pifrom[0x7c0];
    alignas(u64) u8 rom[0xfc00000];

    Memory::Bus *bus;
    ulong cycles;

    enum Action {
        Continue,   /**< Evaluate the instruction at pc+4 */
        Delay,      /**< Evaluate the instruction at pc+4, then perform a jump */
        Jump,       /**< Jump to the specified address. */
    };

    struct Event {
        ulong timeout;
        void (*callback)();
        struct Event *next;

        Event(ulong timeout, void (*callback)(), Event *next)
            : timeout(timeout), callback(callback), next(next) {};
    };

    struct {
        Action  nextAction;
        u64     nextPc;
        ulong   nextEvent;
        bool    delaySlot;
        Event  *eventQueue;
    } cpu, rsp;

    struct controller *controllers[4];

    void plugController(unsigned channel, struct controller *controller);
    void plugAccessory(unsigned channel, struct extension_pak *accessory);
    void unplugController(unsigned channel);

    void swapMemoryBus(Memory::Bus *bus);
    void scheduleEvent(ulong timeout, void (*callback)());
    void cancelEvent(void (*callback)());
    void cancelAllEvents();
    void handleEvent();

    u8 loadHiddenBits(u32 addr);
    void storeHiddenBits(u32 addr, u8 val);

private:
    /**
     * Mutex to control access to concurrent resources:
     * - eventQueue
     */
    std::mutex _mutex;
};

/** Current machine state. */
extern State state;

}; /* namespace R4300 */

#endif /* _R4300_STATE_H_INCLUDED_ */
