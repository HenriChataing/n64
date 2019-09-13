
#include <iostream>
#include <cstring>
#include <cmath>

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
        return true; \
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

bool eval(u32 instr, bool delaySlot)
{
    using namespace Mips::Copz;
    using namespace Mips::Cop1;

    switch (Mips::getFmt(instr)) {
        RType(MF, instr, {
            checkCop1Usable();
            state.reg.gpr[rt] = sign_extend<u64, u32>(state.cp1reg.fpr_s[rs]->w);
        })
        RType(DMF, instr, {
            // NB: the instruction puts an undefined value in rs for
            // odd register access. To remove some checks, the instruction
            // returns the value as if read from the register rt - 1.
            // See \ref cp1reg::setFprAliases for details.
            checkCop1Usable();
            state.reg.gpr[rt] = state.cp1reg.fpr_d[rs]->l;
        })
        RType(Mips::Copz::CF, instr, {
            checkCop1Usable();
            switch (rd) {
                case 0: state.reg.gpr[rt] = state.cp1reg.fcr0; break;
                case 31: state.reg.gpr[rt] = state.cp1reg.fcr31; break;
                default:
                    throw "Nonexisting COP1 control register";
            }
        })
        RType(MT, instr, {
            checkCop1Usable();
            state.cp1reg.fpr_s[rs]->w = state.reg.gpr[rt];
        })
        RType(DMT, instr, {
            // NB: the instruction presents an undefined behaviour for
            // odd register access. To remove some checks, the instruction
            // has for side effect to write as if the register index
            // were rt - 1. See \ref cp1reg::setFprAliases for details.
            checkCop1Usable();
            state.cp1reg.fpr_d[rs]->l = state.reg.gpr[rt];
        })
        RType(CT, instr, {
            checkCop1Usable();
            switch (rd) {
                case 0: state.cp1reg.fcr0 = state.reg.gpr[rt]; break;
                case 31: state.cp1reg.fcr31 = state.reg.gpr[rt]; break;
                default:
                    throw "Nonexisting COP1 control register";
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
                FRType(CVTS, instr, {
                    throw "Unsupported_S";
                })
                FRType(CVTD, instr, {
                    throw "Unsupported_S";
                })
                FRType(CVTW, instr, {
                    state.cp1reg.fpr_s[fd]->w = state.cp1reg.fpr_s[fs]->s;
                })
                FRType(CVTL, instr, {
                    throw "Unsupported_S";
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
                            throw "InvalidOperation";
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
                    throw "Unsupported_S";
            }
            break;
        case Mips::Cop1::D:
            switch (Mips::getFunct(instr)) {
                FRType(CVTS, instr, {
                    throw "Unsupported_D";
                })
                FRType(CVTD, instr, {
                    throw "Unsupported_D";
                })
                FRType(CVTW, instr, {
                    throw "Unsupported_D";
                })
                FRType(CVTL, instr, {
                    throw "Unsupported_D";
                })
                default:
                    throw "Unsupported_D";
            }
            break;
        case Mips::Cop1::W:
            checkCop1Usable();
            switch (Mips::getFunct(instr)) {
                FRType(CVTS, instr, {
                    state.cp1reg.fpr_s[fd]->s = state.cp1reg.fpr_s[fs]->w;
                })
                FRType(CVTD, instr, {
                    throw "Unsupported_W";
                })
                FRType(CVTW, instr, {
                    throw "Unsupported_W";
                })
                FRType(CVTL, instr, {
                    throw "Unsupported_W";
                })
                default:
                    throw "Unsupported_W";
            }
            break;
        case Mips::Cop1::L:
            switch (Mips::getFunct(instr)) {
                FRType(CVTS, instr, {
                    throw "Unsupported_L";
                })
                FRType(CVTD, instr, {
                    throw "Unsupported_L";
                })
                FRType(CVTW, instr, {
                    throw "Unsupported_L";
                })
                FRType(CVTL, instr, {
                    throw "Unsupported_L";
                })
                default:
                    throw "Unsupported_L";
            }
            break;

        case Mips::Copz::BC:
            switch (Mips::getRt(instr)) {
                case Mips::Copz::BCF: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if ((state.cp1reg.fcr31 & FCR31_C) == 0) {
                        R4300::Eval::eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(offset << 2);
                        state.branch = true;
                    }
                    break;
                }
                case Mips::Copz::BCFL: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if (((state.cp1reg.fcr31 & FCR31_C) == 0)) {
                        R4300::Eval::eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(offset << 2);
                        state.branch = true;
                    } else {
                        state.reg.pc += 4;
                    }
                    break;
                }
                case Mips::Copz::BCT: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if ((state.cp1reg.fcr31 & FCR31_C) != 0) {
                        R4300::Eval::eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(offset << 2);
                        state.branch = true;
                    }
                    break;
                }
                case Mips::Copz::BCTL: {
                    u64 offset = sign_extend<u64, u16>(Mips::getImmediate(instr));
                    if (((state.cp1reg.fcr31 & FCR31_C) != 0)) {
                        R4300::Eval::eval(state.reg.pc + 4, true);
                        state.reg.pc += 4 + (i64)(offset << 2);
                        state.branch = true;
                    } else {
                        state.reg.pc += 4;
                    }
                    break;
                }
                default:
                    throw "InvalidCop1BranchCondition";
            }
            break;

        default:
            std::cerr << "fmt is " << std::hex << Mips::getFmt(instr) << std::endl;
            std::cerr << "instr is " << std::hex << instr << std::endl;
            throw "UnsupportedCop1Instruction";
    }
    return false;
}

}; /* Cop1 */

#if 0
    valueFPR(fpr, fmt) {
        if SR 26 = 1 then /* 64-bit wide FGRs */
            case fmt of
                S, W:
                    value ← FGR[fpr] 31...0
                    return
                D, L:
                    value ← FGR[fpr]
                    return
            endcase
        elseif fpr0 = 0 then /* valid specifier, 32-bit wide FGRs */
            case fmt of
                S, W:
                    value ← FGR[fpr]
                    return
                D, L:
                    value ← FGR[fpr+1] || FGR[fpr]
                    return
            endcase
        else /* undefined result for odd 32-bit reg #s */
            value ← undefined
        endif
    }

    StoreFPR(fpr, fmt, value)
        if SR 26 = 1 then /* 64-bit wide FGRs */
            case fmt of
            S, W:
                FGR[fpr] ← undefined 32 || value
                return
            D, L:
                FGR[fpr] ← value
                return
            endcase
        elseif fpr 0 = 0 then /* valid specifier, 32-bit wide FGRs */
            case fmt of
            S, W:
                FGR[fpr+1] ← undefined
                FGR[fpr] ← value
                return
            D, L:
                FGR[fpr+1] ← value 63...32
                FGR[fpr] ← value 31...0
                return
            endcase
        else /* undefined result for odd 32-bit reg #s */
            undefined_result
        endif
#endif

}; /* namespace R4300 */
