
#include <iomanip>
#include <iostream>
#include <sstream>

#include <assembly/disassembler.h>
#include <assembly/opcodes.h>
#include <assembly/registers.h>

using namespace n64::assembly;

namespace n64::assembly {

namespace cpu {

const char *RegisterNames[32] = {
    "zero",         "at",           "v0",           "v1",
    "a0",           "a1",           "a2",           "a3",
    "t0",           "t1",           "t2",           "t3",
    "t4",           "t5",           "t6",           "t7",
    "s0",           "s1",           "s2",           "s3",
    "s4",           "s5",           "s6",           "s7",
    "t8",           "t9",           "k0",           "k1",
    "gp",           "sp",           "fp",           "ra",
};

const char *Cop0RegisterNames[32] = {
    "index",        "random",       "entrylo0",     "entrylo1",
    "context",      "pagemask",     "wired",        "c7",
    "badvaddr",     "count",        "entryhi",      "compare",
    "sr",           "cause",        "epc",          "prid",
    "config",       "lladdr",       "watchlo",      "watchhi",
    "xcontext",     "c21",          "c22",          "c23",
    "c24",          "c25",          "perr",         "cacheerr",
    "taglo",        "taghi",        "errorepc",     "c31",
};

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
        u32 rt = getRt(instr); \
        u32 rs = getRs(instr); \
        u16 imm = getImmediate(instr); \
        (void)rs; (void)rt; (void)imm; \
        buffer << std::setw(8) << std::setfill(' ') << std::left << name; \
        buffer << " " << std::setfill('0'); \
        IType_##fmt(rt, rs, imm); \
        break; \
    }

#define IType_Rt_Rs_Imm(rt, rs, imm) \
    buffer << cpu::getRegisterName(rt); \
    buffer << ", " << cpu::getRegisterName(rs); \
    buffer << ", " << std::dec << (i16)imm;

#define IType_Rt_Rs_XImm(rt, rs, imm) \
    buffer << cpu::getRegisterName(rt); \
    buffer << ", " << cpu::getRegisterName(rs); \
    buffer << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_XImm(rt, rs, imm) \
    buffer << cpu::getRegisterName(rt); \
    buffer << ", 0x" << std::hex << std::left << imm;

#define IType_Rt_Off_Rs(rt, rs, off) \
    i16 soff = (i16)off; \
    buffer << cpu::getRegisterName(rt) << std::hex; \
    if (soff < 0) \
        buffer << ", -0x" << (u16)(-soff); \
    else \
        buffer << ", 0x" << (u16)off; \
    buffer << "(" << cpu::getRegisterName(rs) << ")";

#define IType_CRt_Off_Rs(rt, rs, off) \
    i16 soff = (i16)off; \
    buffer << "cr" << std::dec << rt << std::hex; \
    if (soff < 0) \
        buffer << ", -0x" << (u16)(-soff); \
    else \
        buffer << ", 0x" << (u16)off; \
    buffer << "(" << cpu::getRegisterName(rs) << ")";

#define IType_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << std::hex << "0x" << (pc + 4 + off64);

#define IType_Rs_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << cpu::getRegisterName(rs); \
    buffer << ", 0x" << std::hex << (pc + 4 + off64);

#define IType_Rs_Rt_Tg(rt, rs, off) \
    i64 off64 = (i16)(4 * off); \
    buffer << cpu::getRegisterName(rs); \
    buffer << ", " << cpu::getRegisterName(rt); \
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
        u64 tg = (pc & 0xfffffffff0000000llu) | (4 * getTarget(instr)); \
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
        u32 rd = getRd(instr); \
        u32 rs = getRs(instr); \
        u32 rt = getRt(instr); \
        u32 shamnt = getShamnt(instr); \
        (void)rd; (void)rs; (void)rt; (void)shamnt; \
        buffer << std::setw(8) << std::left << name << " "; \
        RType_##fmt(rd, rs, rt, shamnt); \
        break; \
    }

#define RType_Rd_Rs_Rt(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rd); \
    buffer << ", " << cpu::getRegisterName(rs); \
    buffer << ", " << cpu::getRegisterName(rt);

#define RType_Rd_Rt_Rs(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rd); \
    buffer << ", " << cpu::getRegisterName(rt); \
    buffer << ", " << cpu::getRegisterName(rs);

