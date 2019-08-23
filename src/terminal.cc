
#include <cinttypes>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <list>
#include <string>

#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/eval.h>
#include <r4300/hw.h>
#include <memory.h>
#include <debugger.h>

class Shell;

typedef bool (*Command)(Shell &sh, std::vector<std::string> &args);
typedef bool (*Callback)();

static bool interrupted;

void signalHandler(int signum)
{
    std::cout << "Received signal " << std::dec << signum << std::endl;
    interrupted = true;
}

class Shell
{
public:
    Shell() : abort(false) {}
    ~Shell() {}

    void config(const std::string &name, Command callabck);
    void config(const char *name, Command callback) {
        config(std::string(name), callback);
    }
    void start();
    void autocomplete();
    void trace(u64 vAddr, Callback callback);
    bool execute(std::string cmd);

    std::vector<u64> breakpoints;
    std::vector<std::pair<u64, Callback>> traces;
    std::vector<std::pair<u64, u64>> addresses;
    bool abort;

private:
    std::list<std::pair<std::string, Command>> commands;
    std::list<std::string> history;
};

void Shell::config(const std::string &name, Command callback)
{
    auto it = commands.begin();
    for (; it != commands.end(); it++) {
        if (it->first == name) {
            std::cout << "duplicate command '" << name << "'" << std::endl;
            return;
        }
        if (it->first < name)
            break;
    }
    commands.insert(it, std::pair<std::string, Command>(name, callback));
}

void Shell::start()
{
    std::cout << std::endl;
    for (;;) {
        std::string cmd;
        std::cout << "> ";

        while (cmd.length() == 0) {
            std::getline(std::cin, cmd);
        }

        try {
            if (execute(cmd))
                break;
        } catch (const char *exn) {
            std::cerr << "command '" << cmd << "' failed with exception '";
            std::cerr << exn << "'" << std::endl;
            break;
        }
    }
}

void Shell::trace(u64 vAddr, Callback callback)
{
    if (vAddr & 0x80000000)
        vAddr |= 0xffffffff00000000;
    traces.push_back(std::pair<u64, Callback>(vAddr, callback));
}

bool Shell::execute(std::string cmd)
{
    std::istringstream scmd(cmd);
    std::string token;

    if (!std::getline(scmd, token, ' '))
        return false;

    // comment line
    if (token[0] == '#')
        return false;

    auto it = commands.begin();
    Command c;

    for (; it != commands.end(); it++) {
        if (it->first == token) {
            c = it->second;
            break;
        }
    }
    if (it == commands.end()) {
        std::cout << "unknown command name '" << token << "'" << std::endl;
        return false;
    }

    std::vector<std::string> args;
    while (std::getline(scmd, token, ' ')) {
        args.push_back(token);
    }

    return c(*this, args);
}


bool printHelp(Shell &sh, std::vector<std::string> &args)
{
    std::cout << "help" << std::endl;
    return false;
}

bool printRegisters(Shell &sh, std::vector<std::string> &args)
{
    using namespace std;
    cout << setw(6) << setfill(' ') << left << "pc";
    cout << setw(16) << setfill('0') << right << hex;
    cout << R4300::state.reg.pc << endl;

    for (int i = 0; i < 32; i ++) {
        if (i && !(i % 4))
            cout << endl;

        u64 reg = R4300::state.reg.gpr[i];
        cout << setw(6) << setfill(' ') << left << Mips::getRegisterName(i);
        cout << setw(16) << setfill('0') << right << hex << reg << "    ";
    }
    cout << endl;
    return false;
}

bool printCop0Registers(Shell &sh, std::vector<std::string> &args)
{
    using namespace std;

#define PrintReg(name) \
    cout << setw(10) << setfill(' ') << left << #name; \
    cout << setw(16) << left << hex << R4300::state.cp0reg.name

#define Print2Regs(name0, name1) \
    PrintReg(name0); cout << " "; PrintReg(name1); cout << endl

    Print2Regs(index, random);
    Print2Regs(entryLo0, entryLo1);
    Print2Regs(context, pageMask);
    Print2Regs(wired, badVAddr);
    Print2Regs(count, entryHi);
    Print2Regs(compare, sr);
    Print2Regs(cause, epc);
    Print2Regs(prId, config);
    Print2Regs(llAddr, watchLo);
    Print2Regs(watchHi, xContext);
    Print2Regs(pErr, cacheErr);
    Print2Regs(tagLo, tagHi);
    PrintReg(errorEpc);
    cout << endl;

#undef PrintReg
#undef Print2Regs

    return false;
}

