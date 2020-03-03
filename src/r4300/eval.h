
#ifndef _EVAL_H_INCLUDED_
#define _EVAL_H_INCLUDED_

#include "types.h"

namespace R4300 {

namespace Cop0 {
/**
 * @brief Interpret an instruction with the COP0 special opcode.
 */
bool eval(u32 instr, bool delaySlot);
}; /* namespace Cop0 */

namespace Cop1 {
/**
 * @brief Interpret an instruction with the COP1 special opcode.
 */
bool eval(u32 instr, bool delaySlot);
}; /* namespace Cop1 */

namespace Eval {

/**
 * @brief Start the interpreter loop.
 */
void run();

/**
 * @brief Execute exactly one instruction.
 * @return true if the instruction caused an exception
 */
bool step();

/**
 * @brief Raise an exception and update the state of the processor.
 *
 * @param vAddr         Virtual address being accessed.
 * @param delaySlot     Whether the exception occured in a branch delay
 *                      instruction.
 * @param instr         Whether the exception was triggered by an instruction
 *                      fetch.
 * @param load          Whether the exception was triggered by a load or store
 *                      operation.
 */
void takeException(Exception exn, u64 vAddr,
                   bool delaySlot, bool instr, bool load, u32 ce = 0);

/**
 * @brief Print out a backtrace of the program execution.
 * The backtrace is inferred from the control flow, and can be erroneous.
 */
void backtrace(void);

};
};

#endif /* _EVAL_H_INCLUDED_ */
