
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sys/mman.h>

#include "X86Emitter.h"

using namespace X86;

namespace X86 {

Reg<u32> eax(0);
Reg<u32> ecx(1);
Reg<u32> edx(2);
Reg<u32> ebx(3);
Reg<u32> esp(4);
Reg<u32> ebp(5);
Reg<u32> esi(6);
Reg<u32> edi(7);

Reg<u16> ax(0);
Reg<u16> cx(1);
Reg<u16> dx(2);
Reg<u16> bx(3);
Reg<u16> sp(4);
Reg<u16> bp(5);
Reg<u16> si(6);
Reg<u16> di(7);

Reg<u8> al(0);
Reg<u8> cl(1);
Reg<u8> dl(2);
Reg<u8> bl(3);
Reg<u8> ah(4);
Reg<u8> ch(5);
Reg<u8> dh(6);
Reg<u8> bh(7);

};

Emitter::Emitter(size_t codeSize)
{
    _codeBuffer = NULL;
    _codeSize = ((codeSize + 0xfff) / 0x1000) * 0x1000;
    _codeLength = 0;
    int r;

#if 0
    _codeBuffer = (u8 *)mmap(NULL, _codeSize,
        PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (_codeBuffer == MAP_FAILED) {
        std::cerr << "Cannot allocate mmap memory" << std::endl;
        throw "Emitter mmap failure";
    }
#else
    /* Allocate page aligned code buffer. */
    r = posix_memalign((void **)&_codeBuffer, 0x1000, _codeSize);
    if (r != 0) {
        std::cerr << "Cannot allocate page aligned memory ";
        if (r == EINVAL)
            std::cerr << "(Invalid alignmnt)";
        if (r == ENOMEM)
            std::cerr << "(Out of memory)";
        std::cerr << std::endl;
        throw "Emitter allocation failure";
    }

    /*
     * Change access permissions to code buffer to enable executing
     * code inside.
     */
    r = mprotect(_codeBuffer, _codeSize,
        PROT_WRITE | PROT_READ | PROT_EXEC);
    if (r < 0) {
        std::cerr << "Cannot change memory permissions for buffer ";
        std::cerr << _codeBuffer << " (" << strerror(errno) << ")" << std::endl;
        throw "Emitter mprotect failure";
    }
#endif
}

Emitter::~Emitter()
{
    delete _codeBuffer;
}

/**
 * @brief Generate the bytes for a conditionnal jump instruction.
 * @param ops       opcode if the jump target is close (relative offset
 *                  can fit on a signed char)
 * @param opl       opcode if the jump target is far (relative offset
 *                  can fit on a signed integer). The conditional jump
 *                  always uses an extended opcode in such cases.
 * @param loc       jump target, or NULL if the target is unknown at
 *                  this time
 * @return          pointer to the relative offset if the jump target
 *                  is not known, which can be used to later specify
 *                  the offset, or NULL otherwise
 */
u32 *Emitter::jumpCond(u8 ops, u8 opl, const u8 *loc)
{
    if (loc == NULL) {
        put(0x0f); put(opl); put((u32)0);
        return (u32 *)(_codeBuffer + _codeLength - 4);
    }
    ptrdiff_t rel = loc - (_codeBuffer + _codeLength + 2);
    i8 rel8 = rel;
    if ((ptrdiff_t)rel8 == rel) {
        put(ops); put(rel8);
    } else {
        rel -= 4;
        put(0x0f); put(opl); put((u32)rel);
    }
    return NULL;
}

/**
 * @brief Generate the bytes for an inconditionnal jump instruction.
 * @param ops       opcode if the jump target is close (relative offset
 *                  can fit on a signed char)
 * @param opl       opcode if the jump target is far (relative offset
 *                  can fit on a signed integer)
 * @param loc       jump target, or NULL if the target is unknown at
 *                  this time
 * @return          pointer to the relative offset if the jump target
 *                  is not known, which can be used to later specify
 *                  the offset, or NULL otherwise
 */
u32 *Emitter::jumpAbs(u8 ops, u8 opl, const u8 *loc)
{
    if (loc == NULL) {
        put(opl); put((u32)0);
        return (u32 *)(_codeBuffer + _codeLength - 4);
    }
    ptrdiff_t rel = loc - (_codeBuffer + _codeLength + 2);
    i8 rel8 = rel;
    if ((ptrdiff_t)rel8 == rel) {
        put(ops); put(rel8);
    } else {
        rel -= 3;
        put(opl); put((u32)rel);
    }
    return NULL;
}

u32 *Emitter::jumpAbs(u8 opl, const u8 *loc)
{
    if (loc == NULL) {
        put(opl); put((u32)0);
        return (u32 *)(_codeBuffer + _codeLength - 4);
    }
    ptrdiff_t rel = loc - (_codeBuffer + _codeLength + 5);
    put(opl); put((u32)rel);
    return NULL;
}

void Emitter::dump() const
{
    std::cout << std::hex << std::setfill('0');
    std::cout << "== " << (uintptr_t)_codeBuffer << std::endl;
    for (uint i = 0; i < _codeLength; i++) {
        if (i && !(i % 32))
            std::cout << std::endl;
        std::cout << " " << std::setfill('0') << std::setw(2);
        std::cout << (uint)_codeBuffer[i];
    }
    std::cout << std::endl;
}

void Emitter::dump(const u8 *start) const
{
    std::cout << std::hex << std::setfill('0');
    std::cout << "== " << (uintptr_t)start << std::endl;
    u8 *end = _codeBuffer + _codeLength;
    for (uint i = 0; start < end; start++, i++) {
        if (i && !(i % 32))
            std::cout << std::endl;
        std::cout << " " << std::setw(2) << (uint)start[0];
    }
    std::cout << std::endl;
}
