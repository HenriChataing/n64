
#ifndef _DEBUGGER_H_INCLUDED_
#define _DEBUGGER_H_INCLUDED_

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include <types.h>
#include <circular_buffer.h>
#include <r4300/eval.h>
#include <r4300/rsp.h>


class Debugger {
public:
    Debugger();
    ~Debugger();

    /* Stop condition */
    std::atomic_bool halted;
    std::string haltedReason;

    /* Halt the machine execution, with the proivded reason. */
    void halt(std::string reason);

    /* When the debugger is halted, advance the interpreter one step,
     * or resume execution. */
    void step();
    void resume();

    /* Start the interpreter in a separate thread.
     * Destroy the interpreter thread. */
    void startInterpreter();
    void stopInterpreter();

    /* Called for undefined behaviour, can be configured to hard fail the
     * emulation. */
    void undefined(std::string cause) {}

    /** Create a new breakpoint.
     * \p addr is the physical address of RAM memory location to set the
     * breakpoint to. */
    void setBreakpoint(u64 addr);
    void unsetBreakpoint(u64 addr);
    bool checkBreakpoint(u64 addr);

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
        Cart_2_1,

        LabelCount,
    };

    Verbosity verbose[LabelCount];

    /** Type of execution trace entries. */
    typedef std::pair<u64, u32> TraceEntry;

    circular_buffer<TraceEntry> cpuTrace;
    circular_buffer<TraceEntry> rspTrace;

    struct Breakpoint {
        u64 addr;   /* Virtual memory address of the breakpoint. */
        Breakpoint(u64 addr) : addr(addr) {}
    };

private:
    std::thread *           _interpreterThread;
    std::condition_variable _interpreterCondition;
    std::mutex              _interpreterMutex;
    std::atomic_bool        _interpreterStopped;
    void interpreterRoutine();

    std::map<u64, std::unique_ptr<Breakpoint>> _breakpoints;
};

namespace debugger {

extern Debugger debugger;
extern const char *LabelName[Debugger::Label::LabelCount];
extern const char *LabelColor[Debugger::Label::LabelCount];

template <Debugger::Verbosity verb>
static void vlog(Debugger::Label label, const char* format, fmt::format_args args) {
    if (debugger.verbose[label] >= verb) {
        fmt::print("[{}{:<10}\x1b[0m] ", LabelColor[label], LabelName[label]);
        fmt::vprint(format, args);
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

static void halt(std::string reason) {
    debugger.halt(reason);
}

static void undefined(std::string reason) {
}

}; /* namespace debugger */

#endif /* _DEBUGGER_H_INCLUDED_ */
