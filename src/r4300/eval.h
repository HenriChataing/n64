
#ifndef _EVAL_H_INCLUDED_
#define _EVAL_H_INCLUDED_

#include "types.h"

namespace R4300 {

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
 * @brief Fetch and interpret a single instruction from the provided address.
 * @param vAddr         Virtual address from which to load the instruction
 * @param delaySlot     Whether the instruction executed is in a
 *                      branch delay slot.
 * @return true if the instruction caused an exception
 */
bool eval(u64 vAddr, bool delaySlot);

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
 * @brief Print out the last instructions to execute.
 * @todo export instructions instead
 */
void hist(void);

/**
 * @brief Print out a backtrace of the program execution.
 * The backtrace is inferred from the control flow, and can be erroneous.
 */
void backtrace(void);

};
};

#endif /* _EVAL_H_INCLUDED_ */
