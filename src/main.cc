
#include <iostream>

#include <r4300/hw.h>
#include <memory.h>
#include <core.h>
#include <rsp/server.h>

void terminal();

int main(int argc, char *argv[])
{
    char const *path;
    if (argc < 2)
        path = "SuperMario64_JA_A.z64";
    else
        path = argv[1];

    R4300::init(path);
    R4300::physmem.root->print();
    RSP::startServer();
    // Core::emulate();
    terminal();
    return 0;
}
