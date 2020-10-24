
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <recompiler/ir.h>

bool ir_is_void_instr(ir_instr_t const *instr) {
    return (instr->kind == IR_CALL && instr->type.width == 0) ||
           instr->kind == IR_EXIT ||
           instr->kind == IR_BR ||
           instr->kind == IR_STORE ||
           instr->kind == IR_WRITE;
}

int ir_print_type(char *buf, size_t len, ir_type_t const *type) {
    return snprintf(buf, len, "i%u", type->width);
}

int ir_print_value(char *buf, size_t len, ir_value_t const *value) {
    switch (value->kind) {
    case IR_VAR:   return snprintf(buf, len, "%%%u", value->var);
    case IR_CONST: return snprintf(buf, len, "%" PRIu64, value->const_.int_);
    }
    return 0;
}

static int ir_print_call(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    if (instr->type.width > 0) {
        written  = snprintf(buf, len, "%%%u = call_", instr->res);
        written += ir_print_type(buf+written, len-written, &instr->type);
    } else {
        written  = snprintf(buf, len, "call");
    }
    written += snprintf(buf+written, len-written, " [%p]", instr->call.func);
    for (unsigned i = 0; i < instr->call.nr_params; i++) {
        written += snprintf(buf+written, len-written, ", ");
        written += ir_print_value(buf+written, len-written, &instr->call.params[i]);
    }
    return written;
}

static int ir_print_alloc(char *buf, size_t len, ir_instr_t const *instr) {
    return snprintf(buf, len, "%%%u = alloc_i%u",
        instr->res, instr->alloc.type.width);
}

static int ir_print_unop(char *buf, size_t len, ir_instr_t const *instr) {
    char const *op = "?";
    switch (instr->kind) {
    case IR_NOT:  op = "not"; break;
    default: break;
    }

    int written;
    written  = snprintf(buf, len, "%%%u = %s_", instr->res, op);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->unop.value);
    return written;
}

static int ir_print_binop(char *buf, size_t len, ir_instr_t const *instr) {
    char const *op = "?";
    switch (instr->kind) {
    case IR_ADD:  op = "add"; break;
    case IR_SUB:  op = "sub"; break;
    case IR_MUL:  op = "mul"; break;
    case IR_UDIV: op = "udiv"; break;
    case IR_SDIV: op = "sdiv"; break;
    case IR_UREM: op = "urem"; break;
    case IR_SREM: op = "srem"; break;
    case IR_SLL:  op = "sll"; break;
    case IR_SRL:  op = "srl"; break;
    case IR_SRA:  op = "sra"; break;
    case IR_AND:  op = "and"; break;
    case IR_OR:   op = "or"; break;
    case IR_XOR:  op = "xor"; break;
    default: break;
    }

    int written;
    written  = snprintf(buf, len, "%%%u = %s_", instr->res, op);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->binop.left);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->binop.right);
    return written;
}

static int ir_print_icmp(char *buf, size_t len, ir_instr_t const *instr) {
    char const *op = "?";
    switch (instr->icmp.op) {
    case IR_EQ:  op = "eq"; break;
    case IR_NE:  op = "ne"; break;
    case IR_UGT: op = "ugt"; break;
    case IR_UGE: op = "uge"; break;
    case IR_ULT: op = "ult"; break;
    case IR_ULE: op = "ule"; break;
    case IR_SGT: op = "sgt"; break;
    case IR_SGE: op = "sge"; break;
    case IR_SLT: op = "slt"; break;
    case IR_SLE: op = "sle"; break;
    default: break;
    }

    int written;
    written  = snprintf(buf, len, "%%%u = icmp_%s_", instr->res, op);
    written += ir_print_type(buf+written, len-written, &instr->icmp.left.type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->icmp.left);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->icmp.right);
    return written;
}

static int ir_print_load(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "%%%u = load_", instr->res);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->load.address);
    return written;
}

static int ir_print_store(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "store_");
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->store.address);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->store.value);
    return written;
}

static int ir_print_read(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "%%%u = read_", instr->res);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " $%u", instr->read.register_);
    return written;
}

static int ir_print_write(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "write_");
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " $%u", instr->write.register_);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->write.value);
    return written;
}

static int ir_print_cvt(char *buf, size_t len, ir_instr_t const *instr) {
    char const *op = "?";
    switch (instr->kind) {
    case IR_TRUNC: op = "trunc"; break;
    case IR_SEXT:  op = "sext"; break;
    case IR_ZEXT:  op = "zext"; break;
    default: break;
    }

    int written;
    written  = snprintf(buf, len, "%%%u = %s_", instr->res, op);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, "_");
    written += ir_print_type(buf+written, len-written, &instr->cvt.value.type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->cvt.value);
    return written;
}

int ir_print_instr(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    switch (instr->kind) {
    case IR_EXIT:
        return snprintf(buf, len, "exit");
    case IR_BR:
        written  = snprintf(buf, len, "br ");
        written += ir_print_value(buf+written, len-written, &instr->br.cond);
        written += snprintf(buf+written, len-written, ", L%u, L%u",
                            instr->br.target[0]->label,
                            instr->br.target[1]->label);
        return written;
    case IR_CALL:
        return ir_print_call(buf, len, instr);
    case IR_ALLOC:
        return ir_print_alloc(buf, len, instr);
    case IR_NOT:
        return ir_print_unop(buf, len, instr);
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
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
        return ir_print_binop(buf, len, instr);
    case IR_ICMP:
        return ir_print_icmp(buf, len, instr);
    case IR_LOAD:
        return ir_print_load(buf, len, instr);
    case IR_STORE:
        return ir_print_store(buf, len, instr);
    case IR_READ:
        return ir_print_read(buf, len, instr);
    case IR_WRITE:
        return ir_print_write(buf, len, instr);
    case IR_TRUNC:
    case IR_SEXT:
    case IR_ZEXT:
        return ir_print_cvt(buf, len, instr);
    }

    return 0;
}
