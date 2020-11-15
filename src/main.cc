
#include <cstring>
#include <iostream>
#include <fstream>

#include <cxxopts.hpp>

#include <r4300/state.h>
#include <memory.h>
#include <trace.h>

void startGui();

int main(int argc, char *argv[])
{
    cxxopts::Options options("n64", "N64 console emulator");
    options.add_options()
        ("record",      "Record execution trace", cxxopts::value<std::string>())
        ("replay",      "Replay execution trace", cxxopts::value<std::string>())
        ("recompiler",  "Enable recompiler", cxxopts::value<bool>()->default_value("false"))
        ("b,bios",      "Select PIF boot rom", cxxopts::value<std::string>())
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

    if (result.count("bios") > 0) {
        std::string bios_file = result["bios"].as<std::string>();
        std::ifstream bios_contents(bios_file);
        if (!bios_contents.good()) {
            std::cout << "BIOS file '" << bios_file << "' not found" << std::endl;
            std::cout << options.help() << std::endl;
            exit(1);
        }
        R4300::state.loadBios(bios_contents);
    }

    if (result.count("record") && result.count("replay")) {
        std::cout << "The options --record and --replay can not be set together" << std::endl;
        std::cout << options.help() << std::endl;
        exit(1);
    }
    if (result.count("record")) {
        std::string trace_file = result["record"].as<std::string>();
        std::ofstream *ostream = new std::ofstream(
            trace_file, std::ios::binary);
        if (!ostream->good()) {
            std::cout << "Failed to create trace file '";
            std::cout << trace_file << "'" << std::endl;
            std::cout << options.help() << std::endl;
            exit(1);
        }
        R4300::state.swapMemoryBus(new RecordBus(32, ostream));
    }
    if (result.count("replay")) {
        std::string trace_file = result["replay"].as<std::string>();
        std::ifstream *istream = new std::ifstream(
            trace_file, std::ios::binary);
        if (!istream->good()) {
            std::cout << "Trace file '" << trace_file;
            std::cout << "' not found" << std::endl;
            std::cout << options.help() << std::endl;
            exit(1);
        }
        R4300::state.swapMemoryBus(new ReplayBus(32, istream));
    }

    R4300::state.load(rom_contents);
    rom_contents.close();

    startGui();
    return 0;
}
