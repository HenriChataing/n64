
#include <iostream>
#include <fstream>
#include <cstring>

#include <core.h>
#include <memory.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <debugger.h>
#include <trace.h>

using namespace R4300;

namespace R4300 {
State state;
};

/* Read-As-Zero */
static bool RAZ(unsigned bytes, u64 addr, u64 *val) {
    (void)bytes; (void)addr;
    *val = 0;
    core::halt("RAZ");
    return true;
}

/* Write-Ignored */
static bool WI(unsigned bytes, u64 addr, u64 val) {
    (void)bytes; (void)addr; (void)val;
    // core::halt("WI");
    return true;
}

State::State() : bus(NULL) {
    // Create the physical memory address space for this machine
    // importing the rom bytes for the select file.
    swapMemoryBus(new Memory::Bus(32));
}

State::~State() {
    delete bus;
}

int State::loadBios(std::istream &bios_contents) {
    // Clear the ROM memory and copy the file.
    memset(pifrom, 0, sizeof(pifrom));
    bios_contents.read((char *)pifrom, sizeof(pifrom));
    return bios_contents.gcount() > 0 ? 0 : -1;
}

int State::load(std::istream &rom_contents) {
    // Clear the ROM memory and copy the file.
    memset(rom, 0, sizeof(rom));
    rom_contents.read((char *)rom, sizeof(rom));
    return rom_contents.gcount() > 0 ? 0 : -1;
}

