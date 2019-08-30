
#include <iostream>
#include <cstring>

#include <memory.h>
#include <mips/asm.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <r4300/eval.h>

#include "cpu.h"

using namespace R4300;

#define SignExtend(imm, size) ({ \
    u##size valu##size = (imm); \
    i##size vali##size = valu##size; \
    i64 vali64 = vali##size; \
    (u64)vali64; \
})

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
        Eval::takeException(CoprocessorUnusable, 0, delaySlot, instr, false); \
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
            state.reg.gpr[rt] = SignExtend(state.cp1reg.fpr_s[rs]->w, 32);
        })
        RType(DMF, instr, {
            // NB: the instruction puts an undefined value in rs for
            // odd register access. To remove some checks, the instruction
            // returns the value as if read from the register rt - 1.
            // See \ref cp1reg::setFprAliases for details.
            checkCop1Usable();
            state.reg.gpr[rt] = state.cp1reg.fpr_d[rs]->l;
        })
        RType(CF, instr, {
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
        // BC
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
