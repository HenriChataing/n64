
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

/** Machine memory interface. */
typedef struct ir_memory_backend {
    bool (*load_u8  )(uintmax_t, uint8_t  *value);
    bool (*load_u16 )(uintmax_t, uint16_t *value);
    bool (*load_u32 )(uintmax_t, uint32_t *value);
    bool (*load_u64 )(uintmax_t, uint64_t *value);
    bool (*store_u8 )(uintmax_t, uint8_t   value);
    bool (*store_u16)(uintmax_t, uint16_t  value);
    bool (*store_u32)(uintmax_t, uint32_t  value);
    bool (*store_u64)(uintmax_t, uint64_t  value);
} ir_memory_backend_t;

/** Saves information about a machine register : bit width and host memory
 * location. If `type` is i0, or `ptr` is NULL the register is considered
 * not implemented, and compiled as a zero value. */
typedef struct ir_register_backend {
    ir_type_t           type;
    char const         *name;
    void               *ptr;
} ir_register_backend_t;

/**
 * @brief Recompiler error.
 * Errors can be raised by any of the recompiler passes,
 * or with resource allocation, using \ref recompiler_raise_error().
 * The log can be probed with \ref recompiler_has_error()
 * and \ref recompiler_next_error().
 */
typedef struct recompiler_error {
    struct recompiler_error *next;
    char const *module;
    char message[RECOMPILER_ERROR_MAX_LEN];
} recompiler_error_t;

/**
 * @brief Recompiler backend.
 * Saves information about the machine memory and registers,
 * as well as interrnal data structures.
 */
typedef struct recompiler_backend {
    /* Machine abstraction. */
    ir_memory_backend_t     memory;
    ir_register_backend_t  *registers;
    unsigned                nr_registers;

    /* Resources. */
    ir_graph_t              graph;
    ir_block_t             *blocks;
    unsigned                nr_blocks;
    ir_instr_t             *instrs;
    unsigned                nr_instrs;
    ir_value_t             *params;
    unsigned                nr_params;
    unsigned                cur_block;
    unsigned                cur_instr;
    unsigned                cur_param;

    /* Error logs */
    recompiler_error_t     *errors;
    recompiler_error_t    **last_error;

    /* Exception catch point. */
    jmp_buf                 jmp_buf;
} recompiler_backend_t;

/** Type alias for instruction code continuation. Contains the emplacement
 * where to write the pointer to the next generated instruction. */
typedef struct ir_instr_cont {
    recompiler_backend_t   *backend;
    ir_block_t             *block;
    ir_instr_t            **next;
} ir_instr_cont_t;

/** Allocate a recompiler backend. */
recompiler_backend_t *
create_recompiler_backend(ir_memory_backend_t *memory,
                          unsigned nr_registers,
                          unsigned nr_blocks,
                          unsigned nr_instrs,
                          unsigned nr_params);

/** Bind a register to a physical memory location. */
void ir_bind_register(recompiler_backend_t *backend,
                      ir_register_t register_,
                      ir_type_t type,
                      char const *name,
                      void *ptr);
void ir_bind_register_u32(recompiler_backend_t *backend,
                          ir_register_t register_,
                          char const *name,
                          uint32_t *ptr);
void ir_bind_register_u64(recompiler_backend_t *backend,
                          ir_register_t register_,
                          char const *name,
                          uint64_t *ptr);

/**
 * Initialize the backend context. The function is used as exception
 * catch point for resource allocation failure. Must be used as part of if()
 * condition, otherwise the behavior is undefined.
 * Evaluates to 0 on success, -1 on allocation failure.
 */
#define reset_recompiler_backend(backend) setjmp(backend->jmp_buf)

__attribute__((noreturn))
void fail_recompiler_backend(recompiler_backend_t *backend);
void clear_recompiler_backend(recompiler_backend_t *backend);
void raise_recompiler_error(recompiler_backend_t *backend,
                            char const *module, char const *fmt, ...);
bool has_recompiler_error(recompiler_backend_t *backend);
bool next_recompiler_error(recompiler_backend_t *backend,
                           char const **module,
                           char message[RECOMPILER_ERROR_MAX_LEN]);

ir_var_t    ir_alloc_var(ir_instr_cont_t *cont);
ir_instr_t *ir_alloc_instr(recompiler_backend_t *backend);
ir_block_t *ir_alloc_block(recompiler_backend_t *backend);
ir_graph_t *ir_make_graph(recompiler_backend_t *backend);

void       ir_append_exit(ir_instr_cont_t *cont);
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
                          ir_register_t register_);
void       ir_append_write(ir_instr_cont_t *cont,
                           ir_type_t type,
                           ir_register_t register_,
                           ir_value_t value);
ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value);

#define _define_append_load_iN(N) \
static inline \
ir_value_t ir_append_load_i##N(ir_instr_cont_t *cont, ir_value_t address) { \
    return ir_append_load(cont, ir_make_iN(N), address); \
}

_define_append_load_iN(8);
_define_append_load_iN(16);
_define_append_load_iN(32);
_define_append_load_iN(64);

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

#define _define_append_read_iN(N) \
static inline \
ir_value_t ir_append_read_i##N(ir_instr_cont_t *cont, \
                               ir_register_t register_) { \
    return ir_append_read(cont, ir_make_iN(N), register_); \
}

_define_append_read_iN(8);
_define_append_read_iN(16);
_define_append_read_iN(32);
_define_append_read_iN(64);

#define _define_append_write_iN(N) \
static inline \
void ir_append_write_i##N(ir_instr_cont_t *cont, ir_register_t register_, \
                          ir_value_t value) { \
    ir_append_write(cont, ir_make_iN(N), register_, value); \
}

_define_append_write_iN(8);
_define_append_write_iN(16);
_define_append_write_iN(32);
_define_append_write_iN(64);

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