void State::swapMemoryBus(Memory::Bus *bus) {
    bus->root.insertRam(  0x00000000llu, 0x400000, dram);   /* RDRAM ranges 0, 1 */
    bus->root.insertIOmem(0x00400000llu, 0x400000, RAZ, WI);/* RDRAM ranges 2, 3 (extended) */
    bus->root.insertIOmem(0x03f00000llu, 0x100000, read_RDRAM_REG, write_RDRAM_REG);
    bus->root.insertRam(  0x04000000llu, 0x1000,   dmem);     /* SP DMEM */
    bus->root.insertRam(  0x04001000llu, 0x1000,   imem);     /* SP IMEM */
    bus->root.insertIOmem(0x04040000llu, 0x80000,  read_SP_REG,    write_SP_REG);
    bus->root.insertIOmem(0x04100000llu, 0x100000, read_DPC_REG,   write_DPC_REG);
    bus->root.insertIOmem(0x04200000llu, 0x100000, read_DPS_REG,   write_DPS_REG);
    bus->root.insertIOmem(0x04300000llu, 0x100000, read_MI_REG,    write_MI_REG);
    bus->root.insertIOmem(0x04400000llu, 0x100000, read_VI_REG,    write_VI_REG);
    bus->root.insertIOmem(0x04500000llu, 0x100000, read_AI_REG,    write_AI_REG);
    bus->root.insertIOmem(0x04600000llu, 0x100000, read_PI_REG,    write_PI_REG);
    bus->root.insertIOmem(0x04700000llu, 0x100000, read_RI_REG,    write_RI_REG);
    bus->root.insertIOmem(0x04800000llu, 0x100000,   read_SI_REG,    write_SI_REG);
    bus->root.insertIOmem(0x05000000llu, 0x1000000,  read_CART_2_1, write_CART_2_1);
    bus->root.insertIOmem(0x06000000llu, 0x2000000,  read_CART_1_1, write_CART_1_1);
    bus->root.insertIOmem(0x08000000llu, 0x8000000,  read_CART_2_2, write_CART_2_2);
    bus->root.insertRom(  0x10000000llu, 0xfc00000,  rom); /* Cartridge Domain 1 Address 2 */
    bus->root.insertRom(  0x1fc00000llu, 0x7c0,      pifrom);
    bus->root.insertIOmem(0x1fc007c0llu, 0x40,       read_PIF_RAM,  write_PIF_RAM);
    bus->root.insertIOmem(0x1fd00000llu, 0x60300000lu, read_CART_1_3, write_CART_1_3);
    if (this->bus) delete this->bus;
    this->bus = bus;
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

    // Set the register reset values.
    cp0reg.random = UINT32_C(0x0000001f);
    cp0reg.prid   = UINT32_C(0x00000b00);
    hwreg.RDRAM_DEVICE_TYPE_REG = RDRAM_DEVICE_TYPE_18M;
    hwreg.SP_STATUS_REG = SP_STATUS_HALT;
    hwreg.MI_VERSION_REG = UINT32_C(0x01010101);

    // Reproduce the pif ROM boot sequence, instruction by instruction.
    // The ROM reproduced here is IPL 1.0 NTSC.

    // After reset, the CIC has input the following value
    // at the offset 0x24 of the PIF RAM (based on the CIC-NUS-6102)
    pifram[36] = 0;
    pifram[37] = 0;
    pifram[38] = UINT8_C(0x3f);
    pifram[39] = UINT8_C(0x3f);

    // 1fc00004: Write SR = 0x34000000
    cp0reg.sr     = UINT32_C(0x34000000);
    // 1fc00010: Write CONFIG = 0x0006e463
    cp0reg.config = UINT32_C(0x0006e463);
    // 1fc00020: Wait SP_STATUS_REG.halt = 1
    // 1fc00030: Write SP_STATUS_EG = 0xa (set halt, clear intr)
    // 1fc00040: Wait SP_DMA_BUSY_REG.busy = 0
    // 1fc00050: Write PI_STATUS_REG = 0x3 (reset, clear intr)
    // 1fc0005c: Write VI_INTR_REG = 0x3ff
    hwreg.VI_INTR_REG = UINT32_C(0x3ff);
    // 1fc00064: Write VI_H_START_REG = 0
    hwreg.VI_H_START_REG = 0;
    // 1fc0006c: Write VI_CURRENT_REG = 0
    hwreg.VI_CURRENT_REG = 0;
    // 1fc00074: Write AI_DRAM_ADDR_REG = 0
    hwreg.AI_DRAM_ADDR_REG = 0;
    // 1fc00078: Write AI_LEN_REG = 0
    hwreg.AI_LEN_REG = 0;
    // 1fc0008c: Wait SP_STATUS_REG.dma_full = 0
    // 1fc000ac-1fc000b8:
    //      Copy range 0x1fc000d4-0x1fc00720
    //      to range 0x04001000-0x0400164c
    memcpy(imem, pifrom + 0xd4, 0x64c);
    // 1fc000cc: Jump to 0x04001000
    reg.gpr[29] = UINT64_C(0xffffffffa4001ff0);
    // 1fc000e4: Wait PIFRAM[0].bit7 = 0
    // 1fc000f0-1fc00108:
    //      Write s3 = PIFRAM[37].bit3
    //      Write s7 = PIFRAM[37].bit2
    //      Write t3 = s3 ? 0xffffffffa6000000 : 0xffffffffb0000000
    //      Selects cartridge domain 1 address 1 or 2
    reg.gpr[19] = (pifram[37] >> 3) & UINT8_C(0x1);
    reg.gpr[23] = (pifram[37] >> 2) & UINT8_C(0x1);
    // 1fc00120: Write s6 = PIFRAM[38]
    reg.gpr[22] = pifram[38];
    // 1fc00124: Write s5 = PIFRAM[37].bit1
    reg.gpr[21] = (pifram[37] >> 1) & UINT8_C(0x1);
    // 1fc00128: Write s4 = 0x1
    reg.gpr[20] = UINT32_C(0x1);
    // 1fc00130-1fc00140:
    //      Wait SI_STATUS_REG.io_read_busy = 0
    // 1fc00144: PIFRAM[63] |= 0x10
    // 1fc00150: Write PI_BSD_DOM1_LAT_REG = 0xff
    // 1fc00154: Write PI_BSD_DOM1_PWD_REG = 0xff
    // 1fc0015c: Write PI_BSD_DOM1_PGS_REG = 0xf
    // 1fc00164: Write PI_BSD_DOM1_RLS_REG = 0x3
    // 1fc00178: Write PI_BSD_DOM1_LAT_REG = ROM[0] & 0xff
    hwreg.PI_BSD_DOM1_LAT_REG = rom[3];
    // 1fc00180: Write PI_BSD_DOM1_PWD_REG = ROM[0] >> 8
    hwreg.PI_BSD_DOM1_PWD_REG = rom[2];
    // 1fc00188: Write PI_BSD_DOM1_PGS_REG = ROM[0] >> 16
    hwreg.PI_BSD_DOM1_PGS_REG = rom[1] & UINT8_C(0xf);
    // 1fc00190: Write PI_BSD_DOM1_RLS_REG = ROM[0] >> 20
    hwreg.PI_BSD_DOM1_RLS_REG = (rom[1] >> 4) & UINT8_C(0x3);
    // 1fc00194-1fc001c0:
    //      If DPC_STATUS_REG.xbus_dmem_dma = 1 then
    //      wait DPC_STATUS_REG.pipe_busy = 0
    // 1fc001c4-1fc001e8:
    //      Copy range 0x10000040-0x10001000
    //      to range 0x04000040-0x04001000
    memcpy(dmem + 0x40, rom + 0x40, 0xfc0);
    // 1fc00208: Call 1fc00258 with
    //      a0 = PIFRAM[39] * 0x6c078965 + 0x1
    //      a1 = 0xffffffffa4000040
    //
    // 1fc00210-1fc00254:
    //      Hashing procedure ?
    // 1fc00258-1fc00620:
    //      Procedure, jumps to 1fc00640
    // 1fc00624-1fc0063c:
    //      Multiply procedure
    //
    // 1fc00668: Wait SI_STATUS_REG.io_read_busy = 0
    // 1fc00670: Write PIFRAM[50, 51] = a0
    // 1fc00694: Wait SI_STATUS_REG.io_read_busy = 0
    // 1fc006a4: Write PIFRAM[52, 53, 54, 55] = a1
    // 1fc006b8: Wait SI_STATUS_REG.io_read_busy = 0
    // 1fc006c0: Write PIFRAM[60, 61, 62, 63] |= 0x20
    // 1fc006c4-1fc006e0:
    //      While PIFRAM[60, 61, 62, 63].bit7 = 0:
    //      Delay 32 cycles
    // 1fc006f8: Wait SI_STATUS_REG.io_read_busy = 0
    // 1fc006c0: Write PIFRAM[60, 61, 62, 63] |= 0x40
    // 1fc0070c: Write r3 = 0xffffffffa4000040
    reg.gpr[11] = UINT64_C(0xffffffffa4000040);
    // 1fc00710: Jump to r3
    reg.gpr[31] = UINT64_C(0xffffffffa4001644);
    reg.pc = UINT64_C(0xffffffffa4000040);

    // Configure COP1 registers.
    cp1reg.setFprAliases(true);

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
