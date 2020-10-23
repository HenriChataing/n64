
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <recompiler/backend.h>

ir_recompiler_backend_t *
ir_create_recompiler_backend(ir_memory_backend_t *memory,
                             unsigned nr_registers,
                             unsigned nr_blocks,
                             unsigned nr_instrs,
                             unsigned nr_params) {
    ir_recompiler_backend_t *backend = malloc(sizeof(ir_recompiler_backend_t));
    if (backend == NULL) {
        return NULL;
    }
    backend->registers = NULL;
    backend->blocks = NULL;
    backend->instrs = NULL;
    backend->params = NULL;

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
    backend->nr_params = nr_params;
    backend->params = malloc(nr_params * sizeof(ir_value_t));
    if (backend->params == NULL) {
        goto failure;
    }

    backend->memory = *memory;
    backend->cur_block = 0;
    backend->cur_instr = 0;
    backend->cur_param = 0;
    return backend;

failure:
    free(backend->registers);
    free(backend->blocks);
    free(backend->instrs);
    free(backend->params);
    free(backend);
    return NULL;
}

void ir_bind_register(ir_recompiler_backend_t *backend,
                      ir_register_t register_,
                      ir_type_t type,
                      char const *name,
                      void *ptr) {
    if (backend == NULL || register_ >= backend->nr_registers)
        return;
    backend->registers[register_] =
        (ir_register_backend_t){ type, name, ptr };
}

void ir_bind_register_u32(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          char const *name,
                          uint32_t *ptr) {
    ir_bind_register(backend, register_, ir_make_iN(32), name, ptr);
}
void ir_bind_register_u64(ir_recompiler_backend_t *backend,
                          ir_register_t register_,
                          char const *name,
                          uint64_t *ptr) {
    ir_bind_register(backend, register_, ir_make_iN(64), name, ptr);
}

void ir_reset_backend(ir_recompiler_backend_t *backend) {
    backend->cur_block = 0;
    backend->cur_instr = 0;
    backend->cur_param = 0;
}

ir_var_t ir_alloc_var(ir_instr_cont_t *cont) {
    return cont->block->nr_vars++;
}

ir_instr_t *ir_alloc_instr(ir_recompiler_backend_t *backend) {
    // if (backend->cur_instr >= backend->nr_instrs)
    return &backend->instrs[backend->cur_instr++];
}

ir_block_t *ir_alloc_block(ir_recompiler_backend_t *backend) {
    // if (backend->cur_block >= backend->nr_blocks)
    ir_block_t *block = &backend->blocks[backend->cur_block];
    block->label = backend->cur_block;
    block->nr_vars = 0;
    backend->cur_block++;
    return block;
}

ir_graph_t *ir_make_graph(ir_recompiler_backend_t *backend) {
    backend->graph.blocks = backend->blocks;
    backend->graph.nr_blocks = backend->cur_block;
    return &backend->graph;
}

static inline ir_instr_t * ir_append_instr(ir_instr_cont_t *cont,
                                           ir_instr_t instr) {
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = instr;
    *cont->next = next;
    cont->next = &next->next;
    cont->block->nr_instrs++;
    return next;
}

void ir_append_exit(ir_instr_cont_t *cont) {
    ir_append_instr(cont, ir_make_exit());
}

void ir_append_br(ir_instr_cont_t *cont,
                  ir_value_t cond,
                  ir_instr_cont_t *target_false,
                  ir_instr_cont_t *target_true) {
    ir_block_t *block_false = ir_alloc_block(cont->backend);
    ir_block_t *block_true = ir_alloc_block(cont->backend);

    ir_append_instr(cont,
        ir_make_br(cond, block_false, block_true));

    target_false->backend = cont->backend;
    target_false->block = block_false;
    target_false->next = &block_false->instrs;

    target_true->backend = cont->backend;
    target_true->block = block_true;
    target_true->next = &block_true->instrs;
}

ir_value_t ir_append_call(ir_instr_cont_t *cont,
                          ir_type_t type,
                          void (*func)(),
                          unsigned nr_params,
                          ...) {
    /* Gather the parameters into an allocated parameter array. */
    va_list vparams;
    ir_value_t *params = cont->backend->params + cont->backend->cur_param;
    cont->backend->cur_param += nr_params;
    // if (cont->backend->cur_param > cont->backend->nr_params)

    va_start(vparams, nr_params);
    for (unsigned i = 0; i < nr_params; i++) {
        params[i] = va_arg(vparams, ir_value_t);
    }
    va_end(vparams);

    /* Generate the instruction. */
    ir_var_t res = type.width > 0 ? ir_alloc_var(cont) : 0;
    ir_append_instr(cont, ir_make_call(res, type, func, params, nr_params));
    return ir_make_var(res, type);
}

ir_value_t ir_append_unop(ir_instr_cont_t *cont,
                          ir_instr_kind_t op,
                          ir_value_t value) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_unop(res, op, value));
    return ir_make_var(res, value.type);
}

ir_value_t ir_append_binop(ir_instr_cont_t *cont,
                           ir_instr_kind_t op,
                           ir_value_t left,
                           ir_value_t right) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_binop(res, op, left, right));
    return ir_make_var(res, left.type);
}

ir_value_t ir_append_icmp(ir_instr_cont_t *cont,
                          ir_icmp_kind_t op,
                          ir_value_t left,
                          ir_value_t right) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_icmp(res, op, left, right));
    return ir_make_var(res, ir_make_iN(1));
}

ir_value_t ir_append_load(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t address) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_load(res, type, address));
    return ir_make_var(res, type);
}

void ir_append_store(ir_instr_cont_t *cont,
                     ir_type_t type,
                     ir_value_t address,
                     ir_value_t value) {
    ir_append_instr(cont, ir_make_store(type, address, value));
}

ir_value_t ir_append_read(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_register_t register_) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_read(res, type, register_));
    return ir_make_var(res, type);
}

void ir_append_write(ir_instr_cont_t *cont,
                     ir_type_t type,
                     ir_register_t register_,
                     ir_value_t value) {
    ir_append_instr(cont, ir_make_write(type, register_, value));
}

ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_cvt(res, type, op, value));
    return ir_make_var(res, type);
}
