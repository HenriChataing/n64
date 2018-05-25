
#ifndef _EVAL_H_INCLUDED_
#define _EVAL_H_INCLUDED_

#include "types.h"

namespace R4300 {
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
