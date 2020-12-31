
#ifndef _RECOMPILER_IR_H_INCLUDED_
#define _RECOMPILER_IR_H_INCLUDED_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @enum ir_value_kind
 * @brief List input value types.
 * @var ir_value_kind::IR_CONST
 *      Immediate value.
 * @var ir_value_kind::IR_VAR
 *      Variable reference.
 */
typedef enum ir_value_kind {
    IR_CONST,
    IR_VAR,
} ir_value_kind_t;

/**
 * @struct ir_type
 * @brief Representation of value types.
 *
 * All values are scalar integers with two's complement representation,
 * defined by their bitwidth.
 *
 * @var ir_type::width
 *      Bitwidth of integer type.
 */
typedef struct ir_type {
    unsigned char       width;
} ir_type_t;

/**
 * @brief Create a integer type representation.
 * @param n             Type bitwidth.
 * @return              Value type object with the input bitwidth.
 */
static inline ir_type_t ir_make_iN(unsigned char n) {
    return (ir_type_t){ n };
}

/**
 * @fn ir_make_void
 * @brief Create the void type representation.
 *
 * @fn ir_make_i1
 * @brief Create the type representation for a boolean value.
 *
 * @fn ir_make_i8
 * @brief Create the int8_t type representation.
 *
 * @fn ir_make_i16
 * @brief Create the int16_t type representation.
 *
 * @fn ir_make_i32
 * @brief Create the int32_t type representation.
 *
 * @fn ir_make_i64
 * @brief Create the int64_t type representation.
 *
 * @fn ir_make_iptr
 * @brief Create the host pointer type representation.
 */
static inline ir_type_t ir_make_void(void) { return ir_make_iN(0); }
static inline ir_type_t ir_make_i1(void) { return ir_make_iN(1); }
static inline ir_type_t ir_make_i8(void) { return ir_make_iN(8); }
static inline ir_type_t ir_make_i16(void) { return ir_make_iN(16); }
static inline ir_type_t ir_make_i32(void) { return ir_make_iN(32); }
static inline ir_type_t ir_make_i64(void) { return ir_make_iN(64); }

static inline ir_type_t ir_make_iptr(void) {
    return ir_make_iN(CHAR_BIT * sizeof(uintptr_t));
}

/**
 * @brief Compare two type representations.
 * @param left      First type operand.
 * @param right     Second type operand.
 * @return
 *      True if \p left and \p right represent
 *      the same type, false otherwise.
 */
static inline bool ir_type_equals(ir_type_t left, ir_type_t right) {
    return (left.width == right.width);
}

/** @brief Type of global variables. */
typedef unsigned ir_global_t;

/** @brief Type of pseudo variables. */
typedef unsigned ir_var_t;

/**
 * @union ir_const
 * @brief Representation of constant values.
 * @var ir_const::int_
 *      Read value as integer.
 * @var ir_const::ptr
 *      Read value as host pointer.
 */
typedef union ir_const {
    uintmax_t           int_;
    uintptr_t           ptr;
} ir_const_t;

/**
 * @struct ir_value
 * @brief Representation of input values.
 * @var ir_value::kind
 *      Value kind.
 * @var ir_value::type
 *      Value type.
 * @var ir_value::const_
 *      Value representation as immediate.
 * @var ir_value::var
 *      Value representation as variable.
 */
typedef struct ir_value {
    ir_value_kind_t     kind;
    ir_type_t           type;
    union {
    ir_const_t          const_;
    ir_var_t            var;
    };
} ir_value_t;

/**
 * @brief Create a constant value representation.
 * @param type      Value type.
 * @param const_    Value definition.
 * @return          The created value representation.
 */
static inline ir_value_t ir_make_const(ir_type_t type, ir_const_t const_) {
    return (ir_value_t){ IR_CONST, type, { .const_ = const_ } };
}

/**
 * @brief Create an integer value representation.
 * @param type      Value type.
 * @param int_      Integer value definition.
 * @return          The created value representation.
 */
static inline ir_value_t ir_make_const_int(ir_type_t type, uintmax_t int_) {
    return ir_make_const(type, (ir_const_t){ .int_ = int_ });
}

/**
 * @brief Create an integer value representation.
 * @param n         Value type bitwidth.
 * @param int_      Integer value definition.
 * @return          The created value representation.
 */
