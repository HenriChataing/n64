
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {

extern Coprocessor cop0;
extern Coprocessor cop1;

Coprocessor *cop[4] = {
    &cop0,
    &cop1,
    new Coprocessor(),
    new Coprocessor(),
};

};
