
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/config.h>
#include <recompiler/ir.h>
#include <recompiler/passes.h>

static ir_const_t ir_var_values[RECOMPILER_VAR_MAX];
static uintmax_t ir_var_alloc[RECOMPILER_VAR_MAX];
static const ir_block_t *cur_block;
static const ir_instr_t *cur_instr;
static const ir_instr_t *next_instr;
static char instr_str[128];

static char const *print_instr(ir_instr_t const *instr) {
    ir_print_instr(instr_str, sizeof(instr), instr);
    return instr_str;
}

static inline uintmax_t make_mask(unsigned width) {
    return width >= CHAR_BIT * sizeof(uintmax_t) ?
        UINTMAX_C(-1) : (UINTMAX_C(1) << width) - 1;
}

static inline uintmax_t make_sign_bit(unsigned width) {
    return UINTMAX_C(1) << (width - 1);
}

static inline uintmax_t make_sign_ext(unsigned width, unsigned shift) {
    return make_mask(width) << shift;
}

static ir_const_t eval_value(ir_value_t value) {
    switch (value.kind) {
    case IR_CONST: return value.const_;
    case IR_VAR:   return ir_var_values[value.var];
    default:       return (ir_const_t){ 0 };
    }
}

static bool run_exit(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    return true;
}

static bool run_assert(recompiler_backend_t *backend,
                       ir_instr_t const *instr) {
    ir_const_t value = eval_value(instr->assert_.cond);
    next_instr = value.int_ != 0 ? instr->next : NULL;
    return true;
}

static bool run_br(recompiler_backend_t *backend,
                   ir_instr_t const *instr) {
    ir_const_t value = eval_value(instr->br.cond);
    cur_block = instr->br.target[value.int_ != 0];
    next_instr = cur_block ? cur_block->entry : NULL;
    return true;
}

static bool run_call(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    if (instr->call.nr_params == 0 && instr->type.width == 0) {
        instr->call.func();
        return true;
    }
    else if (instr->call.nr_params == 0) {
        uintmax_t mask = make_mask(instr->type.width);
        uintmax_t res;
        uint32_t (*func_i32)(void) = (void *)instr->call.func;
        uint64_t (*func_i64)(void) = (void *)instr->call.func;
        switch (instr->type.width) {
        case 32: res = func_i32(); break;
        case 64: res = func_i64(); break;
        default:
            raise_recompiler_error(backend, "run",
                "unsupported return bit width %u"
                " in function call with no parameters\n"
                "in block .L%u, instruction:\n    %s",
                instr->type.width, cur_block->label, print_instr(instr));
            return false;
        }
        ir_var_values[instr->res] = (ir_const_t){ res & mask };
        return true;
    }
    else if (instr->call.nr_params == 1 && instr->type.width == 0) {
        uintmax_t value = eval_value(instr->call.params[0]).int_;
        switch (instr->call.params[0].type.width) {
        case 8:  instr->call.func((uint8_t)value);  break;
        case 16: instr->call.func((uint16_t)value); break;
        case 32: instr->call.func((uint32_t)value); break;
        case 64: instr->call.func((uint64_t)value); break;
        default:
            raise_recompiler_error(backend, "run",
                "unsupported parameter bit width %u"
                " in function call with one parameter\n"
                "in block .L%u, instruction:\n    %s",
                instr->call.params[0].type.width,
                cur_block->label, print_instr(instr));
            return false;
        }
        return true;
    }
    else if (instr->call.nr_params == 1 && instr->type.width == 1) {
        uintmax_t value = eval_value(instr->call.params[0]).int_;
        bool (*func)() = (void *)instr->call.func;
        bool res;
        switch (instr->call.params[0].type.width) {
        case 32: res = func((uint32_t)value); break;
        case 64: res = func((uint64_t)value); break;
        default:
            raise_recompiler_error(backend, "run",
                "unsupported parameter bit width %u"
                " in function call with one parameter\n"
                "in block .L%u, instruction:\n    %s",
                instr->call.params[0].type.width,
                cur_block->label, print_instr(instr));
            return false;
        }
        ir_var_values[instr->res] = (ir_const_t){ res };
        return true;
    }
    else if (instr->call.nr_params == 2 &&
             instr->call.params[0].type.width == 64 &&
             instr->type.width == 1) {
        uintmax_t param0 = eval_value(instr->call.params[0]).int_;
        uintmax_t param1 = eval_value(instr->call.params[1]).int_;
        bool (*func)() = (void *)instr->call.func;
        bool res;
        switch (instr->call.params[1].type.width) {
        case 8: res = func(param0,  (uint8_t)param1); break;
        case 16: res = func(param0, (uint16_t)param1); break;
        case 32: res = func(param0, (uint32_t)param1); break;
        case 64: res = func(param0, (uint64_t)param1); break;
        default:
            raise_recompiler_error(backend, "run",
                "unsupported parameter bit width %u"
                " in function call with two parameters\n"
                "in block .L%u, instruction:\n    %s",
                instr->call.params[1].type.width,
                cur_block->label, print_instr(instr));
            return false;
        }
        ir_var_values[instr->res] = (ir_const_t){ res };
        return true;
    }
    else {
        raise_recompiler_error(backend, "run",
            "unsupported function call with %u parameters"
            " and return type i%u\n"
            "in block .L%u, instruction:\n    %s",
            instr->call.nr_params, instr->type.width,
            cur_block->label, print_instr(instr));
        return false;
    }
}

