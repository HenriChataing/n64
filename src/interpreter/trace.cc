
#include <fstream>
#include <string>

#include <fmt/ostream.h>

#include <assembly/disassembler.h>
#include <interpreter/interpreter.h>
#include <r4300/state.h>
#include <r4300/export.h>
#include <debugger.h>

using namespace n64;
using namespace R4300;

namespace interpreter::cpu {

static std::map<u64, unsigned> blockStart;
static unsigned captureCount;
static bool captureRunning = false;
static u64  captureStart;
static struct cpureg captureCpuPre;
static struct cp0reg captureCp0Pre;
static struct cp1reg captureCp1Pre;

void start_capture(void) {
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

void stop_capture(u64 finalAddress) {
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

}; /* namespace interpreter::cpu */
