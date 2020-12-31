
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/config.h>
#include <recompiler/passes.h>

typedef struct ir_var_context {
    ir_value_t value;
} ir_var_context_t;

typedef struct ir_global_context {
    ir_value_t value;
    bool set;
} ir_global_context_t;

static ir_var_context_t      ir_var_context[RECOMPILER_VAR_MAX];
static unsigned              ir_cur_var;
static ir_global_context_t   ir_global_context[RECOMPILER_GLOBAL_MAX];

static inline uintmax_t make_mask(unsigned width) {
    return width >= CHAR_BIT * sizeof(uintmax_t) ?
        UINTMAX_C(-1) : (UINTMAX_C(1) << width) - 1;
}

static inline uintmax_t sign_extend(unsigned in_width, unsigned out_width,
                                    uintmax_t value) {
    uintmax_t sign_bit = UINTMAX_C(1) << (in_width - 1);
    uintmax_t sign_width = out_width - in_width;
    uintmax_t sign_ext = ((UINTMAX_C(1) << sign_width) - 1) << in_width;
    return value & sign_bit ? value | sign_ext : value;
}

static ir_value_t convert_value(ir_value_t value) {
    switch (value.kind) {
    case IR_CONST:  return value;
    case IR_VAR:    return ir_var_context[value.var].value;
    default:        return ir_make_const_int(ir_make_iN(0), 0);
    }
}

static void const_res(ir_instr_t *instr, ir_value_t value) {
    ir_var_context[instr->res].value = value;
}

static void remap_res(ir_instr_t *instr) {
    ir_var_context[instr->res].value = ir_make_var(instr->type, ir_cur_var);
    instr->res = ir_cur_var;
    ir_cur_var++;
}

static bool optimize_exit(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    (void)backend;
    (void)instr;
    return false;
}

static bool optimize_assert(recompiler_backend_t *backend,
                            ir_instr_t *instr) {
    ir_value_t cond = convert_value(instr->assert_.cond);
    if (cond.kind == IR_CONST && cond.const_.int_ == 0) {
        *instr = ir_make_exit();
        return false;
    }
    if (cond.kind == IR_CONST && cond.const_.int_ != 0) {
        return true;
    }
    instr->assert_.cond = cond;
    return false;
}

static bool optimize_br(recompiler_backend_t *backend,
                        ir_instr_t *instr) {
    // TODO the br instruction should be replaced by a jmp when the
    // branch condition is constant.
    instr->br.cond = convert_value(instr->br.cond);
    return false;
}

static bool optimize_call(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    for (unsigned nr = 0; nr < instr->call.nr_params; nr++) {
        instr->call.params[nr] = convert_value(instr->call.params[nr]);
    }
    if (instr->type.width > 0) {
        remap_res(instr);
    }
    // TODO depends on call flags.
    memset(ir_global_context, 0, sizeof(ir_global_context));
    return false;
}

static bool optimize_alloc(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    remap_res(instr);
    return false;
}

static bool optimize_not(recompiler_backend_t *backend,
                         ir_instr_t *instr) {
    ir_value_t value = convert_value(instr->unop.value);
    if (value.kind == IR_CONST) {
        uintmax_t mask = make_mask(value.type.width);
        uintmax_t res = ~value.const_.int_ & mask;
        const_res(instr, ir_make_const_int(value.type, res));
        return true;
    } else {
        instr->unop.value = value;
        remap_res(instr);
        return false;
    }
}