static inline ir_value_t ir_make_const_iN(unsigned char n, uintmax_t int_) {
    return ir_make_const(ir_make_iN(n), (ir_const_t){ .int_ = int_ });
}

/**
 * @fn ir_make_const_i8
 * @brief Create a u8 constant value representation.
 * @param int_      Constant value.
 *
 * @fn ir_make_const_i16
 * @brief Create a u16 constant value representation.
 * @param int_      Constant value.
 *
 * @fn ir_make_const_i32
 * @brief Create a u32 constant value representation.
 * @param int_      Constant value.
 *
 * @fn ir_make_const_i64
 * @brief Create a u64 constant value representation.
 * @param int_      Constant value.
 */
static inline ir_value_t ir_make_const_i8(uint8_t int_) {
    return ir_make_const_iN(8, int_);
}
static inline ir_value_t ir_make_const_i16(uint16_t int_) {
    return ir_make_const_iN(16, int_);
}
static inline ir_value_t ir_make_const_i32(uint32_t int_) {
    return ir_make_const_iN(32, int_);
}
static inline ir_value_t ir_make_const_i64(uint64_t int_) {
    return ir_make_const_iN(64, int_);
}

/**
 * @brief Create a variable value representation.
 * @param type      Value type.
 * @param var       Variable identifier.
 * @return          The created value representation.
 */
static inline ir_value_t ir_make_var(ir_type_t type, ir_var_t var) {
    ir_value_t value = { IR_VAR, type, };
    value.var = var;
    return value;
}

typedef struct ir_instr ir_instr_t;
typedef struct ir_block ir_block_t;
typedef struct ir_graph ir_graph_t;

/**
 * @enum ir_instr_kind
 * @brief List instructions defined by the intermediate representation.
 */
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

/**
 * @enum ir_icmp_kind
 * @brief List comparison operators.
 */
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

/**
 * @struct ir_assert
 * @brief Assert instruction parameters.
 * @var ir_assert::cond
 *      Assert condition. Must be a value of type `i1`.
 */
typedef struct ir_assert {
    ir_value_t          cond;
} ir_assert_t;

/**
 * @struct ir_br
 * @brief Branch instruction parameters.
 * @var ir_br::target
 *      Branch targets. The first target is taken when the
 *      condition is false, the second when it is true.
 * @var ir_br::cond
 *      Branch condition. Must be a value of type `i1`.
 */
typedef struct ir_br {
    ir_block_t         *target[2];
    ir_value_t          cond;
} ir_br_t;

/**
 * @brief Type of host function callbacks.
 *
 * The type is left generic, but host functions should follow these
 * constraints:
 *  - all parameters should have scalar type
 *  - the return type, if present, should also have scalar type
 *  - the function needs to be compiled with the local C ABI.
 */
typedef void (*ir_func_t)();

/**
 * @struct ir_call
 * @brief Function call instruction parameters.
 * @var ir_call::func
 *      Pointer to the host function to be called. Functions called with
 *      call instruction should only input scalar values, in order for the
 *      call generation to be correct.
 * @var ir_call::params
 *      Pointer to the array of function parameter types.
 *      Can be NULL if the function does not input any parameters.
 * @var ir_call::nr_params
 *      Number of parameters input by the function \ref ir_call::func.
 */
typedef struct ir_call {
    ir_func_t           func;
    ir_value_t         *params;
    unsigned            nr_params;
} ir_call_t;

/**
 * @struct ir_alloc
 * @brief Alloc instruction parameters.
 * @var ir_alloc::type
 *      Type of the allocated memory cell. The instruction only supports
 *      allocating scalar variables at the moment. The memory is allocated
 *      from the stack.
 */
typedef struct ir_alloc {
    ir_type_t           type;
} ir_alloc_t;

/**
 * @struct ir_unop
 * @brief Unary instruction parameters.
 * @var ir_unop::value
 *      Instruction operand.
 */
typedef struct ir_unop {
    ir_value_t          value;
} ir_unop_t;

/**
 * @struct ir_binop
 * @brief Binary instruction parameters.
 * @var ir_binop::left
 *      First instruction operand.
 * @var ir_binop::right
 *      Second instruction operand.
 */
typedef struct ir_binop {
    ir_value_t          left;
    ir_value_t          right;
} ir_binop_t;