static bool run_alloc(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    ir_var_values[instr->res] = (ir_const_t){
        (uintptr_t)&ir_var_alloc[instr->res],
    };
    return true;
}

static bool run_not(recompiler_backend_t *backend,
                    ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->unop.value).int_;
    uintmax_t mask = make_mask(instr->type.width);
    ir_var_values[instr->res] = (ir_const_t){ ~value & mask };
    return true;
}

static bool run_binop(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    unsigned width = instr->type.width;
    uintmax_t mask = make_mask(width);
    uintmax_t left = eval_value(instr->binop.left).int_;
    uintmax_t right = eval_value(instr->binop.right).int_;
    uintmax_t sign_bit = make_sign_bit(width);
    uintmax_t sign_ext = make_sign_ext(
        CHAR_BIT * sizeof(uintmax_t) - width, width);
    uintmax_t left_s = left & sign_bit ? left | sign_ext : left;
    uintmax_t right_s = right & sign_bit ? right | sign_ext : right;
    uintmax_t res;

    switch (instr->kind) {
    case IR_ADD:    res = left + right; break;
    case IR_SUB:    res = left - right; break;
    case IR_MUL:    res = left * right; break;
    case IR_UDIV:   res = left / right; break;
    case IR_SDIV:   res = (uintmax_t)((intmax_t)left_s / (intmax_t)right_s); break;
    case IR_UREM:   res = (uintmax_t)((intmax_t)left_s % (intmax_t)right_s); break;
    case IR_SREM:   res = left % right; break;
    case IR_AND:    res = left & right; break;
    case IR_OR:     res = left | right; break;
    case IR_XOR:    res = left ^ right; break;
    case IR_SLL:    res = left << right; break;
    case IR_SRL:    res = left >> right; break;
    case IR_SRA:    res = left_s >> right; break;
    default: return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ res & mask };
    return true;
}

static bool run_icmp(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    unsigned width = instr->icmp.left.type.width;
    uintmax_t left = eval_value(instr->icmp.left).int_;
    uintmax_t right = eval_value(instr->icmp.right).int_;
    uintmax_t sign_bit = make_sign_bit(width);
    uintmax_t sign_ext = make_sign_ext(
        CHAR_BIT * sizeof(uintmax_t) - width, width);
    intmax_t left_s = left & sign_bit ?
        (intmax_t)(left | sign_ext) : (intmax_t)left;
    intmax_t right_s = right & sign_bit ?
        (intmax_t)(right | sign_ext) : (intmax_t)right;
    bool res;

    switch (instr->icmp.op) {
    case IR_EQ:  res = left == right; break;
    case IR_NE:  res = left != right; break;
    case IR_UGT: res = left > right; break;
    case IR_UGE: res = left >= right; break;
    case IR_ULT: res = left < right; break;
    case IR_ULE: res = left <= right; break;
    case IR_SGT: res = left_s > right_s; break;
    case IR_SGE: res = left_s >= right_s; break;
    case IR_SLT: res = left_s < right_s; break;
    case IR_SLE: res = left_s <= right_s; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported icmp comparator %u\n"
            "in block .L%u, instruction:\n    %s",
            instr->icmp.op, cur_block->label, print_instr(instr));
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ res };
    return true;
}

static bool run_load(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    uintmax_t address = eval_value(instr->load.address).int_;
    uintmax_t res;

    switch (instr->type.width) {
    case 8:  res = *(uint8_t *)address; break;
    case 16: res = *(uint16_t *)address; break;
    case 32: res = *(uint32_t *)address; break;
    case 64: res = *(uint64_t *)address; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported load bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ res };
    return true;
}

