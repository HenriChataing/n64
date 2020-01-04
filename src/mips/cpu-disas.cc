
#include <iomanip>
#include <iostream>
#include <sstream>
#include <types.h>

#include "asm.h"

namespace Mips {
namespace CPU {

const char *colorGreen = "\x1b[32;1m";
const char *colorReset = "\x1b[0m";

const char *RegisterNames[32] = {
    "zero",     "at",       "v0",       "v1",
    "a0",       "a1",       "a2",       "a3",
    "t0",       "t1",       "t2",       "t3",
    "t4",       "t5",       "t6",       "t7",
    "s0",       "s1",       "s2",       "s3",
    "s4",       "s5",       "s6",       "s7",
    "t8",       "t9",       "k0",       "k1",
    "gp",       "sp",       "fp",       "ra",
};

const char *Cop0RegisterNames[32] = {
    "index",    "random",   "entrylo0", "entrylo1",
    "context",  "pagemask", "wired",    "$7",
    "badvaddr", "count",    "entryhi",  "compare",
    "sr",       "cause",    "epc",      "prid",
    "config",   "lladdr",   "watchlo",  "watchhi",
    "xcontext", "$21",      "$22",      "$23",
    "$24",      "$25",      "perr",     "cacheerr",
    "taglo",    "taghi",    "errorepc", "$31",
};

/** @brief Return the standardized name for a general purpose register. */
char const *getRegisterName(uint reg) {
    return (reg > 31) ? "?" : RegisterNames[reg];
}

/** @brief Return the standardized name for a coprocessor 0 register. */
char const *getCop0RegisterName(uint reg) {
    return (reg > 31) ? "?" : Cop0RegisterNames[reg];
}

#define Unknown(instr) \
    buffer << "?" << std::hex << std::setfill('0') << std::setw(8); \
    buffer << instr << "?";

/**
 * @brief Unparametrized instruction.
 */
#define SType(opcode, name) \
    case opcode: \
        buffer << name; \
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
        buffer << std::setw(8) << std::setfill(' ') << std::left << name; \
        buffer << " " << std::setfill('0'); \
        IType_##fmt(rt, rs, imm); \
        break; \
    }

#define IType_Rt_Rs_Imm(rt, rs, imm) \
    buffer << getRegisterName(rt); \
    buffer << ", " << getRegisterName(rs); \
    buffer << ", " << std::dec << (i16)imm;

#define IType_Rt_Rs_XImm(rt, rs, imm) \
    buffer << getRegisterName(rt); \
    buffer << ", " << getRegisterName(rs); \
    buffer << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_XImm(rt, rs, imm) \
    buffer << getRegisterName(rt); \
    buffer << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_Off_Rs(rt, rs, off) \
    i16 soff = (i16)off; \
    buffer << getRegisterName(rt) << std::hex; \
    if (soff < 0) \
        buffer << ", -0x" << (u16)(-soff); \
    else \
        buffer << ", 0x" << (u16)off; \
    buffer << "(" << getRegisterName(rs) << ")";

#define IType_CRt_Off_Rs(rt, rs, off) \
    i16 soff = (i16)off; \
    buffer << "cr" << std::dec << rt << std::hex; \
    if (soff < 0) \
        buffer << ", -0x" << (u16)(-soff); \
    else \
        buffer << ", 0x" << (u16)off; \
    buffer << "(" << getRegisterName(rs) << ")";

#define IType_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << std::hex << "0x" << (pc + 4 + off64);

#define IType_Rs_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << getRegisterName(rs); \
    buffer << ", 0x" << std::hex << (pc + 4 + off64);

#define IType_Rs_Rt_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << getRegisterName(rs); \
    buffer << ", " << getRegisterName(rt); \
    buffer << ", 0x" << std::hex << (pc + 4 + off64);

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
#define JType(opcode, name, instr, pc) \
    case opcode: { \
        u64 tg = (pc & 0xfffffffff0000000llu) | (4 * Mips::getTarget(instr)); \
        buffer << std::setw(8) << std::left << name << " 0x" << std::hex; \
        buffer << std::setfill('0') << std::setw(8) << std::right << tg; \
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
        buffer << std::setw(8) << std::left << name << " "; \
        RType_##fmt(rd, rs, rt, shamnt); \
        break; \
    }

#define RType_Rd_Rs_Rt(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rd); \
    buffer << ", " << getRegisterName(rs); \
    buffer << ", " << getRegisterName(rt);

#define RType_Rd_Rt_Rs(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rd); \
    buffer << ", " << getRegisterName(rt); \
    buffer << ", " << getRegisterName(rs);

#define RType_Rs_Rt(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rs); \
    buffer << ", " << getRegisterName(rt);

#define RType_Rd_Rs(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rd); \
    buffer << ", " << getRegisterName(rs);

#define RType_Rs(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rs);

#define RType_Rd(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rd);

#define RType_Rd_Rt_Shamnt(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rd); \
    buffer << ", " << getRegisterName(rt); \
    buffer << ", " << std::dec << shamnt;

#define RType_Rt_CRd(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rt); \
    buffer << ", cr" << std::dec << rd;

#define RType_Rt_C0Rd(rd, rs, rt, shamnt) \
    buffer << getRegisterName(rt); \
    buffer << ", " << getCop0RegisterName(rd);

/**
 * @brief Return the string representation for a format.
 */
static inline char const *getFmtName(u32 fmt) {
    switch (fmt) {
        case 16: return "s";
        case 17: return "d";
        case 20: return "w";
        case 21: return "l";
        default: return "?";
    }
}

/**
 * @brief Preprocessor template for R-type floating point instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as fd, fs, ft in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 * @param fmt               Formatting to use to print the instruction
 */
#define FRType(opcode, name, instr, fmt) \
    case opcode: { \
        u32 fd = Mips::getFd(instr); \
        u32 fs = Mips::getFs(instr); \
        u32 ft = Mips::getFt(instr); \
        (void)fd; (void)fs; (void)ft; \
        std::string nameFmt = name; \
        nameFmt += "."; \
        nameFmt += getFmtName(Mips::getFmt(instr)); \
        buffer << std::setw(8) << std::left << nameFmt << " "; \
        FRType_##fmt(fd, fs, ft); \
        break; \
    }

#define FRType_Fd_Fs(fd, fs, ft) \
    buffer << "f" << std::dec << fd; \
    buffer << ", f" << fs;

#define FRType_Fs_Ft(fd, fs, ft) \
    buffer << "f" << std::dec << fs; \
    buffer << ", f" << ft;

#define FRType_Fd_Fs_Ft(fd, fs, ft) \
    buffer << "f" << std::dec << fd; \
    buffer << ", f" << fs; \
    buffer << ", f" << ft;

static void disasCop0(std::ostringstream &buffer, u64 pc, u32 instr)
{
    using namespace Mips::Opcode;
    using namespace Mips::Cop0;
    using namespace Mips::Copz;

    if (instr & Mips::COFUN) {
        switch (Mips::getFunct(instr)) {
            SType(Cop0::TLBR, "tlbr")
            SType(Cop0::TLBWI, "tlbwi")
            SType(Cop0::TLBWR, "tlbwr")
            SType(Cop0::TLBP, "tlbp")
            SType(Cop0::ERET, "eret")
            default:
                Unknown(instr);
                break;
        }
    } else {
        switch (Mips::getRs(instr)) {
            RType(MF, "mfc0", instr, Rt_C0Rd)
            RType(DMF, "dmfc0", instr, Rt_C0Rd)
            RType(MT, "mtc0", instr, Rt_C0Rd)
            RType(DMT, "dmtc0", instr, Rt_C0Rd)
            RType(Copz::CF, "cfc0", instr, Rt_C0Rd)
            RType(CT, "ctc0", instr, Rt_C0Rd)
            case BC:
                switch (Mips::getRt(instr)) {
                    IType(BCF, "bc0f", instr, Tg)
                    IType(BCT, "bc0t", instr, Tg)
                    IType(BCFL, "bc0fl", instr, Tg)
                    IType(BCTL, "bc0tl", instr, Tg)
                    default:
                        Unknown(instr);
                        break;
                }
                break;
            default:
                Unknown(instr);
                break;
        }
    }
}

static void disasCop1(std::ostringstream &buffer, u32 instr)
{
    switch (Mips::getFunct(instr)) {
        FRType(Cop1::ADD, "add", instr, Fd_Fs_Ft)
        FRType(Cop1::SUB, "sub", instr, Fd_Fs_Ft)
        FRType(Cop1::MUL, "mul", instr, Fd_Fs_Ft)
        FRType(Cop1::DIV, "div", instr, Fd_Fs_Ft)
        FRType(Cop1::SQRT, "sqrt", instr, Fd_Fs)
        FRType(Cop1::ABS, "abs", instr, Fd_Fs)
        FRType(Cop1::MOV, "mov", instr, Fd_Fs)
        FRType(Cop1::NEG, "neg", instr, Fd_Fs)
        FRType(Cop1::ROUNDL, "round.l", instr, Fd_Fs)
        FRType(Cop1::TRUNCL, "trunc.l", instr, Fd_Fs)
        FRType(Cop1::CEILL, "ceil.l", instr, Fd_Fs)
        FRType(Cop1::FLOORL, "floor.l", instr, Fd_Fs)
        FRType(Cop1::ROUNDW, "round.w", instr, Fd_Fs)
        FRType(Cop1::TRUNCW, "trunc.w", instr, Fd_Fs)
        FRType(Cop1::CEILW, "ceil.w", instr, Fd_Fs)
        FRType(Cop1::FLOORW, "floor.w", instr, Fd_Fs)
        FRType(Cop1::CVTS, "cvt.s", instr, Fd_Fs)
        FRType(Cop1::CVTD, "cvt.d", instr, Fd_Fs)
        FRType(Cop1::CVTW, "cvt.w", instr, Fd_Fs)
        FRType(Cop1::CVTL, "cvt.l", instr, Fd_Fs)
        FRType(Cop1::CF, "c.f", instr, Fs_Ft)
        FRType(Cop1::CUN, "c.un", instr, Fs_Ft)
        FRType(Cop1::CEQ, "c.eq", instr, Fs_Ft)
        FRType(Cop1::CUEQ, "c.ueq", instr, Fs_Ft)
        FRType(Cop1::COLT, "c.olt", instr, Fs_Ft)
        FRType(Cop1::CULT, "c.ult", instr, Fs_Ft)
        FRType(Cop1::COLE, "c.ole", instr, Fs_Ft)
        FRType(Cop1::CULE, "c.ule", instr, Fs_Ft)
        FRType(Cop1::CSF, "c.sf", instr, Fs_Ft)
        FRType(Cop1::CNGLE, "c.ngle", instr, Fs_Ft)
        FRType(Cop1::CSEQ, "c.seq", instr, Fs_Ft)
        FRType(Cop1::CNGL, "c.ngl", instr, Fs_Ft)
        FRType(Cop1::CLT, "c.lt", instr, Fs_Ft)
        FRType(Cop1::CNGE, "c.nge", instr, Fs_Ft)
        FRType(Cop1::CLE, "c.le", instr, Fs_Ft)
        FRType(Cop1::CNGT, "c.ngt", instr, Fs_Ft)
    }
}

static void disasCop2(std::ostringstream &buffer, u32 instr)
{
    buffer << std::setw(8) << std::left << "cop2" << std::hex;
    buffer << " $" << std::setfill('0') << std::setw(8) << instr;
}

static void disasCop3(std::ostringstream &buffer, u32 instr)
{
    buffer << std::setw(8) << std::left << "cop3" << std::hex;
    buffer << " $" << std::setfill('0') << std::setw(8) << instr;
}

/**
 * @brief Print out an instruction.
 */
std::string disas(u64 pc, u32 instr)
{
    u32 opcode;
    std::ostringstream buffer;

    using namespace Mips::Opcode;
    using namespace Mips::Special;
    using namespace Mips::Regimm;
    using namespace Mips::Cop0;
    using namespace Mips::Copz;

    // Special case (SLL 0, 0, 0)
    if (instr == 0) {
        return "nop";
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
                    break;
            }
            break;

        case REGIMM:
            switch (Mips::getRt(instr)) {
                IType(BGEZ, "bgez", instr, Rs_Tg)
                IType(BGEZL, "bgezl", instr, Rs_Tg)
                IType(BGEZAL, "bgezal", instr, Rs_Tg)
                IType(BGEZALL, "bgezall", instr, Rs_Tg)
                IType(BLTZ, "bltz", instr, Rs_Tg)
                IType(BLTZL, "bltzl", instr, Rs_Tg)
                IType(BLTZAL, "bltzal", instr, Rs_Tg)
                IType(BLTZALL, "bltzall", instr, Rs_Tg)
                default:
                    Unknown(instr);
                    break;
            }
            break;

        IType(ADDI, "addi", instr, Rt_Rs_Imm)
        IType(ADDIU, "addiu", instr, Rt_Rs_XImm)
        IType(ANDI, "andi", instr, Rt_Rs_XImm)
        IType(BEQ, "beq", instr, Rs_Rt_Tg)
        IType(BEQL, "beql", instr, Rs_Rt_Tg)
        IType(BGTZ, "bgtz", instr, Rs_Rt_Tg)
        IType(BGTZL, "bgtzl", instr, Rs_Rt_Tg)
        IType(BLEZ, "blez", instr, Rs_Rt_Tg)
        IType(BLEZL, "blezl", instr, Rs_Rt_Tg)
        IType(BNE, "bne", instr, Rs_Rt_Tg)
        IType(BNEL, "bnel", instr, Rs_Rt_Tg)
        SType(CACHE, "cache")

        case COP0:
            disasCop0(buffer, pc, instr);
            break;

#define COPz(z) \
        case COP##z: \
            if (instr & Mips::COFUN) { \
                disasCop##z(buffer, instr); \
                break; \
            } \
            switch (Mips::getRs(instr)) { \
                RType(MF, "mfc" #z, instr, Rt_CRd) \
                RType(DMF, "dmfc" #z, instr, Rt_CRd) \
                RType(MT, "mtc" #z, instr, Rt_CRd) \
                RType(DMT, "dmtc" #z, instr, Rt_CRd) \
                RType(Copz::CF, "cfc" #z, instr, Rt_CRd) \
                RType(CT, "ctc" #z, instr, Rt_CRd) \
                case BC: \
                    switch (Mips::getRt(instr)) { \
                        IType(BCF, "bc" #z "f", instr, Tg) \
                        IType(BCT, "bc" #z "t", instr, Tg) \
                        IType(BCFL, "bc" #z "fl", instr, Tg) \
                        IType(BCTL, "bc" #z "tl", instr, Tg) \
                        default: \
                            Unknown(instr); \
                            break; \
                    } \
                    break; \
                default: \
                    Unknown(instr); \
                    break; \
            } \
            break;

        COPz(1)
        COPz(2)
        COPz(3)

        IType(DADDI, "daddi", instr, Rt_Rs_Imm)
        IType(DADDIU, "daddiu", instr, Rt_Rs_XImm)
        JType(J, "j", instr, pc)
        JType(JAL, "jal", instr, pc)
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
            break;
    }

    return buffer.str();
}

}; /* namespace CPU */
}; /* namespace Mips */