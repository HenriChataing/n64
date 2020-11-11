
#ifndef _CORE_H_INCLUDED_
#define _CORE_H_INCLUDED_

#include <cstdint>
#include <string>

namespace core {

/** Number of cycles executed through recompiled code. */
extern unsigned long recompiler_cycles;
/** Number of recompiler cache clears. */
extern unsigned long recompiler_clears;
/** Number of handlded recompiler requests (successful or not). */
extern unsigned long recompiler_requests;

/**
 * @brief Start the interpreter and recompiler in separate threads.
 * The interpreter is initially halted and shoulf be kicked off with
 * \ref core::resume().
 */
void start(void);

/**
 * @brief  Kill the interpreter and recompiler threads.
 * The function first halts the interpreter for a
 * clean exit.
 */
void stop(void);

/** Reset the machine state. */
void reset(void);

/** Halt the interpreter for the given reason. */
void halt(std::string reason);
bool halted(void);
std::string halted_reason(void);

/** When the debugger is halted, advance the interpreter one step */
void step(void);

/** When the debugger is halted, resume execution. */
void resume(void);

/** Invalidate the recompiler cache for the provided address range. */
void invalidate_recompiler_cache(uint64_t start_phys_address,
                                 uint64_t end_phys_address);

/** Return the cache usage statistics. */
void get_recompiler_cache_stats(float *cache_usage,
                                float *buffer_usage);

}; /* namespace core */

#endif /* _CORE_H_INCLUDED_ */