/**
 * @struct ir_icmp
 * @brief Compare instruction parameters.
 * @var ir_icmp::op
 *      Comparison operator.
 * @var ir_binop::left
 *      Left side of the comparison.
 * @var ir_binop::right
 *      Right side of the comparison.
 */
typedef struct ir_icmp {
    ir_icmp_kind_t      op;
    ir_value_t          left;
    ir_value_t          right;
} ir_icmp_t;

/**
 * @struct ir_load
 * @brief Load instruction parameters.
 * @details The load instruction is used to dereference host memory pointers.
 * The load access size is determined by the instruction result type.
 * @var ir_load::address
 *      Host memory address. Must be a value of type `iptr`.
 */
typedef struct ir_load {
    ir_value_t          address;
} ir_load_t;

/**
 * @struct ir_store
 * @brief Store instruction parameters.
 * @details The store instruction is used to dereference host memory pointers.
 * The store access size is determined by the instruction result type.
 * @var ir_store::address
 *      Host memory address. Must be a value of type `iptr`.
 * @var ir_store::value
 *      Stored value. The value type must be a power of two greater than eight.
 *      The value type must additionally be identical to the instruction
 *      result type.
 */
typedef struct ir_store {
    ir_value_t          address;
    ir_value_t          value;
} ir_store_t;

/**
 * @struct ir_read
 * @brief Read instruction parameters.
 * @details The read instruction is used to dereference global variables.
 * The read access size is determined by the instruction result type.
 * The read access size must additionally be identical to the declaration
 * type of \ref ir_read::global.
 * @var ir_read::global
 *      Global variable identifier.
 */
typedef struct ir_read {
    ir_global_t         global;
} ir_read_t;

/**
 * @struct ir_write
 * @brief Write instruction parameters.
 * @details The write instruction is used to dereference global variables.
 * The write access size is determined by the instruction result type.
 * The write access size must additionally be identical to the declaration
 * type of \ref ir_write::global.
 * @var ir_write::global
 *      Global variable identifier.
 * @var ir_write::value
 *      Written value. The value type must be identical to the instruction
 *      result type.
 */
typedef struct ir_write {
    ir_global_t         global;
    ir_value_t          value;
} ir_write_t;

/**
 * @struct ir_cvt
 * @brief Convert instruction parameters.
 * @details This structure is used to record the parameters for the
 * \ref ir_instr_kind::IR_TRUNC, \ref ir_instr_kind::IR_SEXT,
 * \ref ir_instr_kind::IR_ZEXT
 * instructions. The conversion destination type is indicated by the
 * instruction result type.
 * @var ir_cvt::value
 *      Converted value.
 */
typedef struct ir_cvt {
    ir_value_t          value;
} ir_cvt_t;

/**
 * @struct ir_instr
 * @brief Representation of intermediate representation instructions.
 * @var ir_instr::kind
 *      Instruction kind.
 * @var ir_instr::next
 *      Next instruction in the instruction block.
 *      This pointer is NULL if the instruction is terminal, i.e.
 *      one of \ref ir_instr_kind::IR_EXIT, \ref ir_instr_kind::IR_BR.
 * @var ir_instr::type
 *      Instruction result type. This field is also used to qualify
 *      \ref ir_instr_kind::IR_LOAD, \ref ir_instr_kind::IR_STORE,
 *      \ref ir_instr_kind::IR_READ, \ref ir_instr_kind::IR_WRITE instructions.
 * @var ir_instr::res
 *      Result pesudo variable. Since the intermediate representation
 *      is in single static assignment form, this corresponds to the
 *      unique declaration of the variable.
 */
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

/**
 * @struct ir_block
 * @brief Representation of an instruction block.
 * @details An instruction block is a sequence of non branching instructions,
 * terminated by a final instruction, \ref ir_instr_kind::IR_EXIT or
 * \ref ir_instr_kind::IR_BR.
 * @var ir_block::label
 *      Unique block label.
 * @var ir_block::entry
 *      Pointer to the first instruction in the instruction block.
 */
typedef struct ir_block {
    unsigned label;
    ir_instr_t *entry;
} ir_block_t;

/**
 * @struct ir_graph
 * @brief Representation of an instruction graph.
 * @details The graph entrypoint is always the first block in the array
 * \ref ir_graph::blocks.
 * @var ir_graph::blocks
 *      Pointer to the array of instruction blocks composing the graph.
 * @var ir_graph::nr_blocks
 *      Length of the instruction block array.
 * @var ir_graph::nr_vars
 *      Number of pseudo variables allocated inside the graph.
 *      This field can be used to dimension pseudo variable arrays
 *      for compilation passes.
 */
