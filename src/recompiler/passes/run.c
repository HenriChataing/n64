
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/ir.h>
#include <recompiler/passes.h>

#define IR_RUN_VAR_MAX 4096

static ir_const_t ir_var_values[IR_RUN_VAR_MAX];
static unsigned nr_var_values;
static char *ir_error_msg;
static size_t ir_error_msg_len;

static inline uintmax_t ir_make_mask(unsigned width) {
    return width >= CHAR_BIT * sizeof(uintmax_t) ?
        UINTMAX_C(-1) : (UINTMAX_C(1) << width) - 1;
}

static ir_const_t ir_eval_value(ir_value_t value) {
    switch (value.kind) {
    case IR_CONST: return value.const_;
    case IR_VAR:   return ir_var_values[value.var];
    default:       return (ir_const_t){ 0 };
    }
}

static bool ir_run_br(ir_instr_t const *instr,
                      ir_instr_t const **next_instr) {
    ir_const_t value = ir_eval_value(instr->br.cond);
    switch (value.int_) {
    case 0:
        return true;
    case 1:
        *next_instr = instr->br.target->instrs;
        return true;
    default:
        snprintf(ir_error_msg, ir_error_msg_len,
            "branch condition evaluated to non boolean value %" PRIuMAX,
            value.int_);
        return false;
    }
}

static bool ir_run_call(ir_instr_t const *instr) {
    if (instr->call.nr_params == 0) {
        instr->call.func();
        return true;
    }
    if (instr->call.nr_params == 1) {
        ir_const_t const_ = ir_eval_value(instr->call.params[0]);
        switch (instr->call.params[0].type.width) {
        case 8:  instr->call.func((uint8_t)const_.int_);  return true;
        case 16: instr->call.func((uint16_t)const_.int_); return true;
        case 32: instr->call.func((uint32_t)const_.int_); return true;
        case 64: instr->call.func((uint64_t)const_.int_); return true;
        default:
            break;
        }
    }
    snprintf(ir_error_msg, ir_error_msg_len,
        "function call unsupported in IR run");
    return false;
}

static bool ir_run_unop(ir_instr_t const *instr) {
    uintmax_t value = ir_eval_value(instr->unop.value).int_;
    uintmax_t mask = ir_make_mask(instr->unop.value.type.width);
    uintmax_t res;
    switch (instr->kind) {
    case IR_NOT:    res = ~value; break;
    default:
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ .int_ = res & mask };
    return true;
}

static bool ir_run_binop(ir_instr_t const *instr) {
    uintmax_t left  = ir_eval_value(instr->binop.left).int_;
    uintmax_t right = ir_eval_value(instr->binop.right).int_;
    unsigned width = instr->binop.left.type.width;
    uintmax_t mask  = ir_make_mask(width);
    uintmax_t res;

    switch (instr->kind) {
    case IR_ADD:    res = left + right; break;
    case IR_SUB:    res = left - right; break;
    case IR_UMUL:   res = left * right; break;
    case IR_SMUL:   res = left * right; break;
    case IR_UDIV:   res = (left & mask) / (right & mask); break;
    case IR_SDIV:   res = (left & mask) / (right & mask); break;
    case IR_UREM:   res = (left & mask) % (right & mask); break;
    case IR_SREM:   res = (left & mask) % (right & mask); break;
    case IR_AND:    res = left & right; break;
    case IR_OR:     res = left | right; break;
    case IR_XOR:    res = left ^ right; break;
    case IR_SLL:    res = left << right; break;
    case IR_SRL:    res = left >> right; break;
    case IR_SRA: {
        uintmax_t sign_bit = 1lu << (width - 1);
        uintmax_t sign_ext = ir_make_mask(right) << (width - right);
        res = (left & sign_bit) ? (left >> right) | sign_ext : (left >> right);
        break;
    }
    default:
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ .int_ = res & mask };
    return true;
}

static bool ir_run_icmp(ir_instr_t const *instr) {
    unsigned width  = instr->icmp.left.type.width;
    uintmax_t left  = ir_eval_value(instr->icmp.left).int_;
    uintmax_t right = ir_eval_value(instr->icmp.right).int_;
    uintmax_t sign_bit   = 1llu << (width - 1);
    uintmax_t sign_width = CHAR_BIT * sizeof(uintmax_t) - width;
    uintmax_t sign_ext   = ((1llu << sign_width) - 1) << width;
    intmax_t left_s = (left & sign_bit) ?
        (intmax_t)(left | sign_ext) : (intmax_t)left;
    intmax_t right_s = (right & sign_bit) ?
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
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ .int_ = res };
    return true;
}

static bool ir_run_load(ir_recompiler_backend_t const *backend,
                        ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;
    uintmax_t address = ir_eval_value(instr->load.address).int_;
    uintmax_t res;
    uint8_t res_u8;
    uint16_t res_u16;
    uint32_t res_u32;
    uint64_t res_u64;

    switch (instr->load.type.width) {
    case 8:
        if (!memory->load_u8(address, &res_u8))   return false;
        res = res_u8;
        break;
    case 16:
        if (!memory->load_u16(address, &res_u16)) return false;
        res = res_u16;
        break;
    case 32:
        if (!memory->load_u32(address, &res_u32)) return false;
        res = res_u32;
        break;
    case 64:
        if (!memory->load_u64(address, &res_u64)) return false;
        res = res_u64;
        break;
    default:
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ .int_ = res };
    return true;
}