bool printTLB(Shell &sh, std::vector<std::string> &args)
{
    using namespace std;

    for (uint i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];

        // Ignore this entry if no mapping is valid.
        if ((entry.entryLo0 & 2llu) == 0 && (entry.entryLo1 & 2llu) == 0)
            continue;

        // Print region
        uint region = entry.entryHi >> 62;
        switch (region) {
            case 0: cout << "U "; break;
            case 1: cout << "S "; break;
            case 3: cout << "K "; break;
            default: cout << "- "; break;
        }

        // Print asid
        cout << setfill('0');
        cout << hex << setw(2) << right << entry.asid;
        // Print flags
        cout << (entry.global ? " G " : " - ");
        // Print VPN
        u64 pageMask = ~entry.pageMask & 0xfffffe000llu;
        cout << hex << setw(16) << right << (entry.entryHi & pageMask) << " -> ";
        // Print even PFN
        cout << ((entry.entryLo0 & 4llu) ? "D" : "-");
        cout << ((entry.entryLo0 & 2llu) ? "V " : "- ");
        cout << hex << setw(9) << right << ((entry.entryLo0 << 6) & 0xffffff000llu);
        cout << endl;
        cout << "      " << setw(16) << right << pageMask << "    ";
        cout << ((entry.entryLo1 & 4llu) ? "D" : "-");
        cout << ((entry.entryLo1 & 2llu) ? "V " : "- ");
        cout << hex << setw(9) << right << ((entry.entryLo1 << 6) & 0xffffff000llu);
        cout << endl;
    }
    return false;
}

bool printBacktrace(Shell &sh, std::vector<std::string> &args)
{
    debugger.backtrace(R4300::state.reg.pc);
    return false;
}

bool doQuit(Shell &sh, std::vector<std::string> &args)
{
    return true;
}

bool doLoad(Shell &sh, std::vector<std::string> &args)
{
    if (args.size() < 1) {
        std::cout << "missing load argument" << std::endl;
        return false;
    }

    std::ifstream file(args[0]);

    if (!file) {
        std::cout << "failed to open file " << args[0] << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() == 0)
            continue;
        std::cout << "> " << line << std::endl;
        sh.execute(line);
    }

    file.close();
    return false;
}

bool doStep(Shell &sh, std::vector<std::string> &args)
{
    if (sh.abort)
        return false;
    try {
        R4300::Eval::step();
        R4300::Eval::hist();
    } catch (const char *exn) {
        std::cout << "caught exception '" << exn << "'" << std::endl;
        sh.abort = true;
    }
    return false;
}

bool doContinue(Shell &sh, std::vector<std::string> &args)
{
    if (sh.abort)
        return false;
    interrupted = false;
    try {
        for (;;) {
            // Advance one step.
            if (R4300::Eval::step()) {
                R4300::Eval::hist();
                std::cout << "halting at exception" << std::endl;
                return false;
            }

            // Check traces.
            for (size_t i = 0; i < sh.traces.size(); i++) {
                if (sh.traces[i].first == R4300::state.reg.pc) {
                    if (sh.traces[i].second())
                        return false;
                }
            }

            // Check breakpoints.
            for (size_t i = 0; i < sh.breakpoints.size(); i++) {
                if (sh.breakpoints[i] == R4300::state.reg.pc) {
                    R4300::Eval::hist();
                    std::cout << "halting at breakpoint #" << i << ": ";
                    std::cout << std::hex << R4300::state.reg.pc << std::endl;
                    return false;
                }
            }

            // Check watched addresses.
            bool modified = false;
            for (size_t i = 0; i < sh.addresses.size(); i++) {
                u64 val = 0;
                R4300::physmem.load(4, sh.addresses[i].first, &val);
                if (val != sh.addresses[i].second) {
                    R4300::Eval::hist();
                    std::cout << "watched address 0x";
                    std::cout << std::hex << sh.addresses[i].first;
                    std::cout << " modified : 0x";
                    std::cout << sh.addresses[i].second << " -> 0x" << val;
                    std::cout << std::endl;
                    sh.addresses[i].second = val;
                    modified = true;
                }
            }

            if (modified)
                return false;

            // Check interrupt signal.
            if (interrupted)
                return false;
        }
    } catch (const char *exn) {
        R4300::Eval::hist();
        std::cout << "caught exception '" << exn << "'" << std::endl;
        sh.abort = true;
    }
    return false;
}

