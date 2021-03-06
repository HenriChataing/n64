
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <recompiler/ir.h>

bool ir_is_void_instr(ir_instr_t const *instr) {
    _Static_assert(IR_ZEXT == 26,
        "IR instruction set changed, code may need to be updated");
    return (instr->kind == IR_CALL && instr->type.width == 0) ||
           instr->kind == IR_EXIT ||
           instr->kind == IR_ASSERT ||
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

static int print_exit(char *buf, size_t len, ir_instr_t const *instr) {
    return snprintf(buf, len, "exit");
}

static int print_assert(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "assert ");
    written += ir_print_value(buf+written, len-written, &instr->assert_.cond);
    return written;
}

static int print_br(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "br ");
    written += ir_print_value(buf+written, len-written, &instr->br.cond);
    written += snprintf(buf+written, len-written, ", .L%u, .L%u",
        instr->br.target[0]->label,
        instr->br.target[1]->label);
    return written;
}

static int print_call(char *buf, size_t len, ir_instr_t const *instr) {
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

static int print_alloc(char *buf, size_t len, ir_instr_t const *instr) {
    return snprintf(buf, len, "%%%u = alloc_i%u",
        instr->res, instr->alloc.type.width);
}

static int print_unop(char *buf, size_t len, ir_instr_t const *instr) {
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

static int print_binop(char *buf, size_t len, ir_instr_t const *instr) {
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

static int print_icmp(char *buf, size_t len, ir_instr_t const *instr) {
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

static int print_load(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "%%%u = load_", instr->res);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->load.address);
    return written;
}

static int print_store(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "store_");
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " ");
    written += ir_print_value(buf+written, len-written, &instr->store.address);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->store.value);
    return written;
}

static int print_read(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "%%%u = read_", instr->res);
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " $%u", instr->read.global);
    return written;
}

static int print_write(char *buf, size_t len, ir_instr_t const *instr) {
    int written;
    written  = snprintf(buf, len, "write_");
    written += ir_print_type(buf+written, len-written, &instr->type);
    written += snprintf(buf+written, len-written, " $%u", instr->write.global);
    written += snprintf(buf+written, len-written, ", ");
    written += ir_print_value(buf+written, len-written, &instr->write.value);
    return written;
}

static int print_cvt(char *buf, size_t len, ir_instr_t const *instr) {
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

_Static_assert(IR_ZEXT == 26,
    "IR instruction set changed, code may need to be updated");

static const int (*print_callbacks[])(char *, size_t, ir_instr_t const *) = {
    [IR_EXIT]   = print_exit,
    [IR_ASSERT] = print_assert,
    [IR_BR]     = print_br,
    [IR_CALL]   = print_call,
    [IR_ALLOC]  = print_alloc,
    [IR_NOT]    = print_unop,
    [IR_ADD]    = print_binop,
    [IR_SUB]    = print_binop,
    [IR_MUL]    = print_binop,
    [IR_UDIV]   = print_binop,
    [IR_SDIV]   = print_binop,
    [IR_UREM]   = print_binop,
    [IR_SREM]   = print_binop,
    [IR_SLL]    = print_binop,
    [IR_SRL]    = print_binop,
    [IR_SRA]    = print_binop,
    [IR_AND]    = print_binop,
    [IR_OR]     = print_binop,
    [IR_XOR]    = print_binop,
    [IR_ICMP]   = print_icmp,
    [IR_LOAD]   = print_load,
    [IR_STORE]  = print_store,
    [IR_READ]   = print_read,
    [IR_WRITE]  = print_write,
    [IR_TRUNC]  = print_cvt,
    [IR_SEXT]   = print_cvt,
    [IR_ZEXT]   = print_cvt,
};

int ir_print_instr(char *buf, size_t len, ir_instr_t const *instr) {
    return print_callbacks[instr->kind](buf, len, instr);
}

static void iter_none(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    (void)instr;
    (void)iter;
    (void)arg;
}

static void iter_assert(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->assert_.cond);
}

static void iter_br(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->br.cond);
}

static void iter_call(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    for (unsigned nr = 0; nr < instr->call.nr_params; nr++) {
        iter(arg, instr->call.params + nr);
    }
}

static void iter_unop(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->unop.value);
}

static void iter_binop(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->binop.left);
    iter(arg, &instr->binop.right);
}

static void iter_icmp(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->icmp.left);
    iter(arg, &instr->icmp.right);
}

static void iter_load(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->load.address);
}

static void iter_store(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->store.address);
    iter(arg, &instr->store.value);
}

static void iter_write(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->write.value);
}

static void iter_cvt(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter(arg, &instr->cvt.value);
}

_Static_assert(IR_ZEXT == 26,
    "IR instruction set changed, code may need to be updated");

static const void (*iter_callbacks[])(ir_instr_t const *instr,
                                      ir_value_iterator_t iter,
                                      void *arg) = {
    [IR_EXIT]   = iter_none,
    [IR_ASSERT] = iter_assert,
    [IR_BR]     = iter_br,
    [IR_CALL]   = iter_call,
    [IR_ALLOC]  = iter_none,
    [IR_NOT]    = iter_unop,
    [IR_ADD]    = iter_binop,
    [IR_SUB]    = iter_binop,
    [IR_MUL]    = iter_binop,
    [IR_UDIV]   = iter_binop,
    [IR_SDIV]   = iter_binop,
    [IR_UREM]   = iter_binop,
    [IR_SREM]   = iter_binop,
    [IR_SLL]    = iter_binop,
    [IR_SRL]    = iter_binop,
    [IR_SRA]    = iter_binop,
    [IR_AND]    = iter_binop,
    [IR_OR]     = iter_binop,
    [IR_XOR]    = iter_binop,
    [IR_ICMP]   = iter_icmp,
    [IR_LOAD]   = iter_load,
    [IR_STORE]  = iter_store,
    [IR_READ]   = iter_none,
    [IR_WRITE]  = iter_write,
    [IR_TRUNC]  = iter_cvt,
    [IR_SEXT]   = iter_cvt,
    [IR_ZEXT]   = iter_cvt,
};

void ir_iter_values(ir_instr_t const *instr, ir_value_iterator_t iter, void *arg) {
    iter_callbacks[instr->kind](instr, iter, arg);
}
