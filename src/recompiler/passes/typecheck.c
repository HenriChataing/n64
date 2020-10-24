
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/config.h>
#include <recompiler/ir.h>
#include <recompiler/passes.h>

static ir_type_t ir_var_types[RECOMPILER_VAR_MAX];
static const ir_block_t *cur_block;
static const ir_instr_t *cur_instr;
static char instr_str[128];

static char const *print_instr(ir_instr_t const *instr) {
    ir_print_instr(instr_str, sizeof(instr), instr);
    return instr_str;
}

static bool typecheck_res(recompiler_backend_t *backend,
                          ir_var_t res, ir_type_t type) {
    if (ir_var_types[res].width != 0) {
        raise_recompiler_error(backend, "typecheck",
            "the var %%%u is set twice, first set with type i%u\n"
            "in block .L%u, instruction:\n    %s",
            res, ir_var_types[res].width, cur_block->label,
            print_instr(cur_instr));
        return false;
    }
    if (type.width == 0) {
        raise_recompiler_error(backend, "typecheck",
            "the var %%%u is defined with invalie type i0\n"
            "in block .L%u, instruction:\n    %s",
            res, cur_block->label, print_instr(cur_instr));
        return false;
    }

    ir_var_types[res] = type;
    return true;
}

static bool typecheck_const(recompiler_backend_t *backend,
                            ir_value_t const *value, ir_type_t type) {
    if (!ir_type_equals(value->type, type)) {
        raise_recompiler_error(backend, "typecheck",
            "the const %" PRIuMAX " is expected to have type i%u, but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            value->const_.int_, type.width, value->type.width,
            cur_block->label, print_instr(cur_instr));
        return false;
    }
    return true;
}

static bool typecheck_var(recompiler_backend_t *backend,
                          ir_value_t const *value, ir_type_t type) {
    if (ir_var_types[value->var].width == 0) {
        raise_recompiler_error(backend, "typecheck",
            "the var %%%u is used here but is never defined\n"
            "in block .L%u, instruction:\n    %s",
            value->var, cur_block->label, print_instr(cur_instr));
        return false;
    }
    if (!ir_type_equals(ir_var_types[value->var], value->type)) {
        raise_recompiler_error(backend, "typecheck",
            "the var %%%u is defined with type i%u, but is used with type i%u\n"
            "in block .L%u, instruction:\n    %s",
            value->var, ir_var_types[value->var].width, value->type.width,
            cur_block->label, print_instr(cur_instr));
        return false;
    }
    if (!ir_type_equals(value->type, type)) {
        raise_recompiler_error(backend, "typecheck",
            "the var %%%u is expected to have type i%u, but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            value->var, type.width, value->type.width,
            cur_block->label, print_instr(cur_instr));
        return false;
    }
    return true;
}

static bool typecheck_value(recompiler_backend_t *backend,
                            ir_value_t const *value, ir_type_t type) {
    switch (value->kind) {
    case IR_CONST: return typecheck_const(backend, value, type);
    case IR_VAR:   return typecheck_var(backend, value, type);
    default:       return false;
    }
}

static bool typecheck_exit(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    return true;
}

static bool typecheck_br(recompiler_backend_t *backend,
                         ir_instr_t const *instr) {
    return typecheck_value(backend, &instr->br.cond, ir_make_iN(1));
}

static bool typecheck_call(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    bool valid = true;
    for (unsigned i = 0; i < instr->call.nr_params; i++) {
        valid &= typecheck_value(backend,
            &instr->call.params[i], instr->call.params[i].type);
    }
    if (instr->type.width > 0) {
        valid &= typecheck_res(backend, instr->res, instr->type);
    }
    return valid;
}