static bool optimize_binop(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    ir_value_t left = convert_value(instr->binop.left);
    ir_value_t right = convert_value(instr->binop.right);

    if (left.kind == IR_CONST && right.kind == IR_CONST) {
        uintmax_t mask = make_mask(left.type.width);
        uintmax_t vleft = left.const_.int_;
        uintmax_t vright = right.const_.int_;
        uintmax_t res;
        switch (instr->kind) {
        case IR_ADD:    res = vleft + vright; break;
        case IR_SUB:    res = vleft - vright; break;
        case IR_MUL:    res = vleft * vright; break;
        case IR_UDIV:   res = vleft / vright; break;
        case IR_UREM:   res = vleft % vright; break;
        case IR_AND:    res = vleft & vright; break;
        case IR_OR:     res = vleft | vright; break;
        case IR_XOR:    res = vleft ^ vright; break;
        case IR_SLL:    res = vleft << vright; break;
        case IR_SRL:    res = vleft >> vright; break;
        case IR_SRA: {
            uintmax_t sign_bit = UINTMAX_C(1) << (left.type.width - 1);
            uintmax_t sign_ext = make_mask(vright) << (left.type.width - vright);
            res = (vleft & sign_bit) ?
                (vleft >> vright) | sign_ext : (vleft >> vright);
            break;
        }
        default: return false;
        }
        const_res(instr, ir_make_const_int(left.type, res & mask));
        return true;
    }

    else if (left.kind == IR_CONST && left.const_.int_ == 0) {
        ir_value_t res;
        switch (instr->kind) {
        case IR_MUL:
        case IR_UDIV:
        case IR_UREM:
        case IR_AND:
        case IR_SLL:
        case IR_SRL:
        case IR_SRA:
            res = ir_make_const_int(instr->type, 0);
            break;
        case IR_ADD:
        case IR_OR:
        case IR_XOR:
            res = right;
            break;
        case IR_SUB:
        default:
            instr->binop.left = left;
            instr->binop.right = right;
            remap_res(instr);
            return false;
        }
        const_res(instr, res);
        return true;
    }

    else if (right.kind == IR_CONST && right.const_.int_ == 0) {
        ir_value_t res;
        switch (instr->kind) {
        case IR_MUL:
        case IR_AND:
            res = ir_make_const_int(instr->type, 0);
            break;
        case IR_ADD:
        case IR_SUB:
        case IR_UDIV: // TODO special handling.
        case IR_UREM: // TODO special handling.
        case IR_OR:
        case IR_XOR:
        case IR_SLL:
        case IR_SRL:
        case IR_SRA:
            res = left;
            break;
        default:
            instr->binop.left = left;
            instr->binop.right = right;
            remap_res(instr);
            return false;
        }
        const_res(instr, res);
        return true;
    } else {
        instr->binop.left = left;
        instr->binop.right = right;
        remap_res(instr);
        return false;
    }
}

static bool optimize_binop_signed(recompiler_backend_t *backend,
                                  ir_instr_t *instr) {
    ir_value_t left = convert_value(instr->binop.left);
    ir_value_t right = convert_value(instr->binop.right);

    if (left.kind == IR_CONST && right.kind == IR_CONST) {
        uintmax_t mask = make_mask(left.type.width);
        intmax_t vleft = (intmax_t)sign_extend(
            left.type.width, CHAR_BIT * sizeof(uintmax_t), left.const_.int_);
        intmax_t vright = (intmax_t)sign_extend(
            right.type.width, CHAR_BIT * sizeof(uintmax_t), right.const_.int_);
        intmax_t res;
        switch (instr->kind) {
        case IR_SDIV:   res = vleft / vright; break;
        case IR_SREM:   res = vleft % vright; break;
        default: return false;
        }
        const_res(instr, ir_make_const_int(left.type, (uintmax_t)res & mask));
        return true;
    } else {
        instr->binop.left = left;
        instr->binop.right = right;
        remap_res(instr);
        return false;
    }
}

static bool optimize_icmp(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    ir_value_t left = convert_value(instr->icmp.left);
    ir_value_t right = convert_value(instr->icmp.right);

    if (left.kind == IR_CONST && right.kind == IR_CONST) {
        uintmax_t left_u = left.const_.int_;
        uintmax_t right_u = right.const_.int_;
        intmax_t left_s = (intmax_t)sign_extend(
            left.type.width, CHAR_BIT * sizeof(uintmax_t), left_u);
        intmax_t right_s = (intmax_t)sign_extend(
            right.type.width, CHAR_BIT * sizeof(uintmax_t), right_u);
        bool res;
        switch (instr->icmp.op) {
        case IR_EQ:  res = left_u == right_u; break;
        case IR_NE:  res = left_u != right_u; break;
        case IR_UGT: res = left_u > right_u; break;
        case IR_UGE: res = left_u >= right_u; break;
        case IR_ULT: res = left_u < right_u; break;
        case IR_ULE: res = left_u <= right_u; break;
        case IR_SGT: res = left_s > right_s; break;
        case IR_SGE: res = left_s >= right_s; break;
        case IR_SLT: res = left_s < right_s; break;
        case IR_SLE: res = left_s <= right_s; break;
        default: return false;
        }
        const_res(instr, ir_make_const_int(instr->type, res));
        return true;
    } else {
        instr->icmp.left = left;
        instr->icmp.right = right;
        remap_res(instr);
        return false;
    }
}

static bool optimize_load(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    instr->load.address = convert_value(instr->load.address);
    remap_res(instr);
    return false;
}

static bool optimize_store(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    instr->store.address = convert_value(instr->store.address);
    instr->store.value = convert_value(instr->store.value);
    return false;
}

static bool optimize_read(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    if (ir_global_context[instr->read.global].set) {
        const_res(instr, ir_global_context[instr->read.global].value);
        return true;
    } else {
        remap_res(instr);
        ir_global_context[instr->read.global].set = true;
        ir_global_context[instr->read.global].value =
            ir_make_var(instr->type, instr->res);
        return false;
    }
}

