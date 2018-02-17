
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {

class Index : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "INDEX read (" << bytes << ")" << std::endl;
        throw "INDEX read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "INDEX write (" << bytes << ") " << std::hex << value << std::endl;
        throw "INDEX write";
    }
} Index;

class Random : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "RANDOM read (" << bytes << ")" << std::endl;
        throw "RANDOM read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "RANDOM write (" << bytes << ") " << std::hex << value << std::endl;
        throw "RANDOM write";
    }
} Random;

class EntryLo0 : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "ENTRYLO0 read (" << bytes << ")" << std::endl;
        throw "ENTRYLO0 read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "ENTRYLO0 write (" << bytes << ") " << std::hex << value << std::endl;
        throw "ENTRYLO0 write";
    }
} EntryLo0;

class EntryLo1 : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "ENTRYLO1 read (" << bytes << ")" << std::endl;
        throw "ENTRYLO1 read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "ENTRYLO1 write (" << bytes << ") " << std::hex << value << std::endl;
        throw "ENTRYLO1 write";
    }
} EntryLo1;

class Context : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "CONTEXT read (" << bytes << ")" << std::endl;
        throw "CONTEXT read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "CONTEXT write (" << bytes << ") " << std::hex << value << std::endl;
        throw "CONTEXT write";
    }
} Context;

class PageMask : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "PAGEMASK read (" << bytes << ")" << std::endl;
        throw "PAGEMASK read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "PAGEMASK write (" << bytes << ") " << std::hex << value << std::endl;
        throw "PAGEMASK write";
    }
} PageMask;

class Wired : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "WIRED read (" << bytes << ")" << std::endl;
        throw "WIRED read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "WIRED write (" << bytes << ") " << std::hex << value << std::endl;
        throw "WIRED write";
    }
} Wired;

class BadVAddr : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "BADVADDR read (" << bytes << ")" << std::endl;
        throw "BADVADDR read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "BADVADDR write (" << bytes << ") " << std::hex << value << std::endl;
        throw "BADVADDR write";
    }
} BadVAddr;

/**
 * @brief The Count register acts like a timer incrementing at a constant
 * rate (half the maximum instruction issue rate).
 */
class Count : public Register
{
public:
    virtual u64 read(uint bytes) const {
        return state.cp0reg.count;
    }

    virtual void write(uint bytes, u64 value) const {
        state.cp0reg.count = value;
    }
} Count;

class EntryHi : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "ENTRYHI read (" << bytes << ")" << std::endl;
        throw "ENTRYHI read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "ENTRYHI write (" << bytes << ") " << std::hex << value << std::endl;
        throw "ENTRYHI write";
    }
} EntryHi;

/**
 * @brief The Compare register acts as a timer (see also the Count register).
 *
 * It maintains a stable value that does not change on its own.
 * When the value of the Count register equals the value of the Compare
 * register, interrupt bit IP(7) in the Cause register is set. This causes an
 * interrupt as soon as the interrupt is enabled.
 */
class Compare : public Register
{
public:
    virtual u64 read(uint bytes) const {
        return state.cp0reg.compare;
    }

    virtual void write(uint bytes, u64 value) const {
        state.cp0reg.compare = value;
    }
} Compare;

class SR : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "SR read (" << bytes << ")" << std::endl;
        throw "SR read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "SR write (" << bytes << ") " << std::hex << value << std::endl;
        throw "SR write";
    }
} SR;

/**
 * @brief The Cause register describes the cause of the most recent exception.
 *
 *  + [31] BD
 *      Indicates whether the last exception taken occurred in a branch delay
 *      slot (1-> delay slot, 0 -> normal)
 *  + [30] 0
 *  + [29:28] CE
 *      Coprocessor unit referenced when a Coprocessor Unusable exception is
 *      taken
 *  + [27:16] 0
 *  + [15:8] IP0-7
 *      Indicates if an instruction is pending (1 -> pending, 0 -> no)
 *  + [7] 0
 *  + [6:2] ExcCode
 *      Exception code
 *  + [1:0] 0
 */