static bool typecheck_alloc(recompiler_backend_t *backend,
                            ir_instr_t const *instr) {
    if (!ir_type_equals(instr->type, ir_make_iptr())) {
        raise_recompiler_error(backend, "typecheck",
            "alloc result is expected to have type i%u, but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            ir_make_iptr().width, instr->type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
    return typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_unop(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    if (!typecheck_value(backend, &instr->unop.value, instr->unop.value.type))  {
        return false;
    }
    if (!ir_type_equals(instr->type, instr->unop.value.type)) {
        raise_recompiler_error(backend, "typecheck",
            "the result type i%u is incompatible with unop parameter type i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, instr->unop.value.type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
    return typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_binop(recompiler_backend_t *backend,
                            ir_instr_t const *instr) {
    if (!typecheck_value(backend, &instr->binop.left, instr->binop.left.type) ||
        !typecheck_value(backend, &instr->binop.right, instr->binop.right.type)) {
        return false;
    }
    if (!ir_type_equals(instr->binop.left.type, instr->binop.right.type)) {
        raise_recompiler_error(backend, "typecheck",
            "binop has parameters of different types i%u and i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->binop.left.type.width, instr->binop.right.type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
    if (!ir_type_equals(instr->type, instr->binop.left.type)) {
        raise_recompiler_error(backend, "typecheck",
            "the result type i%u is incompatible with binop parameter type i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, instr->binop.left.type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
    return typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_icmp(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    if (!typecheck_value(backend, &instr->icmp.left, instr->icmp.left.type) ||
        !typecheck_value(backend, &instr->icmp.right, instr->icmp.right.type))  {
        return false;
    }
    if (!ir_type_equals(instr->icmp.left.type, instr->icmp.right.type)) {
        raise_recompiler_error(backend, "typecheck",
            "icmp has parameters of different types i%u and i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->icmp.left.type.width, instr->icmp.right.type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
    if (!ir_type_equals(instr->type, ir_make_iN(1))) {
        raise_recompiler_error(backend, "typecheck",
            "icmp result is expected to have type i1, but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_load(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    return  typecheck_value(backend,
                &instr->load.address, instr->load.address.type) &&
            typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_store(recompiler_backend_t *backend,
                            ir_instr_t const *instr) {
    return  typecheck_value(backend,
                &instr->store.address, instr->store.address.type) &&
            typecheck_value(backend, &instr->store.value, instr->type);
}

static bool typecheck_read(recompiler_backend_t *backend,
                           ir_instr_t const *instr) {
    unsigned register_ = instr->read.register_;
    if (register_ >= backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        raise_recompiler_error(backend, "typecheck",
            "read access to undefined register $%u\n"
            "in block .L%u, instruction:\n    %s",
            register_, cur_block->label, print_instr(instr));
        return false;
    }
    if (!ir_type_equals(backend->registers[register_].type, instr->type)) {
        raise_recompiler_error(backend, "typecheck",
            "read access to register $%u is expected to have type i%u,"
            " but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            register_, backend->registers[register_].type.width,
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return  typecheck_res(backend, instr->res, instr->type);
}

static bool typecheck_write(recompiler_backend_t *backend,
                            ir_instr_t const *instr) {
    unsigned register_ = instr->write.register_;
    if (register_ >= backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        raise_recompiler_error(backend, "typecheck",
            "write access to undefined register $%u\n"
            "in block .L%u, instruction:\n    %s",
            cur_block->label, print_instr(instr));
        return false;
    }
    if (!ir_type_equals(backend->registers[register_].type, instr->type)) {
        raise_recompiler_error(backend, "typecheck",
            "write access to register $%u is expected to have type i%u,"
            " but has type i%u\n"
            "in block .L%u, instruction:\n    %s",
            register_, backend->registers[register_].type.width,
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return  typecheck_value(backend, &instr->write.value, instr->type);
}

static bool typecheck_cvt(recompiler_backend_t *backend,
                          ir_instr_t const *instr) {
    return  typecheck_value(backend, &instr->cvt.value, instr->cvt.value.type) &&
            typecheck_res(backend, instr->res, instr->type);
}

/**
 * Typecheck callbacks specialized for one IR instruction.
 * Return true if the instruction is well-typed, false otherwise.
 */
static const bool (*typecheck_callbacks[])(recompiler_backend_t *backend,
                                           ir_instr_t const *instr) = {
    [IR_EXIT]  = typecheck_exit,
    [IR_BR]    = typecheck_br,
    [IR_CALL]  = typecheck_call,
    [IR_ALLOC] = typecheck_alloc,
    [IR_NOT]   = typecheck_unop,
    [IR_ADD]   = typecheck_binop,
    [IR_SUB]   = typecheck_binop,
    [IR_MUL]   = typecheck_binop,
    [IR_UDIV]  = typecheck_binop,
    [IR_SDIV]  = typecheck_binop,
    [IR_UREM]  = typecheck_binop,
    [IR_SREM]  = typecheck_binop,
    [IR_SLL]   = typecheck_binop,
    [IR_SRL]   = typecheck_binop,
    [IR_SRA]   = typecheck_binop,
    [IR_AND]   = typecheck_binop,
    [IR_OR]    = typecheck_binop,
    [IR_XOR]   = typecheck_binop,
    [IR_ICMP]  = typecheck_icmp,
    [IR_LOAD]  = typecheck_load,
    [IR_STORE] = typecheck_store,
    [IR_READ]  = typecheck_read,
    [IR_WRITE] = typecheck_write,
    [IR_TRUNC] = typecheck_cvt,
    [IR_SEXT]  = typecheck_cvt,
    [IR_ZEXT]  = typecheck_cvt,
};

static bool typecheck_instr(recompiler_backend_t *backend,
                            ir_instr_t const *instr) {
    return typecheck_callbacks[instr->kind](backend, instr);
}

static bool typecheck_block(recompiler_backend_t *backend,
                            ir_block_t const *block) {
    bool valid = true;
    memset(ir_var_types, 0, sizeof(ir_var_types));
    cur_block = block;
    for (cur_instr = block->instrs; cur_instr != NULL; cur_instr = cur_instr->next) {
        valid &= typecheck_instr(backend, cur_instr);
    }
    return valid;
}

/**
 * @brief Perform a type checking pass on a generated instruction graph.
 * @param backend
 *      Pointer to the recompiler backend used to generate
 *      the instruction graph.
 * @param graph     Pointer to the instruction graph.
 * @return          True if the graph is well-typed, false otherwise.
 */
bool ir_typecheck(recompiler_backend_t *backend,
                  ir_graph_t const *graph) {
    bool valid = true;
    for (unsigned nr = 0; nr < graph->nr_blocks; nr++) {
        valid &= typecheck_block(backend, &graph->blocks[nr]);
    }
    return valid;
}
