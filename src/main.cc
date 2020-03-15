
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
    char const *path = NULL;
    bool gui = false;
    argc--; argv++;

    for (; argc > 0; argc--, argv++) {
        if (strcmp(argv[0], "--gui") == 0)
            gui = true;
        else
            path = argv[0];
    }

    if (path == NULL) {
        std::cerr << "Missing path to .z64/.N64 ROM image" << std::endl;
        return -1;
    }

    R4300::state.init(path);
    gui ? startGui() : startTerminal();
    return 0;
}