#define RType_Rs_Rt(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rs); \
    buffer << ", " << cpu::getRegisterName(rt);

#define RType_Rd_Rs(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rd); \
    buffer << ", " << cpu::getRegisterName(rs);

#define RType_Rs(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rs);

#define RType_Rd(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rd);

#define RType_Rd_Rt_Shamnt(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rd); \
    buffer << ", " << cpu::getRegisterName(rt); \
    buffer << ", " << std::dec << shamnt;

#define RType_Rt_CRd(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rt); \
    buffer << ", c" << std::dec << rd;

#define RType_Rt_C0Rd(rd, rs, rt, shamnt) \
    buffer << cpu::getRegisterName(rt); \
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
        u32 fd = getFd(instr); \
        u32 fs = getFs(instr); \
        u32 ft = getFt(instr); \
        (void)fd; (void)fs; (void)ft; \
        std::string nameFmt = name; \
        nameFmt += "."; \
        nameFmt += getFmtName(getFmt(instr)); \
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
    if (instr & (1lu << 25)) {
        switch (getFunct(instr)) {
            SType(TLBR, "tlbr")
            SType(TLBWI, "tlbwi")
            SType(TLBWR, "tlbwr")
            SType(TLBP, "tlbp")
            SType(ERET, "eret")
            default:
                Unknown(instr);
                break;
        }
    } else {
        switch (getRs(instr)) {
            RType(MFCz, "mfc0", instr, Rt_C0Rd)
            RType(DMFCz, "dmfc0", instr, Rt_C0Rd)
            RType(MTCz, "mtc0", instr, Rt_C0Rd)
            RType(DMTCz, "dmtc0", instr, Rt_C0Rd)
            RType(CFCz, "cfc0", instr, Rt_C0Rd)
            RType(CTCz, "ctc0", instr, Rt_C0Rd)
            case BCz:
                switch (getRt(instr)) {
                    IType(BCzF, "bc0f", instr, Tg)
                    IType(BCzT, "bc0t", instr, Tg)
                    IType(BCzFL, "bc0fl", instr, Tg)
                    IType(BCzTL, "bc0tl", instr, Tg)
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
    switch (getFunct(instr)) {
        FRType(FADD, "add", instr, Fd_Fs_Ft)
        FRType(FSUB, "sub", instr, Fd_Fs_Ft)
        FRType(FMUL, "mul", instr, Fd_Fs_Ft)
        FRType(FDIV, "div", instr, Fd_Fs_Ft)
        FRType(SQRT, "sqrt", instr, Fd_Fs)
        FRType(ABS, "abs", instr, Fd_Fs)
        FRType(MOV, "mov", instr, Fd_Fs)
        FRType(NEG, "neg", instr, Fd_Fs)
        FRType(ROUNDL, "round.l", instr, Fd_Fs)
        FRType(TRUNCL, "trunc.l", instr, Fd_Fs)
        FRType(CEILL, "ceil.l", instr, Fd_Fs)
        FRType(FLOORL, "floor.l", instr, Fd_Fs)
        FRType(ROUNDW, "round.w", instr, Fd_Fs)
        FRType(TRUNCW, "trunc.w", instr, Fd_Fs)
        FRType(CEILW, "ceil.w", instr, Fd_Fs)
        FRType(FLOORW, "floor.w", instr, Fd_Fs)
        FRType(CVTS, "cvt.s", instr, Fd_Fs)
        FRType(CVTD, "cvt.d", instr, Fd_Fs)
        FRType(CVTW, "cvt.w", instr, Fd_Fs)
        FRType(CVTL, "cvt.l", instr, Fd_Fs)
        FRType(CF, "c.f", instr, Fs_Ft)
        FRType(CUN, "c.un", instr, Fs_Ft)
        FRType(CEQ, "c.eq", instr, Fs_Ft)
        FRType(CUEQ, "c.ueq", instr, Fs_Ft)
        FRType(COLT, "c.olt", instr, Fs_Ft)
        FRType(CULT, "c.ult", instr, Fs_Ft)
        FRType(COLE, "c.ole", instr, Fs_Ft)
        FRType(CULE, "c.ule", instr, Fs_Ft)
        FRType(CSF, "c.sf", instr, Fs_Ft)
        FRType(CNGLE, "c.ngle", instr, Fs_Ft)
        FRType(CSEQ, "c.seq", instr, Fs_Ft)
        FRType(CNGL, "c.ngl", instr, Fs_Ft)
        FRType(CLT, "c.lt", instr, Fs_Ft)
        FRType(CNGE, "c.nge", instr, Fs_Ft)
        FRType(CLE, "c.le", instr, Fs_Ft)
        FRType(CNGT, "c.ngt", instr, Fs_Ft)
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

std::string disassemble(u64 pc, u32 instr)
{
    u32 opcode;
    std::ostringstream buffer;

    // Special case (SLL 0, 0, 0)
    if (instr == 0) {
        return "nop";
    }

    switch (opcode = getOpcode(instr)) {
        case SPECIAL:
            switch (getFunct(instr)) {
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
            switch (getRt(instr)) {
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
            if (instr & (1lu << 25)) { \
                disasCop##z(buffer, instr); \
                break; \
            } \
            switch (getRs(instr)) { \
                RType(MFCz, "mfc" #z, instr, Rt_CRd) \
                RType(DMFCz, "dmfc" #z, instr, Rt_CRd) \
                RType(MTCz, "mtc" #z, instr, Rt_CRd) \
                RType(DMTCz, "dmtc" #z, instr, Rt_CRd) \
                RType(CFCz, "cfc" #z, instr, Rt_CRd) \
                RType(CTCz, "ctc" #z, instr, Rt_CRd) \
                case BCz: \
                    switch (getRt(instr)) { \
                        IType(BCzF, "bc" #z "f", instr, Tg) \
                        IType(BCzT, "bc" #z "t", instr, Tg) \
                        IType(BCzFL, "bc" #z "fl", instr, Tg) \
                        IType(BCzTL, "bc" #z "tl", instr, Tg) \
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

}; /* namespace cpu */

namespace rsp {

const char *Cop0RegisterNames[32] = {
    "dma_cache",    "dma_dram",     "dma_rd_len",   "dma_wr_len",
    "sp_status",    "dma_full",     "dma_busy",     "sp_reserved",
    "cmd_start",    "cmd_end",      "cmd_current",  "cmd_status",
    "cmd_clock",    "cmd_busy",     "cmd_pipe_busy","cmd_tmem_busy",
    "c16",          "c17",          "c18",          "c19",
    "c20",          "c21",          "c22",          "c23",
    "c24",          "c25",          "c26",          "c27",
    "c28",          "c29",          "c30",          "c31",
};

/**
 * @brief Preprocessor template for vector load and store
 *  instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamt in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 */
#define VLSType(opcode, name, instr, offset_shift) \
    case opcode: { \
        u32 base = (instr >> 21) & 0x1flu; \
        u32 vt = (instr >> 16) & 0x1flu; \
        u32 element = (instr >> 7) & 0xflu; \
        i32 offset = i7_to_i32(instr & 0x7flu); \
        buffer << std::setw(8) << std::left << name << " "; \
        buffer << "v" << std::dec << vt; \
        buffer << "[" << element << "], "; \
        buffer << (offset << offset_shift) << "("; \
        buffer << cpu::getRegisterName(base) << ")" << std::endl; \
        break; \
    }

/**
 * @brief Preprocessor template for R-type vector instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as e, vd, vs, vt in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param name              Display name of the instruction
 * @param instr             Original instruction
 * @param fmt               Formatting to use to print the instruction
 */
#define VRType(opcode, name, instr, fmt) \
    case opcode: { \
        u32 vd = getVd(instr); \
        u32 vs = getVs(instr); \
        u32 vt = getVt(instr); \
        u32 e = (instr >> 21) & UINT32_C(0xf); \
        (void)vd; (void)vs; (void)vt; (void)e; \
        buffer << std::setw(8) << std::left << name << " "; \
        VRType_##fmt(vd, vs, vt, e); \
        break; \
    }

#define VRType_Vd_Vs(vd, vs, vt, e) \
    buffer << "v" << std::dec << vd; \
    buffer << ", v" << vs << "[" << e << "]";

#define VRType_Vd_Vs_Vt(vd, vs, vt, e) \
    buffer << "v" << std::dec << vd; \
    buffer << ", v" << vs; \
    buffer << ", v" << vt << "[" << e << "]";

#define VRType_Vd_Vde_Vt(vd, vs, vt, e) \
    buffer << "v" << std::dec << vd << "[" << vs << "]"; \
    buffer << ", v" << vt << "[" << e << "]";

static void disasCop0(std::ostringstream &buffer, u64 pc, u32 instr)
{
    switch (getRs(instr)) {
        RType(MFCz, "mfc0", instr, Rt_C0Rd)
        RType(MTCz, "mtc0", instr, Rt_C0Rd)
        default:
            Unknown(instr);
            break;
    }
}

static void disasCop2(std::ostringstream &buffer, u64 pc, u32 instr)
{
    if ((instr & (1lu << 25)) != 0) {
        switch (instr & 0x3flu) {
            VRType(0x13, "vabs", instr, Vd_Vs_Vt)
            VRType(0x10, "vadd", instr, Vd_Vs_Vt)
            VRType(0x14, "vaddc", instr, Vd_Vs_Vt)
            VRType(0x28, "vand", instr, Vd_Vs_Vt)
            VRType(0x25, "vch", instr, Vd_Vs_Vt)
            VRType(0x24, "vcl", instr, Vd_Vs_Vt)
            VRType(0x26, "vcr", instr, Vd_Vs_Vt)
            VRType(0x21, "veq", instr, Vd_Vs_Vt)
            VRType(0x23, "vge", instr, Vd_Vs_Vt)
            VRType(0x20, "vlt", instr, Vd_Vs_Vt)
            VRType(0x08, "vmacf", instr, Vd_Vs_Vt)
            VRType(0x0b, "vmacq", instr, Vd_Vs_Vt)
            VRType(0x09, "vmacu", instr, Vd_Vs_Vt)
            VRType(0x0f, "vmadh", instr, Vd_Vs_Vt)
            VRType(0x0c, "vmadl", instr, Vd_Vs_Vt)
            VRType(0x0d, "vmadm", instr, Vd_Vs_Vt)
            VRType(0x0e, "vmadn", instr, Vd_Vs_Vt)
            VRType(0x33, "vmov", instr, Vd_Vde_Vt)
            VRType(0x27, "vmrg", instr, Vd_Vs_Vt)
            VRType(0x07, "vmudh", instr, Vd_Vs_Vt)
            VRType(0x04, "vmudl", instr, Vd_Vs_Vt)
            VRType(0x05, "vmudm", instr, Vd_Vs_Vt)
            VRType(0x06, "vmudn", instr, Vd_Vs_Vt)
            VRType(0x00, "vmulf", instr, Vd_Vs_Vt)
            VRType(0x03, "vmulq", instr, Vd_Vs_Vt)
            VRType(0x01, "vmulu", instr, Vd_Vs_Vt)
            VRType(0x29, "vnand", instr, Vd_Vs_Vt)
            VRType(0x22, "vne", instr, Vd_Vs_Vt)
            VRType(0x37, "vnop", instr, Vd_Vs_Vt)
            VRType(0x2b, "vnor", instr, Vd_Vs_Vt)
            VRType(0x2d, "vnxor", instr, Vd_Vs_Vt)
            VRType(0x2a, "vor", instr, Vd_Vs_Vt)
            VRType(0x30, "vrcp", instr, Vd_Vde_Vt)
            VRType(0x32, "vrcph", instr, Vd_Vde_Vt)
            VRType(0x31, "vrcpl", instr, Vd_Vde_Vt)
            VRType(0x0a, "vrndn", instr, Vd_Vs_Vt)
            VRType(0x02, "vrndp", instr, Vd_Vs_Vt)
            VRType(0x34, "vrsq", instr, Vd_Vde_Vt)
            VRType(0x36, "vrsqh", instr, Vd_Vde_Vt)
            VRType(0x35, "vrsql", instr, Vd_Vde_Vt)
            VRType(0x1d, "vsar", instr, Vd_Vs_Vt)
            VRType(0x11, "vsub", instr, Vd_Vs_Vt)
            VRType(0x15, "vsubc", instr, Vd_Vs_Vt)
            VRType(0x2c, "vxor", instr, Vd_Vs_Vt)
            default:
                Unknown(instr);
                break;
        }
    } else {
        switch (getRs(instr)) {
            RType(MFCz, "mfc2", instr, Rt_C0Rd)
            RType(MTCz, "mtc2", instr, Rt_C0Rd)
            default:
                Unknown(instr);
                break;
        }
    }
}

std::string disassemble(u64 pc, u32 instr)
{
    std::ostringstream buffer;
    u32 opcode;

    // Special case (SLL 0, 0, 0)
    if (instr == 0) {
        return "nop";
    }

    switch (opcode = getOpcode(instr)) {
        case SPECIAL:
            switch (getFunct(instr)) {
                RType(ADD, "add", instr, Rd_Rs_Rt)
                RType(ADDU, "addu", instr, Rd_Rs_Rt)
                RType(AND, "and", instr, Rd_Rs_Rt)
                SType(BREAK, "break")
                RType(JALR, "jalr", instr, Rd_Rs)
                RType(JR, "jr", instr, Rs)
                /* MOVN, MOVZ */
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
                RType(XOR, "xor", instr, Rd_Rs_Rt)
                default:
                    Unknown(instr);
                    break;
            }
            break;

        case REGIMM:
            switch (getRt(instr)) {
                IType(BGEZ, "bgez", instr, Rs_Tg)
                IType(BGEZAL, "bgezal", instr, Rs_Tg)
                IType(BLTZ, "bltz", instr, Rs_Tg)
                IType(BLTZAL, "bltzal", instr, Rs_Tg)
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
        IType(BLEZ, "blez", instr, Rs_Rt_Tg)
        IType(BNE, "bne", instr, Rs_Rt_Tg)
        SType(CACHE, "cache")

        case COP0:
            disasCop0(buffer, pc, instr);
            break;
        case COP2:
            disasCop2(buffer, pc, instr);
            break;

        JType(J, "j", instr, pc)
        JType(JAL, "jal", instr, pc)
        IType(LB, "lb", instr, Rt_Off_Rs)
        IType(LBU, "lbu", instr, Rt_Off_Rs)
        IType(LH, "lh", instr, Rt_Off_Rs)
        IType(LHU, "lhu", instr, Rt_Off_Rs)
        IType(LUI, "lui", instr, Rt_XImm)
        IType(LW, "lw", instr, Rt_Off_Rs)
        case LWC2:
            switch ((instr >> 11) & 0x1flu) {
                VLSType(0x0, "lbv", instr, 0)
                VLSType(0x1, "lsv", instr, 1)
                VLSType(0x2, "llv", instr, 2)
                VLSType(0x3, "ldv", instr, 3)
                VLSType(0x4, "lqv", instr, 4)
                VLSType(0x5, "lrv", instr, 4)
                VLSType(0x6, "lpv", instr, 0)
                VLSType(0x7, "luv", instr, 0)
                VLSType(0x8, "lhv", instr, 0)
                VLSType(0x9, "lfv", instr, 0)
                VLSType(0xa, "lwv", instr, 4)
                VLSType(0xb, "ltv", instr, 4)
                default:
                    Unknown(instr);
                    break;
            }
            break;
        IType(ORI, "ori", instr, Rt_Rs_XImm)
        IType(SB, "sb", instr, Rt_Off_Rs)
        IType(SH, "sh", instr, Rt_Off_Rs)
        IType(SLTI, "slti", instr, Rt_Rs_Imm)
        IType(SLTIU, "sltiu", instr, Rt_Rs_Imm)
        IType(SW, "sw", instr, Rt_Off_Rs)
        case SWC2:
            switch ((instr >> 11) & 0x1flu) {
                VLSType(0x0, "sbv", instr, 0)
                VLSType(0x1, "ssv", instr, 1)
                VLSType(0x2, "slv", instr, 2)
                VLSType(0x3, "sdv", instr, 3)
                VLSType(0x4, "sqv", instr, 4)
                VLSType(0x5, "srv", instr, 4)
                VLSType(0x6, "spv", instr, 0)
                VLSType(0x7, "suv", instr, 3)
                VLSType(0x8, "shv", instr, 0)
                VLSType(0x9, "sfv", instr, 0)
                VLSType(0xa, "swv", instr, 4)
                VLSType(0xb, "stv", instr, 4)
                default:
                    Unknown(instr);
                    break;
            }
            break;
        IType(XORI, "xori", instr, Rt_Rs_XImm)
        default:
            Unknown(instr);
            break;
    }

    return buffer.str();
}

}; /* namespace rsp */

}; /* namespace n64::assembly */