bool addBreakpoint(Shell &sh, std::vector<std::string> &args)
{
    if (args.size() < 1) {
        std::cout << "missing breakpoint argument" << std::endl;
        return false;
    }
    u64 br = strtoull(args[0].c_str(), NULL, 0);
    if (errno != 0) {
        std::cout << "invalid breakpoint argument" << std::endl;
        return false;
    }
    if (br & 0x80000000)
        br |= 0xffffffff00000000;
    for (size_t i = 0; i < sh.breakpoints.size(); i++) {
        if (sh.breakpoints[i] == br) {
            std::cout << "the breakpoint ";
            std::cout << std::hex << br;
            std::cout << " is already set" << std::endl;
            return false;
        }
    }
    sh.breakpoints.push_back(br);
    std::cout << "breakpoint #" << (sh.breakpoints.size() - 1) << ": ";
    std::cout << std::hex << br << std::endl;
    return false;
}

bool watchAddress(Shell &sh, std::vector<std::string> &args)
{
    if (args.size() < 1) {
        if (sh.addresses.size() == 0) {
            std::cout << "no currently watched addresses" << std::endl;
        } else {
            std::cout << "watched addresses:" << std::endl;
            for (size_t i = 0; i < sh.addresses.size(); i++) {
                std::cout << "#" << std::dec << i << "  ";
                std::cout << std::hex << sh.addresses[i].first << std::endl;
            }
        }
        return false;
    }
    u64 phys = strtoull(args[0].c_str(), NULL, 0);
    if (errno != 0) {
        std::cout << "invalid breakpoint argument" << std::endl;
        return false;
    }
    if (phys & 0x80000000)
        phys |= 0xffffffff00000000;

    u64 init = 0;
    R4300::physmem.load(4, phys, &init);
    sh.addresses.push_back(std::pair<u64, u64>(phys, init));

    return false;
}

bool doDisas(Shell &sh, std::vector<std::string> &args)
{
    u64 vAddr = R4300::state.reg.pc, pAddr, instr;
    size_t count = 16;

    if (args.size() > 0) {
        count = strtoull(args[0].c_str(), NULL, 0);
        if (errno != 0) {
            std::cout << "invalid disas argument" << std::endl;
            return false;
        }
    }
    if (args.size() > 1) {
        vAddr = strtoull(args[1].c_str(), NULL, 0);
        if (errno != 0) {
            std::cout << "invalid disas argument" << std::endl;
            return false;
        }
        if (vAddr & 0x80000000llu)
            vAddr |= 0xffffffff00000000llu;
    }

    R4300::translateAddress(vAddr, &pAddr, 0);

    interrupted = false;
    for (size_t i = 0; i < count; i++, pAddr += 4, vAddr += 4) {
        if (interrupted)
            return false;
        R4300::physmem.load(4, pAddr, &instr);
        std::cout << std::hex << std::setfill(' ') << std::right;
        std::cout << std::setw(16) << vAddr << "    ";
        std::cout << std::hex << std::setfill('0');
        std::cout << std::setw(8) << instr << "    ";
        std::cout << std::setfill(' ');
        Mips::disas(vAddr, instr);
        std::cout << std::endl;
    }

    return false;
}

bool doPrint(Shell &sh, std::vector<std::string> &args)
{
    if (args.size() < 1) {
        std::cout << "missing print argument" << std::endl;
        return false;
    }
    u64 phys = strtoull(args[0].c_str(), NULL, 0);
    if (errno != 0) {
        std::cout << "invalid print argument" << std::endl;
        return false;
    }
    size_t count = 16;
    if (args.size() > 1) {
        count = strtoull(args[1].c_str(), NULL, 0);
        if (errno != 0) {
            std::cout << "invalid print argument" << std::endl;
            return false;
        }
    }
    if (count == 0)
        return false;

    for (size_t i = 0; i < count; i++, phys += 4) {
        u64 value;
        R4300::physmem.load(4, phys, &value);
        if (!(i % 4)) {
            if (i)
                std::cout << std::endl;
            std::cout << std::hex << std::setfill(' ') << std::right;
            std::cout << std::setw(16) << phys << "   ";
        }
        std::cout << std::hex << std::setfill('0');
        std::cout << std::setw(8) << value << "    ";
        std::cout << std::setfill(' ');
    }
    std::cout << std::endl;

    return false;
}

