
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <debugger.h>

using namespace R4300;

namespace R4300 {
State state;
};

/* Read-As-Zero */
static bool RAZ(unsigned bytes, u64 addr, u64 *val) {
    (void)bytes; (void)addr;
    *val = 0;
    debugger::halt("RAZ");
    return true;
}

/* Write-Ignored */
static bool WI(unsigned bytes, u64 addr, u64 val) {
    (void)bytes; (void)addr; (void)val;
    // debugger::halt("WI");
    return true;
}

State::State() : physmem(0, 0x100000000llu) {
    // Create the physical memory address space for this machine
    // importing the rom bytes for the select file.
    physmem.root.insertRam(  0x00000000llu, 0x400000, dram);   /* RDRAM ranges 0, 1 */
    physmem.root.insertIOmem(0x00400000llu, 0x400000, RAZ, WI);/* RDRAM ranges 2, 3 (extended) */
    physmem.root.insertIOmem(0x03f00000llu, 0x100000, read_RDRAM_REG, write_RDRAM_REG);
    physmem.root.insertRam(  0x04000000llu, 0x1000,   dmem);     /* SP DMEM */
    physmem.root.insertRam(  0x04001000llu, 0x1000,   imem);     /* SP IMEM */
    physmem.root.insertIOmem(0x04040000llu, 0x80000,  read_SP_REG,    write_SP_REG);
    physmem.root.insertIOmem(0x04100000llu, 0x100000, read_DPC_REG,   write_DPC_REG);
    physmem.root.insertIOmem(0x04200000llu, 0x100000, read_DPS_REG,   write_DPS_REG);
    physmem.root.insertIOmem(0x04300000llu, 0x100000, read_MI_REG,    write_MI_REG);
    physmem.root.insertIOmem(0x04400000llu, 0x100000, read_VI_REG,    write_VI_REG);
    physmem.root.insertIOmem(0x04500000llu, 0x100000, read_AI_REG,    write_AI_REG);
    physmem.root.insertIOmem(0x04600000llu, 0x100000, read_PI_REG,    write_PI_REG);
    physmem.root.insertIOmem(0x04700000llu, 0x100000, read_RI_REG,    write_RI_REG);
    physmem.root.insertIOmem(0x04800000llu, 0x100000, read_SI_REG,    write_SI_REG);
    physmem.root.insertIOmem(0x05000000llu, 0x1000000,    read_CART_2_1, write_CART_2_1);
    physmem.root.insertIOmem(0x06000000llu, 0x2000000,    read_CART_1_1, write_CART_1_1);
    physmem.root.insertIOmem(0x08000000llu, 0x8000000,    read_CART_2_2, write_CART_2_2);
    physmem.root.insertRom(  0x10000000llu, 0xfc00000, rom); /* Cartridge Domain 1 Address 2 */
    physmem.root.insertIOmem(0x1fc00000llu, 0x100000,     read_PIF_RAM,  write_PIF_RAM);
    physmem.root.insertIOmem(0x1fd00000llu, 0x60300000lu, read_CART_1_3, write_CART_1_3);
}

State::~State() {
}

int State::load(std::string file) {
    FILE *fd = fopen(file.c_str(), "r");
    if (fd == NULL)
        return -1;

    // Obtain file size
    fseek(fd, 0, SEEK_END);
    size_t size = ftell(fd);
    rewind(fd);

    if (size > sizeof(rom)) {
        fclose(fd);
        return -1;
    }

    // Clear the ROM memory and copy the file.
    memset(rom, 0, sizeof(rom));
    int result = fread(rom, 1, size, fd);
    fclose(fd);

    return (result != (int)size) ? -1 : 0;
}

