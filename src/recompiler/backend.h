
#ifndef _RECOMPILER_BACKEND_H_INCLUDED_
#define _RECOMPILER_BACKEND_H_INCLUDED_

#include <limits.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <recompiler/config.h>
#include <recompiler/ir.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @struct ir_global_definition
 * @brief Global variable definition.
 *
 * Saves information about pre-allocated global variables:
 * bit width and host memory location. If \ref ir_global_definition::type
 * is `i0`, or \ref ir_global_definition::type is `NULL` the
 * global is considered not implemented, and load/store accesses will raise
 * an error.
 *
 * @var ir_global_definition::type
 *    Global variable declaration type. Must be a power of two
 *    greater than one.
 * @var ir_global_definition::name
 *    Global variable name.
 * @var ir_global_definition::ptr
 *    Pointer to the host memory variable storing the value of the global
 *    variable. The pointer alignment must be compatible with the indicated
 *    type.
 */
typedef struct ir_global_definition {
    ir_type_t           type;
    char const         *name;
    void               *ptr;
} ir_global_definition_t;

/**
 * @struct recompiler_error
 * @brief Recompiler error.
 *
 * Errors can be raised by any of the recompiler passes,
 * or with resource allocation, using \ref raise_recompiler_error().
 * The error log can be probed with \ref has_recompiler_error()
 * and \ref next_recompiler_error().
 *
 * @var recompiler_error::next
 *    Pointer to the next recompiler error. Errors are ordered from
 *    oldest to most recent.
 * @var recompiler_error::module
 *    Name of the module which generated the error.
 *    e.g. `backend`, `typecheck`, `run`.
 * @var recompiler_error::message
 *    Recompiler error message, nul terminated.
 */
typedef struct recompiler_error {
    struct recompiler_error *next;
    char const *module;
    char message[RECOMPILER_ERROR_MAX_LEN];
} recompiler_error_t;

/**
 * @struct recompiler_backend
 * @brief Recompiler backend.
 *
 * Contains pre-allocated data structures for code generation:
 * instructions, blocks etc., as well as other internal information
 * for error tracking. To simplify error handling and reduce allocation
 * overhead, intermediate representation objects are allocated from
 * pre-allocated pools; the objects are all released when
 * \ref clear_recompiler_backend() is called, by simply resetting the
 * allocation indexes to 0.
 *
 * @var recompiler_backend::graph
 * @brief Completed intermediate representation graph.
 * @details The graph definition is filled when \ref ir_make_graph() is called,
 *    the field should not be directly referenced.
 *
 * @var recompiler_backend::globals
 * @brief Pointer to the array of global variable definitions.
 * @var recompiler_backend::nr_globals
 * @brief Number of defined global variables.
 *    Length of \ref recompiler_backend::globals.
 *
 * @var recompiler_backend::blocks
 * @brief Pointer to the array of pre-allocated instruction blocks.
 * @var recompiler_backend::nr_blocks
 * @brief Number of pre-allocated instruction blocks.
 *    Length of \ref recompiler_backend::blocks.
 *
 * @var recompiler_backend::instrs
 * @brief Pointer to the array of pre-allocated instructions.
 * @var recompiler_backend::nr_instrs
 * @brief Number of pre-allocated instructions.
 *    Length of \ref recompiler_backend::instrs.
 *
 * @var recompiler_backend::params
 * @brief Pointer to the array of pre-allocated function parameters.
 * @brief The arrays \ref ir_call::params are allocated from this buffer
 *    when \ref ir_append_call() is called.
 * @var recompiler_backend::nr_params
 * @brief Number of pre-allocated function parameters.
 *    Length of \ref recompiler_backend::params.
 *
 * @var recompiler_backend::cur_block
 * @brief Allocation index for \ref recompiler_backend::blocks.
 * @var recompiler_backend::cur_instr
 * @brief Allocation index for \ref recompiler_backend::instrs.
 * @var recompiler_backend::cur_param
 * @brief Allocation index for \ref recompiler_backend::params.
 * @var recompiler_backend::cur_var
 * @brief Next pseudo variable to be generated.
 *
 * @var recompiler_backend::errors
 * @brief Pointer to the oldest recompilation error generated
 *    from the time \ref clear_recompiler_backend() or
 *    \ref next_recompiler_error() was called.
 *
 * @var recompiler_backend::last_error
 * @brief Fast append pointer for the recompilation error list.
 *
 * @var recompiler_backend::jmp_buf
 * @brief Jump buffer for exception handling.
 * @details This jump buffer is used by \ref catch_recompiler_error()
 *    to catch recompilation errors raised with \ref fail_recompiler_backend().
 */