typedef struct ir_graph {
    ir_block_t *blocks;
    unsigned nr_blocks;
    unsigned nr_vars;
} ir_graph_t;

/**
 * @brief Create an exit instruction.
 * @return          Created exit instruction.
 */
static inline
ir_instr_t ir_make_exit(void) {
    return (ir_instr_t){ .kind = IR_EXIT };
}

/**
 * @brief Create an assert instruction.
 * @param cond      Assert condition. Must be a value of type `i1`.
 * @return          Created assert instruction.
 */
static inline
ir_instr_t ir_make_assert(ir_value_t cond) {
    ir_instr_t instr = { .kind = IR_ASSERT };
    instr.assert_ = (ir_assert_t){ .cond = cond };
    return instr;
}

/**
 * @brief Create a branch instruction.
 * @param target_false      Pointer to the false branch target block.
 * @param target_true       Pointer to the true branch target block.
 * @param cond      Branch condition. Must be a value of type `i1`.
 * @return          Created branc instruction.
 */
static inline
ir_instr_t ir_make_br(ir_value_t cond,
                      ir_block_t *target_false,
                      ir_block_t *target_true) {
    ir_instr_t instr = { .kind = IR_BR };
    instr.br = (ir_br_t){ .target = { target_false, target_true },
        .cond = cond, };
    return instr;
}

/**
 * @brief Create a call instruction.
 * @param res       Function result variable, ignored if the function is void.
 * @param type      Function result type, `i0` if the function is void.
 * @param func      Function pointer.
 * @param params
 *      Pointer to the array of function parameters. Can be NULL
 *      if the function takes no arguments.
 * @param nr_params
 *      Length of \p params.
 * @return          Created call instruction.
 */
static inline
ir_instr_t ir_make_call(ir_var_t res,
                        ir_type_t type,
                        ir_func_t func,
                        ir_value_t *params,
                        unsigned nr_params) {
    ir_instr_t instr = { .kind = IR_CALL, NULL, type, res };
    instr.call = (ir_call_t){ .func = func, .params = params,
        .nr_params = nr_params, };
    return instr;
}

/**
 * @brief Create an alloc instruction.
 * @param res       Result variable.
 * @param type      Type of the allocated memory cell.
 * @return          Created alloc instruction.
 */
static inline
ir_instr_t ir_make_alloc(ir_var_t res,
                         ir_type_t type) {
    ir_instr_t instr = { .kind = IR_ALLOC, NULL, ir_make_iptr(), res };
    instr.alloc = (ir_alloc_t){ type };
    return instr;
}

/**
 * @brief Create a unary instruction.
 * @param res       Result variable.
 * @param op        Unary operator.
 * @param value     Instruction operand.
 * @return          Created unary instruction.
 */
static inline
ir_instr_t ir_make_unop(ir_var_t res,
                        ir_instr_kind_t op,
                        ir_value_t value) {
    ir_instr_t instr = { .kind = op, NULL, value.type, res };
    instr.unop = (ir_unop_t){ value };
    return instr;
}

/**
 * @brief Create a binary instruction.
 * @param res       Result variable.
 * @param op        Binary operator.
 * @param left      First instruction operand.
 * @param right     Second instruction operand.
 * @return          Created binary instruction.
 */
static inline
ir_instr_t ir_make_binop(ir_var_t res,
                         ir_instr_kind_t op,
                         ir_value_t left,
                         ir_value_t right) {
    ir_instr_t instr = { .kind = op, NULL, left.type, res };
    instr.binop = (ir_binop_t){ left, right };
    return instr;
}

/**
 * @brief Create a compare instruction.
 * @param res       Result variable.
 * @param op        Comparison operator.
 * @param left      Left operand.
 * @param right     Right operand.
 * @return          Created compare instruction.
 */
static inline
ir_instr_t ir_make_icmp(ir_var_t res,
                        ir_icmp_kind_t op,
                        ir_value_t left,
                        ir_value_t right) {
    ir_instr_t instr = { .kind = IR_ICMP, NULL, ir_make_i1(), res };
    instr.icmp = (ir_icmp_t){ op, left, right };
    return instr;
}

