
#pragma once

#include <string>
#include "types.h"

namespace Mips {

namespace Opcode {
const static u32 SPECIAL    = 0x00;
const static u32 REGIMM     = 0x01;
const static u32 ADDI       = 0x08;
const static u32 ADDIU      = 0x09;
const static u32 ANDI       = 0x0c;
const static u32 BEQ        = 0x04;
const static u32 BEQL       = 0x14;
const static u32 BGTZ       = 0x07;
const static u32 BGTZL      = 0x17;
const static u32 BLEZ       = 0x06;
const static u32 BLEZL      = 0x16;
const static u32 BNE        = 0x05;
const static u32 BNEL       = 0x15;
const static u32 CACHE      = 0x2f;
const static u32 COP0       = 0x10;
const static u32 COP1       = 0x11;
const static u32 COP2       = 0x12;
const static u32 COP3       = 0x13;
const static u32 DADDI      = 0x18;
const static u32 DADDIU     = 0x19;
const static u32 J          = 0x02;
const static u32 JAL        = 0x03;
const static u32 LB         = 0x20;
const static u32 LBU        = 0x24;
const static u32 LD         = 0x37;
const static u32 LDC1       = 0x35;
const static u32 LDC2       = 0x36;
const static u32 LDL        = 0x1a;
const static u32 LDR        = 0x1b;
const static u32 LH         = 0x21;
const static u32 LHU        = 0x25;
const static u32 LL         = 0x30;
const static u32 LLD        = 0x34;
const static u32 LUI        = 0x0f;
const static u32 LW         = 0x23;
const static u32 LWC1       = 0x31;
const static u32 LWC2       = 0x32;
const static u32 LWC3       = 0x33;
const static u32 LWL        = 0x22;
const static u32 LWR        = 0x26;
const static u32 LWU        = 0x27;
const static u32 ORI        = 0x0d;
const static u32 SB         = 0x28;
const static u32 SC         = 0x38;
const static u32 SCD        = 0x3c;
const static u32 SD         = 0x3f;
const static u32 SDC1       = 0x3d;
const static u32 SDC2       = 0x3e;
const static u32 SDL        = 0x2c;
const static u32 SDR        = 0x2d;
const static u32 SH         = 0x29;
const static u32 SLTI       = 0x0a;
const static u32 SLTIU      = 0x0b;
const static u32 SW         = 0x2b;
const static u32 SWC1       = 0x39;
const static u32 SWC2       = 0x3a;
const static u32 SWC3       = 0x3b;
const static u32 SWL        = 0x2a;
const static u32 SWR        = 0x2e;
const static u32 XORI       = 0x0e;
};

namespace Special {
const static u32 ADD        = 0x20;
const static u32 ADDU       = 0x21;
const static u32 AND        = 0x24;
const static u32 BREAK      = 0x0d;
const static u32 DADD       = 0x2c;
const static u32 DADDU      = 0x2d;
const static u32 DDIV       = 0x1e;
const static u32 DDIVU      = 0x1f;
const static u32 DIV        = 0x1a;
const static u32 DIVU       = 0x1b;
const static u32 DMULT      = 0x1c;
const static u32 DMULTU     = 0x1d;
const static u32 DSLL       = 0x38;
const static u32 DSLL32     = 0x3c;
const static u32 DSLLV      = 0x14;
const static u32 DSRA       = 0x3b;
const static u32 DSRA32     = 0x3f;
const static u32 DSRAV      = 0x17;
const static u32 DSRL       = 0x3a;
const static u32 DSRL32     = 0x3e;
const static u32 DSRLV      = 0x16;
const static u32 DSUB       = 0x2e;
const static u32 DSUBU      = 0x2f;
const static u32 JALR       = 0x09;
const static u32 JR         = 0x08;
const static u32 MFHI       = 0x10;
const static u32 MFLO       = 0x12;
const static u32 MOVN       = 0x0b;
const static u32 MOVZ       = 0x0a;
const static u32 MTHI       = 0x11;
const static u32 MTLO       = 0x13;
const static u32 MULT       = 0x18;
const static u32 MULTU      = 0x19;
const static u32 NOR        = 0x27;
const static u32 OR         = 0x25;
const static u32 SLL        = 0x00;
const static u32 SLLV       = 0x04;
const static u32 SLT        = 0x2a;
const static u32 SLTU       = 0x2b;
const static u32 SRA        = 0x03;
const static u32 SRAV       = 0x07;
const static u32 SRL        = 0x02;
const static u32 SRLV       = 0x06;
const static u32 SUB        = 0x22;
const static u32 SUBU       = 0x23;
const static u32 SYNC       = 0x0f;
const static u32 SYSCALL    = 0x0c;
const static u32 TEQ        = 0x34;
const static u32 TGE        = 0x30;
const static u32 TGEU       = 0x31;
const static u32 TLT        = 0x32;
const static u32 TLTU       = 0x33;
const static u32 TNE        = 0x36;
const static u32 XOR        = 0x26;
};

namespace Regimm {
const static u32 BGEZ       = 0x01;
const static u32 BGEZAL     = 0x11;
const static u32 BGEZALL    = 0x13;
const static u32 BGEZL      = 0x03;
const static u32 BLTZ       = 0x00;
const static u32 BLTZAL     = 0x10;
const static u32 BLTZALL    = 0x12;
const static u32 BLTZL      = 0x02;
const static u32 TEQI       = 0x0c;
const static u32 TGEI       = 0x08;
const static u32 TGEIU      = 0x09;
const static u32 TLTI       = 0x0a;
const static u32 TLTIU      = 0x0b;
const static u32 TNEI       = 0x0e;
};

namespace Cop0 {
/* Funct */
const static u32 TLBR       = 0x01;
const static u32 TLBWI      = 0x02;
const static u32 TLBWR      = 0x06;
const static u32 TLBP       = 0x08;
const static u32 ERET       = 0x18;
};

namespace Cop1 {
/* Fmt */
const static u32 S          = 0x10;
const static u32 D          = 0x11;
const static u32 W          = 0x14;
const static u32 L          = 0x15;
/* Funct */
const static u32 ADD        = 0x00;
const static u32 SUB        = 0x01;
const static u32 MUL        = 0x02;
const static u32 DIV        = 0x03;
const static u32 SQRT       = 0x04;
const static u32 ABS        = 0x05;
const static u32 MOV        = 0x06;
const static u32 NEG        = 0x07;
const static u32 ROUNDL     = 0x08;
const static u32 TRUNCL     = 0x09;
const static u32 CEILL      = 0x0a;
const static u32 FLOORL     = 0x0b;
const static u32 ROUNDW     = 0x0c;
const static u32 TRUNCW     = 0x0d;
const static u32 CEILW      = 0x0e;
const static u32 FLOORW     = 0x0f;
const static u32 CVTS       = 0x20;
const static u32 CVTD       = 0x21;
const static u32 CVTW       = 0x24;
const static u32 CVTL       = 0x25;
const static u32 CF         = 0x30;
const static u32 CUN        = 0x31;
const static u32 CEQ        = 0x32;
const static u32 CUEQ       = 0x33;
const static u32 COLT       = 0x34;
const static u32 CULT       = 0x35;
const static u32 COLE       = 0x36;
const static u32 CULE       = 0x37;
const static u32 CSF        = 0x38;
const static u32 CNGLE      = 0x39;
const static u32 CSEQ       = 0x3a;
const static u32 CNGL       = 0x3b;
const static u32 CLT        = 0x3c;
const static u32 CNGE       = 0x3d;
const static u32 CLE        = 0x3e;
const static u32 CNGT       = 0x3f;
}

namespace Copz {
/* Rs */
const static u32 MF         = 0x00;
const static u32 DMF        = 0x01;
const static u32 CF         = 0x02;
const static u32 MT         = 0x04;
const static u32 DMT        = 0x05;
const static u32 CT         = 0x06;
const static u32 BC         = 0x08;
/* Rt */
const static u32 BCF        = 0x00;
const static u32 BCT        = 0x01;
const static u32 BCFL       = 0x02;
const static u32 BCTL       = 0x03;
};

const static u32 COFUN      = (1lu << 25);

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
