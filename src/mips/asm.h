
#pragma once

#include <string>
#include "types.h"

namespace Mips {

/**
 * @brief Extract the instruction opcode.
 */
static inline u32 getOpcode(u32 instr) {
    return (instr >> 26) & 0x3flu;
}

/**
 * @brief Extract the instruction format.
 */
static inline u32 getFmt(u32 instr) {
    return (instr >> 21) & 0x1flu;
}

/**
 * @brief Extract the instruction source register.
 */
static inline u32 getRs(u32 instr) {
    return (instr >> 21) & 0x1flu;
}

/**
 * @brief Extract the instruction target register.
 */
static inline u32 getRt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

/**
 * @brief Extract the instruction destination register.
 */
static inline u32 getRd(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

/**
 * @brief Extract the instruction source register.
 */
static inline u32 getFs(u32 instr) {
    return (instr >> 11) & 0x1flu;
}

/**
 * @brief Extract the instruction target register.
 */
static inline u32 getFt(u32 instr) {
    return (instr >> 16) & 0x1flu;
}

/**
 * @brief Extract the instruction destination register.
 */
static inline u32 getFd(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

/**
 * @brief Extract the instruction shift amount.
 */
static inline u32 getShamnt(u32 instr) {
    return (instr >> 6) & 0x1flu;
}

/**
 * @brief Extract the instruction target jump address.
 */
static inline u32 getTarget(u32 instr) {
    return instr & 0x3fffffflu;
}

/**
 * @brief Extract the instruction immediate value.
 */
static inline u32 getImmediate(u32 instr) {
    return instr & 0xfffflu;
}

/**
 * @brief Extract the instruction function (Special opcode).
 */
static inline u32 getFunct(u32 instr) {
    return instr & 0x3flu;
}

namespace CPU {

/** @brief Pretty print an instruction. */
std::string disas(u64 pc, u32 instr);
/** @brief Return the standardized name for a general purpose register. */
char const *getRegisterName(uint reg);

};

namespace RSP {

/** @brief Pretty print an instruction. */
std::string disas(u64 pc, u32 instr);
/** @brief Return the standardized name for a general purpose register. */
char const *getRegisterName(uint reg);

};

};