/**
 * @brief Create a load instruction.
 * @param res       Result variable.
 * @param type      Load access type.
 * @param address   Load memory address.
 * @return          Created load instruction.
 */
static inline
ir_instr_t ir_make_load(ir_var_t res,
                        ir_type_t type,
                        ir_value_t address) {
    ir_instr_t instr = { .kind = IR_LOAD, NULL, type, res };
    instr.load = (ir_load_t){ address };
    return instr;
}

/**
 * @brief Create a store instruction.
 * @param type      Store access type.
 * @param address   Store memory address.
 * @param value     Stored value.
 * @return          Created store instruction.
 */
static inline
ir_instr_t ir_make_store(ir_type_t type,
                         ir_value_t address,
                         ir_value_t value) {
    ir_instr_t instr = { .kind = IR_STORE, NULL, type, 0 };
    instr.store = (ir_store_t){ address, value };
    return instr;
}

/**
 * @brief Create a read instruction.
 * @param res       Result variable.
 * @param type      Read access type.
 * @param global    Global variable identifier.
 * @return          Created read instruction.
 */
static inline
ir_instr_t ir_make_read(ir_var_t res,
                        ir_type_t type,
                        ir_global_t global) {
    ir_instr_t instr = { .kind = IR_READ, NULL, type, res };
    instr.read = (ir_read_t){ global };
    return instr;
}

/**
 * @brief Create a write instruction.
 * @param type      Write access type.
 * @param address   Global variable identifier.
 * @param value     Written value.
 * @return          Created write instruction.
 */
static inline
ir_instr_t ir_make_write(ir_type_t type,
                         ir_global_t global,
                         ir_value_t value) {
    ir_instr_t instr = { .kind = IR_WRITE, NULL, type, 0 };
    instr.write = (ir_write_t){ global, value };
    return instr;
}

/**
 * @brief Create a convert instruction.
 * @param res       Result variable.
 * @param type      Destination type of the conversion.
 * @param op        Conversion operator.
 * @param value     Converted value.
 * @return          Created convert instruction.
 */
static inline
ir_instr_t ir_make_cvt(ir_var_t res,
                       ir_type_t type,
                       ir_instr_kind_t op,
                       ir_value_t value) {
    ir_instr_t instr = { .kind = op, NULL, type, res };
    instr.cvt = (ir_cvt_t){ value };
    return instr;
}

/**
 * @brief Test if the input instruction has a result.
 * @details Void instructions are \ref ir_instr_kind::IR_ASSERT,
 * \ref ir_instr_kind::IR_EXIT, \ref ir_instr_kind::IR_BR,
 * \ref ir_instr_kind::IR_STORE, \ref ir_instr_kind::IR_WRITE and
 * \ref ir_instr_kind::IR_CALL if the function is void.
 * @param Pointer to the tested instruction.
 * @return
 *      True if the instruction does not return a value, false otherwise.
 */
bool ir_is_void_instr(ir_instr_t const *instr);

/**
 * @brief Print the input type to the selected buffer.
 * @param buf       Pointer to the output buffer.
 * @param len       Length of the output buffer.
 * @param type      Pointer to the input type.
 * @return          Number of written characters, or -1 if the print failed.
 */
int ir_print_type(char *buf, size_t len, ir_type_t const *type);

/**
 * @brief Print the input value to the selected buffer.
 * @param buf       Pointer to the output buffer.
 * @param len       Length of the output buffer.
 * @param type      Pointer to the input value.
 * @return          Number of written characters, or -1 if the print failed.
 */
int ir_print_value(char *buf, size_t len, ir_value_t const *value);

/**
 * @brief Print the input instruction to the selected buffer.
 * @param buf       Pointer to the output buffer.
 * @param len       Length of the output buffer.
 * @param type      Pointer to the input instruction.
 * @return          Number of written characters, or -1 if the print failed.
 */
int ir_print_instr(char *buf, size_t len, ir_instr_t const *instr);

/**
 * @brief Iterator callback function.
 * @details This callback is called for every input value in an instruction.
 * @param arg       Iterator private argument.
 * @param value     Pointer to the iterated value.
 */
typedef void (*ir_value_iterator_t)(void *arg, ir_value_t const *value);

/**
 * @brief Iterate over the input values of an instruction.
 * @param iter      Iterator callback function.
 * @param arg       Iterator private argument.
 */
void ir_iter_values(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_IR_H_INCLUDED_ */
