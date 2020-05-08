
#include <iomanip>
#include <iostream>

#include "debugger.h"

/** @brief Global debugger. */
Debugger debugger::debugger;

char const *debugger::LabelName[Debugger::Label::LabelCount] = {
    "cpu",
    "cop0",
    "cop1",
    "rsp",
    "rdp",
    "RdRam",
    "SP",
    "DPCommand",
    "DPSpan",
    "MI",
    "VI",
    "AI",
    "PI",
    "RI",
    "SI",
    "PIF",
    "Cart_2_1",
};

char const *debugger::LabelColor[Debugger::LabelCount] = {
    "\x1b[34;1m",
    "\x1b[34;1m",
    "\x1b[34;1m",
    "\x1b[34;1m",
    "\x1b[93;1m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
    "\x1b[0m",
};

/* Class */

Debugger::Debugger()
    : halted(true), haltedReason("Reset"),
      cpuTrace(0x10000), rspTrace(0x10000)
{
    _interpreterStopped = false;
    for (int label = 0; label < Debugger::LabelCount; label++) {
        verbose[label] = Debugger::Verbosity::Error;
    }

    verbose[Debugger::RDP] = Debugger::Info;
}

Debugger::~Debugger() {
}

/* Interpreter */

void Debugger::halt(std::string reason) {
    if (!halted) {
        haltedReason = reason;
        halted = true;
    }
}

void Debugger::step() {
    if (halted) {
        R4300::Eval::step();
        R4300::RSP::step();
    }
}

void Debugger::resume() {
    if (_interpreterThread) {
        halted = false;
        _interpreterCondition.notify_one();
    }
}

void Debugger::interpreterRoutine() {
    try {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(_interpreterMutex);
                std::cout << "interpreter thread pending" << std::endl;
                _interpreterCondition.wait(lock,
                    [this] { return !halted || _interpreterStopped; });
            }

            while (!halted) {
                R4300::Eval::step();
                R4300::RSP::step();
            }

            if (_interpreterStopped) {
                std::cout << "interpreter thread exiting" << std::endl;
                return;
            }
        }
    } catch (const char *exn) {
        std::cout << "caught exception '" << exn << "'" << std::endl;
        std::cout << "interpreter thread exiting" << std::endl;
    }
}

void Debugger::startInterpreter() {
    if (!_interpreterThread) {
        _interpreterThread =
            new std::thread(&Debugger::interpreterRoutine, this);
    }
}

void Debugger::stopInterpreter() {
    if (_interpreterThread) {
        _interpreterStopped = true;
        _interpreterCondition.notify_one();
        _interpreterThread->join();
        delete _interpreterThread;
        _interpreterThread = NULL;
    }
}

/* Breakpoints */

void Debugger::setBreakpoint(u64 addr) {
    if (_breakpoints.find(addr) == _breakpoints.end()) {
        Breakpoint* bp = new Breakpoint(addr);
        _breakpoints[addr] = std::unique_ptr<Breakpoint>(bp);
    }
}

void Debugger::unsetBreakpoint(u64 addr) {
    _breakpoints.erase(addr);
}

bool Debugger::checkBreakpoint(u64 addr) {
    return _breakpoints.find(addr) != _breakpoints.end();
}
