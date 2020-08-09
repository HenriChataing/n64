
#include <iomanip>
#include <iostream>
#include <fstream>

#include <fmt/ostream.h>

#include <assembly/disassembler.h>
#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <r4300/export.h>
#include <debugger.h>
#include <interpreter.h>

using namespace n64;

namespace R4300 {
/**
 * @brief Update the count register.
 *  The count register increments at half the CPU frequency.
 *  If the value of the Count register equals that of the Compare
 *  register, set the IP7 bit of the Cause register.
 */
void handleCounterEvent() {
    // Note: the interpreter being far from cycle exact,
    // the Count register value will necessary be inexact.

    ulong diff = (state.cycles - state.cp0reg.lastCounterUpdate) / 2;
    u32 untilCompare = state.cp0reg.compare - state.cp0reg.count;

    debugger::debug(Debugger::COP0, "counter event");
    debugger::debug(Debugger::COP0, "  count:{}", state.cp0reg.count);
    debugger::debug(Debugger::COP0, "  compare:{}", state.cp0reg.compare);
    debugger::debug(Debugger::COP0, "  cycles:{}", state.cycles);
    debugger::debug(Debugger::COP0, "  last_counter_update:{}", state.cp0reg.lastCounterUpdate);

    if (diff >= untilCompare) {
        state.cp0reg.cause |= CAUSE_IP7;
        checkInterrupt();
    } else {
        debugger::halt("Spurious counter event");
    }

    state.cp0reg.count = (u32)((ulong)state.cp0reg.count + diff);
    state.cp0reg.lastCounterUpdate = state.cycles;
    untilCompare = state.cp0reg.compare - state.cp0reg.count - 1;

    debugger::debug(Debugger::COP0, "  now:{}", state.cycles);
    debugger::debug(Debugger::COP0, "  trig:{}", state.cycles + 2 * (ulong)untilCompare);
    state.scheduleEvent(state.cycles + 2 * (ulong)untilCompare, handleCounterEvent);
}

/**
 * Called to reconfigure the counter event in the case either the Compare
 * of the Counter register is written.
 */
void scheduleCounterEvent() {
    ulong diff = (state.cycles - state.cp0reg.lastCounterUpdate) / 2;
    state.cp0reg.count = (u32)((ulong)state.cp0reg.count + diff);
    state.cp0reg.lastCounterUpdate = state.cycles;
    u32 untilCompare = state.cp0reg.compare - state.cp0reg.count;

    debugger::debug(Debugger::COP0, "scheduling counter event");
    debugger::debug(Debugger::COP0, "  now:{}", state.cycles);
    debugger::debug(Debugger::COP0, "  trig:{}", state.cycles + 2 * (ulong)untilCompare);
    state.cancelEvent(handleCounterEvent);
    state.scheduleEvent(state.cycles + 2 * (ulong)untilCompare, handleCounterEvent);
}

/**
 * @brief Configure the memory aliases for single and double word access
 *  to the floating point registers.
 */
void cp1reg::setFprAliases(bool fr) {
    if (fr) {
        for (unsigned int r = 0; r < 32; r++) {
            fpr_s[r] = (fpr_s_t *)&fpr[r];
            fpr_d[r] = (fpr_d_t *)&fpr[r];
        }
    } else {
        for (unsigned int r = 0; r < 32; r++) {
            fpr_s[r] = (fpr_s_t *)&fpr[r / 2] + r % 2;
            fpr_d[r] = (fpr_d_t *)&fpr[r / 2];
        }
    }
}

/**
 * @brief Check whether an interrupt exception is raised from the current state.
 *  Take the interrupt exception if this is the case.
 */
void checkInterrupt(void) {
    // For the interrupt to be taken, the interrupts must globally enabled
    // (IE = 1) and the particular interrupt must be unmasked (IM[irq] = 1).
    // Interrupt exceptions are also disabled during exception
    // handling (EXL = 1).
    if (!state.cp0reg.EXL() && state.cp0reg.IE() &&
        (state.cp0reg.IM() & state.cp0reg.IP())) {

        // Arrange for the interrupt to be taken at the following instruction :
        // The present instruction which enabled the interrupt must
        // not be repeated.
        //
        // Two cases here :
        // 1. called from instruction eval function,
        //    check next action to determine the following instruction.
        // 2. called from event handler. The result is the same, event
        //    handlers are always called before the instruction to execute
        //    is determined.
        switch (state.cpu.nextAction) {
        case State::Action::Continue:
            state.reg.pc += 4;
            state.cpu.delaySlot = false;
            break;

        case State::Action::Delay:
            state.reg.pc += 4;
            state.cpu.delaySlot = true;
            break;

        case State::Action::Jump:
            state.reg.pc = state.cpu.nextPc;
            state.cpu.delaySlot = false;
            break;
        }

        takeException(Interrupt, 0, false, false, 0);
    }
}

/**
 * @brief Set the selected interrupt pending bit in the Cause register.
 *  The Interrupt exception will be taken just before executing the next
 *  instruction if the conditions are met (see \ref checkInterrupt).
 *
 * @param irq           Interrupt number.
 */
void setInterruptPending(uint irq) {
    // Update the pending bits in the Cause register.
    state.cp0reg.cause |= CAUSE_IP(1lu << irq);
    checkInterrupt();
}

void clearInterruptPending(uint irq) {
    // Update the pending bits in the Cause register.
    state.cp0reg.cause &= ~CAUSE_IP(1lu << irq);
}

/**
 * @brief Raise an exception and update the state of the processor.
 *  The delay slot parameter is provided by the state member cpu.delaySlot.
 *
 * @param vAddr
 *      Virtual address being accessed. Required for AddressError,
 *      TLBRefill, XTLBRefill, TLBInvalid, TLBModified,
 *      VirtualCoherency exceptions.
 * @param instr
 *      Whether the exception was triggered by an instruction fetch.
 * @param load
 *      Whether the exception was triggered by a load or store operation.
 * @param ce
 *      Index of the coprocessor for CoprocessorUnusable exceptions.
 */
void takeException(Exception exn, u64 vAddr, bool instr, bool load, u32 ce)
{
    // Default vector valid for all general exceptions.
    u64 vector = 0x180u;
    u32 exccode = 0;

    // Following the diagrams printed in the following section of the
    // reference manual:
    //      5.4 Exception Handling and Servicing Flowcharts,

    // Code specific to each exception.
    // The exception code and vector is determined by the exception type.
    // Each exception may set various register fields.
    switch (exn) {
        // The Address Error exception occurs when an attempt is made to execute
        // one of the following:
        //      - load or store from/to an unaligned memory location.
        //          (e.g. LW from an address that is not aligned to a word)
        //      - reference the kernel address space from User or Supervisor
        //          mode
        //      - reference the supervisor address space from User mode
        case AddressError:
            exccode = load ? 4 : 5; // AdEL : AdES
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception AddressError({:08x},{})",
                vAddr, load);
            debugger::halt("AddressError");
            break;
        // TLB Refill occurs when there is no TLB entry that matches an
        // attempted reference to a mapped address space.
        case TLBRefill:
        case XTLBRefill:
            vector = exn == XTLBRefill ? 0x080llu : 0x000llu;
            /* fallthrough */
        // TLB Invalid occurs when a virtual address reference matches a
        // TLB entry that is marked invalid.
        case TLBInvalid:
            exccode = load ? 2 : 3; // TLBL : TLBS
            state.cp0reg.badvaddr = vAddr;
            state.cp0reg.entryhi &= ~0xffffffe000llu;
            state.cp0reg.entryhi |= (vAddr & 0xffffffe000llu);
            state.cp0reg.context &= (CONTEXT_PTEBASE_MASK << CONTEXT_PTEBASE_SHIFT);
            state.cp0reg.context |=
                ((vAddr >> 13) & CONTEXT_BADVPN2_MASK) << CONTEXT_BADVPN2_SHIFT;
            debugger::info(Debugger::CPU,
                "exception TLBRefill/TLBInvalid({:08x},{})",
                vAddr, load);
            // TODO : XContext
            break;
        // TLB Modified occurs when a store operation virtual address
        // reference to memory matches a TLB entry which is marked
        // valid but is not dirty (the entry is not writable).
        case TLBModified:
            exccode = 1; // Mod
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception TLBModified({:08x},{})", vAddr);
            debugger::halt("TLBModified");
            // TODO : Context, XContext, EntryHi
            break;
        // The Cache Error exception occurs when either a secondary cache ECC
        // error, primary cache parity error, or SysAD bus parity/ECC error
        // condition occurs and error detection is enabled.
        case CacheError:
            // vector = 0x100llu;
            debugger::info(Debugger::CPU, "exception CacheError");
            debugger::halt("CacheError");
            break;
        // A Virtual Coherency exception occurs when all of the following
        // conditions are true:
        //      - a primary cache miss hits in the secondary cache
        //      - bits 14:12 of the virtual address were not equal to the
        //        corresponding bits of the PIdx field of the secondary
        //        cache tag
        //      - the cache algorithm for the page (from the C field in the TLB)
        //        specifies that the page is cached
        case VirtualCoherency:
            exccode = instr ? 14 : 31; // VCEI : VCED
            state.cp0reg.badvaddr = vAddr;
            debugger::info(Debugger::CPU,
                "exception VirtualCoherency({:08x},{})",
                vAddr, instr);
            debugger::halt("VirtualCoherency");
            break;
        // A Bus Error exception is raised by board-level circuitry for events
        // such as bus time-out, backplane bus parity errors, and invalid
        // physical memory addresses or access types.
        case BusError:
            exccode = instr ? 6 : 7; // IBE : DBE
            debugger::info(Debugger::CPU, "exception BusError({})", instr);
            debugger::halt("BusError");
            break;
        // An Integer Overflow exception occurs when an ADD, ADDI, SUB, DADD,
        // DADDI or DSUB instruction results in a 2’s complement overflow
        case IntegerOverflow:
            exccode = 12; // Ov
            debugger::info(Debugger::CPU, "exception IntegerOverflow");
            debugger::halt("IntegerOverflow");
            break;
        // The Trap exception occurs when a TGE, TGEU, TLT, TLTU, TEQ, TNE,
        // TGEI, TGEUI, TLTI, TLTUI, TEQI, or TNEI instruction results in a TRUE
        // condition.
        case Trap:
            exccode = 13; // Tr
            debugger::info(Debugger::CPU, "exception Trap");
            debugger::halt("Trap");
            break;
        // A System Call exception occurs during an attempt to execute the
        // SYSCALL instruction.
        case SystemCall:
            exccode = 8; // Sys
            debugger::info(Debugger::CPU, "exception SystemCall");
            break;
        // A Breakpoint exception occurs when an attempt is made to execute the
        // BREAK instruction.
        case Breakpoint:
            exccode = 9; // Bp
            debugger::info(Debugger::CPU, "exception Breakpoint");
            debugger::halt("Breakpoint");
            break;
        // The Reserved Instruction exception occurs when one of the following
        // conditions occurs:
        //      - an attempt is made to execute an instruction with an undefined
        //        major opcode (bits 31:26)
        //      - an attempt is made to execute a SPECIAL instruction with an
        //        undefined minor opcode (bits 5:0)
        //      - an attempt is made to execute a REGIMM instruction with an
        //        undefined minor opcode (bits 20:16)
        //      - an attempt is made to execute 64-bit operations in 32-bit mode
        //        when in User or Supervisor mode
        case ReservedInstruction:
            exccode = 10; // RI
            debugger::info(Debugger::CPU, "exception ReservedInstruction");
            debugger::halt("ReservedInstruction");
            break;
        // The Coprocessor Unusable exception occurs when an attempt is made to
        // execute a coprocessor instruction for either:
        //      - a corresponding coprocessor unit that has not been marked
        //        usable, or
        //      - CP0 instructions, when the unit has not been marked usable
        //        and the process executes in either User or Supervisor mode.
        case CoprocessorUnusable:
            exccode = 11; // CpU
            debugger::info(Debugger::CPU, "exception CoprocessorUnusable({})", ce);
            break;
        // The Floating-Point exception is used by the floating-point
        // coprocessor.
        case FloatingPoint:
            exccode = 15; // FPEEXL
            debugger::info(Debugger::CPU, "exception FloatingPoint");
            debugger::halt("FloatingPoint");
            // TODO: Set FP Control Status Register
            break;
        // A Watch exception occurs when a load or store instruction references
        // the  physical address specified in the WatchLo/WatchHi System Control
        // Coprocessor (CP0) registers. The WatchLo register specifies whether a
        // load or store initiated this exception.
        case Watch:
            exccode = 23; // WATCH
            debugger::info(Debugger::CPU, "exception Watch");
            debugger::halt("Watch");
            // TODO: Set Watch register
            break;
        // The Interrupt exception occurs when one of the eight interrupt conditions
        // is asserted.
        case Interrupt:
            exccode = 0;
            debugger::info(Debugger::CPU, "exception Interrupt");
            break;
        default:
            debugger::halt("UndefinedException");
            break;
    }