typedef struct recompiler_backend {
    /* Resources. */
    ir_graph_t              graph;
    ir_global_definition_t *globals;
    unsigned                nr_globals;
    ir_block_t             *blocks;
    unsigned                nr_blocks;
    ir_instr_t             *instrs;
    unsigned                nr_instrs;
    ir_value_t             *params;
    unsigned                nr_params;
    unsigned                cur_block;
    unsigned                cur_instr;
    unsigned                cur_param;
    unsigned                cur_var;

    /* Error logs */
    recompiler_error_t     *errors;
    recompiler_error_t    **last_error;

    /* Exception catch point. */
    jmp_buf                 jmp_buf;
} recompiler_backend_t;

recompiler_backend_t *
alloc_recompiler_backend(ir_global_definition_t const *globals,
                         unsigned nr_globals,
                         unsigned nr_blocks,
                         unsigned nr_instrs,
                         unsigned nr_params);
void free_recompiler_backend(recompiler_backend_t *backend);

/**
 * @brief Catch recompiler errors.
 *
 * Set a long jump point for catching recompilation errors.
 * Must be used as part of if() condition, otherwise the behavior is undefined.
 * Evaluates to `0` on success, `-1` when an error was raised.
 */
#define catch_recompiler_error(backend) setjmp(backend->jmp_buf)

__attribute__((noreturn))
void fail_recompiler_backend(recompiler_backend_t *backend);
void clear_recompiler_backend(recompiler_backend_t *backend);
void raise_recompiler_error(recompiler_backend_t *backend,
                            char const *module, char const *fmt, ...);
bool has_recompiler_error(recompiler_backend_t *backend);
bool next_recompiler_error(recompiler_backend_t *backend,
                           char const **module,
                           char message[RECOMPILER_ERROR_MAX_LEN]);

/**
 * @struct ir_instr_cont
 * @brief Instruction code continuation contex.
 *
 * Contains the contextual information to know where to add the
 * next generated instruction. Using the continuation context,
 * the instructions block are generated by calling `ir_append_xx` sequentially
 * on the block continuation context.
 *
 * @var ir_instr_cont::backend
 *    Pointer to the recompiler backend context used to allocated
 *    code objects.
 * @var ir_instr_cont::block
 *    Pointer to the current instruction block.
 * @var ir_instr_cont::next
 *    Pointer to the location where to write the pointer to the next
 *    generated instruction.
 */
typedef struct ir_instr_cont {
    recompiler_backend_t   *backend;
    ir_block_t             *block;
    ir_instr_t            **next;
} ir_instr_cont_t;

ir_var_t    ir_alloc_var(ir_instr_cont_t *cont);
ir_instr_t *ir_alloc_instr(recompiler_backend_t *backend);
ir_block_t *ir_alloc_block(recompiler_backend_t *backend);
ir_graph_t *ir_make_graph(recompiler_backend_t *backend);

void       ir_append_exit(ir_instr_cont_t *cont);
void       ir_append_assert(ir_instr_cont_t *cont,
                            ir_value_t cond);
void       ir_append_br(ir_instr_cont_t *cont,
                        ir_value_t cond,
                        ir_instr_cont_t *target_false,
                        ir_instr_cont_t *target_true);
ir_value_t ir_append_call(ir_instr_cont_t *cont,
                          ir_type_t type,
                          void (*func)(),
                          unsigned nr_params,
                          ...);
ir_value_t ir_append_alloc(ir_instr_cont_t *cont,
                           ir_type_t type);
ir_value_t ir_append_unop(ir_instr_cont_t *cont,
                          ir_instr_kind_t op,
                          ir_value_t value);
ir_value_t ir_append_binop(ir_instr_cont_t *cont,
                           ir_instr_kind_t op,
                           ir_value_t left,
                           ir_value_t right);
ir_value_t ir_append_icmp(ir_instr_cont_t *cont,
                          ir_icmp_kind_t op,
                          ir_value_t left,
                          ir_value_t right);
ir_value_t ir_append_load(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t address);
void       ir_append_store(ir_instr_cont_t *cont,
                           ir_type_t type,
                           ir_value_t address,
                           ir_value_t value);
ir_value_t ir_append_read(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_global_t global);
void       ir_append_write(ir_instr_cont_t *cont,
                           ir_type_t type,
                           ir_global_t global,
                           ir_value_t value);
ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value);

/**
 * @fn ir_append_load_i8
 * @brief See \ref ir_append_load
 * @fn ir_append_load_i16
 * @brief See \ref ir_append_load
 * @fn ir_append_load_i32
 * @brief See \ref ir_append_load
 * @fn ir_append_load_i64
 * @brief See \ref ir_append_load
 */

