
#ifndef _INTERPRETER_H_INCLUDED_
#define _INTERPRETER_H_INCLUDED_

#include <r4300/state.h>
#include <types.h>

namespace interpreter {
namespace cpu {

/** Fetch and execute exactly one instruction at the current
 * program counter address. */
void eval(void);

/** Start and stop capturing a cpu trace. */
void start_capture(void);
void stop_capture(u64 address);

void eval_Reserved(u32 instr);

void eval_ADD(u32 instr);
void eval_ADDU(u32 instr);
void eval_AND(u32 instr);
void eval_BREAK(u32 instr);
void eval_DADD(u32 instr);
void eval_DADDU(u32 instr);
void eval_DDIV(u32 instr);
void eval_DDIVU(u32 instr);
void eval_DIV(u32 instr);
void eval_DIVU(u32 instr);
void eval_DMULT(u32 instr);
void eval_DMULTU(u32 instr);
void eval_DSLL(u32 instr);
void eval_DSLL32(u32 instr);
void eval_DSLLV(u32 instr);
void eval_DSRA(u32 instr);
void eval_DSRA32(u32 instr);
void eval_DSRAV(u32 instr);
void eval_DSRL(u32 instr);
void eval_DSRL32(u32 instr);
void eval_DSRLV(u32 instr);
void eval_DSUB(u32 instr);
void eval_DSUBU(u32 instr);
void eval_JALR(u32 instr);
void eval_JR(u32 instr);
void eval_MFHI(u32 instr);
void eval_MFLO(u32 instr);
void eval_MOVN(u32 instr);
void eval_MOVZ(u32 instr);
void eval_MTHI(u32 instr);
void eval_MTLO(u32 instr);
void eval_MULT(u32 instr);
void eval_MULTU(u32 instr);
void eval_NOR(u32 instr);
void eval_OR(u32 instr);
void eval_SLL(u32 instr);
void eval_SLLV(u32 instr);
void eval_SLT(u32 instr);
void eval_SLTU(u32 instr);
void eval_SRA(u32 instr);
void eval_SRAV(u32 instr);
void eval_SRL(u32 instr);
void eval_SRLV(u32 instr);
void eval_SUB(u32 instr);
void eval_SUBU(u32 instr);
void eval_SYNC(u32 instr);
void eval_SYSCALL(u32 instr);
void eval_TEQ(u32 instr);
void eval_TGE(u32 instr);
void eval_TGEU(u32 instr);
void eval_TLT(u32 instr);
void eval_TLTU(u32 instr);
void eval_TNE(u32 instr);
void eval_XOR(u32 instr);
void eval_BGEZ(u32 instr);
void eval_BGEZL(u32 instr);
void eval_BLTZ(u32 instr);
void eval_BLTZL(u32 instr);
void eval_BGEZAL(u32 instr);
void eval_BGEZALL(u32 instr);
void eval_BLTZAL(u32 instr);
void eval_BLTZALL(u32 instr);
void eval_TEQI(u32 instr);
void eval_TGEI(u32 instr);
void eval_TGEIU(u32 instr);
void eval_TLTI(u32 instr);
void eval_TLTIU(u32 instr);
void eval_TNEI(u32 instr);
void eval_ADDI(u32 instr);
void eval_ADDIU(u32 instr);
void eval_ANDI(u32 instr);
void eval_BEQ(u32 instr);
void eval_BEQL(u32 instr);
void eval_BGTZ(u32 instr);
void eval_BGTZL(u32 instr);
void eval_BLEZ(u32 instr);
void eval_BLEZL(u32 instr);
void eval_BNE(u32 instr);
void eval_BNEL(u32 instr);
void eval_CACHE(u32 instr);
void eval_COP2(u32 instr);
void eval_COP3(u32 instr);
void eval_DADDI(u32 instr);
void eval_DADDIU(u32 instr);
void eval_J(u32 instr);
void eval_JAL(u32 instr);
void eval_LB(u32 instr);
void eval_LBU(u32 instr);
void eval_LD(u32 instr);
void eval_LDC1(u32 instr);
void eval_LDC2(u32 instr);
void eval_LDL(u32 instr);
void eval_LDR(u32 instr);
void eval_LH(u32 instr);
void eval_LHU(u32 instr);
void eval_LL(u32 instr);
void eval_LLD(u32 instr);
void eval_LUI(u32 instr);
void eval_LW(u32 instr);
void eval_LWC1(u32 instr);
void eval_LWC2(u32 instr);
void eval_LWC3(u32 instr);
void eval_LWL(u32 instr);
void eval_LWR(u32 instr);
void eval_LWU(u32 instr);
void eval_ORI(u32 instr);
void eval_SB(u32 instr);
void eval_SC(u32 instr);
void eval_SCD(u32 instr);
void eval_SD(u32 instr);
void eval_SDC1(u32 instr);
void eval_SDC2(u32 instr);
void eval_SDL(u32 instr);
void eval_SDR(u32 instr);
void eval_SH(u32 instr);
void eval_SLTI(u32 instr);
void eval_SLTIU(u32 instr);
void eval_SW(u32 instr);
void eval_SWC1(u32 instr);
void eval_SWC2(u32 instr);
void eval_SWC3(u32 instr);
void eval_SWL(u32 instr);
void eval_SWR(u32 instr);
void eval_XORI(u32 instr);
void eval_SPECIAL(u32 instr);
void eval_REGIMM(u32 instr);
void eval_Instr(u32 instr);

void eval_MFC0(u32 instr);
void eval_DMFC0(u32 instr);
void eval_MTC0(u32 instr);
void eval_DMTC0(u32 instr);
void eval_CFC0(u32 instr);
void eval_CTC0(u32 instr);
void eval_TLBR(u32 instr);
void eval_TLBW(u32 instr);
void eval_TLBP(u32 instr);
void eval_ERET(u32 instr);
void eval_COP0(u32 instr);

void eval_MFC1(u32 instr);
void eval_DMFC1(u32 instr);
void eval_CFC1(u32 instr);
void eval_MTC1(u32 instr);
void eval_DMTC1(u32 instr);
void eval_CTC1(u32 instr);
void eval_BC1(u32 instr);
void eval_ADD_S(u32 instr);
void eval_SUB_S(u32 instr);
void eval_MUL_S(u32 instr);
void eval_DIV_S(u32 instr);
void eval_SQRT_S(u32 instr);
void eval_ABS_S(u32 instr);
void eval_MOV_S(u32 instr);
void eval_NEG_S(u32 instr);
void eval_ROUND_L_S(u32 instr);
void eval_TRUNC_L_S(u32 instr);
void eval_CEIL_L_S(u32 instr);
void eval_FLOOR_L_S(u32 instr);
void eval_ROUND_W_S(u32 instr);
void eval_TRUNC_W_S(u32 instr);
void eval_CEIL_W_S(u32 instr);
void eval_FLOOR_W_S(u32 instr);
void eval_CVT_D_S(u32 instr);
void eval_CVT_W_S(u32 instr);
void eval_CVT_L_S(u32 instr);
void eval_CMP_S(u32 instr);
void eval_ADD_D(u32 instr);
void eval_SUB_D(u32 instr);
void eval_MUL_D(u32 instr);
void eval_DIV_D(u32 instr);
void eval_SQRT_D(u32 instr);
void eval_ABS_D(u32 instr);
void eval_MOV_D(u32 instr);
void eval_NEG_D(u32 instr);
void eval_ROUND_L_D(u32 instr);
void eval_TRUNC_L_D(u32 instr);
void eval_CEIL_L_D(u32 instr);
void eval_FLOOR_L_D(u32 instr);
void eval_ROUND_W_D(u32 instr);
void eval_TRUNC_W_D(u32 instr);
void eval_CEIL_W_D(u32 instr);
void eval_FLOOR_W_D(u32 instr);
void eval_CVT_S_D(u32 instr);
void eval_CVT_W_D(u32 instr);
void eval_CVT_L_D(u32 instr);
void eval_CMP_D(u32 instr);
void eval_CVT_S_W(u32 instr);
void eval_CVT_D_W(u32 instr);
void eval_CVT_S_L(u32 instr);
void eval_CVT_D_L(u32 instr);
void eval_COP1_S(u32 instr);
void eval_COP1_D(u32 instr);
void eval_COP1_W(u32 instr);
void eval_COP1_L(u32 instr);
void eval_COP1(u32 instr);

/** Helper for branch instructions: update the state to branch to \p btrue
 * or \p bfalse depending on the tested condition \p cond. */
static inline void branch(bool cond, u64 btrue, u64 bfalse) {
    R4300::state.cpu.nextAction = R4300::State::Delay;
    R4300::state.cpu.nextPc = cond ? btrue : bfalse;
}

/** Helper for branch likely instructions: update the state to branch to
 * \p btrue or \p bfalse depending on the tested condition \p cond. */
static inline void branch_likely(bool cond, u64 btrue, u64 bfalse) {
    R4300::state.cpu.nextAction = cond ? R4300::State::Delay : R4300::State::Jump;
    R4300::state.cpu.nextPc = cond ? btrue : bfalse;
}

}; /* namespace cpu */
}; /* namespace interpreter */

#endif /* _INTERPRETER_H_INCLUDED_ */
