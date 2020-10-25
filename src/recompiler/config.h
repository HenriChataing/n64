
#ifndef _RECOMPILER_CONFIG_H_INCLUDED_
#define _RECOMPILER_CONFIG_H_INCLUDED_

/** Maximum number of blocks defined per graph. */
#define RECOMPILER_BLOCK_MAX            32

/** Maximum number of instructions defined per graph. */
#define RECOMPILER_INSTR_MAX            1024

/** Maximum number of call parameters defined per graph. */
#define RECOMPILER_PARAM_MAX            1024

/** Maximum number of local variables defined per graph. */
#define RECOMPILER_VAR_MAX              1024

/** Maximum number of global variables defined per backend. */
#define RECOMPILER_GLOBAL_MAX           128

/** Maximum length of recompiler error messages. */
#define RECOMPILER_ERROR_MAX_LEN        256

/** Defines whether the disassembly stops at branch instructions or not. */
#define RECOMPILER_DISAS_BRANCH_ENABLE  0

#endif /* _RECOMPILER_CONFIG_H_INCLUDED_ */
