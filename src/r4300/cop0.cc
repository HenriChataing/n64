
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
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << "INDEX write (" << bytes << ") " << value << std::endl;
    }
} Index;

class Count : public Register
{
public:
    virtual u64 read(uint bytes) const {
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << "COUNT write (" << bytes << ") " << value << std::endl;
    }
} Count;

class Compare : public Register
{
public:
    virtual u64 read(uint bytes) const {
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << "COMPARE write (" << bytes << ") " << value << std::endl;
    }
} Compare;

class Cause : public Register
{
public:
    virtual u64 read(uint bytes) const {
        return 0;
    }

    virtual void write(uint bytes, u64 value) const {
        std::cerr << "CAUSE write (" << bytes << ") " << value << std::endl;
    }
} Cause;

}; /* namespace Cop0 */