void State::reset() {
    // Clear the machine state.
    memset(dram, 0, sizeof(dram));
    memset(dmem, 0, sizeof(dmem));
    memset(imem, 0, sizeof(imem));
    memset(tmem, 0, sizeof(tmem));
    memset(pifram, 0, sizeof(pifram));

    cycles = 0;
    reg = (R4300::cpureg){};
    cp0reg = (R4300::cp0reg){};
    cp1reg = (R4300::cp1reg){};
    rspreg = (R4300::rspreg){};
    hwreg = (R4300::hwreg){};
    for (unsigned nr = 0; nr < tlbEntryCount; nr++)
        tlb[nr] = (R4300::tlbEntry){};

    cp0reg.lastCounterUpdate = 0;
    cancelAllEvents();

    // Reproduce the pif ROM boot sequence.
    // Referenced from http://www.emulation64.com/ultra64/bootn64.html
    // @todo test with actual pif ROM and compare the operations
    memset(reg.gpr, 0, sizeof(reg.gpr));
    reg.gpr[20]   = 0x1;
    reg.gpr[22]   = 0x3f;
    reg.gpr[29]   = 0xffffffffa4001ff0llu;
    memset(&cp0reg, 0, sizeof(cp0reg));
    cp0reg.random = 0x0000001flu;
    cp0reg.sr     = 0x74400004lu; // 0x70400004
    cp0reg.prid   = 0x00000b00lu;
    cp0reg.config = 0x0006e463lu;
    memset(&cp1reg, 0, sizeof(cp1reg));
    memset(tlb, 0, sizeof(tlb));
    cp1reg.setFprAliases(true);

    // Copy the cart boot code to the beginning of the physical RAM.
    memcpy(dmem, rom, 0x1000);
    reg.pc = 0xffffffffa4000040llu;

    // Set HW config registers.
    hwreg.RDRAM_DEVICE_TYPE_REG = RDRAM_DEVICE_TYPE_18M;
    hwreg.SP_STATUS_REG = SP_STATUS_HALT;
    hwreg.MI_VERSION_REG = 0x01010101lu;

    // Set reset video mode to NTSC.
    hwreg.VI_V_SYNC_REG = UINT32_C(0x20d); // 525 lines
    hwreg.VI_H_SYNC_REG = UINT32_C(0xc15); // 773,4 pixels per line
    hwreg.vi_NextIntr = 1562500lu;
    hwreg.vi_IntrInterval = 1562500lu;
    hwreg.vi_LastCycleCount = 0;
    hwreg.vi_CyclesPerLine = 2971lu;
    scheduleEvent(hwreg.vi_NextIntr, raise_VI_INTR);
    scheduleEvent(std::numeric_limits<u32>::max() * 2, handleCounterEvent);

    // Setup initial action.
    cpu.nextAction = Action::Jump;
    cpu.nextPc = reg.pc;
    cpu.nextEvent = -1lu;
    rsp.nextAction = Action::Jump;
    rsp.nextPc = 0x0;
}

u8 State::loadHiddenBits(u32 addr) {
    unsigned offset = addr / 8;
    unsigned shift = (addr % 8) / 2;
    return (dram_bit9[offset] >> shift) & 0x3u;
}

void State::storeHiddenBits(u32 addr, u8 val) {
    unsigned offset = addr / 8;
    unsigned shift = (addr % 8) / 2;
    dram_bit9[offset] &= ~(0x3u << shift);
    dram_bit9[offset] |= (val & 0x3u) << shift;
}

void State::scheduleEvent(ulong timeout, void (*callback)()) {
    State::Event *event = new Event(timeout, callback, NULL);
    State::Event *pos = cpu.eventQueue, **prev = &cpu.eventQueue;
    while (pos != NULL) {
        ulong udiff = timeout - pos->timeout;
        if (udiff > std::numeric_limits<long long>::max())
            break;
        prev = &pos->next;
        pos = pos->next;
    }
    *prev = event;
    event->next = pos;
    cpu.nextEvent = cpu.eventQueue->timeout;
}

void State::cancelEvent(void (*callback)()) {
    State::Event *pos = cpu.eventQueue, **prev = &cpu.eventQueue;
    while (pos != NULL) {
        if (pos->callback == callback) {
            State::Event *tmp = pos;
            *prev = pos->next;
            pos = pos->next;
            delete tmp;
        } else {
            prev = &pos->next;
            pos = pos->next;
        }
    }
}

void State::cancelAllEvents(void) {
    State::Event *pos = cpu.eventQueue;
    cpu.eventQueue = NULL;
    while (pos != NULL) {
        State::Event *tmp = pos;
        pos = pos->next;
        delete tmp;
    }
}

void State::handleEvent(void) {
    if (cpu.eventQueue != NULL) {
        Event *event = cpu.eventQueue;
        cpu.eventQueue = event->next;
        event->callback();
        delete event;
    }
    cpu.nextEvent =
        (cpu.eventQueue != NULL) ? cpu.eventQueue->timeout : cycles - 1;
}
