
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ncurses.h>
#include <vector>
#include <map>
#include <list>
#include <string>

#include <mips/asm.h>
#include <r4300/cpu.h>
#include <r4300/eval.h>
#include <r4300/hw.h>
#include <memory.h>

#if 0

void printRegisters()
{
    mvprintw(3, 2, "%08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32,
        (uint32_t)R4300::state.reg.pc,
        0, 0, 0);
    for (int i = 0; i < 8; i++)
        mvprintw(5 + i, 2,
            "%08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32,
            (uint32_t)R4300::state.reg.gpr[4 * i],
            (uint32_t)R4300::state.reg.gpr[4 * i + 1],
            (uint32_t)R4300::state.reg.gpr[4 * i + 2],
            (uint32_t)R4300::state.reg.gpr[4 * i + 3]);
}

void terminal()
{
    initscr();          /* Init curses. */
    noecho();           /* No echoing input. */
    raw();              /* No line buffering. */
    curs_set(0);        /* Hide the cursor. */
    start_color();      /* Activate coloring. */
    init_pair(1, COLOR_BLACK, COLOR_YELLOW);

    /* Initialise the terminal. */
    mvaddstr(2, 2, "PC       LR       SP       FP");
    mvaddstr(4, 2, "GPR");
    printRegisters();

    try {
        R4300::state.boot();
        for (;;) {
            int c = getch();
            if (c == 'q')
                goto quit;

            R4300::Eval::step();
            printRegisters();
        }
    } catch (const char *exn) {
        std::cerr << "Caught exception '" << exn << "'" << std::endl;
        std::cerr << R4300::state << std::endl;
    }

quit:
    endwin();
}

#endif

/*
PC       LR       SP       FP
00000000 00000000 00000000 00000000
GPR
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
*/

class Shell;

typedef bool (*Command)(Shell &sh, std::vector<char *> &args);

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
    void execute();

    std::vector<u64> breakpoints;
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
            std::cerr << "duplicate command '" << name << "'" << std::endl;
            return;
        }
        if (it->first < name)
            break;
    }
    commands.insert(it, std::pair<std::string, Command>(name, callback));
}

void Shell::start()
{
#if 0
    initscr();          /* Init curses. */
    noecho();           /* No echoing input. */
    raw();              /* No line buffering. */
    curs_set(0);        /* Hide the cursor. */
    start_color();      /* Activate coloring. */
    init_pair(1, COLOR_BLACK, COLOR_YELLOW);

    /* Initialise the terminal. */
    mvaddstr(2, 2, "PC       LR       SP       FP");
    mvaddstr(4, 2, "GPR");
    printRegisters();

    try {
        R4300::state.boot();
        for (;;) {
            int c = getch();
            if (c == 'q')
                goto quit;

            R4300::Eval::step();
            printRegisters();
        }
    } catch (const char *exn) {
        std::cerr << "Caught exception '" << exn << "'" << std::endl;
        std::cerr << R4300::state << std::endl;
    }

quit:
    endwin();
#endif
    std::cout << std::endl;
    for (;;) {
        char cmd[256];
        std::cout << "> ";
        std::cin.getline(cmd, 256);

        char *tmp = strtok(cmd, " ");
        if (tmp == NULL)
            continue;

        std::string cstr(tmp);
        auto it = commands.begin();
        Command c;

        for (; it != commands.end(); it++) {
            if (it->first == cstr) {
                c = it->second;
                break;
            }
        }
        if (it == commands.end()) {
            std::cout << "unknown command name '" << cstr << "'" << std::endl;
            continue;
        }

        std::vector<char *> args;
        tmp = strtok(NULL, " ");
        while (tmp != NULL) {
            args.push_back(tmp);
            tmp = strtok(NULL, " ");
        }

        if (c(*this, args))
            break;
    }
}


bool printHelp(Shell &sh, std::vector<char *> &args)
{
    std::cout << "help" << std::endl;
    return false;
}

bool printRegisters(Shell &sh, std::vector<char *> &args)
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

bool printCop0Registers(Shell &sh, std::vector<char *> &args)
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

bool printTLB(Shell &sh, std::vector<char *> &args)
{
    using namespace std;

    for (uint i = 0; i < R4300::tlbEntryCount; i++) {
        R4300::tlbEntry &entry = R4300::state.tlb[i];

#if 1
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
#endif
    }
    return false;
}

bool doQuit(Shell &sh, std::vector<char *> &args)
{
    return true;
}

bool doStep(Shell &sh, std::vector<char *> &args)
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

bool doContinue(Shell &sh, std::vector<char *> &args)
{
    if (sh.abort)
        return false;
    try {
        for (;;) {
            R4300::Eval::step();
            for (size_t i = 0; i < sh.breakpoints.size(); i++) {
                if (sh.breakpoints[i] == R4300::state.reg.pc) {
                    R4300::Eval::hist();
                    std::cerr << "halting at breakpoint #" << i << ": ";
                    std::cerr << std::hex << R4300::state.reg.pc << std::endl;
                    return false;
                }
            }
        }
    } catch (const char *exn) {
        R4300::Eval::hist();
        std::cout << "caught exception '" << exn << "'" << std::endl;
        sh.abort = true;
    }
    return false;
}

bool addBreakpoint(Shell &sh, std::vector<char *> &args)
{
    if (args.size() < 1) {
        std::cerr << "missing breakpoint argument" << std::endl;
        return false;
    }
    u64 br = strtoull(args[0], NULL, 0);
    if (errno != 0) {
        std::cerr << "invalid breakpoint argument" << std::endl;
        return false;
    }
    if (br & 0x80000000)
        br |= 0xffffffff00000000;
    for (size_t i = 0; i < sh.breakpoints.size(); i++) {
        if (sh.breakpoints[i] == br) {
            std::cerr << "the breakpoint ";
            std::cerr << std::hex << br;
            std::cerr << " is already set" << std::endl;
            return false;
        }
    }
    sh.breakpoints.push_back(br);
    std::cout << "breakpoint #" << (sh.breakpoints.size() - 1) << ": ";
    std::cerr << std::hex << br << std::endl;
    return false;
}

bool doDump(Shell &sh, std::vector<char *> &args)
{
    size_t count = 16;
    if (args.size() > 0) {
        count = strtoull(args[0], NULL, 0);
        if (errno != 0) {
            std::cerr << "invalid dump argument" << std::endl;
            return false;
        }
    }

    u64 pAddr;

    R4300::translateAddress(R4300::state.reg.pc, &pAddr, 0);

    std::cerr << std::hex;
    for (size_t i = 0; i < count; i++, pAddr += 4) {
        u64 val;
        R4300::physmem.load(4, pAddr, &val);
        std::cerr << std::setw(8) << val << std::endl;
    }
    return false;
}

void terminal()
{
    Shell sh;
    sh.config("help", printHelp);
    sh.config("q", doQuit);
    sh.config("quit", doQuit);
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
    sh.config("d", doDump);
    sh.config("disas", doDump);

    R4300::state.boot();

    sh.start();
}