static bool ir_run_store(ir_recompiler_backend_t const *backend,
                         ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;
    uintmax_t address = ir_eval_value(instr->store.address).int_;
    uintmax_t value   = ir_eval_value(instr->store.value).int_;

    switch (instr->store.value.type.width) {
    case 8:     return memory->store_u8(address, value);
    case 16:    return memory->store_u16(address, value);
    case 32:    return memory->store_u32(address, value);
    case 64:    return memory->store_u64(address, value);
    default:
        return false;
    }
}

static bool ir_run_read(ir_recompiler_backend_t const *backend,
                        ir_instr_t const *instr) {
    ir_register_t register_ = instr->read.register_;
    if (register_ > backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "undefined register $%u", register_);
        return false;
    }
    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;
    ir_const_t *const_ = &ir_var_values[instr->res];

    switch (type.width) {
    case 8:  const_->int_ = *(uint8_t *)ptr; break;
    case 16: const_->int_ = *(uint16_t *)ptr; break;
    case 32: const_->int_ = *(uint32_t *)ptr; break;
    case 64: const_->int_ = *(uint64_t *)ptr; break;
    default:
        snprintf(ir_error_msg, ir_error_msg_len,
            "invalid register size %u for $%u", type.width, register_);
        return false;
    }
    return true;
}

static bool ir_run_write(ir_recompiler_backend_t const *backend,
                         ir_instr_t const *instr) {
    ir_register_t register_ = instr->write.register_;
    if (register_ > backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        snprintf(ir_error_msg, ir_error_msg_len,
            "undefined register $%u", register_);
        return false;
    }
    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;
    ir_const_t const_ = ir_eval_value(instr->write.value);
    switch (type.width) {
    case 8:  *(uint8_t *)ptr = const_.int_; break;
    case 16: *(uint16_t *)ptr = const_.int_; break;
    case 32: *(uint32_t *)ptr = const_.int_; break;
    case 64: *(uint64_t *)ptr = const_.int_; break;
    default:
        snprintf(ir_error_msg, ir_error_msg_len,
            "invalid register size %u for $%u", type.width, register_);
        return false;
    }
    return true;
}

static bool ir_run_cvt(ir_recompiler_backend_t const *backend,
                       ir_instr_t const *instr) {
    uintmax_t value = ir_eval_value(instr->cvt.value).int_;
    unsigned in_width = instr->cvt.value.type.width;
    unsigned out_width = instr->type.width;
    uintmax_t sign_bit   = 1llu << (in_width - 1);
    uintmax_t sign_width = CHAR_BIT * sizeof(uintmax_t) - in_width;
    uintmax_t sign_ext   = ((1llu << sign_width) - 1) << in_width;
    uintmax_t out_mask =
        out_width >= CHAR_BIT * sizeof(uintmax_t) ? -1llu : (1llu << out_width) - 1;
    uintmax_t res;

    switch (instr->kind) {
    case IR_ZEXT:    res = value; break;
    case IR_TRUNC:   res = value & out_mask; break;
    case IR_SEXT:    res = value & sign_bit ? value | sign_ext : value; break;
    default:
        return false;
    }

    ir_var_values[instr->res] = (ir_const_t){ .int_ = res };
    return true;
}


static bool ir_run_instr(ir_recompiler_backend_t const *backend,
                         ir_instr_t const *instr,
                         ir_instr_t const **next_instr) {
    *next_instr = instr->next;
    switch (instr->kind) {
    case IR_EXIT:       return true;
    case IR_BR:
        return ir_run_br(instr, next_instr);
    case IR_CALL:
        return ir_run_call(instr);
    case IR_NOT:
        return ir_run_unop(instr);
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
        return ir_run_binop(instr);
    case IR_ICMP:
        return ir_run_icmp(instr);
    case IR_LOAD:
        return ir_run_load(backend, instr);
    case IR_STORE:
        return ir_run_store(backend, instr);
    case IR_READ:
        return ir_run_read(backend, instr);
    case IR_WRITE:
        return ir_run_write(backend, instr);
    case IR_TRUNC:
    case IR_SEXT:
    case IR_ZEXT:
        return ir_run_cvt(backend, instr);
    }

    return false;
}

bool ir_run(ir_recompiler_backend_t const *backend,
            ir_graph_t const *graph,
            ir_instr_t const **err_instr,
            char *err_msg, size_t err_msg_len) {

    memset(ir_var_values, 0, sizeof(ir_var_values));
    ir_error_msg = err_msg;
    ir_error_msg_len = err_msg_len;
    nr_var_values = 0;

    for (ir_instr_t const *instr = graph->blocks[0].instrs; instr != NULL;) {
        ir_instr_t const *next = instr;
        nr_var_values += instr->type.width != 0;

        if (!ir_run_instr(backend, instr, &next)) {
            *err_instr = instr;
            return false;
        }

        instr = next;
    }
    return true;
}

void ir_run_vars(ir_const_t const **vars, unsigned *nr_vars) {
    *vars = ir_var_values;
    *nr_vars = nr_var_values;
}
