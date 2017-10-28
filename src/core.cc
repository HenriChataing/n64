
#include <iostream>
#include <ctime>
#include <chrono>
#include <thread>

#include "core.h"
#include "r4300/cpu.h"
#include "r4300/eval.h"

using namespace std;

namespace Core {

void emulate()
{
    try {
        R4300::state.boot();
        R4300::Eval::run();
    } catch (const char *exn) {
        cerr << "Caught exception '" << exn << "'" << endl;
    }
}

};
