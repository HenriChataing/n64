
#include <iostream>

#include <r4300/hw.h>
#include <memory.h>
#include <core.h>
#include <rsp/server.h>

void terminal();

int main(int argc, char *argv[])
{
    if (argc < 2)
        return -1;
    R4300::init(argv[1]);
    R4300::physmem.root->print();
    RSP::startServer();
    // Core::emulate();
    terminal();
    return 0;
}
