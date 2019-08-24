
#include <iostream>

#include <r4300/state.h>
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

    R4300::state.init(path);
    R4300::state.physmem.root->print();
    RSP::startServer();
    // Core::emulate();
    terminal();
    return 0;
}
