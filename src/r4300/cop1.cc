
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
#include <r4300/cpu.h>

using namespace R4300;
using namespace R4300::Eval;

/**
 * @brief Preprocessor template for R-type instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as rd, rs, rt, shamnt in a new scope.
 *
 * @param instr             Original instruction
 */
#define RType(instr) \
    u32 rd = Mips::getRd(instr); \
    u32 rs = Mips::getRs(instr); \
    u32 rt = Mips::getRt(instr); \
    u32 shamnt = Mips::getShamnt(instr); \
    (void)rd; (void)rs; (void)rt; (void)shamnt;

/**
 * @brief Preprocessor template for R-type floating point instructions.
 *
 * The registers are automatically extracted from the instruction and added
 * as fd, fs, ft in a new scope.
 *
 * @param instr             Original instruction
 */
#define FRType(instr) \
    u32 fd = Mips::getFd(instr); \
    u32 fs = Mips::getFs(instr); \
    u32 ft = Mips::getFt(instr); \
    (void)fd; (void)fs; (void)ft;

namespace R4300 {

/**
 * @brief Configure the memory aliases for single and double word access
 *  to the floating point registers.
 */
void cp1reg::setFprAliases(bool fr) {
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

namespace Eval {

void eval_MFC1(u32 instr) {
    RType(instr);
    state.reg.gpr[rt] = sign_extend<u64, u32>(state.cp1reg.fpr_s[rd]->w);
}

void eval_DMFC1(u32 instr) {
    RType(instr);
    // NB: the instruction puts an undefined value in rd for
    // odd register access. To remove some checks, the instruction
    // returns the value as if read from the register rd - 1.
    // See \ref cp1reg::setFprAliases for details.
    state.reg.gpr[rt] = state.cp1reg.fpr_d[rd]->l;
    debugger::halt("DMFC1 instruction");
}

void eval_CFC1(u32 instr) {
    RType(instr);
    switch (rd) {
        case 0:  state.reg.gpr[rt] = state.cp1reg.fcr0; break;
        case 31: state.reg.gpr[rt] = state.cp1reg.fcr31; break;
        default:
            debugger::halt("COP1::CF Unimplemented control register");
    }
}

void eval_MTC1(u32 instr) {
    RType(instr);
    state.cp1reg.fpr_s[rd]->w = state.reg.gpr[rt];
}

void eval_DMTC1(u32 instr) {
    RType(instr);
    // NB: the instruction presents an undefined behaviour for
    // odd register access. To remove some checks, the instruction
    // has for side effect to write as if the register index
    // were rt - 1. See \ref cp1reg::setFprAliases for details.
    state.cp1reg.fpr_d[rd]->l = state.reg.gpr[rt];
}

void eval_CTC1(u32 instr) {
    RType(instr);
    switch (rd) {
        case 0:  state.cp1reg.fcr0  = state.reg.gpr[rt]; break;
        case 31: state.cp1reg.fcr31 = state.reg.gpr[rt]; break;
        default:
            debugger::halt("COP1::CT Unimplemented control register");
    }
}

void eval_BC1(u32 instr) {

    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
    bool taken = false;
    bool likely = false;

    switch (Mips::getRt(instr)) {
    case Mips::Copz::BCF:
        taken = (state.cp1reg.fcr31 & FCR31_C) == 0;
        break;
    case Mips::Copz::BCFL:
        taken = (state.cp1reg.fcr31 & FCR31_C) == 0;
        likely = true;
        break;
    case Mips::Copz::BCT:
        taken = (state.cp1reg.fcr31 & FCR31_C) != 0;
        break;
    case Mips::Copz::BCTL:
        taken = (state.cp1reg.fcr31 & FCR31_C) != 0;
        likely = true;
        break;
    default:
        debugger::halt("COP1::BC::* invalid instruction");
        break;
    }

    extern u64 captureEnd;
    captureEnd = state.reg.pc + 8;

    if (taken) {
        state.cpu.nextAction = State::Action::Delay;
        state.cpu.nextPc = state.reg.pc + 4 + (i64)(offset << 2);
    } else if (likely) {
        state.reg.pc += 4;
    }
}

void eval_ADD_S(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s =
        state.cp1reg.fpr_s[fs]->s +
        state.cp1reg.fpr_s[ft]->s;
}

void eval_SUB_S(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s =
        state.cp1reg.fpr_s[fs]->s -
        state.cp1reg.fpr_s[ft]->s;
}

void eval_MUL_S(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s =
        state.cp1reg.fpr_s[fs]->s *
        state.cp1reg.fpr_s[ft]->s;
}

void eval_DIV_S(u32 instr) {
    // TODO divide by zero
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s =
        state.cp1reg.fpr_s[fs]->s /
        state.cp1reg.fpr_s[ft]->s;
}

void eval_SQRT_S(u32 instr) {
    // TODO inexact
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s =
        sqrt(state.cp1reg.fpr_s[fs]->s);
}

void eval_ABS_S(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s = ::fabs(state.cp1reg.fpr_s[fs]->s);
}

void eval_MOV_S(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_s[fs]->s;
}

void eval_NEG_S(u32 instr) {
    FRType(instr);
    if (std::isnan(state.cp1reg.fpr_s[fs]->s)) {
        // TODO invalid operation exception is NaN
        debugger::halt("COP1::S::NEG invalid operation");
    }
    state.cp1reg.fpr_s[fd]->s = -state.cp1reg.fpr_s[fs]->s;
}

void eval_ROUND_L_S(u32 instr) {
    FRType(instr);

    int rmode = std::fegetround();
    std::fesetround(FE_TONEAREST);
    float in = ::nearbyint(state.cp1reg.fpr_s[fs]->s);
    std::fesetround(rmode);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::S::ROUNDL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_TRUNC_L_S(u32 instr) {
    FRType(instr);

    float in = ::trunc(state.cp1reg.fpr_s[fs]->s);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::S::TRUNCL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_CEIL_L_S(u32 instr) {
    FRType(instr);

    float in = ::ceil(state.cp1reg.fpr_s[fs]->s);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::S::CEILL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_FLOOR_L_S(u32 instr) {
    FRType(instr);

    float in = ::floor(state.cp1reg.fpr_s[fs]->s);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::S::FLOORL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_ROUND_W_S(u32 instr) {
    FRType(instr);

    int rmode = std::fegetround();
    std::fesetround(FE_TONEAREST);
    float in = ::nearbyint(state.cp1reg.fpr_s[fs]->s);
    std::fesetround(rmode);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::S::ROUNDW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_TRUNC_W_S(u32 instr) {
    FRType(instr);

    float in = ::trunc(state.cp1reg.fpr_s[fs]->s);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::S::TRUNCW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_CEIL_W_S(u32 instr) {
    FRType(instr);

    float in = ::ceil(state.cp1reg.fpr_s[fs]->s);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::S::CEILW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_FLOOR_W_S(u32 instr) {
    FRType(instr);

    float in = ::floor(state.cp1reg.fpr_s[fs]->s);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::S::FLOORW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_CVT_D_S(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d = state.cp1reg.fpr_s[fs]->s;
}

void eval_CVT_W_S(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_s[fd]->w = state.cp1reg.fpr_s[fs]->s;
}

void eval_CVT_L_S(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_d[fd]->l = state.cp1reg.fpr_s[fs]->s;
}

void eval_CMP_S(u32 instr) {
    FRType(instr);

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
            debugger::halt("COP1::S::COMP invalid operation");
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
}

void eval_ADD_D(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d =
        state.cp1reg.fpr_d[fs]->d +
        state.cp1reg.fpr_d[ft]->d;
}

void eval_SUB_D(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d =
        state.cp1reg.fpr_d[fs]->d -
        state.cp1reg.fpr_d[ft]->d;
}

void eval_MUL_D(u32 instr) {
    // TODO overflow
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d =
        state.cp1reg.fpr_d[fs]->d *
        state.cp1reg.fpr_d[ft]->d;
}

void eval_DIV_D(u32 instr) {
    // TODO divide by zero
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d =
        state.cp1reg.fpr_d[fs]->d /
        state.cp1reg.fpr_d[ft]->d;
}

void eval_SQRT_D(u32 instr) {
    // TODO inexact
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d =
        sqrt(state.cp1reg.fpr_d[fs]->d);
}

void eval_ABS_D(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d = ::fabs(state.cp1reg.fpr_d[fs]->d);
}

void eval_MOV_D(u32 instr) {
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d = state.cp1reg.fpr_d[fs]->d;
}

void eval_NEG_D(u32 instr) {
    FRType(instr);
    if (std::isnan(state.cp1reg.fpr_d[fs]->d)) {
        // TODO invalid operation exception is NaN
        debugger::halt("COP1::D::NEG invalid operation");
    }
    state.cp1reg.fpr_d[fd]->d = -state.cp1reg.fpr_d[fs]->d;
}

void eval_ROUND_L_D(u32 instr) {
    FRType(instr);

    int rmode = std::fegetround();
    std::fesetround(FE_TONEAREST);
    double in = ::nearbyint(state.cp1reg.fpr_d[fs]->d);
    std::fesetround(rmode);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::D::ROUNDL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_TRUNC_L_D(u32 instr) {
    FRType(instr);

    double in = ::trunc(state.cp1reg.fpr_d[fs]->d);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::D::TRUNCL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_CEIL_L_D(u32 instr) {
    FRType(instr);

    double in = ::ceil(state.cp1reg.fpr_d[fs]->d);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::D::CEILL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_FLOOR_L_D(u32 instr) {
    FRType(instr);

    double in = ::floor(state.cp1reg.fpr_d[fs]->d);
    int64_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT64_MAX || in < INT64_MIN) {
        debugger::halt("COP1::D::FLOORL invalid operation");
        out = INT64_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_d[fd]->l = out;
}

void eval_ROUND_W_D(u32 instr) {
    FRType(instr);

    int rmode = std::fegetround();
    std::fesetround(FE_TONEAREST);
    double in = ::nearbyint(state.cp1reg.fpr_d[fs]->d);
    std::fesetround(rmode);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::D::ROUNDW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_TRUNC_W_D(u32 instr) {
    FRType(instr);

    double in = ::trunc(state.cp1reg.fpr_d[fs]->d);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        // TODO invalid operation exception if enabled
        debugger::halt("COP1::D::TRUNCW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_CEIL_W_D(u32 instr) {
    FRType(instr);

    double in = ::ceil(state.cp1reg.fpr_d[fs]->d);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::D::CEILW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_FLOOR_W_D(u32 instr) {
    FRType(instr);

    double in = ::floor(state.cp1reg.fpr_d[fs]->d);
    int32_t out;
    if (std::isnan(in) || std::isinf(in) ||
        in > INT32_MAX || in < INT32_MIN) {
        debugger::halt("COP1::D::FLOORW invalid operation");
        out = INT32_MIN;
    } else {
        out = in;
    }
    state.cp1reg.fpr_s[fd]->w = out;
}

void eval_CVT_S_D(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_d[fs]->d;
}

void eval_CVT_W_D(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_s[fd]->w = state.cp1reg.fpr_d[fs]->d;
}

void eval_CVT_L_D(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_d[fd]->l = state.cp1reg.fpr_d[fs]->d;
}

void eval_CMP_D(u32 instr) {
    FRType(instr);

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
            debugger::halt("COP1::D::COMP invalid operation");
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
}

void eval_CVT_S_W(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s = (i32)state.cp1reg.fpr_s[fs]->w;
}

void eval_CVT_D_W(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d = (i32)state.cp1reg.fpr_s[fs]->w;
}

void eval_CVT_S_L(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_s[fd]->s = (i64)state.cp1reg.fpr_d[fs]->l;
}

void eval_CVT_D_L(u32 instr) {
    // TODO Rounding occurs with specified rounding mode
    FRType(instr);
    state.cp1reg.fpr_d[fd]->d = (i64)state.cp1reg.fpr_d[fs]->l;
}

void (*COP1_S_callbacks[64])(u32) = {
    eval_ADD_S,     eval_SUB_S,     eval_MUL_S,     eval_DIV_S,
    eval_SQRT_S,    eval_ABS_S,     eval_MOV_S,     eval_NEG_S,
    eval_ROUND_L_S, eval_TRUNC_L_S, eval_CEIL_L_S,  eval_FLOOR_L_S,
    eval_ROUND_W_S, eval_TRUNC_W_S, eval_CEIL_W_S,  eval_FLOOR_W_S,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_CVT_D_S,   eval_Reserved,  eval_Reserved,
    eval_CVT_W_S,   eval_CVT_L_S,   eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CMP_S,     eval_CMP_S,     eval_CMP_S,     eval_CMP_S,
    eval_CMP_S,     eval_CMP_S,     eval_CMP_S,     eval_CMP_S,
    eval_CMP_S,     eval_CMP_S,     eval_CMP_S,     eval_CMP_S,
    eval_CMP_S,     eval_CMP_S,     eval_CMP_S,     eval_CMP_S,
};

void eval_COP1_S(u32 instr) {
    COP1_S_callbacks[Mips::getFunct(instr)](instr);
}

void (*COP1_D_callbacks[64])(u32) = {
    eval_ADD_D,     eval_SUB_D,     eval_MUL_D,     eval_DIV_D,
    eval_SQRT_D,    eval_ABS_D,     eval_MOV_D,     eval_NEG_D,
    eval_ROUND_L_D, eval_TRUNC_L_D, eval_CEIL_L_D,  eval_FLOOR_L_D,
    eval_ROUND_W_D, eval_TRUNC_W_D, eval_CEIL_W_D,  eval_FLOOR_W_D,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CVT_S_D,   eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CVT_W_D,   eval_CVT_L_D,   eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CMP_D,     eval_CMP_D,     eval_CMP_D,     eval_CMP_D,
    eval_CMP_D,     eval_CMP_D,     eval_CMP_D,     eval_CMP_D,
    eval_CMP_D,     eval_CMP_D,     eval_CMP_D,     eval_CMP_D,
    eval_CMP_D,     eval_CMP_D,     eval_CMP_D,     eval_CMP_D,
};

void eval_COP1_D(u32 instr) {
    COP1_D_callbacks[Mips::getFunct(instr)](instr);
}

void (*COP1_W_callbacks[64])(u32) = {
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CVT_S_W,   eval_CVT_D_W,   eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

void eval_COP1_W(u32 instr) {
    COP1_W_callbacks[Mips::getFunct(instr)](instr);
}

void (*COP1_L_callbacks[64])(u32) = {
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_CVT_S_L,   eval_CVT_D_L,   eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

void eval_COP1_L(u32 instr) {
    COP1_L_callbacks[Mips::getFunct(instr)](instr);
}

void (*COP1_callbacks[64])(u32) = {
    eval_MFC1,      eval_DMFC1,     eval_CFC1,      eval_Reserved,
    eval_MTC1,      eval_DMTC1,     eval_CTC1,      eval_Reserved,
    eval_BC1,       eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_COP1_S,    eval_COP1_D,    eval_Reserved,  eval_Reserved,
    eval_COP1_W,    eval_COP1_L,    eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
    eval_Reserved,  eval_Reserved,  eval_Reserved,  eval_Reserved,
};

void eval_COP1(u32 instr) {
    if (!state.cp0reg.CU1()) {
        Eval::takeException(CoprocessorUnusable, 0, false, false, 1u);
    } else {
        COP1_callbacks[Mips::getFmt(instr)](instr);
    }
}

}; /* namespace Eval */
}; /* namespace R4300 */
