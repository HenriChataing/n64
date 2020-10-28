
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/config.h>
#include <recompiler/ir.h>
#include <recompiler/target/x86_64/emitter.h>
#include <recompiler/target/x86_64.h>


typedef struct ir_block_context {
    unsigned char *start;
} ir_block_context_t;

typedef struct ir_var_context {
    int32_t stack_offset;
    bool allocated;
} ir_var_context_t;

typedef struct ir_br_context {
    ir_block_t const *block;
    unsigned char *rel32;
} ir_br_context_t;

typedef struct ir_exit_context {
    unsigned char *rel32;
} ir_exit_context_t;

static ir_block_context_t ir_block_context[RECOMPILER_BLOCK_MAX];
static ir_var_context_t   ir_var_context[RECOMPILER_VAR_MAX];
static ir_br_context_t    ir_br_queue[RECOMPILER_BLOCK_MAX];
static unsigned           ir_br_queue_len;
static ir_exit_context_t  ir_exit_queue[RECOMPILER_INSTR_MAX]; // TODO BLOCK
static unsigned           ir_exit_queue_len;

static inline unsigned round_up_to_power2(unsigned v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v < 8 ? 8 : v;
}

/** Load a value into the selected register. */
static void load_value(code_buffer_t *emitter, ir_value_t value, unsigned r) {
    unsigned width = round_up_to_power2(value.type.width);
    if (value.kind == IR_CONST) {
        switch (width) {
        case 8:  emit_mov_r8_imm8(emitter, r, value.const_.int_);   break;
        case 16: emit_mov_r16_imm16(emitter, r, value.const_.int_); break;
        case 32: emit_mov_r32_imm32(emitter, r, value.const_.int_); break;
        case 64: emit_mov_r64_imm64(emitter, r, value.const_.int_); break;
        default: fail_code_buffer(emitter);
        }
    } else if (ir_var_context[value.var].allocated) {
        emit_mov_r64_r64(emitter, r, RBP);
        emit_add_r64_imm32(emitter, r, ir_var_context[value.var].stack_offset);
    } else {
        x86_64_mem_t mN = mem_indirect_disp(RBP,
            ir_var_context[value.var].stack_offset);
        switch (width) {
        case 8:  emit_mov_r8_m8(emitter, r, mN);   break;
        case 16: emit_mov_r16_m16(emitter, r, mN); break;
        case 32: emit_mov_r32_m32(emitter, r, mN); break;
        case 64: emit_mov_r64_m64(emitter, r, mN); break;
        default: fail_code_buffer(emitter);
        }
    }
}

/** Load a value to the selected pseudo register. */
static void store_value(code_buffer_t *emitter, ir_type_t type,
                        ir_var_t var, unsigned r) {
    unsigned width = round_up_to_power2(type.width);
    x86_64_mem_t mN = mem_indirect_disp(RBP,
        ir_var_context[var].stack_offset);
    switch (width) {
    case 8:  emit_mov_m8_r8(emitter, mN, r);   break;
    case 16: emit_mov_m16_r16(emitter, mN, r); break;
    case 32: emit_mov_m32_r32(emitter, mN, r); break;
    case 64: emit_mov_m64_r64(emitter, mN, r); break;
    default: fail_code_buffer(emitter);
    }
}

static void assemble_exit(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    ir_exit_queue[ir_exit_queue_len].rel32 = emit_jmp_rel32(emitter);
    ir_exit_queue_len++;
}

