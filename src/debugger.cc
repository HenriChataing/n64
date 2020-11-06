
#include <iomanip>
#include <iostream>

#include "debugger.h"

/** @brief Global debugger. */
Debugger debugger::debugger;

char const *debugger::LabelName[Debugger::Label::LabelCount] = {
    "cpu",
    "cop0",
    "cop1",
    "tlb",
    "rsp",
    "rdp",
    "RdRam",
    "SP",
    "DPCmd",
    "DPSpan",
    "MI",
    "VI",
    "AI",
    "PI",
    "RI",
    "SI",
    "PIF",
    "Cart",
};

fmt::text_style debugger::VerbosityStyle[] = {
    fmt::fg(fmt::color::black),
    fmt::emphasis::italic | fmt::emphasis::bold | fmt::fg(fmt::color::orange_red),
    fmt::emphasis::italic | fmt::emphasis::bold | fmt::fg(fmt::color::yellow),
    fmt::fg(fmt::color::floral_white),
    fmt::fg(fmt::color::dim_gray),
};

/* Class */

Debugger::Debugger()
    : cpuTrace(0x10000), rspTrace(0x10000)
{
    for (int label = 0; label < Debugger::LabelCount; label++) {
        verbosity[label] = Debugger::Warn;
    }

    color[Debugger::CPU] = fmt::color::cadet_blue;
    color[Debugger::COP0] = fmt::color::aquamarine;
    color[Debugger::COP1] = fmt::color::midnight_blue;
    color[Debugger::TLB] = fmt::color::blue_violet;
    color[Debugger::RSP] = fmt::color::chartreuse;
    color[Debugger::RDP] = fmt::color::medium_sea_green;
    color[Debugger::RdRam] = fmt::color::deep_pink;
    color[Debugger::SP] = fmt::color::medium_orchid;
    color[Debugger::DPCommand] = fmt::color::green_yellow;
    color[Debugger::DPSpan] = fmt::color::dark_orange;
    color[Debugger::MI] = fmt::color::golden_rod;
    color[Debugger::VI] = fmt::color::medium_slate_blue;
    color[Debugger::AI] = fmt::color::coral;
    color[Debugger::PI] = fmt::color::lemon_chiffon;
    color[Debugger::RI] = fmt::color::wheat;
    color[Debugger::SI] = fmt::color::turquoise;
    color[Debugger::PIF] = fmt::color::tomato;
    color[Debugger::Cart] = fmt::color::indian_red;
}

Debugger::~Debugger() {
}

/* Breakpoints */

void Debugger::setBreakpoint(u64 addr) {
    if (_breakpoints.find(addr) == _breakpoints.end()) {
        Breakpoint* bp = new Breakpoint(addr);
        _breakpoints[addr] = std::unique_ptr<Breakpoint>(bp);
    }
}

void Debugger::unsetBreakpoint(u64 addr) {
    _breakpoints.erase(addr);
}

bool Debugger::checkBreakpoint(u64 addr) {
    auto it = _breakpoints.find(addr);
    return it != _breakpoints.end() && it->second->enabled;
}

std::map<u64, std::unique_ptr<Debugger::Breakpoint>>::const_iterator
Debugger::breakpointsBegin() {
    return _breakpoints.begin();
}

std::map<u64, std::unique_ptr<Debugger::Breakpoint>>::const_iterator
Debugger::breakpointsEnd() {
    return _breakpoints.end();
}
