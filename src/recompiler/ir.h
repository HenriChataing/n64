
#ifndef _RECOMPILER_IR_H_INCLUDED_
#define _RECOMPILER_IR_H_INCLUDED_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Defines the intermediate representation used to handle disassebmled
 * MIPS bytecode before instruction emission. */

/** List input value types. */
typedef enum ir_value_kind {
    IR_CONST,
    IR_VAR,
} ir_value_kind_t;

/** Type of value types. All values are scalar integers in the two's
 * complement format, defined by their bitwidth. */
typedef struct ir_type {
    unsigned            width;
} ir_type_t;

/** @brief Create a integer type representation. */
static inline ir_type_t ir_make_iN(unsigned n) {
    return (ir_type_t){ n };
}

static inline ir_type_t ir_make_i8(void) { return ir_make_iN(8); }
static inline ir_type_t ir_make_i16(void) { return ir_make_iN(16); }
static inline ir_type_t ir_make_i32(void) { return ir_make_iN(32); }
static inline ir_type_t ir_make_i64(void) { return ir_make_iN(64); }

/** @brief Create a host pointer type representation. */
static inline ir_type_t ir_make_iptr(void) {
    return (ir_type_t){ CHAR_BIT * sizeof(void *) };
}

static inline bool ir_type_equals(ir_type_t left, ir_type_t right) {
    return (left.width == right.width);
}

/** Type of global variables. */
typedef unsigned ir_global_t;

/** Type of pseudo variables. */
typedef unsigned ir_var_t;

typedef struct ir_graph ir_graph_t;
typedef struct ir_block ir_block_t;
typedef struct ir_instr ir_instr_t;

/** Type of constant values. */
typedef union ir_const {
    uintmax_t           int_;
    uintptr_t           ptr;
} ir_const_t;

/** Type of input values. */
typedef struct ir_value {
    ir_value_kind_t     kind;
    ir_type_t           type;
    union {
    ir_const_t          const_;
    ir_var_t            var;
    };
} ir_value_t;


/** @brief Create a constant value representation. */
static inline ir_value_t ir_make_const(ir_type_t type, ir_const_t const_) {
    return (ir_value_t){ IR_CONST, type, { const_ } };
}

/** @brief Create a constant value representation. */
static inline ir_value_t ir_make_const_int(ir_type_t type, uintmax_t int_) {
    return ir_make_const(type, (ir_const_t){ .int_ = int_ });
}

/** @brief Create a u8 constant value representation. */
static inline ir_value_t ir_make_const_u8(uint8_t const_) {
    return ir_make_const(ir_make_iN(8), (ir_const_t){ .int_ = const_ });
}

/** @brief Create a u16 constant value representation. */
static inline ir_value_t ir_make_const_u16(uint16_t const_) {
    return ir_make_const(ir_make_iN(16), (ir_const_t){ .int_ = const_ });
}

/** @brief Create a u16 constant value representation. */
static inline ir_value_t ir_make_const_u32(uint32_t const_) {
    return ir_make_const(ir_make_iN(32), (ir_const_t){ .int_ = const_ });
}

/** @brief Create a u16 constant value representation. */
static inline ir_value_t ir_make_const_u64(uint64_t const_) {
    return ir_make_const(ir_make_iN(64), (ir_const_t){ .int_ = const_ });
}

/** @brief Create a variable value representation. */
static inline ir_value_t ir_make_var(ir_var_t var, ir_type_t type) {
    ir_value_t value = { IR_VAR, type, };
    value.var = var;
    return value;
}

typedef struct ir_graph {
    ir_block_t *blocks;
    unsigned nr_blocks;
    unsigned nr_vars;
} ir_graph_t;

typedef struct ir_block {
    unsigned label;
    ir_instr_t *instrs;
    unsigned nr_instrs;
} ir_block_t;

/** List instructions defined by the intermediate representation. */
typedef enum ir_instr_kind {
    /* Control flow. */
    IR_EXIT,
    IR_ASSERT,
    IR_BR,
    IR_CALL,
    /* Variable allocation. */
    IR_ALLOC,
    /* Unary operations. */
    IR_NOT,
    /* Binary operations. */
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_UDIV,
    IR_SDIV,
    IR_UREM,
    IR_SREM,
    /* Bitwise operations. */
    IR_SLL,
    IR_SRL,
    IR_SRA,
    IR_AND,
    IR_OR,
    IR_XOR,
    /* Comparisons. */
    IR_ICMP,
    /* Memory accesses. */
    IR_LOAD,
    IR_STORE,
    /* Register accesses. */
    IR_READ,
    IR_WRITE,
    /* Type conversions. */
    IR_TRUNC,
    IR_SEXT,
    IR_ZEXT,
} ir_instr_kind_t;

/** List comparison operators. */
typedef enum ir_icmp_kind {
    IR_EQ,
    IR_NE,
    IR_UGT,
    IR_UGE,
    IR_ULT,
    IR_ULE,
    IR_SGT,
    IR_SGE,
    IR_SLT,
    IR_SLE,
} ir_icmp_kind_t;

typedef enum ir_call_flag {
    IR_READ_REGISTER  = 1<<0,
    IR_WRITE_REGISTER = 1<<1,
    IR_READ_MEMORY    = 1<<2,
    IR_WRITE_MEMORY   = 1<<3,
} ir_call_flag_t;

typedef struct ir_assert {
    ir_value_t          cond;
} ir_assert_t;

typedef struct ir_br {
    ir_block_t         *target[2];
    ir_value_t          cond;
} ir_br_t;

typedef void (*ir_callback_t)();

