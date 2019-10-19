
#include <cstring>
#include <iostream>

#include <r4300/state.h>
#include <memory.h>
#include <core.h>
#include <rsp/server.h>

void startTerminal();
void startGui();

int main(int argc, char *argv[])
{
    char const *path = "SuperMario64_JA_A.z64";

    void (*start)() = startTerminal;
    if (argc > 1 && strcmp(argv[1], "--gui") == 0)
        start = startGui;

    R4300::state.init(path);
    start();
    return 0;
}