bool log_osCreateThread()
{
    std::cerr << "\x1b[31;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;
    u32 entry = R4300::state.reg.gpr[6];

    u64 priorityPaddr;
    u64 priority = 0;

    R4300::translateAddress(R4300::state.reg.gpr[29] + 0x14, &priorityPaddr, 0);
    R4300::physmem.load(4, priorityPaddr, &priority);

    std::cerr << "osCreateThread(&_thread_" << ptr << ", " << entry;
    std::cerr << ", " << priority << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osStartThread()
{
    std::cerr << "\x1b[31;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;

    std::cerr << "osStartThread(&_thread_" << ptr << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osSetThreadPri()
{
    std::cerr << "\x1b[31;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;

    std::cerr << "osSetThreadPri(&_thread_" << ptr;
    std::cerr << ", " << R4300::state.reg.gpr[5] << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osYieldThread()
{
    std::cerr << "\x1b[31;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;

    std::cerr << "osYieldThread(&_queue_" << ptr << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osRunThread()
{
    std::cerr << "\x1b[31;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[2] & 0xffffff;
    debugger.runThread(ptr);

    std::cerr << "osRunThread(&_thread_" << ptr << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osDestroyThread()
{
    return false;
}

bool log_osSendMessage()
{
    std::cerr << "\x1b[32;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;

    std::cerr << "osSendMessage(&_mqueue_" << ptr << ")" << "\x1b[0m" << std::endl;

    return false;
}

bool log_osWaitMessage()
{
    std::cerr << "\x1b[32;1m" << std::hex; // red

    u32 ptr = R4300::state.reg.gpr[4] & 0xffffff;

    std::cerr << "osWaitMessage(&_mqueue_" << ptr << ")";
    std::cerr << " @ " << std::hex << (R4300::state.reg.gpr[31] & 0xfffffflu);
    std::cerr << "\x1b[0m" << std::endl;

    return false;
}

void terminal()
{
    Shell sh;
    sh.config("help", printHelp);
    sh.config("q", doQuit);
    sh.config("quit", doQuit);
    sh.config("l", doLoad);
    sh.config("load", doLoad);
    sh.config("regs", printRegisters);
    sh.config("registers", printRegisters);
    sh.config("cp0", printCop0Registers);
    sh.config("cop0", printCop0Registers);
    sh.config("cp0regs", printCop0Registers);
    sh.config("cop0regs", printCop0Registers);
    sh.config("tlb", printTLB);
    sh.config("s", doStep);
    sh.config("step", doStep);
    sh.config("c", doContinue);
    sh.config("continue", doContinue);
    sh.config("br", addBreakpoint);
    sh.config("break", addBreakpoint);
    sh.config("bt", printBacktrace);
    sh.config("backtrace", printBacktrace);
    sh.config("d", doDisas);
    sh.config("disas", doDisas);
    sh.config("p", doPrint);
    sh.config("print", doPrint);
    sh.config("w", watchAddress);
    sh.config("watch", watchAddress);

    debugger.addSymbol(0xffffffff80304fc0, "intrDisable");
    debugger.addSymbol(0xffffffff80304fe0, "intrEnable");
    debugger.addSymbol(0xffffffff803016d0, "osCreateThread");
    debugger.addSymbol(0xffffffff8030655c, "osPushThread");
    debugger.addSymbol(0xffffffff803065a4, "osPopThread");
    debugger.addSymbol(0xffffffff80301820, "osStartThread");
    debugger.addSymbol(0xffffffff8030645c, "osYieldThread");
    debugger.addSymbol(0xffffffff803065b4, "osRunThread");
    debugger.addSymbol(0xffffffff80301e80, "osSendMessage");
    debugger.addSymbol(0xffffffff80301500, "osWaitMessage");

    sh.trace(0x803016d0, log_osCreateThread);
    sh.trace(0x80301820, log_osStartThread);
    sh.trace(0x80302770, log_osSetThreadPri);
    sh.trace(0x8030645c, log_osYieldThread);
    sh.trace(0x803065cc, log_osRunThread);
    // sh.trace(, log_osDestroyThread);
    sh.trace(0x80301e80, log_osSendMessage);
    sh.trace(0x80301500, log_osWaitMessage);

    R4300::state.boot();

    signal(SIGINT, signalHandler);
    sh.start();
}
