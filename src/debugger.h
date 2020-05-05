
#ifndef _DEBUGGER_H_INCLUDED_
#define _DEBUGGER_H_INCLUDED_

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

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

    struct {
        bool cop0;
        bool cop1;
        bool memory;

        bool rdram;
        bool SP;
        bool DPCommand;
        bool DPSpan;
        bool MI;
        bool VI;
        bool AI;
        bool PI;
        bool RI;
        bool SI;
        bool PIF;
        bool cart_2_1;
        bool RDP;

        bool thread;
    } verbose;

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

extern Debugger debugger;

#endif /* _DEBUGGER_H_INCLUDED_ */
