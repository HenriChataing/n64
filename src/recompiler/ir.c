
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <recompiler/ir.h>

ir_recompiler_backend_t *
ir_create_recompiler_backend(ir_memory_backend_t *memory,
                             unsigned nr_registers,
                             unsigned nr_blocks,
                             unsigned nr_instrs) {
    ir_recompiler_backend_t *backend = malloc(sizeof(ir_recompiler_backend_t));
    if (backend == NULL) {
        return NULL;
    }
    backend->registers = NULL;
    backend->blocks = NULL;
    backend->instrs = NULL;

    backend->nr_registers = nr_registers;
    backend->registers = malloc(nr_registers * sizeof(ir_recompiler_backend_t));
    if (backend->registers == NULL) {
        goto failure;
    }
    backend->nr_blocks = nr_blocks;
    backend->blocks = malloc(nr_blocks * sizeof(ir_block_t));
    if (backend->blocks == NULL) {
        goto failure;
    }
    backend->nr_instrs = nr_instrs;
    backend->instrs = malloc(nr_instrs * sizeof(ir_instr_t));
    if (backend->instrs == NULL) {
        goto failure;
    }

    backend->memory = *memory;
    backend->cur_block = 0;
    backend->cur_instr = 0;
    return backend;

failure:
    free(backend->registers);
    free(backend->blocks);
    free(backend->instrs);
    free(backend);
    return NULL;
}

void ir_bind_register(ir_recompiler_backend_t *backend,
                      ir_register_t register_,
                      ir_type_t type, void *ptr) {
    if (backend == NULL || register_ >= backend->nr_registers)
        return;
    backend->registers[register_] =
        (ir_register_backend_t){ type, ptr };
}

void ir_bind_register_u32(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          uint32_t *ptr) {
    ir_bind_register(backend, register_, ir_make_iN(32), ptr);
}
void ir_bind_register_u64(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          uint64_t *ptr) {
    ir_bind_register(backend, register_, ir_make_iN(64), ptr);
}

void ir_reset_backend(ir_recompiler_backend_t *backend) {
    backend->cur_block = 0;
    backend->cur_instr = 0;
    backend->cur_var = 0;
}

ir_var_t ir_alloc_var(ir_recompiler_backend_t *backend) {
    return backend->cur_var++;
}

ir_instr_t *ir_alloc_instr(ir_recompiler_backend_t *backend) {
    // if (backend->cur_instr >= backend->nr_instrs)
    return &backend->instrs[backend->cur_instr++];
}

ir_block_t *ir_alloc_block(ir_recompiler_backend_t *backend) {
    // if (backend->cur_block >= backend->nr_blocks)
    ir_block_t *block = &backend->blocks[backend->cur_block];
    block->label = backend->cur_block;
    backend->cur_block++;
    return block;
}

ir_graph_t *ir_make_graph(ir_recompiler_backend_t *backend) {
    backend->graph.blocks = backend->blocks;
    backend->graph.nr_blocks = backend->cur_block;
    return &backend->graph;
}

void       ir_append_exit(ir_instr_cont_t *cont)
{
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_exit();
    *cont->next = next;
    cont->block->nr_instrs++;
}

void       ir_append_br(ir_instr_cont_t *cont,
                        ir_value_t cond,
                        ir_instr_cont_t *target)
{
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    ir_block_t *block = ir_alloc_block(cont->backend);

    *next = ir_make_br(cond, block);
    *cont->next = next;
    cont->next = &next->next;
    cont->block->nr_instrs++;

    target->backend = cont->backend;
    target->block = block;
    target->next = &block->instrs;
}

ir_value_t ir_append_call(ir_instr_cont_t *cont,
                          ir_type_t type,
                          void (*func)(),
                          unsigned nr_params,
                          ...) {
    /* Gather the parameters into an allocated parameter array. */
    va_list vparams;
    ir_value_t *params = malloc(nr_params * sizeof(ir_value_t)); // TODO
    va_start(vparams, nr_params);
    for (unsigned i = 0; i < nr_params; i++) {
        params[i] = va_arg(vparams, ir_value_t);
    }
    va_end(vparams);

    /* Generate the instruction. */
    ir_var_t res = type.width > 0 ? ir_alloc_var(cont->backend) : 0;
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_call(res, type, func, params, nr_params);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, type);
}

ir_value_t ir_append_unop(ir_instr_cont_t *cont,
                          ir_instr_kind_t op,
                          ir_value_t value)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_unop(res, op, value);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, value.type);
}

ir_value_t ir_append_binop(ir_instr_cont_t *cont,
                           ir_instr_kind_t op,
                           ir_value_t left,
                           ir_value_t right)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_binop(res, op, left, right);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, left.type);
}

ir_value_t ir_append_icmp(ir_instr_cont_t *cont,
                          ir_icmp_kind_t op,
                          ir_value_t left,
                          ir_value_t right)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_icmp(res, op, left, right);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, ir_make_iN(1));
}

ir_value_t ir_append_load(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t address)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_load(res, type, address);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, type);
}

void       ir_append_store(ir_instr_cont_t *cont,
                           ir_value_t address,
                           ir_value_t value)
{
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_store(address, value);
    *cont->next = next;
    cont->next = &next->next;
}

ir_value_t ir_append_read(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_register_t register_)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_read(res, type, register_);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, type);
}

void       ir_append_write(ir_instr_cont_t *cont,
                           ir_register_t register_,
                           ir_value_t value)
{
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_write(register_, value);
    *cont->next = next;
    cont->next = &next->next;
}

ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value)
{
    ir_var_t res = ir_alloc_var(cont->backend);
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = ir_make_cvt(res, type, op, value);
    *cont->next = next;
    cont->next = &next->next;
    return ir_make_var(res, type);
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
    case IR_UMUL: op = "umul"; break;
    case IR_SMUL: op = "smul"; break;
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
    written += ir_print_type(buf+written, len-written, &instr->store.value.type);
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
    written += ir_print_type(buf+written, len-written, &instr->write.value.type);
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
        written += snprintf(buf+written, len-written, ", L%u",
                            instr->br.target->label);
        return written;
    case IR_CALL:
        return ir_print_call(buf, len, instr);
    case IR_NOT:
        return ir_print_unop(buf, len, instr);
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
