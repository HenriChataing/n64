
#ifndef _DEBUGGER_H_INCLUDED_
#define _DEBUGGER_H_INCLUDED_

#include <map>
#include <vector>
#include <types.h>

class Debugger {
public:
    Debugger();
    ~Debugger();

    /* Symbols. */
    void addSymbol(u64 address, std::string name) {
        _symbols[address] = name;
    }

    /* Event handlers for function calls and function returns. */
    void newStackFrame(u64 functionAddr, u64 callerAddr, u64 stackPointer);
    void editStackFrame(u64 functionAddr, u64 stackPointer);
    void deleteStackFrame(u64 returnAdd, u64 callerAddr, u64 stackPointer);

    /* Event handlers for thread context changes. */
    void newThread(u64 ptr);
    void deleteThread(u64 ptr);
    void runThread(u64 ptr);

    /* Request a backtrace of the current thread. */
    void backtrace(u64 programCounter);

private:
    struct StackFrame {
        u64 functionAddr;   /**< Address of the function. */
        u64 callerAddr;     /**< Address of the call point. */
        u64 stackPointer;   /**< Value of the stack pointer on function entry. */
    };
    struct Thread {
        Thread(u64 id) : id(id) {}
        u64 id;
        std::vector<StackFrame> backtrace;
    };

    std::map<u64, std::string> _symbols;
    std::map<u64, Thread *> _threads;
    Thread *_current;
};

extern Debugger debugger;

#endif /* _DEBUGGER_H_INCLUDED_ */