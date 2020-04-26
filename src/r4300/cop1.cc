
#include <iostream>
#include <cstring>
#include <cmath>
#include <cfenv>

#include <debugger.h>
#include <memory.h>
#include <mips/asm.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <r4300/eval.h>

#include "cpu.h"

using namespace R4300;

/**
 * @brief Preprocessor template for R-type instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamnt in a new scope.
 *
 * @param opcode            Instruction opcode
 * @param instr             Original instruction
 * @param ...               Instruction implementation
 */
#define RType(opcode, instr, ...) \
    case opcode: { \
        u32 rd = Mips::getRd(instr); \
        u32 rs = Mips::getRs(instr); \
        u32 rt = Mips::getRt(instr); \
        u32 shamnt = Mips::getShamnt(instr); \
        (void)rd; (void)rs; (void)rt; (void)shamnt; \
        __VA_ARGS__; \
        break; \
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
#define FRType(opcode, instr, ...) \
    case opcode: { \
        u32 fd = Mips::getFd(instr); \
        u32 fs = Mips::getFs(instr); \
        u32 ft = Mips::getFt(instr); \
        (void)fd; (void)fs; (void)ft; \
        __VA_ARGS__; \
        break; \
    }

#define checkCop1Usable() \
    if (!CU1()) { \
        Eval::takeException(CoprocessorUnusable, 0, \
                            delaySlot, instr, false, 1u); \
        return; \
    }

namespace R4300 {

/**
 * @brief Configure the memory aliases for single and double word access
 *  to the floating point registers.
 */
void cp1reg::setFprAliases(bool fr)
{
    if (fr) {
        for (unsigned int r = 0; r < 32; r++) {
            fpr_s[r] = (fpr_s_t *)&fpr[r];
            fpr_d[r] = (fpr_d_t *)&fpr[r];
        }
    } else {
        for (unsigned int r = 0; r < 32; r++) {
            fpr_s[r] = (fpr_s_t *)&fpr[r / 2] + r % 2;
            fpr_d[r] = (fpr_d_t *)&fpr[r / 2];
        }
    }
}

namespace Cop1 {

void eval(u32 instr, bool delaySlot)
{
    using namespace Mips::Copz;
    using namespace Mips::Cop1;

    switch (Mips::getFmt(instr)) {
        RType(MF, instr, {
            checkCop1Usable();
            state.reg.gpr[rt] = sign_extend<u64, u32>(state.cp1reg.fpr_s[rd]->w);
        })
        RType(DMF, instr, {
            debugger.halt("DMFC1 instruction");
            // NB: the instruction puts an undefined value in rd for
            // odd register access. To remove some checks, the instruction
            // returns the value as if read from the register rd - 1.
            // See \ref cp1reg::setFprAliases for details.
            checkCop1Usable();
            state.reg.gpr[rt] = state.cp1reg.fpr_d[rd]->l;
        })
        RType(Mips::Copz::CF, instr, {
            checkCop1Usable();
            switch (rd) {
                case 0: state.reg.gpr[rt] = state.cp1reg.fcr0; break;
                case 31: state.reg.gpr[rt] = state.cp1reg.fcr31; break;
                default:
                    debugger.halt("COP1::CF Unimplemented control register");
            }
        })
        RType(MT, instr, {
            checkCop1Usable();
            state.cp1reg.fpr_s[rd]->w = state.reg.gpr[rt];
        })
        RType(DMT, instr, {
            debugger.halt("DMTC1 instruction");
            // NB: the instruction presents an undefined behaviour for
            // odd register access. To remove some checks, the instruction
            // has for side effect to write as if the register index
            // were rt - 1. See \ref cp1reg::setFprAliases for details.
            checkCop1Usable();
            state.cp1reg.fpr_d[rd]->l = state.reg.gpr[rt];
        })
        RType(CT, instr, {
            checkCop1Usable();
            switch (rd) {
                case 0: state.cp1reg.fcr0 = state.reg.gpr[rt]; break;
                case 31: state.cp1reg.fcr31 = state.reg.gpr[rt]; break;
                default:
                    debugger.halt("COP1::CT Unimplemented control register");
            }
        })

        case Mips::Cop1::S:
            checkCop1Usable();
            switch (Mips::getFunct(instr)) {
                FRType(ADD, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_s[fd]->s =
                        state.cp1reg.fpr_s[fs]->s +
                        state.cp1reg.fpr_s[ft]->s;
                })
                FRType(SUB, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_s[fd]->s =
                        state.cp1reg.fpr_s[fs]->s -
                        state.cp1reg.fpr_s[ft]->s;
                })
                FRType(MUL, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_s[fd]->s =
                        state.cp1reg.fpr_s[fs]->s *
                        state.cp1reg.fpr_s[ft]->s;
                })
                FRType(DIV, instr, {
                    // TODO divide by zero
                    state.cp1reg.fpr_s[fd]->s =
                        state.cp1reg.fpr_s[fs]->s /
                        state.cp1reg.fpr_s[ft]->s;
                })
                FRType(SQRT, instr, {
                    // TODO inexact
                    state.cp1reg.fpr_s[fd]->s =
                        sqrt(state.cp1reg.fpr_s[fs]->s);
                })
                FRType(ABS, instr, {
                    state.cp1reg.fpr_s[fd]->s = ::fabs(state.cp1reg.fpr_s[fs]->s);
                })
                FRType(MOV, instr, {
                    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_s[fs]->s;
                })
                FRType(NEG, instr, {
                    if (std::isnan(state.cp1reg.fpr_s[fs]->s)) {
                        // TODO invalid operation exception is NaN
                        debugger.halt("COP1::S::NEG invalid operation");
                    }
                    state.cp1reg.fpr_s[fd]->s = -state.cp1reg.fpr_s[fs]->s;
                })
                FRType(ROUNDL, instr, {
                    int rmode = std::fegetround();
                    std::fesetround(FE_TONEAREST);
                    float in = ::nearbyint(state.cp1reg.fpr_s[fs]->s);
                    std::fesetround(rmode);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::S::ROUNDL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(TRUNCL, instr, {
                    float in = ::trunc(state.cp1reg.fpr_s[fs]->s);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::S::TRUNCL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(CEILL, instr, {
                    float in = ::ceil(state.cp1reg.fpr_s[fs]->s);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::S::CEILL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(FLOORL, instr, {
                    float in = ::floor(state.cp1reg.fpr_s[fs]->s);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::S::FLOORL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(ROUNDW, instr, {
                    int rmode = std::fegetround();
                    std::fesetround(FE_TONEAREST);
                    float in = ::nearbyint(state.cp1reg.fpr_s[fs]->s);
                    std::fesetround(rmode);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::S::ROUNDW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(TRUNCW, instr, {
                    float in = ::trunc(state.cp1reg.fpr_s[fs]->s);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::S::TRUNCW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(CEILW, instr, {
                    float in = ::ceil(state.cp1reg.fpr_s[fs]->s);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::S::CEILW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(FLOORW, instr, {
                    float in = ::floor(state.cp1reg.fpr_s[fs]->s);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::S::FLOORW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(CVTS, instr, {
                    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_s[fs]->s;
                })
                FRType(CVTD, instr, {
                    state.cp1reg.fpr_d[fd]->d = state.cp1reg.fpr_s[fs]->s;
                })
                FRType(CVTW, instr, {
                    state.cp1reg.fpr_s[fd]->w = state.cp1reg.fpr_s[fs]->s;
                })
                FRType(CVTL, instr, {
                    state.cp1reg.fpr_d[fd]->l = state.cp1reg.fpr_s[fs]->s;
                })
                case Mips::Cop1::CF:
                case CUN:
                case CEQ:
                case CUEQ:
                case COLT:
                case CULT:
                case COLE:
                case CULE:
                case CSF:
                case CNGLE:
                case CSEQ:
                case CNGL:
                case CLT:
                case CNGE:
                case CLE:
                FRType(CNGT, instr, {
                    float s = state.cp1reg.fpr_s[fs]->s;
                    float t = state.cp1reg.fpr_s[ft]->s;
                    u32 funct = Mips::getFunct(instr);
                    bool less;
                    bool equal;
                    bool unordered;

                    if (std::isnan(s) || std::isnan(t)) {
                        less = false;
                        equal = false;
                        unordered = true;
                        if (funct & 0x8lu) {
                            debugger.halt("COP1::S::COMP invalid operation");
                        }
                    } else {
                        less = s < t;
                        equal = s == t;
                        unordered = false;
                    }

                    bool condition =
                        ((funct & 0x4lu) != 0 && less) ||
                        ((funct & 0x2lu) != 0 && equal) ||
                        ((funct & 0x1lu) != 0 && unordered);
                    if (condition) {
                        // Sets Coprocessor unit 1 condition signal.
                        state.cp1reg.fcr31 |= FCR31_C;
                    } else {
                        // Clears Coprocessor unit 1 condition signal.
                        state.cp1reg.fcr31 &= ~FCR31_C;
                    }
                })
                default:
                    debugger.halt("COP1::S::* unsupported");
            }
            break;
        case Mips::Cop1::D:
            switch (Mips::getFunct(instr)) {
                FRType(ADD, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_d[fd]->d =
                        state.cp1reg.fpr_d[fs]->d +
                        state.cp1reg.fpr_d[ft]->d;
                })
                FRType(SUB, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_d[fd]->d =
                        state.cp1reg.fpr_d[fs]->d -
                        state.cp1reg.fpr_d[ft]->d;
                })
                FRType(MUL, instr, {
                    // TODO overflow
                    state.cp1reg.fpr_d[fd]->d =
                        state.cp1reg.fpr_d[fs]->d *
                        state.cp1reg.fpr_d[ft]->d;
                })
                FRType(DIV, instr, {
                    // TODO divide by zero
                    state.cp1reg.fpr_d[fd]->d =
                        state.cp1reg.fpr_d[fs]->d /
                        state.cp1reg.fpr_d[ft]->d;
                })
                FRType(SQRT, instr, {
                    // TODO inexact
                    state.cp1reg.fpr_d[fd]->d =
                        sqrt(state.cp1reg.fpr_d[fs]->d);
                })
                FRType(ABS, instr, {
                    state.cp1reg.fpr_d[fd]->d = ::fabs(state.cp1reg.fpr_d[fs]->d);
                })
                FRType(MOV, instr, {
                    state.cp1reg.fpr_d[fd]->d = state.cp1reg.fpr_d[fs]->d;
                })
                FRType(NEG, instr, {
                    if (std::isnan(state.cp1reg.fpr_d[fs]->d)) {
                        // TODO invalid operation exception is NaN
                        debugger.halt("COP1::D::NEG invalid operation");
                    }
                    state.cp1reg.fpr_d[fd]->d = -state.cp1reg.fpr_d[fs]->d;
                })
                FRType(ROUNDL, instr, {
                    int rmode = std::fegetround();
                    std::fesetround(FE_TONEAREST);
                    double in = ::nearbyint(state.cp1reg.fpr_d[fs]->d);
                    std::fesetround(rmode);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::D::ROUNDL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(TRUNCL, instr, {
                    double in = ::trunc(state.cp1reg.fpr_d[fs]->d);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::D::TRUNCL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(CEILL, instr, {
                    double in = ::ceil(state.cp1reg.fpr_d[fs]->d);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::D::CEILL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(FLOORL, instr, {
                    double in = ::floor(state.cp1reg.fpr_d[fs]->d);
                    int64_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT64_MAX || in < INT64_MIN) {
                        debugger.halt("COP1::D::FLOORL invalid operation");
                        out = INT64_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_d[fd]->l = out;
                })
                FRType(ROUNDW, instr, {
                    int rmode = std::fegetround();
                    std::fesetround(FE_TONEAREST);
                    double in = ::nearbyint(state.cp1reg.fpr_d[fs]->d);
                    std::fesetround(rmode);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::D::ROUNDW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(TRUNCW, instr, {
                    double in = ::trunc(state.cp1reg.fpr_d[fs]->d);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        // TODO invalid operation exception if enabled
                        debugger.halt("COP1::D::TRUNCW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(CEILW, instr, {
                    double in = ::ceil(state.cp1reg.fpr_d[fs]->d);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::D::CEILW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(FLOORW, instr, {
                    double in = ::floor(state.cp1reg.fpr_d[fs]->d);
                    int32_t out;
                    if (std::isnan(in) || std::isinf(in) ||
                        in > INT32_MAX || in < INT32_MIN) {
                        debugger.halt("COP1::D::FLOORW invalid operation");
                        out = INT32_MIN;
                    } else {
                        out = in;
                    }
                    state.cp1reg.fpr_s[fd]->w = out;
                })
                FRType(CVTS, instr, {
                    // TODO Rounding occurs with specified rounding mode
                    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_d[fs]->d;
                })
                FRType(CVTD, instr, {
                    // TODO Rounding occurs with specified rounding mode
                    state.cp1reg.fpr_d[fd]->l = state.cp1reg.fpr_d[fs]->d;
                })
                FRType(CVTW, instr, {
                    // TODO Rounding occurs with specified rounding mode
                    state.cp1reg.fpr_s[fd]->w = state.cp1reg.fpr_d[fs]->d;
                })
                FRType(CVTL, instr, {
                    // TODO Rounding occurs with specified rounding mode
                    state.cp1reg.fpr_d[fd]->l = state.cp1reg.fpr_d[fs]->d;
                })
                case Mips::Cop1::CF:
                case CUN:
                case CEQ:
                case CUEQ:
                case COLT:
                case CULT:
                case COLE:
                case CULE:
                case CSF:
                case CNGLE:
                case CSEQ:
                case CNGL:
                case CLT:
                case CNGE:
                case CLE:
                FRType(CNGT, instr, {
                    double s = state.cp1reg.fpr_d[fs]->d;
                    double t = state.cp1reg.fpr_d[ft]->d;
                    u32 funct = Mips::getFunct(instr);
                    bool less;
                    bool equal;
                    bool unordered;

                    if (std::isnan(s) || std::isnan(t)) {
                        less = false;
                        equal = false;
                        unordered = true;
                        if (funct & 0x8lu) {
                            debugger.halt("COP1::D::COMP invalid operation");
                        }
                    } else {
                        less = s < t;
                        equal = s == t;
                        unordered = false;
                    }

                    bool condition =
                        ((funct & 0x4lu) != 0 && less) ||
                        ((funct & 0x2lu) != 0 && equal) ||
                        ((funct & 0x1lu) != 0 && unordered);
                    if (condition) {
                        // Sets Coprocessor unit 1 condition signal.
                        state.cp1reg.fcr31 |= FCR31_C;
                    } else {
                        // Clears Coprocessor unit 1 condition signal.
                        state.cp1reg.fcr31 &= ~FCR31_C;
                    }
                })
                default:
                    debugger.halt("COP1::D::* unsupported");
            }
            break;
        case Mips::Cop1::W:
            checkCop1Usable();
            switch (Mips::getFunct(instr)) {
                FRType(CVTS, instr, {
                    state.cp1reg.fpr_s[fd]->s = (i32)state.cp1reg.fpr_s[fs]->w;
                })
                FRType(CVTD, instr, {
                    state.cp1reg.fpr_d[fd]->d = (i32)state.cp1reg.fpr_s[fs]->w;
                })
                FRType(CVTW, instr, {
                    state.cp1reg.fpr_s[fd]->w = (i32)state.cp1reg.fpr_s[fs]->w;
                })
                FRType(CVTL, instr, {
                    state.cp1reg.fpr_d[fd]->l = (i32)state.cp1reg.fpr_s[fs]->w;
                })
                default:
                    debugger.halt("COP1::W::* unsupported");
            }
            break;
        case Mips::Cop1::L:
            switch (Mips::getFunct(instr)) {
                FRType(CVTS, instr, {
                    state.cp1reg.fpr_s[fd]->s = (i64)state.cp1reg.fpr_d[fs]->l;
                })
                FRType(CVTD, instr, {
                    state.cp1reg.fpr_d[fd]->d = (i64)state.cp1reg.fpr_d[fs]->l;
                })
                FRType(CVTW, instr, {
                    state.cp1reg.fpr_s[fd]->w = (i64)state.cp1reg.fpr_d[fs]->l;
                })
                FRType(CVTL, instr, {
                    state.cp1reg.fpr_d[fd]->l = (i64)state.cp1reg.fpr_d[fs]->l;
                })
                default:
                    debugger.halt("COP1::L::* unsupported");
            }
            break;

        case Mips::Copz::BC:
            switch (Mips::getRt(instr)) {
                case Mips::Copz::BCF: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if ((state.cp1reg.fcr31 & FCR31_C) == 0) {
                        state.cpu.nextAction = State::Action::Delay;
                        state.cpu.nextPc = state.reg.pc + 4 + (i64)(offset << 2);
                    }
                    break;
                }
                case Mips::Copz::BCFL: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if (((state.cp1reg.fcr31 & FCR31_C) == 0)) {
                        state.cpu.nextAction = State::Action::Delay;
                        state.cpu.nextPc = state.reg.pc + 4 + (i64)(offset << 2);
                    } else {
                        state.reg.pc += 4;
                    }
                    break;
                }
                case Mips::Copz::BCT: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if ((state.cp1reg.fcr31 & FCR31_C) != 0) {
                        state.cpu.nextAction = State::Action::Delay;
                        state.cpu.nextPc = state.reg.pc + 4 + (i64)(offset << 2);
                    }
                    break;
                }
                case Mips::Copz::BCTL: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if (((state.cp1reg.fcr31 & FCR31_C) != 0)) {
                        state.cpu.nextAction = State::Action::Delay;
                        state.cpu.nextPc = state.reg.pc + 4 + (i64)(offset << 2);
                    } else {
                        state.reg.pc += 4;
                    }
                    break;
                }
                default:
                    debugger.halt("COP1::BC invalid branch condition");
            }
            break;

        default:
            std::cerr << "fmt is " << std::hex << Mips::getFmt(instr) << std::endl;
            std::cerr << "instr is " << std::hex << instr << std::endl;
            debugger.halt("COP1::* unsupported instruction");
            break;
    }
}

}; /* Cop1 */

}; /* namespace R4300 */