static void assemble_br(recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {
    unsigned char *rel32;
    load_value(emitter, instr->br.cond, AL);
    emit_test_al_imm8(emitter, 1);

    if (instr->br.target[0] && instr->br.target[1]) {
        // True branching instruction, the branch targets are two
        // distinct blocks.
        rel32 = emit_jne_rel32(emitter);
        ir_br_queue[ir_br_queue_len].rel32 = rel32;
        ir_br_queue[ir_br_queue_len].block = instr->br.target[1];
        ir_br_queue_len++;
        // The false branch is assembled directly after the current block
        // (the branch instruction is final). No additional branch
        // instruction required.
        ir_br_queue[ir_br_queue_len].rel32 = NULL;
        ir_br_queue[ir_br_queue_len].block = instr->br.target[0];
        ir_br_queue_len++;
    }
    else if (instr->br.target[0]) {
        // True condition is an exit condition.
        rel32 = emit_jne_rel32(emitter);
        ir_exit_queue[ir_exit_queue_len].rel32 = rel32;
        ir_exit_queue_len++;
        // The false branch is assembled directly after the current block
        // (the branch instruction is final). No additional branch
        // instruction required.
        ir_br_queue[ir_br_queue_len].rel32 = NULL;
        ir_br_queue[ir_br_queue_len].block = instr->br.target[0];
        ir_br_queue_len++;
    }
    else if (instr->br.target[1]) {
        // False condition is an exit condition.
        rel32 = emit_je_rel32(emitter);
        ir_exit_queue[ir_exit_queue_len].rel32 = rel32;
        ir_exit_queue_len++;
        // The false branch is assembled directly after the current block
        // (the branch instruction is final). No additional branch
        // instruction required.
        ir_br_queue[ir_br_queue_len].rel32 = NULL;
        ir_br_queue[ir_br_queue_len].block = instr->br.target[1];
        ir_br_queue_len++;
    }
    else {
        // Both conditions lead to the exit, an inconditional jump is generated.
        // Should not happen in pratice.
        ir_exit_queue[ir_exit_queue_len].rel32 = emit_jmp_rel32(emitter);
        ir_exit_queue_len++;
    }
}

static void assemble_call(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    // The call IR instruction only supports passing scalar parameters,
    // the System V ABI is quite simple in this case: all types are rounded
    // up to 64bits, the first 6 parameters are passed by register, the others
    // on the stack.
    // For this implementation, we are not concerned with caller saved
    // registers, as register allocation is not present.

    unsigned frame_size = 0;
    static const unsigned register_parameters[6] = {
        RDI, RSI, RDX, RCX, R8, R9,
    };
    static const bool register_parameters_u8[6] = {
        false, false, true, true, false, false,
    };
    static const bool register_parameters_u16[6] = {
        true, true, true, true, false, false,
    };

    for (unsigned nr = 0; nr < instr->call.nr_params && nr < 6; nr++) {
        ir_value_t param = instr->call.params[nr];
        if ((param.type.width <= 8  && !register_parameters_u8[nr]) ||
            (param.type.width <= 16 && !register_parameters_u16[nr])) {
            load_value(emitter, param, RAX);
            emit_mov_r64_r64(emitter, register_parameters[nr], RAX);
        } else {
            load_value(emitter, param, register_parameters[nr]);
        }
    }
    emit_push_r64(emitter, R12);
    emit_push_r64(emitter, R13);
    if (instr->call.nr_params > 6) {
        frame_size = 8 * (instr->call.nr_params - 6);
        frame_size = (frame_size + 15) / 16;
        frame_size = frame_size * 16;
        emit_sub_r64_imm32(emitter, RSP, frame_size);
        emit_mov_r64_r64(emitter, R12, RSP);
    }
    for (unsigned nr = 6; nr < instr->call.nr_params; nr++) {
        load_value(emitter, instr->call.params[nr], RAX);
        emit_mov_m64_r64(emitter, mem_indirect_disp(R12, 8 * (nr - 6)), RAX);
    }

    emit_call(emitter, instr->call.func, R13);

    if (instr->call.nr_params > 6) {
        emit_add_r64_imm32(emitter, RSP, frame_size);
    }
    emit_pop_r64(emitter, R13);
    emit_pop_r64(emitter, R12);
    if (instr->type.width > 0) {
        store_value(emitter, instr->type, instr->res, RAX);
    }
}

static void assemble_alloc(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    // Stack allocated variables are phantom variables,
    // rather than materializing the variable containing the address to
    // the allocated memory, the variable is inlined in all its uses.
    (void)backend;
    (void)emitter;
    (void)instr;
}

static void assemble_not(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->unop.value, RAX);
    emit_not_r64(emitter, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_add(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_add_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}


static void assemble_sub(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_sub_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_mul(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_imul_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_udiv(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_xor_r64_r64(emitter, RDX, RDX);

    switch (instr->type.width) {
    case 32: emit_div_edx_eax_r32(emitter, ECX);
    case 64: emit_div_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sdiv(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);

    switch (instr->type.width) {
    case 32:
        emit_cdq(emitter);
        emit_idiv_edx_eax_r32(emitter, ECX);
    case 64:
        emit_cqo(emitter);
        emit_idiv_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_urem(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_xor_r64_r64(emitter, RDX, RDX);

    switch (instr->type.width) {
    case 32: emit_div_edx_eax_r32(emitter, ECX);
    case 64: emit_div_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    store_value(emitter, instr->type, instr->res, RDX);
}

static void assemble_srem(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);

    switch (instr->type.width) {
    case 32:
        emit_cdq(emitter);
        emit_idiv_edx_eax_r32(emitter, ECX);
    case 64:
        emit_cqo(emitter);
        emit_idiv_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    store_value(emitter, instr->type, instr->res, RDX);
}

static void assemble_and(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_and_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_or(recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_or_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_xor(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_xor_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sll(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_shl_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_srl(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_shr_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sra(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_sra_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_icmp(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->icmp.left, RAX);
    load_value(emitter, instr->icmp.right, RCX);
    emit_cmp_rN_rN(emitter, instr->icmp.left.type.width, RAX, RCX);

    x86_64_mem_t m8 = mem_indirect_disp(RBP,
        ir_var_context[instr->res].stack_offset);

    switch (instr->icmp.op) {
    case IR_EQ:  emit_sete_m8(emitter, m8); break;
    case IR_NE:  emit_setne_m8(emitter, m8); break;
    case IR_UGT: emit_seta_m8(emitter, m8); break;
    case IR_UGE: emit_setae_m8(emitter, m8); break;
    case IR_ULT: emit_setb_m8(emitter, m8); break;
    case IR_ULE: emit_setbe_m8(emitter, m8); break;
    case IR_SGT: emit_setg_m8(emitter, m8); break;
    case IR_SGE: emit_setge_m8(emitter, m8); break;
    case IR_SLT: emit_setl_m8(emitter, m8); break;
    case IR_SLE: emit_setle_m8(emitter, m8); break;
    default:     fail_code_buffer(emitter);
    }
}

static void assemble_load(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    x86_64_mem_t dst = mem_indirect_disp(RBP,
        ir_var_context[instr->res].stack_offset);

    load_value(emitter, instr->load.address, RAX);
    emit_mov_rN_mN(emitter, instr->type.width, RCX, mem_indirect(RAX));

    switch (instr->type.width) {
    case 8:  emit_mov_m8_r8(emitter, dst, CL); break;
    case 16: emit_mov_m16_r16(emitter, dst, CX); break;
    case 32: emit_mov_m32_r32(emitter, dst, ECX); break;
    case 64: emit_mov_m64_r64(emitter, dst, RCX); break;
    default: fail_code_buffer(emitter); break;
    }
}

static void assemble_store(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    x86_64_mem_t dst = mem_indirect(RAX);

    load_value(emitter, instr->store.address, RAX);
    load_value(emitter, instr->store.value, RCX);

    switch (instr->type.width) {
    case 8:  emit_mov_m8_r8(emitter, dst, CL); break;
    case 16: emit_mov_m16_r16(emitter, dst, CX); break;
    case 32: emit_mov_m32_r32(emitter, dst, ECX); break;
    case 64: emit_mov_m64_r64(emitter, dst, RCX); break;
    default: fail_code_buffer(emitter); break;
    }
}

static void assemble_read(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    ir_global_t global = instr->read.global;
    if (global > backend->nr_globals ||
        backend->globals[global].ptr == NULL) {
        fail_code_buffer(emitter);
        return;
    }

    void *ptr = backend->globals[global].ptr;
    ir_type_t type = backend->globals[global].type;

    emit_mov_r64_imm64(emitter, RAX, (intptr_t)ptr);
    emit_mov_rN_mN(emitter, type.width, RAX, mem_indirect(RAX));
    store_value(emitter, type, instr->res, RAX);
}

static void assemble_write(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {

    ir_global_t global = instr->write.global;
    if (global > backend->nr_globals ||
        backend->globals[global].ptr == NULL) {
        fail_code_buffer(emitter);
        return;
    }

    void *ptr = backend->globals[global].ptr;
    ir_type_t type = backend->globals[global].type;

    emit_mov_r64_imm64(emitter, RAX, (intptr_t)ptr);
    load_value(emitter, instr->write.value, RCX);
    emit_mov_mN_rN(emitter, type.width, mem_indirect(RAX), RCX);
}

static void assemble_trunc(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    load_value(emitter, instr->cvt.value, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sext(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    unsigned from_width = instr->cvt.value.type.width;
    unsigned to_width   = instr->type.width;

    load_value(emitter, instr->cvt.value, RAX);

    if (from_width <=  8 && to_width >  8) {
        emit_cbw(emitter);
    }
    if (from_width <= 16 && to_width > 16) {
        emit_cwde(emitter);
    }
    if (from_width <= 32 && to_width > 32) {
        emit_cdqe(emitter);
    }

    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_zext(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    emit_xor_r64_r64(emitter, RAX, RAX);
    load_value(emitter, instr->cvt.value, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static const void (*assemble_callbacks[])(recompiler_backend_t const *backend,
                                          code_buffer_t *emitter,
                                          ir_instr_t const *instr) = {
    [IR_EXIT]  = assemble_exit,
    [IR_BR]    = assemble_br,
    [IR_CALL]  = assemble_call,
    [IR_ALLOC] = assemble_alloc,
    [IR_NOT]   = assemble_not,
    [IR_ADD]   = assemble_add,
    [IR_SUB]   = assemble_sub,
    [IR_MUL]   = assemble_mul,
    [IR_UDIV]  = assemble_udiv,
    [IR_SDIV]  = assemble_sdiv,
    [IR_UREM]  = assemble_urem,
    [IR_SREM]  = assemble_srem,
    [IR_SLL]   = assemble_sll,
    [IR_SRL]   = assemble_srl,
    [IR_SRA]   = assemble_sra,
    [IR_AND]   = assemble_and,
    [IR_OR]    = assemble_or,
    [IR_XOR]   = assemble_xor,
    [IR_ICMP]  = assemble_icmp,
    [IR_LOAD]  = assemble_load,
    [IR_STORE] = assemble_store,
    [IR_READ]  = assemble_read,
    [IR_WRITE] = assemble_write,
    [IR_TRUNC] = assemble_trunc,
    [IR_SEXT]  = assemble_sext,
    [IR_ZEXT]  = assemble_zext,
};

static void assemble_instr(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    return assemble_callbacks[instr->kind](backend, emitter, instr);
}

static void assemble_block(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_block_t const *block) {
    ir_instr_t const *instr = block->instrs;
    for (; instr != NULL; instr = instr->next) {
        assemble_instr(backend, emitter, instr);
    }
}

/**
 * Allocate the stack frame for storing all intermediate variables.
 * The allocation spills all variables, and ignores lifetime.
 * Returns the required stack frame size.
 */
static unsigned alloc_vars(ir_graph_t const *graph) {
    unsigned offset = 0;
    for (unsigned label = 0; label < graph->nr_blocks; label++) {
        ir_block_t const *block = graph->blocks + label;
        ir_instr_t const *instr = block->instrs;

        for (; instr != NULL; instr = instr->next) {
            if (ir_is_void_instr(instr))
                continue;

            // Allocated variables are phantom, only the requested memory
            // is allocated, not the variable slot.
            unsigned width = instr->kind == IR_ALLOC ?
                instr->alloc.type.width : instr->type.width;

            // Align the offset to the return type size.
            width = round_up_to_power2(width) / 8;
            offset = (offset + width - 1) & ~(width - 1);
            offset = offset + width;

            // Save the offset to the var metadata, update the current
            // stack offset.
            ir_var_context[instr->res].stack_offset = -offset;
            ir_var_context[instr->res].allocated = instr->kind == IR_ALLOC;
        }
    }

    // Round to 16 to preserve the stack alignment on function calls.
    return (offset + 15) & ~15u;
}

code_entry_t ir_x86_64_assemble(recompiler_backend_t const *backend,
                                code_buffer_t *emitter,
                                ir_graph_t const *graph,
                                size_t *binary_len) {

    // Reset the emitter. Sets a catch point for exception generated by the
    // emit_* helpers. An negative return signifies a generation failure.
    if (reset_code_buffer(emitter) < 0) {
        return NULL;
    }

    // Clear the assembler context.
    ir_br_queue_len = 0;
    ir_exit_queue_len = 0;
    for (unsigned nr = 0; nr < RECOMPILER_BLOCK_MAX; nr++) {
        ir_block_context[nr].start = NULL;
    }

    // Generate the standard function prelude to enter into compiled code.
    void *entry = code_buffer_ptr(emitter);
    unsigned stack_size = alloc_vars(graph);
    emit_push_r64(emitter, RBP);
    emit_mov_r64_r64(emitter, RBP, RSP);
    emit_sub_r64_imm32(emitter, RSP, stack_size);

    // Start the assembly with the first block.
    ir_br_queue[0].rel32 = NULL;
    ir_br_queue[0].block = &graph->blocks[0];
    ir_br_queue_len = 1;

    // Loop until all instruction blocks are compiled.
    while (ir_br_queue_len > 0) {
        ir_br_context_t context = ir_br_queue[--ir_br_queue_len];
        unsigned char *start = ir_block_context[context.block->label].start;
        if (start == NULL) {
            start = code_buffer_ptr(emitter);
            ir_block_context[context.block->label].start = start;
            assemble_block(backend, emitter, context.block);
        }
        patch_jmp_rel32(emitter, context.rel32, start);
    }

    // Generate the standard function postlude.
    unsigned char *exit_label = code_buffer_ptr(emitter);
    emit_mov_r64_r64(emitter, RSP, RBP);
    emit_pop_r64(emitter, RBP);
    emit_ret(emitter);

    // Patch all exit instructions to jump to the exit label.
    for (unsigned nr = 0; nr < ir_exit_queue_len; nr++) {
        patch_jmp_rel32(emitter, ir_exit_queue[nr].rel32, exit_label);
    }

    // Return the length of the generated binary code.
    if (binary_len) *binary_len =
        (size_t)(code_buffer_ptr(emitter) - (unsigned char *)entry);

    // Return the address of the graph entry.
    return entry;
}
