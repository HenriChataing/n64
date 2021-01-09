
#ifndef _TRACE_H_INCLUDED_
#define _TRACE_H_INCLUDED_

#include <iostream>
#include <vector>
#include "memory.h"

/**
 * @brief Special bus implementation which saves all memory
 *  accesses passing through.
 */
class DebugBus: public Memory::Bus
{
public:
    DebugBus(unsigned bits) : Memory::Bus(bits), enable_trace(false) {};
    ~DebugBus() {};
    virtual bool load(unsigned bytes, u64 address, u64 *value);
    virtual bool store(unsigned bytes, u64 address, u64 value);

    void start_trace();
    void end_trace();
    void clear_trace();
    void copy_trace(std::vector<Memory::BusTransaction> &trace);

    std::vector<Memory::BusTransaction> trace;

private:
    bool enable_trace;
};

/**
 * @brief Set a trace point.
 * The trace point records the current program counter and cycles.
 * Depending the the current bus implementation (RecordBus or ReplayBus),
 * the trace point will be added to, or matched against, the memory trace.
 */
void trace_point(uint64_t pc, uint64_t cycles);

/**
 * @brief Special bus implementation which serializes a trace of the memory
 * accesses to a defined destination.
 *
 * The recorded traces can be replayed for regression checks using the
 * \ref ReplayBus class.
 * The trace includes contextual information such as the program counter and
 * the cycle count, which are also checked when replayed.
 */
class RecordBus: public Memory::Bus
{
public:
    RecordBus(unsigned bits, std::ostream *os);
    ~RecordBus();
    virtual bool load(unsigned bytes, u64 address, u64 *value);
    virtual bool store(unsigned bytes, u64 address, u64 value);

    std::ostream *os;
};

/**
 * @brief Special bus implementation which matches memory accesses against a
 * serialized memory trace loaded from the source. Any mismatch is reported
 * with error logs.
 */
class ReplayBus: public Memory::Bus
{
public:
    ReplayBus(unsigned bits, std::istream *is);
    ~ReplayBus();
    virtual bool load(unsigned bytes, u64 address, u64 *value);
    virtual bool store(unsigned bytes, u64 address, u64 value);

    std::istream *is;
};

#endif /* _TRACE_H_INCLUDED_ */