#define _define_append_load_iN(N) \
static inline \
ir_value_t ir_append_load_i##N(ir_instr_cont_t *cont, ir_value_t address) { \
    return ir_append_load(cont, ir_make_iN(N), address); \
}

_define_append_load_iN(8);
_define_append_load_iN(16);
_define_append_load_iN(32);
_define_append_load_iN(64);

/**
 * @fn ir_append_store_i8
 * @brief See \ref ir_append_store
 * @fn ir_append_store_i16
 * @brief See \ref ir_append_store
 * @fn ir_append_store_i32
 * @brief See \ref ir_append_store
 * @fn ir_append_store_i64
 * @brief See \ref ir_append_store
 */

#define _define_append_store_iN(N) \
static inline \
void ir_append_store_i##N(ir_instr_cont_t *cont, ir_value_t address, \
                          ir_value_t value) { \
    ir_append_store(cont, ir_make_iN(N), address, value); \
}

_define_append_store_iN(8);
_define_append_store_iN(16);
_define_append_store_iN(32);
_define_append_store_iN(64);

/**
 * @fn ir_append_read_i8
 * @brief See \ref ir_append_read
 * @fn ir_append_read_i16
 * @brief See \ref ir_append_read
 * @fn ir_append_read_i32
 * @brief See \ref ir_append_read
 * @fn ir_append_read_i64
 * @brief See \ref ir_append_read
 */

#define _define_append_read_iN(N) \
static inline \
ir_value_t ir_append_read_i##N(ir_instr_cont_t *cont, \
                               ir_global_t global) { \
    return ir_append_read(cont, ir_make_iN(N), global); \
}

_define_append_read_iN(8);
_define_append_read_iN(16);
_define_append_read_iN(32);
_define_append_read_iN(64);

/**
 * @fn ir_append_write_i8
 * @brief See \ref ir_append_write
 * @fn ir_append_write_i16
 * @brief See \ref ir_append_write
 * @fn ir_append_write_i32
 * @brief See \ref ir_append_write
 * @fn ir_append_write_i64
 * @brief See \ref ir_append_write
 */

#define _define_append_write_iN(N) \
static inline \
void ir_append_write_i##N(ir_instr_cont_t *cont, ir_global_t global, \
                          ir_value_t value) { \
    ir_append_write(cont, ir_make_iN(N), global, value); \
}

_define_append_write_iN(8);
_define_append_write_iN(16);
_define_append_write_iN(32);
_define_append_write_iN(64);

/**
 * @fn ir_append_trunc
 * @brief See \ref ir_append_cvt
 * @fn ir_append_trunc_i8
 * @brief See \ref ir_append_cvt
 * @fn ir_append_trunc_i16
 * @brief See \ref ir_append_cvt
 * @fn ir_append_trunc_i32
 * @brief See \ref ir_append_cvt
 *
 * @fn ir_append_sext
 * @brief See \ref ir_append_cvt
 * @fn ir_append_sext_i64
 * @brief See \ref ir_append_cvt
 *
 * @fn ir_append_zext
 * @brief See \ref ir_append_cvt
 * @fn ir_append_zext_i64
 * @brief See \ref ir_append_cvt
 */
static inline
ir_value_t ir_append_trunc(ir_instr_cont_t *cont,
                           ir_type_t type,
                           ir_value_t value) {
    return ir_append_cvt(cont, type, IR_TRUNC, value);
}

static inline
ir_value_t ir_append_trunc_i8(ir_instr_cont_t *cont,
                               ir_value_t value) {
    return ir_append_cvt(cont, ir_make_iN(8), IR_TRUNC, value);
}

static inline
ir_value_t ir_append_trunc_i16(ir_instr_cont_t *cont,
                               ir_value_t value) {
    return ir_append_cvt(cont, ir_make_iN(16), IR_TRUNC, value);
}

static inline
ir_value_t ir_append_trunc_i32(ir_instr_cont_t *cont,
                               ir_value_t value) {
    return ir_append_cvt(cont, ir_make_iN(32), IR_TRUNC, value);
}

static inline
ir_value_t ir_append_sext(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t value) {
    return ir_append_cvt(cont, type, IR_SEXT, value);
}

static inline
ir_value_t ir_append_sext_i64(ir_instr_cont_t *cont,
                              ir_value_t value) {
    return ir_append_cvt(cont, ir_make_iN(64), IR_SEXT, value);
}

static inline
ir_value_t ir_append_zext(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t value) {
    return ir_append_cvt(cont, type, IR_ZEXT, value);
}

static inline
ir_value_t ir_append_zext_i64(ir_instr_cont_t *cont,
                              ir_value_t value) {
    return ir_append_cvt(cont, ir_make_iN(64), IR_ZEXT, value);
}

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_BACKEND_H_INCLUDED_ */
