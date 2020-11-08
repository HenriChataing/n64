
#include <cstring>
#include <iostream>
#include <fstream>

#include <cxxopts.hpp>

#include <r4300/state.h>
#include <memory.h>

void startGui();

int main(int argc, char *argv[])
{
    cxxopts::Options options("n64", "N64 console emulator");
    options.add_options()
        ("record",      "Record execution trace", cxxopts::value<std::string>())
        ("replay",      "Replay execution trace", cxxopts::value<std::string>())
        ("recompiler",  "Enable recompiler", cxxopts::value<bool>()->default_value("false"))
        ("rom",         "ROM file", cxxopts::value<std::string>())
        ("h,help",      "Print usage");
    options.parse_positional("rom");
    options.positional_help("FILE");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    if (result.count("rom") == 0) {
        std::cout << "ROM file unspecified" << std::endl;
        std::cout << options.help() << std::endl;
        exit(1);
    }

    std::string rom_file = result["rom"].as<std::string>();
    std::ifstream rom_contents(rom_file);
    if (!rom_contents.good()) {
        std::cout << "ROM file '" << rom_file << "' not found" << std::endl;
        std::cout << options.help() << std::endl;
        exit(1);
    }

    R4300::state.load(rom_contents);
    rom_contents.close();

    startGui();
    return 0;
}
