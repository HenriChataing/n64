
#include <iomanip>
#include <iostream>
#include <types.h>

#include "asm.h"

namespace Mips {

const char *RegisterNames[32] = {
    "zero", "at",   "v0",   "v1",
    "a0",   "a1",   "a2",   "a3",
    "t0",   "t1",   "t2",   "t3",
    "t4",   "t5",   "t6",   "t7",
    "s0",   "s1",   "s2",   "s3",
    "s4",   "s5",   "s6",   "s7",
    "t8",   "t9",   "k0",   "k1",
    "gp",   "sp",   "fp",   "ra",
};

/**
 * @brief Return the standardized name for a general purpose register.
 */
char const *getRegisterName(uint reg)
{
    return (reg > 31) ? "?" : RegisterNames[reg];
}

#define Unknown(instr) \
    std::cout << "?" << std::hex << std::setfill('0') << std::setw(8); \
    std::cout << instr << "?";

/**
 * @brief Unparametrized instruction.
 */
#define SType(opcode, name) \
    case opcode: \
        std::cout << name; \
        break;

/**
 * @brief Preprocessor template for I-type instructions.
 *
 * The registers and immediate value are automatically extracted from the
 * instruction and added as rs, rt, imm in a new scope. The immediate value
 * is sign extended into a 64 bit unsigned integer.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 * @param fmt               Formatting to use to print the instruction
 */
#define IType(opcode, name, instr, fmt) \
    case opcode: { \
        u32 rt = Mips::getRt(instr); \
        u32 rs = Mips::getRs(instr); \
        u16 imm = Mips::getImmediate(instr); \
        (void)rs; (void)rt; (void)imm; \
        std::cout << std::setw(8) << std::setfill(' ') << std::left << name; \
        std::cout << " " << std::setfill('0'); \
        IType_##fmt(rt, rs, imm); \
        break; \
    }

#define IType_Rt_Rs_Imm(rt, rs, imm) \
    std::cout << getRegisterName(rt); \
    std::cout << ", " << getRegisterName(rs); \
    std::cout << ", " << std::dec << (i16)imm;

#define IType_Rt_Rs_XImm(rt, rs, imm) \
    std::cout << getRegisterName(rt); \
    std::cout << ", " << getRegisterName(rs); \
    std::cout << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_XImm(rt, rs, imm) \
    std::cout << getRegisterName(rt); \
    std::cout << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_Off_Rs(rt, rs, off) \
    std::cout << getRegisterName(rt); \
    std::cout << ", " << std::dec << (i16)off; \
    std::cout << "(" << getRegisterName(rs) << ")";

#define IType_CRt_Off_Rs(rt, rs, off) \
    std::cout << "cr" << std::dec << rt; \
    std::cout << ", " << std::dec << (i16)off; \
    std::cout << "(" << getRegisterName(rs) << ")";

#define IType_Off(rt, rs, off) \
    std::cout << std::dec << (i16)off;

#define IType_Rs_Off(rt, rs, off) \
    std::cout << getRegisterName(rs); \
    std::cout << ", " << std::dec << (i16)off;

#define IType_Rs_Rt_Off(rt, rs, off) \
    std::cout << getRegisterName(rs); \
    std::cout << ", " << getRegisterName(rt); \
    std::cout << ", " << std::dec << (i16)off;

/**
 * @brief Preprocessor template for J-type instructions.
 *
 * The target is automatically extracted from the instruction and added as
 * tg in a new scope. The target is sign extended into a 64 bit unsigned
 * integer.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 */
#define JType(opcode, name, instr) \
    case opcode: { \
        u64 tg = Mips::getTarget(instr); \
        std::cout << std::setw(8) << std::left << name << " $" << std::hex; \
        std::cout << std::setfill('0') << std::setw(8) << std::right << tg; \
        break; \
    }

/**
 * @brief Preprocessor template for R-type instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamt in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 * @param fmt               Formatting to use to print the instruction
 */
#define RType(opcode, name, instr, fmt) \
    case opcode: { \
        u32 rd = Mips::getRd(instr); \
        u32 rs = Mips::getRs(instr); \
        u32 rt = Mips::getRt(instr); \
        u32 shamnt = Mips::getShamnt(instr); \
        (void)rd; (void)rs; (void)rt; (void)shamnt; \
        std::cout << std::setw(8) << std::left << name << " "; \
        RType_##fmt(rd, rs, rt, shamnt); \
        break; \
    }

#define RType_Rd_Rs_Rt(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rd); \
    std::cout << ", " << getRegisterName(rs); \
    std::cout << ", " << getRegisterName(rt);

#define RType_Rd_Rt_Rs(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rd); \
    std::cout << ", " << getRegisterName(rt); \
    std::cout << ", " << getRegisterName(rs);

#define RType_Rs_Rt(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rs); \
    std::cout << ", " << getRegisterName(rt);

#define RType_Rd_Rs(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rd); \
    std::cout << ", " << getRegisterName(rs);

#define RType_Rs(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rs);

#define RType_Rd(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rd);

#define RType_Rd_Rt_Shamnt(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rd); \
    std::cout << ", " << getRegisterName(rt); \
    std::cout << ", " << std::dec << shamnt;

#define RType_Rt_CRd(rd, rs, rt, shamnt) \
    std::cout << getRegisterName(rt); \
    std::cout << ", cr" << std::dec << rd;

/**
 * @brief Print out an instruction.
 */
void disas(u32 instr)
{
    u32 opcode;

    using namespace Mips::Opcode;
    using namespace Mips::Special;
    using namespace Mips::Regimm;
    using namespace Mips::Cop0;
    using namespace Mips::Copz;

    // Special case (SLL 0, 0, 0)
    if (instr == 0) {
        std::cout << "nop";
        return;
    }

    switch (opcode = Mips::getOpcode(instr)) {
        case SPECIAL:
            switch (Mips::getFunct(instr)) {
                RType(ADD, "add", instr, Rd_Rs_Rt)
                RType(ADDU, "addu", instr, Rd_Rs_Rt)
                RType(AND, "and", instr, Rd_Rs_Rt)
                SType(BREAK, "break")
                RType(DADD, "dadd", instr, Rd_Rs_Rt)
                RType(DADDU, "daddu", instr, Rd_Rs_Rt)
                RType(DDIV, "ddiv", instr, Rs_Rt)
                RType(DDIVU, "ddivu", instr, Rs_Rt)
                RType(DIV, "div", instr, Rs_Rt)
                RType(DIVU, "divu", instr, Rs_Rt)
                RType(DMULT, "dmult", instr, Rs_Rt)
                RType(DMULTU, "dmultu", instr, Rs_Rt)
                RType(DSLL, "dsll", instr, Rd_Rt_Shamnt)
                RType(DSLL32, "dsll32", instr, Rd_Rt_Shamnt)
                RType(DSLLV, "dsllv", instr, Rd_Rt_Rs)
                RType(DSRA, "dsra", instr, Rd_Rt_Shamnt)
                RType(DSRA32, "dsra32", instr, Rd_Rt_Shamnt)
                RType(DSRAV, "dsrav", instr, Rd_Rt_Rs)
                RType(DSRL, "dsrl", instr, Rd_Rt_Shamnt)
                RType(DSRL32, "dsrl32", instr, Rd_Rt_Shamnt)
                RType(DSRLV, "dsrlv", instr, Rd_Rt_Rs)
                RType(DSUB, "dsub", instr, Rd_Rs_Rt)
                RType(DSUBU, "dsubu", instr, Rd_Rs_Rt)
                RType(JALR, "jalr", instr, Rd_Rs)
                RType(JR, "jr", instr, Rs)
                RType(MFHI, "mfhi", instr, Rd)
                RType(MFLO, "mflo", instr, Rd)
                RType(MTHI, "mthi", instr, Rs)
                RType(MTLO, "mtlo", instr, Rs)
                RType(MULT, "mult", instr, Rs_Rt)
                RType(MULTU, "multu", instr, Rs_Rt)
                RType(NOR, "nor", instr, Rd_Rs_Rt)
                RType(OR, "or", instr, Rd_Rs_Rt)
                RType(SLL, "sll", instr, Rd_Rt_Shamnt)
                RType(SLLV, "sllv", instr, Rd_Rt_Rs)
                RType(SLT, "slt", instr, Rd_Rs_Rt)
                RType(SLTU, "sltu", instr, Rd_Rs_Rt)
                RType(SRA, "sra", instr, Rd_Rt_Shamnt)
                RType(SRAV, "srav", instr, Rd_Rt_Rs)
                RType(SRL, "srl", instr, Rd_Rt_Shamnt)
                RType(SRLV, "srlv", instr, Rd_Rt_Rs)
                RType(SUB, "sub", instr, Rd_Rs_Rt)
                RType(SUBU, "subu", instr, Rd_Rs_Rt)
                SType(SYSCALL, "syscall")
                RType(XOR, "xor", instr, Rd_Rs_Rt)
                default:
                    Unknown(instr);
                    return;
            }
            break;

        case REGIMM:
            switch (Mips::getRt(instr)) {
                IType(BGEZ, "bgez", instr, Rs_Off)
                IType(BGEZL, "bgezl", instr, Rs_Off)
                IType(BGEZAL, "bgezal", instr, Rs_Off)
                IType(BGEZALL, "bgezall", instr, Rs_Off)
                IType(BLTZ, "bltz", instr, Rs_Off)
                IType(BLTZL, "bltzl", instr, Rs_Off)
                IType(BLTZAL, "bltzal", instr, Rs_Off)
                IType(BLTZALL, "bltzall", instr, Rs_Off)
                default:
                    Unknown(instr);
                    return;
            }
            break;

        IType(ADDI, "addi", instr, Rt_Rs_Imm)
        IType(ADDIU, "addiu", instr, Rt_Rs_XImm)
        IType(ANDI, "andi", instr, Rt_Rs_XImm)
        IType(BEQ, "beq", instr, Rs_Rt_Off)
        IType(BEQL, "beql", instr, Rs_Rt_Off)
        IType(BGTZ, "bgtz", instr, Rs_Rt_Off)
        IType(BGTZL, "bgtzl", instr, Rs_Rt_Off)
        IType(BLEZ, "blez", instr, Rs_Rt_Off)
        IType(BLEZL, "blezl", instr, Rs_Rt_Off)
        IType(BNE, "bne", instr, Rs_Rt_Off)
        IType(BNEL, "bnel", instr, Rs_Rt_Off)
        SType(CACHE, "cache")

#define COPz(z) \
        case COP##z: \
            if (instr & Mips::COFUN) { \
                std::cout << "cop" #z " $" << std::hex; \
                std::cout << std::setfill('0') << std::setw(8) << instr; \
                break; \
            } \
            switch (Mips::getRs(instr)) { \
                RType(MF, "mfc" #z, instr, Rt_CRd) \
                RType(DMF, "dmfc" #z, instr, Rt_CRd) \
                RType(MT, "mtc" #z, instr, Rt_CRd) \
                RType(DMT, "dmtc" #z, instr, Rt_CRd) \
                RType(CF, "cfc" #z, instr, Rt_CRd) \
                RType(CT, "ctc" #z, instr, Rt_CRd) \
                case BC: \
                    switch (Mips::getRt(instr)) { \
                        IType(BCF, "bc" #z "f", instr, Off) \
                        IType(BCT, "bc" #z "t", instr, Off) \
                        IType(BCFL, "bc" #z "fl", instr, Off) \
                        IType(BCTL, "bc" #z "tl", instr, Off) \
                        default: \
                            Unknown(instr); \
                            return; \
                    } \
                    break; \
                default: \
                    Unknown(instr); \
                    return; \
            } \
            break;

        COPz(0)
        COPz(1)
        COPz(2)
        COPz(3)

        IType(DADDI, "daddi", instr, Rt_Rs_Imm)
        IType(DADDIU, "daddiu", instr, Rt_Rs_XImm)
        JType(J, "j", instr)
        JType(JAL, "jal", instr)
        IType(LB, "lb", instr, Rt_Off_Rs)
        IType(LBU, "lbu", instr, Rt_Off_Rs)
        IType(LD, "ld", instr, Rt_Off_Rs)
        IType(LDC1, "ldc1", instr, CRt_Off_Rs)
        IType(LDC2, "ldc2", instr, CRt_Off_Rs)
        IType(LDL, "ldl", instr, Rt_Off_Rs)
        IType(LDR, "ldr", instr, Rt_Off_Rs)
        IType(LH, "lh", instr, Rt_Off_Rs)
        IType(LHU, "lhu", instr, Rt_Off_Rs)
        IType(LL, "ll", instr, Rt_Off_Rs)
        IType(LLD, "lld", instr, Rt_Off_Rs)
        IType(LUI, "lui", instr, Rt_XImm)
        IType(LW, "lw", instr, Rt_Off_Rs)
        IType(LWC1, "lwc1", instr, CRt_Off_Rs)
        IType(LWC2, "lwc2", instr, CRt_Off_Rs)
        IType(LWC3, "lwc3", instr, CRt_Off_Rs)
        IType(LWL, "lwl", instr, Rt_Off_Rs)
        IType(LWR, "lwr", instr, Rt_Off_Rs)
        IType(LWU, "lwu", instr, Rt_Off_Rs)
        IType(ORI, "ori", instr, Rt_Rs_XImm)
        IType(SB, "sb", instr, Rt_Off_Rs)
        IType(SC, "sc", instr, Rt_Off_Rs)
        IType(SCD, "scd", instr, Rt_Off_Rs)
        IType(SD, "sd", instr, Rt_Off_Rs)
        IType(SDC1, "sdc1", instr, CRt_Off_Rs)
        IType(SDC2, "sdc2", instr, CRt_Off_Rs)
        IType(SDL, "sdl", instr, Rt_Off_Rs)
        IType(SDR, "sdr", instr, Rt_Off_Rs)
        IType(SH, "sh", instr, Rt_Off_Rs)
        IType(SLTI, "slti", instr, Rt_Rs_Imm)
        IType(SLTIU, "sltiu", instr, Rt_Rs_Imm)
        IType(SW, "sw", instr, Rt_Off_Rs)
        IType(SWC1, "swc1", instr, CRt_Off_Rs)
        IType(SWC2, "swc2", instr, CRt_Off_Rs)
        IType(SWC3, "swc3", instr, CRt_Off_Rs)
        IType(SWL, "swl", instr, Rt_Off_Rs)
        IType(SWR, "swr", instr, Rt_Off_Rs)
        IType(XORI, "xori", instr, Rt_Rs_XImm)
        default:
            Unknown(instr);
            return;
    }
}

}; /* namespace Mips */
