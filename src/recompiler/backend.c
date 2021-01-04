
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <recompiler/backend.h>

/**
 * @brief Allocate a recompiler backend.
 * @param globals
 *      Array of global variable definitions. The array is copied
 *      to the newly allocated structure.
 * @param nr_globals    Number of allocated global variables.
 * @param nr_blocks     Number of pre-allocated instruction blocks.
 * @param nr_instrs     Number of pre-allocated instructions.
 * @param nr_params     Number of pre-allocated function parameters.
 * @return              The allocated backend, or NULL on failure.
 */
recompiler_backend_t *
create_recompiler_backend(ir_global_definition_t const *globals,
                          unsigned nr_globals,
                          unsigned nr_blocks,
                          unsigned nr_instrs,
                          unsigned nr_params) {
    recompiler_backend_t *backend = malloc(sizeof(recompiler_backend_t));
    if (backend == NULL) {
        return NULL;
    }
    backend->globals = NULL;
    backend->blocks = NULL;
    backend->instrs = NULL;
    backend->params = NULL;

    backend->nr_globals = nr_globals;
    backend->globals = malloc(nr_globals * sizeof(ir_global_definition_t));
    if (backend->globals == NULL) {
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

    memcpy(backend->globals, globals,
        nr_globals * sizeof(ir_global_definition_t));
    backend->cur_block = 0;
    backend->cur_instr = 0;
    backend->cur_param = 0;
    backend->errors = NULL;
    backend->last_error = &backend->errors;
    return backend;

failure:
    free(backend->globals);
    free(backend->blocks);
    free(backend->instrs);
    free(backend->params);
    free(backend);
    return NULL;
}

/**
 * @brief Raise an exception on the recompiler backend.
 * @details Jumps to the latest call to \ref catch_recompiler_error.
 *  Undefined if called before \ref catch_recompiler_error.
 * @param backend
 *      Pointer to the recompiler backend context.
 *      Must not be NULL.
 */
void fail_recompiler_backend(recompiler_backend_t *backend) {
    longjmp(backend->jmp_buf, -1);
}

/**
 * @brief Clear a recompiler backend.
 * @details All allocated resources are reset, the error logs are cleared.
 * @param backend
 *      Pointer to the recompiler backend context.
 *      Must not be NULL.
 */
void clear_recompiler_backend(recompiler_backend_t *backend) {
    recompiler_error_t *error, *next;
    for (error = backend->errors; error != NULL; error = next) {
        next = error->next;
        free(error);
    }

    backend->errors = NULL;
    backend->last_error = &backend->errors;
    backend->cur_block = 0;
    backend->cur_instr = 0;
    backend->cur_var = 0;
    backend->cur_param = 0;
}

/**
 * @brief Raise a recompiler error.
 * @details Generates an error log for the recompiler module \p module,
 * with the formatted message \p fmt. The message line is limited to 128
 * characters.
 *
 * @param backend
 *      Pointer to the recompiler backend context.
 *      Must not be NULL.
 * @param module    Name of the recompiler module raising the error.
 * @param fmt       Error message format.
 */
void raise_recompiler_error(recompiler_backend_t *backend,
                            char const *module, char const *fmt, ...) {

    recompiler_error_t *error = malloc(sizeof(*error));
    va_list vparams;
    if (error == NULL) {
        return;
    }

    error->module = module;
    error->next = NULL;
    va_start(vparams, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, vparams);
    va_end(vparams);

    *backend->last_error = error;
    backend->last_error = &error->next;
}

/**
 * @brief Return whether recompiler errors were raised.
 * @param backend   Pointer to the recompiler backend context.
 * @return
 *    true if \ref raise_recompiler_error was called since the last time
 *    the error list was cleared, false otherwise.
 */
bool has_recompiler_error(recompiler_backend_t *backend) {
    return backend->errors != NULL;
}

/**
 * @brief Fetch and pop the oldest recompiler error.
 * @details Always succeeds if \ref has_recompiler_error() returned true.
 * @param backend
 *      Pointer to the recompiler backend context.
 *      Must not be NULL.
 * @param module
 *      Pointer to a buffer where to store the recompiler module which
 *      raised the error, must not be NULL.
 * @param message
 *      Pointer to a buffer where to copy the nul-terminated
 *      error message, must not be NULL.
 * @return
 *      true if \ref raise_recompiler_error was called since the last time
 *      the error list was cleared, false otherwise.
 */
bool next_recompiler_error(recompiler_backend_t *backend,
                           char const **module,
                           char message[RECOMPILER_ERROR_MAX_LEN]) {
    if (backend->errors == NULL) {
        return false;
    }

    recompiler_error_t *error = backend->errors;
    backend->errors = error->next;
    backend->last_error = backend->errors ?
        backend->last_error : &backend->errors;
    *module = error->module;
    memcpy(message, error->message, sizeof(error->message));
    free(error);
    return true;
}

/**
 * @brief Allocate a pseudo variable.
 * @param cont      Pointer to the contination context, must not be NULL.
 * @return          Newly allocated pseudo variable.
 */
ir_var_t ir_alloc_var(ir_instr_cont_t *cont) {
    return cont->backend->cur_var++;
}

/**
 * @brief Allocate an instruction.
 * @details The function will call \ref fail_recompiler_backend()
 *  if the allocation fails. The returned pointer is owned by the context,
 *  it must not be freed, and it must not be used after
 *  \ref clear_recompiler_backend() was called.
 * @param backend   Pointer to the recompiler backend context, must not be NULL.
 * @return          Newly allocated instruction.
 */
ir_instr_t *ir_alloc_instr(recompiler_backend_t *backend) {
    if (backend->cur_instr >= backend->nr_instrs) {
        raise_recompiler_error(backend,
            "backend", "out of ir instruction memory");
        fail_recompiler_backend(backend);
    }
    return &backend->instrs[backend->cur_instr++];
}

/**
 * @brief Allocate an instruction block.
 * @details The function will call \ref fail_recompiler_backend()
 *  if the allocation fails. The returned pointer is owned by the context,
 *  it must not be freed, and it must not be used after
 *  \ref clear_recompiler_backend() was called.
 * @param backend   Pointer to the recompiler backend context, must not be NULL.
 * @return          Newly allocated instruction block.
 */
ir_block_t *ir_alloc_block(recompiler_backend_t *backend) {
    if (backend->cur_block >= backend->nr_blocks) {
        raise_recompiler_error(backend,
            "backend", "out of ir block memory");
        fail_recompiler_backend(backend);
    }
    ir_block_t *block = &backend->blocks[backend->cur_block];
    block->label = backend->cur_block;
    backend->cur_block++;
    return block;
}

/**
 * @brief Close the graph generated by successive `ir_append_*` calls.
 * @details The returned pointer is owned by the context,
 *  it must not be freed, and it must not be used after
 *  \ref clear_recompiler_backend() was called.
 * @param backend   Pointer to the recompiler backend context, must not be NULL.
 * @return          Pointer to the generated instruction graph.
 */
ir_graph_t *ir_make_graph(recompiler_backend_t *backend) {
    backend->graph.blocks = backend->blocks;
    backend->graph.nr_blocks = backend->cur_block;
    backend->graph.nr_vars = backend->cur_var;
    return &backend->graph;
}

static inline ir_instr_t * ir_append_instr(ir_instr_cont_t *cont,
                                           ir_instr_t instr) {
    ir_instr_t *next = ir_alloc_instr(cont->backend);
    *next = instr;
    *cont->next = next;
    cont->next = &next->next;
    return next;
}

/**
 * @brief Append an `exit` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 */
void ir_append_exit(ir_instr_cont_t *cont) {
    ir_append_instr(cont, ir_make_exit());
}

/**
 * @brief Append an `assert` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param cond      Assert condition.
 */
void ir_append_assert(ir_instr_cont_t *cont,
                      ir_value_t cond) {
    ir_append_instr(cont, ir_make_assert(cond));
}

/**
 * @brief Append a `br` instruction.
 * @details Allocates two blocks for the true and false branch conditions.
 *  The continuations \p target_false and \p target_true are updated
 *  to write to the allocated blocks.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param cond      Branch condition.
 * @param target_false
 *      Pointer to the false branch continuation. Must not be NULL.
 * @param target_true
 *      Pointer to the true branch continuation. Must not be NULL.
 */
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
    target_false->next = &block_false->entry;

    target_true->backend = cont->backend;
    target_true->block = block_true;
    target_true->next = &block_true->entry;
}

