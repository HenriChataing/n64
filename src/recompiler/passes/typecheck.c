
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/ir.h>
#include <recompiler/passes.h>

#define IR_TYPECHECK_VAR_MAX 4096

static ir_type_t ir_var_types[IR_TYPECHECK_VAR_MAX];
static char *ir_error_msg;
static size_t ir_error_msg_len;

static bool ir_typecheck_res(ir_var_t res, ir_type_t type) {
    if (ir_var_types[res].width != 0) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the var %%%u is set twice, first set with type i%u",
            res, ir_var_types[res].width);
        return false;
    }

    ir_var_types[res] = type;
    return true;
}

static bool ir_typecheck_const(ir_value_t const *value, ir_type_t type) {
    if (!ir_type_equals(value->type, type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the const %" PRIuMAX " is expected to have type i%u, but has type i%u",
            value->const_.int_, type.width, value->type.width);
        return false;
    }
    return true;
}

static bool ir_typecheck_var(ir_value_t const *value, ir_type_t type) {
    if (ir_var_types[value->var].width == 0) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the var %%%u is used here by is not defined",
            value->var);
        return false;
    }
    if (!ir_type_equals(ir_var_types[value->var], value->type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the var %%%u is defined with type i%u, but is used with type i%u",
            value->var, ir_var_types[value->var].width, value->type.width);
        return false;
    }
    if (!ir_type_equals(value->type, type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the var %%%u is expected to have type i%u, but has type i%u",
            value->var, type.width, value->type.width);
        return false;
    }
    return true;
}

static bool ir_typecheck_value(ir_value_t const *value, ir_type_t type) {
    switch (value->kind) {
    case IR_CONST: return ir_typecheck_const(value, type);
    case IR_VAR:   return ir_typecheck_var(value, type);
    }
    return false;
}

static bool ir_typecheck_call(ir_instr_t const *instr) {
    for (unsigned i = 0; i < instr->call.nr_params; i++) {
        if (!ir_typecheck_value(&instr->call.params[i],
                                instr->call.params[i].type))  {
            return false;
        }
    }
    if (instr->type.width > 0 &&
        !ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_unop(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->unop.value, instr->unop.value.type))  {
        return false;
    }
    if (!ir_type_equals(instr->type, instr->unop.value.type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the result type i%u is incompatible with unop parameter type i%u",
            instr->type.width, instr->unop.value.type.width);
        return false;
    }
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_binop(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->binop.left, instr->binop.left.type) ||
        !ir_typecheck_value(&instr->binop.right, instr->binop.right.type))  {
        return false;
    }
    if (!ir_type_equals(instr->binop.left.type, instr->binop.right.type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "binop has parameters of different types i%u and i%u",
            instr->binop.left.type.width, instr->binop.right.type.width);
        return false;
    }
    if (!ir_type_equals(instr->type, instr->binop.left.type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the result type i%u is incompatible with binop parameter type i%u",
            instr->type.width, instr->binop.left.type.width);
        return false;
    }
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_icmp(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->icmp.left, instr->icmp.left.type) ||
        !ir_typecheck_value(&instr->icmp.right, instr->icmp.right.type))  {
        return false;
    }
    if (!ir_type_equals(instr->icmp.left.type, instr->icmp.right.type)) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "icmp has parameters of different types i%u and i%u",
            instr->icmp.left.type.width, instr->icmp.right.type.width);
        return false;
    }
    if (!ir_type_equals(instr->type, ir_make_iN(1))) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "the result is expected to have type i1, but has type i%u",
            instr->type.width);
        return false;
    }
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_load(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->load.address, instr->load.address.type))  {
        return false;
    }
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_store(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->store.address, instr->store.address.type) ||
        !ir_typecheck_value(&instr->store.value, instr->type))  {
        return false;
    }

    return true;
}

static bool ir_typecheck_read(ir_instr_t const *instr) {
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}

static bool ir_typecheck_write(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->write.value, instr->type))  {
        return false;
    }

    return true;
}

static bool ir_typecheck_cvt(ir_instr_t const *instr) {
    if (!ir_typecheck_value(&instr->cvt.value, instr->cvt.value.type))  {
        return false;
    }
    if (!ir_typecheck_res(instr->res, instr->type)) {
        return false;
    }

    return true;
}


static bool ir_typecheck_instr(ir_instr_t const *instr) {
    switch (instr->kind) {
    case IR_EXIT:       return true;
    case IR_BR:
        return ir_typecheck_value(&instr->br.cond, ir_make_iN(1));
    case IR_CALL:
        return ir_typecheck_call(instr);
    case IR_NOT:
        return ir_typecheck_unop(instr);
    case IR_ADD:
    case IR_SUB:
    case IR_UMUL:
    case IR_SMUL:
    case IR_UDIV:
    case IR_SDIV:
    case IR_UREM:
    case IR_SREM:
    case IR_SLL:
    case IR_SRL:
    case IR_SRA:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
        return ir_typecheck_binop(instr);
    case IR_ICMP:
        return ir_typecheck_icmp(instr);
    case IR_LOAD:
        return ir_typecheck_load(instr);
    case IR_STORE:
        return ir_typecheck_store(instr);
    case IR_READ:
        return ir_typecheck_read(instr);
    case IR_WRITE:
        return ir_typecheck_write(instr);
    case IR_TRUNC:
    case IR_SEXT:
    case IR_ZEXT:
        return ir_typecheck_cvt(instr);
    }

    return false;
}

static bool ir_typecheck_block(ir_block_t const *block,
                               ir_instr_t const **err_instr) {
    for (ir_instr_t *instr = block->instrs; instr != NULL; instr = instr->next) {
        if (!ir_typecheck_instr(instr)) {
            *err_instr = instr;
            return false;
        }
    }
    return true;
}

bool ir_typecheck(ir_graph_t const *graph, ir_instr_t const **err_instr,
                  char *err_msg, size_t err_msg_len) {
    /* Set the default type i0 for all variables. */
    memset(ir_var_types, 0, sizeof(ir_var_types));
    ir_error_msg = err_msg;
    ir_error_msg_len = err_msg_len;

    for (unsigned nr = 0; nr < graph->nr_blocks; nr++) {
        if (!ir_typecheck_block(&graph->blocks[nr], err_instr)) {
            return false;
        }
    }
    return true;
}
