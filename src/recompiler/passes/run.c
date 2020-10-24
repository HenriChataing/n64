
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

static bool run_exit(ir_recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    return true;
}

static bool run_br(ir_recompiler_backend_t *backend,
                   ir_instr_t const *instr) {
    ir_const_t value = eval_value(instr->br.cond);
    cur_block = instr->br.target[value.int_ != 0];
    next_instr = cur_block->instrs;
    return true;
}

static bool run_call(ir_recompiler_backend_t *backend,
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

static bool run_alloc(ir_recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    ir_var_values[instr->res] = (ir_const_t){
        (uintptr_t)&ir_var_alloc[instr->res],
    };
    return true;
}

static bool run_not(ir_recompiler_backend_t *backend,
                    ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->unop.value).int_;
    uintmax_t mask = make_mask(instr->type.width);
    ir_var_values[instr->res] = (ir_const_t){ ~value & mask };
    return true;
}

static bool run_binop(ir_recompiler_backend_t *backend,
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

static bool run_icmp(ir_recompiler_backend_t *backend,
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

static bool run_load(ir_recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;
    uintmax_t address = eval_value(instr->load.address).int_;
    uintmax_t res;
    uint8_t res_u8 = 0;
    uint16_t res_u16 = 0;
    uint32_t res_u32 = 0;
    uint64_t res_u64 = 0;
    bool exception = false;

    switch (instr->type.width) {
    case 8:
        exception = !memory->load_u8(address, &res_u8);
        res = res_u8;
        break;
    case 16:
        exception = !memory->load_u16(address, &res_u16);
        res = res_u16;
        break;
    case 32:
        exception = !memory->load_u32(address, &res_u32);
        res = res_u32;
        break;
    case 64:
        exception = !memory->load_u64(address, &res_u64);
        res = res_u64;
        break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported load bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }

    if (exception) {
        raise_recompiler_error(backend, "run",
            "unhandled synchronous exception in memory load\n"
            "in block .L%u, instruction:\n    %s",
            cur_block->label, print_instr(instr));
        return false;
    } else {
        ir_var_values[instr->res] = (ir_const_t){ res };
        return true;
    }
}

static bool run_store(ir_recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;
    uintmax_t address = eval_value(instr->store.address).int_;
    uintmax_t value   = eval_value(instr->store.value).int_;
    bool exception = false;

    switch (instr->type.width) {
    case 8:     exception = !memory->store_u8(address, value);  break;
    case 16:    exception = !memory->store_u16(address, value); break;
    case 32:    exception = !memory->store_u32(address, value); break;
    case 64:    exception = !memory->store_u64(address, value); break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported store bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            instr->type.width, cur_block->label, print_instr(instr));
        return false;
    }

    if (exception) {
        raise_recompiler_error(backend, "run",
            "unhandled synchronous exception in memory store\n"
            "in block .L%u, instruction:\n    %s",
            cur_block->label, print_instr(instr));
    }
    return !exception;
}

static bool run_read(ir_recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    ir_register_t register_ = instr->read.register_;
    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;
    uintmax_t *value_ptr = &ir_var_values[instr->res].int_;

    switch (type.width) {
    case 8:  *value_ptr = *(uint8_t  *)ptr; break;
    case 16: *value_ptr = *(uint16_t *)ptr; break;
    case 32: *value_ptr = *(uint32_t *)ptr; break;
    case 64: *value_ptr = *(uint64_t *)ptr; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported register bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return true;
}

static bool run_write(ir_recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    ir_register_t register_ = instr->write.register_;
    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;
    uintmax_t value = eval_value(instr->write.value).int_;

    switch (type.width) {
    case 8:  *(uint8_t  *)ptr = value; break;
    case 16: *(uint16_t *)ptr = value; break;
    case 32: *(uint32_t *)ptr = value; break;
    case 64: *(uint64_t *)ptr = value; break;
    default:
        raise_recompiler_error(backend, "run",
            "unsupported register bit width %u\n"
            "in block .L%u, instruction:\n    %s",
            type.width, cur_block->label, print_instr(instr));
        return false;
    }
    return true;
}

static bool run_trunc(ir_recompiler_backend_t *backend,
                      ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->cvt.value).int_;
    uintmax_t mask = make_mask(instr->type.width);
    ir_var_values[instr->res] = (ir_const_t) { value & mask };
    return true;
}

static bool run_sext(ir_recompiler_backend_t *backend,
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

static bool run_zext(ir_recompiler_backend_t *backend,
                     ir_instr_t const *instr) {
    uintmax_t value = eval_value(instr->cvt.value).int_;
    ir_var_values[instr->res] = (ir_const_t) { value };
    return true;
}

/**
 * Run callbacks specialized for one IR instruction.
 * Return true if the execution is successful, false otherwise.
 */
static const bool (*run_callbacks[])(ir_recompiler_backend_t *backend,
                                     ir_instr_t const *instr) = {
    [IR_EXIT]  = run_exit,
    [IR_BR]    = run_br,
    [IR_CALL]  = run_call,
    [IR_ALLOC] = run_alloc,
    [IR_NOT]   = run_not,
    [IR_ADD]   = run_binop,
    [IR_SUB]   = run_binop,
    [IR_MUL]   = run_binop,
    [IR_UDIV]  = run_binop,
    [IR_SDIV]  = run_binop,
    [IR_UREM]  = run_binop,
    [IR_SREM]  = run_binop,
    [IR_SLL]   = run_binop,
    [IR_SRL]   = run_binop,
    [IR_SRA]   = run_binop,
    [IR_AND]   = run_binop,
    [IR_OR]    = run_binop,
    [IR_XOR]   = run_binop,
    [IR_ICMP]  = run_icmp,
    [IR_LOAD]  = run_load,
    [IR_STORE] = run_store,
    [IR_READ]  = run_read,
    [IR_WRITE] = run_write,
    [IR_TRUNC] = run_trunc,
    [IR_SEXT]  = run_sext,
    [IR_ZEXT]  = run_zext,
};

static bool run_instr(ir_recompiler_backend_t *backend,
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
bool ir_run(ir_recompiler_backend_t *backend,
            ir_graph_t const *graph) {

    memset(ir_var_values, 0, sizeof(ir_var_values));
    cur_block = &graph->blocks[0];
    ir_instr_t const *instr;

    for (instr = cur_block->instrs; instr != NULL; instr = next_instr) {
        next_instr = instr->next;
        cur_instr = instr;

        if (!run_instr(backend, instr)) {
            return false;
        }
    }
    return true;
}