    // Set Cause Register : EXCCode, CE
    state.cp0reg.cause &= ~(CAUSE_EXCCODE_MASK | CAUSE_CE_MASK);
    state.cp0reg.cause |= CAUSE_EXCCODE(exccode) | CAUSE_CE(ce);
    // Check if exception within exception.
    if (!state.cp0reg.EXL()) {
        // Check if the exception was caused by a delay slot instruction.
        // Set EPC and Cause:BD accordingly.
        if (state.cpu.delaySlot) {
            state.cp0reg.epc = state.reg.pc - 4;
            state.cp0reg.cause |= CAUSE_BD;
        } else {
            state.cp0reg.epc = state.reg.pc;
            state.cp0reg.cause &= ~CAUSE_BD;
        }
    } else {
        // The vector is forced to 0x180 even for TLB/XTLB Miss in this case.
        vector = 0x180llu;
    }
    // Processor forced to Kernel Mode
    // & interrupt disabled.
    state.cp0reg.sr |= STATUS_EXL;
    // Check if exceuting bootstrap code
    // and jump to the designated handler.
    u64 pc;
    if (state.cp0reg.BEV()) {
        pc = 0xffffffffbfc00200llu + vector;
    } else {
        pc = 0xffffffff80000000llu + vector;
    }

    state.cpu.nextAction = State::Action::Jump;
    state.cpu.nextPc = pc;
}

