
#include <iomanip>
#include <iostream>
#include "debugger.h"

/** @brief Global debugger. */
Debugger debugger;

/**
 * @brief Initialize the debugger with a default thread.
 */
Debugger::Debugger()
{
    _current = new Thread(0);
    _threads[0] = _current;
}

/**
 * @brief Delete all thread contexts.
 */
Debugger::~Debugger()
{
    _current = NULL;
    for (auto it : _threads)
        delete it.second;
}

/**
 * @brief Push a new stack frame to the backtrace.
 */
void Debugger::newStackFrame(u64 functionAddr, u64 callerAddr, u64 stackPointer)
{
    StackFrame sf = { functionAddr, callerAddr, stackPointer };
    _current->backtrace.push_back(sf);
}

/**
 * @brief Implement a tail call by editing the last stack frame.
 */
void Debugger::editStackFrame(u64 functionAddr, u64 stackPointer)
{
    StackFrame &sf = _current->backtrace.back();
    if (stackPointer != sf.stackPointer) {
        std::cerr << "Thread " << std::hex << _current->id << ":";
        std::cerr << "InvalidStackPointer " << std::hex << stackPointer << std::endl;
    }
    sf.functionAddr = functionAddr;
}

/**
 * @brief Delete the top stack frame(s).
 *
 * Check that the return address and stack pointer match the values recorded
 * in the stack frame.
 */
void Debugger::deleteStackFrame(u64 returnAddr, u64 callerAddr, u64 stackPointer)
{
    uint i;

    if (_current->backtrace.size() == 0)
        return;

    for (i = _current->backtrace.size(); i > 0; i--) {
        StackFrame &sf = _current->backtrace[i - 1];
        if ((returnAddr == sf.callerAddr + 8) &&
            (stackPointer == sf.stackPointer))
            break;
    }

    if (i == 0) {
        std::cerr << "Thread " << std::hex << _current->id << ":";
        std::cerr << "UnknownStackFrame " << std::hex << returnAddr << std::endl;
        backtrace(callerAddr);
        return;
    }

    if (i != _current->backtrace.size()) {
        std::cerr << "Thread " << std::hex << _current->id << ":";
        std::cerr << "DiscardedStackFrames (" << std::dec << i << "/";
        std::cerr << _current->backtrace.size() << ") ";
        std::cerr << std::hex << returnAddr << " " << stackPointer << std::endl;
        backtrace(callerAddr);
        return;
    }

    _current->backtrace.resize(i - 1);
}

void Debugger::newThread(u64 ptr)
{
    if (_threads.find(ptr) != _threads.end()) {
        std::cerr << "createThread: thread " << std::hex << ptr;
        std::cerr << " already exists" << std::endl;
        return;
    }

    _threads[ptr] = new Thread(ptr);
}

void Debugger::deleteThread(u64 ptr)
{
    if (_threads.find(ptr) == _threads.end()) {
        std::cerr << "destroyThread: thread " << std::hex << ptr;
        std::cerr << " does not exist" << std::endl;
        return;
    }

    delete _threads[ptr];
    _threads.erase(ptr);
}

void Debugger::runThread(u64 ptr)
{
    /*
     * Assumes the calling function sets the context to jump to the return
     * address on exception return.
     */
    StackFrame &sf = _current->backtrace.back();
    deleteStackFrame(sf.callerAddr + 8, 0, sf.stackPointer);

    if (_threads.find(ptr) == _threads.end()) {
        std::cerr << "runThread: thread " << std::hex << ptr;
        std::cerr << " does not exist" << std::endl;
        _threads[ptr] = new Thread(ptr);
    }
    _current = _threads[ptr];
}

void Debugger::backtrace(u64 programCounter)
{
    std::cout << "Thread " << std::hex << _current->id << ":" << std::endl;
    std::cout << std::hex << std::setfill(' ') << std::right;

    StackFrame &top = _current->backtrace.back();
    std::cout << std::setw(16) << programCounter << " (";
    if (_symbols.find(top.functionAddr) != _symbols.end())
        std::cout << _symbols[top.functionAddr] << " + ";
    else
        std::cout << top.functionAddr << " + ";
    std::cout << (programCounter - top.functionAddr) << " ; )" << std::endl;

    for (uint i = _current->backtrace.size(); i > 0; i--) {
        StackFrame &cur = _current->backtrace[i - 1];
        if (i > 1) {
            StackFrame &prev = _current->backtrace[i - 2];
            std::cout << std::setw(16) << cur.callerAddr << " (";
            if (_symbols.find(prev.functionAddr) != _symbols.end())
                std::cout << _symbols[prev.functionAddr] << " + ";
            else
                std::cout << prev.functionAddr << " + ";
            std::cout << (cur.callerAddr - prev.functionAddr) << " ; ";
            std::cout << cur.stackPointer << ")" << std::endl;
        } else {
            std::cout << std::setw(16) << cur.callerAddr << " ( ; ";
            std::cout << cur.stackPointer << ")" << std::endl;
        }
    }
}
