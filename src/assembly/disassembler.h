
#ifndef _ASSEMBLY_DISASSEMBLER_H_INCLUDED_
#define _ASSEMBLY_DISASSEMBLER_H_INCLUDED_

#include <string>
#include <types.h>

namespace n64::assembly {
namespace cpu {

/** Disassemble and format a CPU instruction. */
std::string disassemble(u64 pc, u32 instr);

}; /* namespace cpu */

namespace rsp {

/** Disassemble and format an RSP instruction. */
std::string disassemble(u64 pc, u32 instr);

}; /* namespace rsp */


static inline u32 getOpcode(u32 instr) {
    return (instr >> 26) & 0x3flu;
}

static inline u32 getFmt(u32 instr) {
    return (instr >> 21) & 0x1flu;
}

static inline u32 getRs(u32 instr) {
    return (instr >> 21) & 0x1flu;
}

static inline u32 getRt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

static inline u32 getRd(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

static inline u32 getFs(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

static inline u32 getFt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

static inline u32 getFd(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

static inline u32 getVt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

static inline u32 getVs(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

static inline u32 getVd(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

static inline u32 getElement(u32 instr) {
     return (instr >> 21) & 0xfu;
}

static inline u32 getShamnt(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

static inline u32 getTarget(u32 instr) {
    return instr & 0x3fffffflu;
}

static inline u32 getImmediate(u32 instr) {
    return instr & 0xfffflu;
}

static inline u32 getFunct(u32 instr) {
    return instr & 0x3flu;
}

}; /* namespace n64::assembly */

#endif /* _ASSEMBLY_DISASSEMBLER_H_INCLUDED_ */