static bool run_store(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    uintmax_t address = eval_value(instr->store.address).int_;
    uintmax_t value   = eval_value(instr->store.value).int_;

    switch (instr->type.width) {
    case 8:  *(uint8_t *)address = value; break;
    case 16: *(uint16_t *)address = value; break;
    case 32: *(uint32_t *)address = value; break;
    case 64: *(uint64_t *)address = value; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported store bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return true;
}

static bool run_read(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    ir_global_t global = instr->read.global;
    void *ptr = backend->globals[global].ptr;
    ir_type_t type = backend->globals[global].type;
    uintmax_t *value_ptr = &ir_var_values[instr->res].int_;

    switch (type.width) {
    case 8:  *value_ptr = *(uint8_t  *)ptr; break;
    case 16: *value_ptr = *(uint16_t *)ptr; break;
    case 32: *value_ptr = *(uint32_t *)ptr; break;
    case 64: *value_ptr = *(uint64_t *)ptr; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported global variable bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return true;
}

static bool run_write(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    ir_global_t global = instr->write.global;
    void *ptr = backend->globals[global].ptr;
    ir_type_t type = backend->globals[global].type;
    uintmax_t value = eval_value(instr->write.value).int_;

    switch (type.width) {
    case 8:  *(uint8_t  *)ptr = value; break;
    case 16: *(uint16_t *)ptr = value; break;
    case 32: *(uint32_t *)ptr = value; break;
    case 64: *(uint64_t *)ptr = value; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported global variable bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return true;
}

static bool run_trunc(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->cvt.value).int_;
    uintmax_t mask = make_mask(instr->type.width);
    ir_var_values[instr->res] = (ir_const_t) { value & mask };
    return true;
}

static bool run_sext(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->cvt.value).int_;
    unsigned in_width = instr->cvt.value.type.width;
    unsigned out_width = instr->type.width;
    uintmax_t sign_bit = make_sign_bit(in_width);
    uintmax_t sign_ext = make_sign_ext(out_width - in_width, in_width);
    uintmax_t res = value & sign_bit ? value | sign_ext : value;
    ir_var_values[instr->res] = (ir_const_t) { res };
    return true;
}

static bool run_zext(recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->cvt.value).int_;
    ir_var_values[instr->res] = (ir_const_t) { value };
    return true;
}

/**
 * Run callbacks specialized for one IR instruction.
 * Return true if the execution is successful, false otherwise.
 */
static const bool (*run_callbacks[])(recompiler_backend_t *backend,
                                     ir_instr_t const *instr) = {
    [IR_EXIT]   = run_exit,
    [IR_ASSERT] = run_assert,
    [IR_BR]     = run_br,
    [IR_CALL]   = run_call,
    [IR_ALLOC]  = run_alloc,
    [IR_NOT]    = run_not,
    [IR_ADD]    = run_binop,
    [IR_SUB]    = run_binop,
    [IR_MUL]    = run_binop,
    [IR_UDIV]   = run_binop,
    [IR_SDIV]   = run_binop,
    [IR_UREM]   = run_binop,
    [IR_SREM]   = run_binop,
    [IR_SLL]    = run_binop,
    [IR_SRL]    = run_binop,
    [IR_SRA]    = run_binop,
    [IR_AND]    = run_binop,
    [IR_OR]     = run_binop,
    [IR_XOR]    = run_binop,
    [IR_ICMP]   = run_icmp,
    [IR_LOAD]   = run_load,
    [IR_STORE]  = run_store,
    [IR_READ]   = run_read,
    [IR_WRITE]  = run_write,
    [IR_TRUNC]  = run_trunc,
    [IR_SEXT]   = run_sext,
    [IR_ZEXT]   = run_zext,
};

_Static_assert(IR_ZEXT == 26,
    "IR instruction set changed, code may need to be updated");

static bool run_instr(recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    return run_callbacks[instr->kind](backend, instr);
}

/**
 * @brief Execute the generated instruction graph.
 * The initial state is assumed to have been previously loaded
 * into the global variables.
 * @param backend
 *      Pointer to the recompiler backend used to generate
 *      the instruction graph.
 * @param graph     Pointer to the instruction graph.
 * @return
 *      True if the execution completed successfully, false otherwise.
 */
bool ir_run(recompiler_backend_t *backend,
            ir_graph_t const *graph) {

    memset(ir_var_values, 0, sizeof(ir_var_values));
    cur_block = &graph->blocks[0];
    ir_instr_t const *instr;

    for (instr = cur_block->entry; instr != NULL; instr = next_instr) {
        next_instr = instr->next;
        cur_instr = instr;

        if (!run_instr(backend, instr)) {
            return false;
        }
    }
    return true;
}