typedef struct ir_call {
    ir_callback_t       func;
    ir_value_t         *params;
    unsigned            nr_params;
    unsigned            flags;
} ir_call_t;

typedef struct ir_alloc {
    ir_type_t           type;
} ir_alloc_t;

typedef struct ir_unop {
    ir_value_t          value;
} ir_unop_t;

typedef struct ir_binop {
    ir_value_t          left;
    ir_value_t          right;
} ir_binop_t;

typedef struct ir_icmp {
    ir_icmp_kind_t      op;
    ir_value_t          left;
    ir_value_t          right;
} ir_icmp_t;

typedef struct ir_load {
    ir_value_t          address;
    ir_type_t           type;
} ir_load_t;

typedef struct ir_store {
    ir_value_t          address;
    ir_value_t          value;
} ir_store_t;

typedef struct ir_read {
    ir_global_t         global;
    ir_type_t           type;
} ir_read_t;

typedef struct ir_write {
    ir_global_t         global;
    ir_value_t          value;
} ir_write_t;

typedef struct ir_trunc {
    ir_value_t          value;
    ir_type_t           type;
} ir_cvt_t;

/** Type of intermediate representation instructions. */
typedef struct ir_instr {
    ir_instr_kind_t     kind;
    ir_instr_t         *next;
    ir_type_t           type;
    ir_var_t            res;
    union {
    ir_assert_t         assert_;
    ir_br_t             br;
    ir_call_t           call;
    ir_alloc_t          alloc;
    ir_unop_t           unop;
    ir_binop_t          binop;
    ir_icmp_t           icmp;
    ir_load_t           load;
    ir_store_t          store;
    ir_read_t           read;
    ir_write_t          write;
    ir_cvt_t            cvt;
    };
} ir_instr_t;

#ifndef __cplusplus

static inline
ir_instr_t ir_make_exit(void) {
    return (ir_instr_t){ .kind = IR_EXIT, };
}

static inline
ir_instr_t ir_make_assert(ir_value_t cond) {
    return (ir_instr_t){ .kind = IR_ASSERT, .assert_ = { .cond = cond } };
}

static inline
ir_instr_t ir_make_br(ir_value_t cond,
                      ir_block_t *target_false,
                      ir_block_t *target_true) {
    return (ir_instr_t){ .kind = IR_BR,
        .br = { .cond = cond, .target = { target_false, target_true } } };
}

static inline
ir_instr_t ir_make_call(ir_var_t res,
                        ir_type_t type,
                        void (*func)(),
                        ir_value_t *params,
                        unsigned nr_params) {
    return (ir_instr_t){ .kind = IR_CALL, .res = res, .type = type,
        .call = { .func = func, .params = params, .nr_params = nr_params } };
}

static inline
ir_instr_t ir_make_alloc(ir_var_t res,
                         ir_type_t type) {
    return (ir_instr_t){ .kind = IR_ALLOC, .res = res, .type = ir_make_iptr(),
        .alloc = { .type = type, } };
}

static inline
ir_instr_t ir_make_unop(ir_var_t res,
                        ir_instr_kind_t op,
                        ir_value_t value) {
    return (ir_instr_t){ .kind = op, .res = res, .type = value.type,
        .unop = { .value = value, } };
}

static inline
ir_instr_t ir_make_binop(ir_var_t res,
                         ir_instr_kind_t op,
                         ir_value_t left,
                         ir_value_t right) {
    return (ir_instr_t){ .kind = op, .res = res, .type = left.type,
        .binop = { .left = left, .right = right, } };
}

static inline
ir_instr_t ir_make_icmp(ir_var_t res,
                        ir_icmp_kind_t op,
                        ir_value_t left,
                        ir_value_t right) {
    return (ir_instr_t){ .kind = IR_ICMP, .res = res, .type = ir_make_iN(1),
        .icmp = { .op = op, .left = left, .right = right, } };
}

static inline
ir_instr_t ir_make_load(ir_var_t res,
                        ir_type_t type,
                        ir_value_t address) {
    return (ir_instr_t){ .kind = IR_LOAD, .res = res, .type = type,
        .load = { .address = address, .type = type, } };
}

static inline
ir_instr_t ir_make_store(ir_type_t type,
                         ir_value_t address,
                         ir_value_t value) {
    return (ir_instr_t){ .kind = IR_STORE, .type = type,
        .store = { .address = address, .value = value, } };
}

static inline
ir_instr_t ir_make_read(ir_var_t res,
                        ir_type_t type,
                        ir_global_t global) {
    return (ir_instr_t){ .kind = IR_READ, .res = res, .type = type,
        .read = { .global = global, .type = type, } };
}

static inline
ir_instr_t ir_make_write(ir_type_t type,
                         ir_global_t global,
                         ir_value_t value) {
    return (ir_instr_t){ .kind = IR_WRITE, .type = type,
        .write = { .global = global, .value = value, } };
}

static inline
ir_instr_t ir_make_cvt(ir_var_t res,
                       ir_type_t type,
                       ir_instr_kind_t op,
                       ir_value_t value) {
    return (ir_instr_t){ .kind = op, .res = res, .type = type,
        .cvt = { .value = value, .type = type, } };
}

#endif /* __cplusplus */

bool ir_is_void_instr(ir_instr_t const *instr);

int ir_print_type(char *buf, size_t len, ir_type_t const *type);
int ir_print_value(char *buf, size_t len, ir_value_t const *value);
int ir_print_instr(char *buf, size_t len, ir_instr_t const *instr);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_IR_H_INCLUDED_ */