class Cause : public Register
{
public:
    virtual u64 read(uint bytes) const {
        // @todo exception
        // std::cerr << std::dec << "CAUSE read (" << bytes << ")" << std::endl;
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        // @todo write IP bits
        // std::cerr << std::dec << "CAUSE write (" << bytes << ") " << std::hex << value << std::endl;
    }
} Cause;

class EPC : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "EPC read (" << bytes << ")" << std::endl;
        throw "EPC read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "EPC write (" << bytes << ") " << std::hex << value << std::endl;
        throw "EPC write";
    }
} EPC;

class PrId : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "PRID read (" << bytes << ")" << std::endl;
        throw "PRID read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "PRID write (" << bytes << ") " << std::hex << value << std::endl;
        throw "PRID write";
    }
} PrId;

class Config : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "CONFIG read (" << bytes << ")" << std::endl;
        throw "CONFIG read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "CONFIG write (" << bytes << ") " << std::hex << value << std::endl;
        throw "CONFIG write";
    }
} Config;

class LLAddr : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "LLADDR read (" << bytes << ")" << std::endl;
        throw "LLADDR read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "LLADDR write (" << bytes << ") " << std::hex << value << std::endl;
        throw "LLADDR write";
    }
} LLAddr;

class WatchLo : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "WATCHLO read (" << bytes << ")" << std::endl;
        throw "WATCHLO read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "WATCHLO write (" << bytes << ") " << std::hex << value << std::endl;
        throw "WATCHLO write";
    }
} WatchLo;

class WatchHi : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "WATCHHI read (" << bytes << ")" << std::endl;
        throw "WATCHHI read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "WATCHHI write (" << bytes << ") " << std::hex << value << std::endl;
        throw "WATCHHI write";
    }
} WatchHi;

class XContext : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "XCONTEXT read (" << bytes << ")" << std::endl;
        throw "XCONTEXT read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "XCONTEXT write (" << bytes << ") " << std::hex << value << std::endl;
        throw "XCONTEXT write";
    }
} XContext;

class PErr : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "PERR read (" << bytes << ")" << std::endl;
        throw "PERR read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "PERR write (" << bytes << ") " << std::hex << value << std::endl;
        throw "PERR write";
    }
} PErr;

class CacheErr : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "CACHEERR read (" << bytes << ")" << std::endl;
        throw "CACHEERR read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "CACHEERR write (" << bytes << ") " << std::hex << value << std::endl;
        throw "CACHEERR write";
    }
} CacheErr;

/**
 * @brief The TagLo and TagHi registers are 32-bit read/write registers that hold
 * either the primary cache tag and parity, or the secondary cache tag and
 * ECC during cache initialization, cache diagnostics, or cache error
 * processing.
 */
class TagLo : public Register
{
public:
    virtual u64 read(uint bytes) const {
        // @todo cache support ?
        return state.cp0reg.tagLo;
    }

    virtual void write(uint bytes, u64 value) const {
        // @todo cache support ?
        state.cp0reg.tagLo = value;
    }
} TagLo;

class TagHi : public Register
{
public:
    virtual u64 read(uint bytes) const {
        // @todo cache support ?
        return state.cp0reg.tagHi;
    }

    virtual void write(uint bytes, u64 value) const {
        // @todo cache support ?
        state.cp0reg.tagHi = value;
    }
} TagHi;

class ErrPC : public Register
{
public:
    virtual u64 read(uint bytes) const {
        std::cerr << std::dec << "ERRPC read (" << bytes << ")" << std::endl;
        throw "ERRPC read";
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << std::dec << "ERRPC write (" << bytes << ") " << std::hex << value << std::endl;
        throw "ERRPC write";
    }
} ErrPC;

}; /* namespace Cop0 */
