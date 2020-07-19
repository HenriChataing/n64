
#ifndef _EVAL_H_INCLUDED_
#define _EVAL_H_INCLUDED_

#include "types.h"
#include "cpu.h"

namespace R4300 {
namespace Eval {

void eval_Instr(u32 instr);
void eval_Reserved(u32 instr);
void eval_COP0(u32 instr);
void eval_COP1(u32 instr);

/** @brief Execute exactly one instruction. */
void step();

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
void takeException(R4300::Exception exn, u64 vAddr,
                   bool instr, bool load, u32 ce = 0);

/**
 * @brief Print out a backtrace of the program execution.
 * The backtrace is inferred from the control flow, and can be erroneous.
 */
void backtrace(void);

};
};

#endif /* _EVAL_H_INCLUDED_ */
