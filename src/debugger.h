
#ifndef _DEBUGGER_H_INCLUDED_
#define _DEBUGGER_H_INCLUDED_

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <fmt/color.h>

#include <types.h>
#include <circular_buffer.h>
#include <r4300/rsp.h>
#include <r4300/state.h>


class Debugger {
public:
    Debugger();
    ~Debugger();

    enum Verbosity {
        None,
        Error,
        Warn,
        Info,
        Debug,
    };

    enum Label {
        CPU,
        COP0,
        COP1,
        TLB,
        RSP,
        RDP,
        RdRam,
        SP,
        DPCommand,
        DPSpan,
        MI,
        VI,
        AI,
        PI,
        RI,
        SI,
        PIF,
        Cart,

        LabelCount,
    };

    Verbosity verbosity[LabelCount];
    fmt::rgb color[LabelCount];

    /** Type of execution trace entries. */
    typedef std::pair<u64, u32> TraceEntry;

    circular_buffer<TraceEntry> cpuTrace;
    circular_buffer<TraceEntry> rspTrace;

    struct Breakpoint {
        unsigned id;
        uint64_t addr;
        bool enabled;

        Breakpoint(unsigned id, uint64_t addr)
            : id(id), addr(addr), enabled(true) {}
        Breakpoint()
            : id(0), addr(0), enabled(false) {}
    };

    /**
     * @brief Create a new breakpoint.
     * @details The breakpoint is always created; the input address is not
     *  checked for duplicates. The breakpoint identifier is monotonic.
     * @param addr
     *      Physical address of the RAM memory location to set
     *      the breakpoint to.
     * @return The identifier of the created breakpoint.
     */
    unsigned set_breakpoint(uint64_t addr);
    /**
     * @brief Remove a previously created breakpoint.
     * @param id    Identifier of the breakpoint to remove.
     */
    void unset_breakpoint(unsigned id);
    /**
     * @brief Check if the input address triggers a breakpoint.
     * @param addr  Physical address to check.
     * @param id    Return the identifier of the triggered breakpoint.
     * @return True if and only if the address \p addr is marked by a breakpoint.
     */
    bool check_breakpoint(uint64_t addr, unsigned *id = NULL);

    std::map<unsigned, Debugger::Breakpoint>::iterator breakpointsBegin() {
        return _breakpoints.begin();
    }

    std::map<unsigned, Debugger::Breakpoint>::iterator breakpointsEnd() {
        return _breakpoints.end();
    }

private:
    unsigned _breakpoints_counter;
    std::map<unsigned, Breakpoint> _breakpoints;
};

namespace debugger {

extern Debugger debugger;
extern char const *LabelName[Debugger::Label::LabelCount];
extern fmt::text_style VerbosityStyle[5];

template <Debugger::Verbosity verb>
static void vlog(Debugger::Label label, const char* format, fmt::format_args args) {
    if (debugger.verbosity[label] >= verb) {
        fmt::print(fmt::fg(debugger.color[label]), "{:>7} | ", LabelName[label]);
        fmt::vprint(::stdout, VerbosityStyle[verb], format, args);
        fmt::print("\n");
    }
}

template <typename... Args>
static void debug(Debugger::Label label, const char* format, const Args & ... args) {
    vlog<Debugger::Verbosity::Debug>(label, format, fmt::make_format_args(args...));
}

template <typename... Args>
static void info(Debugger::Label label, const char* format, const Args & ... args) {
    vlog<Debugger::Verbosity::Info>(label, format, fmt::make_format_args(args...));
}

template <typename... Args>
static void warn(Debugger::Label label, const char* format, const Args & ... args) {
    vlog<Debugger::Verbosity::Warn>(label, format, fmt::make_format_args(args...));
}

template <typename... Args>
static void error(Debugger::Label label, const char* format, const Args & ... args) {
    vlog<Debugger::Verbosity::Error>(label, format, fmt::make_format_args(args...));
}

/* Called for undefined behaviour, can be configured to hard fail the
 * emulation. */
static void undefined(std::string reason) {
}

}; /* namespace debugger */

#endif /* _DEBUGGER_H_INCLUDED_ */
