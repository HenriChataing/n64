
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

/** @brief Create a host pointer type representation. */
static inline ir_type_t ir_make_iptr(void) {
    return (ir_type_t){ CHAR_BIT * sizeof(void *) };
}

static inline bool ir_type_equals(ir_type_t left, ir_type_t right) {
    return (left.width == right.width);
}

/** Type of machine registers. */
typedef unsigned ir_register_t;

/** Type of pseudo variables. */
typedef unsigned ir_var_t;

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

/** Recompiler backend. */
typedef struct ir_recompiler_backend ir_recompiler_backend_t;

/** Allocate a recompiler backend. */
ir_recompiler_backend_t *ir_create_recompiler_backend(ir_memory_backend_t *memory,
                                                      unsigned nr_registers);

/** Bind a register to a physical memory location. */
void ir_bind_register(ir_recompiler_backend_t *backend,
                      ir_register_t register_,
                      ir_type_t type, void *ptr);
void ir_bind_register_u32(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          uint32_t *ptr);
void ir_bind_register_u64(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          uint64_t *ptr);

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

#ifndef __cplusplus
#endif /* __cplusplus */

typedef struct ir_instr ir_instr_t;

/** List instructions defined by the intermediate representation. */
typedef enum ir_instr_kind {
    /* Control flow. */
    IR_EXIT,
    IR_BR,
    IR_CALL,
    /* Unary operations. */
    IR_NOT,
    /* Binary operations. */
    IR_ADD,
    IR_SUB,
    IR_UMUL,
    IR_SMUL,
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

typedef struct ir_br {
    ir_instr_t         *target;
    ir_value_t          cond;
} ir_br_t;

typedef void (*ir_callback_t)();

typedef struct ir_call {
    ir_callback_t       func;
    ir_value_t         *params;
    unsigned            nr_params;
    unsigned            flags;
} ir_call_t;

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
    ir_register_t       register_;
    ir_type_t           type;
} ir_read_t;

typedef struct ir_write {
    ir_register_t       register_;
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
    ir_br_t             br;
    ir_call_t           call;
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

/** Type alias for instruction code continuation. Contains the emplacement
 * where to write the pointer to the next generated instruction. */
typedef ir_instr_t **ir_instr_cont_t;

#ifndef __cplusplus

static inline
ir_instr_t ir_make_exit(void) {
    return (ir_instr_t){ .kind = IR_EXIT, };
}

static inline
ir_instr_t ir_make_br(ir_value_t cond, ir_instr_t *target) {
    return (ir_instr_t){ .kind = IR_BR,
        .br = { .cond = cond, .target = target, } };
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
ir_instr_t ir_make_store(ir_value_t address,
                         ir_value_t value) {
    return (ir_instr_t){ .kind = IR_STORE,
        .store = { .address = address, .value = value, } };
}

static inline
ir_instr_t ir_make_read(ir_var_t res,
                        ir_type_t type,
                        ir_register_t register_) {
    return (ir_instr_t){ .kind = IR_READ, .res = res, .type = type,
        .read = { .register_ = register_, .type = type, } };
}

static inline
ir_instr_t ir_make_write(ir_register_t register_,
                         ir_value_t value) {
    return (ir_instr_t){ .kind = IR_WRITE,
        .write = { .register_ = register_, .value = value, } };
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

ir_var_t ir_alloc_var(void);
ir_instr_t *ir_alloc_instr(void);
void ir_free_instr(ir_instr_t *instr);

int ir_print_type(char *buf, size_t len, ir_type_t const *type);
int ir_print_value(char *buf, size_t len, ir_value_t const *value);
int ir_print_instr(char *buf, size_t len, ir_instr_t const *instr);

void       ir_append_exit(ir_instr_cont_t *cont);
void       ir_append_br(ir_instr_cont_t *cont,
                        ir_value_t cond,
                        ir_instr_cont_t *target);
ir_value_t ir_append_call(ir_instr_cont_t *cont,
                          ir_type_t type,
                          void (*func)(),
                          unsigned nr_params,
                          ...);
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
                           ir_value_t address,
                           ir_value_t value);
ir_value_t ir_append_read(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_register_t register_);
void       ir_append_write(ir_instr_cont_t *cont,
                           ir_register_t register_,
                           ir_value_t value);
ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value);

static inline
ir_value_t ir_append_trunc(ir_instr_cont_t *cont,
                           ir_type_t type,
                           ir_value_t value) {
    return ir_append_cvt(cont, type, IR_TRUNC, value);
}

static inline
ir_value_t ir_append_sext(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t value) {
    return ir_append_cvt(cont, type, IR_SEXT, value);
}

static inline
ir_value_t ir_append_zext(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t value) {
    return ir_append_cvt(cont, type, IR_ZEXT, value);
}

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_IR_H_INCLUDED_ */
