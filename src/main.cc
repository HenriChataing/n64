
#include <iostream>

#include <memory.h>
#include <core.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
        return -1;
    Memory::loadRom(argv[1]);
    Core::emulate();
    return 0;
}
