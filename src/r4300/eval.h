
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
 */
void step();

/**
 * @brief Fetch and interpret a single instruction from the provided address.
 * @param vAddr         Virtual address from which to load the instruction
 */
void eval(u64 vAddr, bool delaySlot);

/**
 * @brief Print out the last instructions to execute.
 * @todo export instructions instead
 */
void hist(void);

};
};

#endif /* _EVAL_H_INCLUDED_ */