static bool optimize_write(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    // TODO only the last write to a global should be kept,
    // (except when global values must be committed,
    // e.g. before a function call).
    instr->write.value = convert_value(instr->write.value);
    ir_global_context[instr->write.global].set = true;
    ir_global_context[instr->write.global].value = instr->write.value;
    return false;
}

static bool optimize_trunc(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    ir_value_t value = convert_value(instr->cvt.value);
    if (value.kind == IR_CONST) {
        uintmax_t mask = make_mask(instr->type.width);
        uintmax_t res = value.const_.int_ & mask;
        const_res(instr, ir_make_const_int(instr->type, res));
        return true;
    } else {
        instr->cvt.value = value;
        remap_res(instr);
        return false;
    }
}

static bool optimize_sext(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    ir_value_t value = convert_value(instr->cvt.value);
    if (value.kind == IR_CONST) {
        uintmax_t res =
            sign_extend(value.type.width, instr->type.width, value.const_.int_);
        const_res(instr, ir_make_const_int(instr->type, res));
        return true;
    } else {
        instr->cvt.value = value;
        remap_res(instr);
        return false;
    }
}

static bool optimize_zext(recompiler_backend_t *backend,
                          ir_instr_t *instr) {
    ir_value_t value = convert_value(instr->cvt.value);
    if (value.kind == IR_CONST) {
        const_res(instr, ir_make_const_int(instr->type, value.const_.int_));
        return true;
    } else {
        instr->cvt.value = value;
        remap_res(instr);
        return false;
    }
}

/** Optimize callbacks specialized for one IR instruction.
 * Return true iff the instruction is optimized away, false otherwise. */
static const bool (*optimize_callbacks[])(recompiler_backend_t *backend,
                                          ir_instr_t *instr) = {
    [IR_EXIT]   = optimize_exit,
    [IR_ASSERT] = optimize_assert,
    [IR_BR]     = optimize_br,
    [IR_CALL]   = optimize_call,
    [IR_ALLOC]  = optimize_alloc,
    [IR_NOT]    = optimize_not,
    [IR_ADD]    = optimize_binop,
    [IR_SUB]    = optimize_binop,
    [IR_MUL]    = optimize_binop,
    [IR_UDIV]   = optimize_binop,
    [IR_SDIV]   = optimize_binop_signed,
    [IR_UREM]   = optimize_binop,
    [IR_SREM]   = optimize_binop_signed,
    [IR_SLL]    = optimize_binop,
    [IR_SRL]    = optimize_binop,
    [IR_SRA]    = optimize_binop,
    [IR_AND]    = optimize_binop,
    [IR_OR]     = optimize_binop,
    [IR_XOR]    = optimize_binop,
    [IR_ICMP]   = optimize_icmp,
    [IR_LOAD]   = optimize_load,
    [IR_STORE]  = optimize_store,
    [IR_READ]   = optimize_read,
    [IR_WRITE]  = optimize_write,
    [IR_TRUNC]  = optimize_trunc,
    [IR_SEXT]   = optimize_sext,
    [IR_ZEXT]   = optimize_zext,
};

_Static_assert(IR_ZEXT == 26,
    "IR instruction set changed, code may need to be updated");

/**
 * Optimize an instruction block.
 * @param backend   Pointer to the recompiler backend.
 * @param block     Pointer to the block to optimize.
 */
static bool optimize_instr(recompiler_backend_t *backend,
                           ir_instr_t *instr) {
    return optimize_callbacks[instr->kind](backend, instr);
}

/**
 * Optimize an instruction block.
 * @param backend   Pointer to the recompiler backend.
 * @param block     Pointer to the block to optimize.
 */
static void optimize_block(recompiler_backend_t *backend,
                           ir_block_t *block) {
    ir_instr_t *instr = block->entry;
    ir_instr_t *next_instr;
    ir_instr_t **prev_instr = &block->entry;

    memset(ir_global_context, 0, sizeof(ir_global_context));

    for (; instr != NULL; instr = next_instr) {
        next_instr = instr->next;
        if (!optimize_instr(backend, instr)) {
            *prev_instr = instr;
            prev_instr = &instr->next;
        }
    }
}

/**
 * Optimize an instruction graph.
 * @param backend   Pointer to the recompiler backend.
 * @param block     Pointer to the graph to optimize.
 */
void ir_optimize(recompiler_backend_t *backend,
                 ir_graph_t *graph) {
    ir_cur_var = 0;
    for (unsigned nr = 0; nr < graph->nr_blocks; nr++) {
        optimize_block(backend, &graph->blocks[nr]);
    }
}
