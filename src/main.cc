
#include <iostream>

#include <r4300/hw.h>
#include <memory.h>
#include <core.h>

void terminal();

int main(int argc, char *argv[])
{
    if (argc < 2)
        return -1;
    R4300::init(argv[1]);
    R4300::physmem.root->print();
    // Core::emulate();
    terminal();
    return 0;
}
