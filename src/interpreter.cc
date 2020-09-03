
#include <iomanip>
#include <iostream>

#include "debugger.h"

void Debugger::step() {
    if (halted) {
        R4300::step();
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
                _interpreterCondition.wait(lock,
                    [this] { return !halted || _interpreterStopped; });
            }

            while (!halted) {
                R4300::step();
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