/**
 * @brief Append a `call` instruction.
 * @details The variadic parameters are all input values of type
 *  \ref ir_value_t, numbered \p nr_values. The values
 *  are copied to a parameter array allocated from the backend context.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Call function return type.
 * @param func      Pointer to the host function to be called.
 * @param nr_params Number of parameters input by the function \p func.
 * @param ...       List of input parameter values.
 * @return          Value representing the function result.
 */
ir_value_t ir_append_call(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_func_t func,
                          unsigned nr_params,
                          ...) {
    /* Gather the parameters into an allocated parameter array. */
    va_list vparams;
    ir_value_t *params = cont->backend->params + cont->backend->cur_param;
    cont->backend->cur_param += nr_params;

    if (cont->backend->cur_param > cont->backend->nr_params) {
        raise_recompiler_error(cont->backend,
            "backend", "out of ir parameter memory");
        fail_recompiler_backend(cont->backend);
    }

    va_start(vparams, nr_params);
    for (unsigned i = 0; i < nr_params; i++) {
        params[i] = va_arg(vparams, ir_value_t);
    }
    va_end(vparams);

    /* Generate the instruction. */
    ir_var_t res = type.width > 0 ? ir_alloc_var(cont) : 0;
    ir_append_instr(cont, ir_make_call(res, type, func, params, nr_params));
    return ir_make_var(type, res);
}

