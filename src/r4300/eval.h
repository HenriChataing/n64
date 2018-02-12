
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

};
};

#endif /* _EVAL_H_INCLUDED_ */