static std::map<u64, unsigned> blockStart;
static unsigned captureCount;
static bool captureRunning = false;
static u64  captureStart;
static struct cpureg captureCpuPre;
static struct cp0reg captureCp0Pre;
static struct cp1reg captureCp1Pre;

void startCapture(void) {
    if (captureCount > 1000) {
        return;
    }
    if (blockStart.find(state.reg.pc) == blockStart.end()) {
        blockStart[state.reg.pc] = 1;
        return;
    }

    unsigned count = blockStart[state.reg.pc] + 1;
    blockStart[state.reg.pc] = count;
    if (count < 1000 || count > 1500 || (count % 100)) {
        return;
    }

    debugger::warn(Debugger::CPU, "starting capture for address {:x}",
        state.reg.pc);

    captureRunning = true;
    captureStart = state.reg.pc;
    captureCpuPre = state.reg;
    captureCp0Pre = state.cp0reg;
    captureCp1Pre = state.cp1reg;
    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    bus->capture(true);
}

void stopCapture(u64 finalAddress) {
    if (!captureRunning)
        return;

    Memory::LoggingBus *bus = dynamic_cast<Memory::LoggingBus *>(state.bus);
    std::string filename =
        fmt::format("test/recompiler/test_{:08x}.toml",
            captureStart & 0xfffffffflu);
    std::string filename_pre =
        fmt::format("test/recompiler/test_{:08x}.input",
            captureStart & 0xfffffffflu);
    std::string filename_post =
        fmt::format("test/recompiler/test_{:08x}.output",
            captureStart & 0xfffffffflu);

    std::ofstream ofs, prefs, postfs;
    bool exists;

    // Check whether the Toml file recording test data already exists
    // (to decide whether to append the code binary at all).
    ofs.open(filename, std::ios::in);
    exists = ofs.good();
    ofs.close();

    // Reopen the Toml file with correction permissions, open binary
    // input and output files.
    ofs.open(filename, std::ios::app);
    prefs.open(filename_pre, std::ios::binary | std::ios::app);
    postfs.open(filename_post, std::ios::binary | std::ios::app);

    debugger::warn(Debugger::CPU, "saving capture for address {:x}",
        captureStart);

    if (ofs.bad() || prefs.bad() || postfs.bad()) {
        debugger::error(Debugger::CPU, "cannot open capture files\n");
        debugger::halt("failed to open capture files");
        return;
    }

    if (!exists) {
        fmt::print(ofs, "start_address = \"0x{:016x}\"\n\n", captureStart);

        std::string asm_code;
        std::string bin_code;

        u64 address = captureStart;
        unsigned count = 0;
        for (Memory::BusLog entry: bus->log) {
            debugger::warn(Debugger::CPU,
                "  {}_{}(0x{:x}, 0x{:x})",
                entry.access == Memory::BusAccess::Load ? "load" : "store",
                entry.bytes * 8, entry.address, entry.value);

            if (entry.access == Memory::BusAccess::Load && entry.bytes == 4 &&
                (entry.address & 0xffffffflu) == (address & 0xffffffflu)) {
                if ((count % 4) == 0) bin_code += "\n   ";
                bin_code += fmt::format(" 0x{:08x},", entry.value);
                asm_code += "    " + assembly::cpu::disassemble(address, entry.value) + "\n";
                address += 4;
                count++;
            }
        }
        if (address == state.reg.pc) {
            // Missing instruction fetch for the suppressed delay instruction
            // of a branch likely.
            u32 instr;
            u64 phys_address;
            translateAddress(address, &phys_address, false);
            bus->load_u32(address, &instr);
            if ((count % 4) == 0) bin_code += "\n   ";
            bin_code += fmt::format(" 0x{:08x},", instr);
            asm_code += "    " + assembly::cpu::disassemble(address, instr) + "\n";
            address += 4;
            count++;
        }
        if (address != (state.reg.pc + 4)) {
            debugger::warn(Debugger::CPU,
                "incomplete memory trace: missing instruction fetches {}/{}/{}",
                count, bus->log.size(), state.reg.pc - captureStart + 4);
            debugger::halt(
                "incomplete memory trace: missing instruction fetches");
        }

        fmt::print(ofs, "asm_code = \"\"\"\n{}\"\"\"\n\n", asm_code);
        fmt::print(ofs, "bin_code = [{}\n]\n\n", bin_code);
    }

    fmt::print(ofs, "[[test]]\n");
    fmt::print(ofs, "end_address = \"0x{:016x}\"\n", finalAddress);
    fmt::print(ofs, "trace = [\n");
    u64 address = captureStart;
    for (Memory::BusLog entry: bus->log) {
        if (entry.access == Memory::BusAccess::Load && entry.bytes == 4 &&
            (entry.address & 0xffffffflu) == (address & 0xffffffflu)) {
            address += 4;
        } else {
            fmt::print(ofs,
                "    {{ type = \"{}_u{}\", address = \"0x{:08x}\", value = \"0x{:x}\" }},\n",
                entry.access == Memory::BusAccess::Load ? "load" : "store",
                entry.bytes * 8, entry.address, entry.value);
        }
    }
    fmt::print(ofs, "]\n\n");

    serialize(prefs, captureCpuPre);
    serialize(prefs, captureCp0Pre);
    serialize(prefs, captureCp1Pre);

    serialize(postfs, state.reg);
    serialize(postfs, state.cp0reg);
    serialize(postfs, state.cp1reg);

    ofs.close();
    prefs.close();
    postfs.close();

    bus->capture(false);
    bus->clear();
    captureRunning = false;
    captureCount++;
}

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
void step()
{
    if (state.cycles >= state.cpu.nextEvent) {
        state.handleEvent();
    }

    switch (state.cpu.nextAction) {
        case State::Action::Continue:
            state.reg.pc += 4;
            state.cpu.delaySlot = false;
            interpreter::cpu::eval();
            break;

        case State::Action::Delay:
            state.reg.pc += 4;
            state.cpu.nextAction = State::Action::Jump;
            state.cpu.delaySlot = true;
            interpreter::cpu::eval();
            break;

        case State::Action::Jump:
            stopCapture(state.cpu.nextPc);
            state.reg.pc = state.cpu.nextPc;
            state.cpu.nextAction = State::Action::Continue;
            state.cpu.delaySlot = false;
            startCapture();
            interpreter::cpu::eval();
            break;
    }
}

}; /* namespace R4300 */