/**
 * @brief Append an `alloc` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Type of the allocated memory cell.
 * @return          Value representing the pointer to the allocated memory.
 */
ir_value_t ir_append_alloc(ir_instr_cont_t *cont,
                           ir_type_t type) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_alloc(res, type));
    return ir_make_var(ir_make_iptr(), res);
}

/**
 * @brief Append a unary instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param op        Unary instruction kind.
 * @param value     Instruction operand.
 * @return          Value representing the operation result.
 */
ir_value_t ir_append_unop(ir_instr_cont_t *cont,
                          ir_instr_kind_t op,
                          ir_value_t value) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_unop(res, op, value));
    return ir_make_var(value.type, res);
}

/**
 * @brief Append a binary instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param op        Binary instruction kind.
 * @param left      First instruction operand.
 * @param right     Second instruction operand.
 * @return          Value representing the operation result.
 */
ir_value_t ir_append_binop(ir_instr_cont_t *cont,
                           ir_instr_kind_t op,
                           ir_value_t left,
                           ir_value_t right) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_binop(res, op, left, right));
    return ir_make_var(left.type, res);
}

/**
 * @brief Append an `icmp` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param op        Comparison operator.
 * @param left      Left side of the comparison.
 * @param right     Right side of the comparison.
 * @return          Value representing the comparison result.
 */
ir_value_t ir_append_icmp(ir_instr_cont_t *cont,
                          ir_icmp_kind_t op,
                          ir_value_t left,
                          ir_value_t right) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_icmp(res, op, left, right));
    return ir_make_var(ir_make_i1(), res);
}

/**
 * @brief Append a `load` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Load access type.
 * @param address   Host memory address.
 * @return          Value representing the load result.
 */
ir_value_t ir_append_load(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_value_t address) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_load(res, type, address));
    return ir_make_var(type, res);
}

/**
 * @brief Append a `store` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Store access type.
 * @param address   Host memory address.
 * @param value     Stored value.
 */
void ir_append_store(ir_instr_cont_t *cont,
                     ir_type_t type,
                     ir_value_t address,
                     ir_value_t value) {
    ir_append_instr(cont, ir_make_store(type, address, value));
}

/**
 * @brief Append a `read` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Read access type.
 * @param global    Global variable identifier.
 * @return          Value representing the read result.
 */
ir_value_t ir_append_read(ir_instr_cont_t *cont,
                          ir_type_t type,
                          ir_global_t global) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_read(res, type, global));
    return ir_make_var(type, res);
}

/**
 * @brief Append a `write` instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Write access type.
 * @param global    Global variable identifier.
 * @param value     Written value.
 */
void ir_append_write(ir_instr_cont_t *cont,
                     ir_type_t type,
                     ir_global_t global,
                     ir_value_t value) {
    ir_append_instr(cont, ir_make_write(type, global, value));
}

/**
 * @brief Append a convert instruction.
 * @param cont      Pointer to the continuation context, must not be NULL.
 * @param type      Destination type of the conversion.
 * @param op        Conversion instruction kind.
 * @param value     Converted value.
 * @return          Value representing the result of the conversion.
 */
ir_value_t ir_append_cvt(ir_instr_cont_t *cont,
                         ir_type_t type,
                         ir_instr_kind_t op,
                         ir_value_t value) {
    ir_var_t res = ir_alloc_var(cont);
    ir_append_instr(cont, ir_make_cvt(res, type, op, value));
    return ir_make_var(type, res);
}
