#ifndef _EXCEPTION_H_INCLUDED_
#define _EXCEPTION_H_INCLUDED_

#include <exception>

class InvalidRom : public std::exception
{
public:
    InvalidRom() {}
    ~InvalidRom() {}
    const char *what() const noexcept { return "Invalid ROM File"; }
};

class UnsupportedInstruction : public std::exception
{
public:
    UnsupportedInstruction(u16 pc, u8 opcode) : address(pc), opcode(opcode) {}
    ~UnsupportedInstruction() {}
    const char *what() const noexcept { return "Unsupported Instruction"; }

    u16 address;
    u8 opcode;
};

class JammingInstruction : public std::exception
{
public:
    JammingInstruction(u16 pc, u8 opcode) : address(pc), opcode(opcode) {}
    ~JammingInstruction() {}
    const char *what() const noexcept { return "Jamming Instruction"; }

    u16 address;
    u8 opcode;
};

#endif /* _EXCEPTION_H_INCLUDED_ */
