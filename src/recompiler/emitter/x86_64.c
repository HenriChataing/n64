
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <recompiler/emitter/x86_64/emitter.h>
#include <recompiler/ir.h>
#include <recompiler/passes.h>

#define IR_BLOCK_MAX 64
#define IR_VAR_MAX 4096
#define IR_INSTR_MAX 1024

typedef struct ir_block_context {
    unsigned char *start;
} ir_block_context_t;

typedef struct ir_var_context {
    unsigned stack_offset;
} ir_var_context_t;

typedef struct ir_br_context {
    ir_block_t const *block;
    unsigned char *rel32;
} ir_br_context_t;

typedef struct ir_exit_context {
    unsigned char *rel32;
} ir_exit_context_t;

static ir_block_context_t ir_block_context[IR_BLOCK_MAX];
static ir_var_context_t   ir_var_context[IR_VAR_MAX];
static ir_br_context_t    ir_br_queue[IR_INSTR_MAX];
static unsigned           ir_br_queue_len;
static ir_exit_context_t  ir_exit_queue[IR_INSTR_MAX];
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
    x86_64_mem_t mN = mem_indirect_disp(RBP,
        ir_var_context[var].stack_offset);
    switch (type.width) {
    case 8:  emit_mov_m8_r8(emitter, mN, r);   break;
    case 16: emit_mov_m16_r16(emitter, mN, r); break;
    case 32: emit_mov_m32_r32(emitter, mN, r); break;
    case 64: emit_mov_m64_r64(emitter, mN, r); break;
    default: fail_code_buffer(emitter);
    }
}

static void assemble_exit(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    ir_exit_queue[ir_exit_queue_len].rel32 = emit_jmp_rel32(emitter);
    ir_exit_queue_len++;
}

static void assemble_br(ir_recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {
    unsigned char *rel32;
    load_value(emitter, instr->br.cond, RAX);
    emit_cmp_al_imm8(emitter, 0);
    rel32 = emit_je_rel32(emitter);
    ir_br_queue[ir_br_queue_len].rel32 = rel32;
    ir_br_queue[ir_br_queue_len].block = instr->br.target;
    ir_br_queue_len++;
}

static void assemble_call(ir_recompiler_backend_t const *backend,
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

    for (unsigned nr = 0; nr < instr->call.nr_params && nr < 6; nr++) {
        load_value(emitter, instr->call.params[nr], register_parameters[nr]);
    }
    if (instr->call.nr_params > 6) {
        frame_size = 8 * (instr->call.nr_params - 6);
        frame_size = (frame_size + 15) / 16;
        frame_size = frame_size * 16;
        emit_push_r64(emitter, R12);
        emit_push_r64(emitter, R13); // To preserve stack alignment.
        emit_sub_r64_imm32(emitter, RSP, frame_size);
        emit_mov_r64_r64(emitter, R12, RSP);
    }
    for (unsigned nr = 6; nr < instr->call.nr_params; nr++) {
        load_value(emitter, instr->call.params[nr], RAX);
        emit_mov_m64_r64(emitter, mem_indirect_disp(R12, 8 * (nr - 6)), RAX);
    }

    emit_call(emitter, instr->call.func);

    if (instr->call.nr_params > 6) {
        emit_add_r64_imm32(emitter, RSP, frame_size);
        emit_pop_r64(emitter, R13);
        emit_pop_r64(emitter, R12);
    }
    if (instr->type.width > 0) {
        store_value(emitter, instr->type, instr->res, RAX);
    }
}

static void assemble_not(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->unop.value, RAX);
    emit_not_r64(emitter, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_add(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_add_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}


static void assemble_sub(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_sub_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_umul(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_imul_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_smul(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_imul_rN_rN(emitter, instr->type.width, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_udiv(ir_recompiler_backend_t const *backend,
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

static void assemble_sdiv(ir_recompiler_backend_t const *backend,
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

static void assemble_urem(ir_recompiler_backend_t const *backend,
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

static void assemble_srem(ir_recompiler_backend_t const *backend,
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

static void assemble_and(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_and_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_or(ir_recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_or_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_xor(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, RCX);
    emit_xor_r64_r64(emitter, RAX, RCX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sll(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_shl_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_srl(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_shr_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sra(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {
    load_value(emitter, instr->binop.left, RAX);
    load_value(emitter, instr->binop.right, CL);
    emit_sra_rN_cl(emitter, instr->binop.left.type.width, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_icmp(ir_recompiler_backend_t const *backend,
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

static void assemble_load(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;

    load_value(emitter, instr->load.address, RDI);
    emit_mov_r64_r64(emitter, RSI, RSP);
    emit_add_r64_imm32(emitter, RSI, ir_var_context[instr->res].stack_offset);

    switch (instr->type.width) {
    case 8:  emit_call(emitter, memory->load_u8); break;
    case 16: emit_call(emitter, memory->load_u16); break;
    case 32: emit_call(emitter, memory->load_u32); break;
    case 64: emit_call(emitter, memory->load_u64); break;
    default: fail_code_buffer(emitter); break;
    }
}

static void assemble_store(ir_recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    ir_memory_backend_t const *memory = &backend->memory;

    load_value(emitter, instr->store.address, RDI);
    load_value(emitter, instr->store.value, RSI);

    switch (instr->type.width) {
    case 8:  emit_call(emitter, memory->store_u8); break;
    case 16: emit_call(emitter, memory->store_u16); break;
    case 32: emit_call(emitter, memory->store_u32); break;
    case 64: emit_call(emitter, memory->store_u64); break;
    default: fail_code_buffer(emitter); break;
    }
}

static void assemble_read(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    ir_register_t register_ = instr->read.register_;
    if (register_ > backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        fail_code_buffer(emitter);
        return;
    }

    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;

    emit_mov_r64_imm64(emitter, RAX, (intptr_t)ptr);
    emit_mov_rN_mN(emitter, type.width, RAX, mem_indirect(RAX));
    store_value(emitter, type, instr->res, RAX);
}

static void assemble_write(ir_recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {

    ir_register_t register_ = instr->write.register_;
    if (register_ > backend->nr_registers ||
        backend->registers[register_].ptr == NULL) {
        fail_code_buffer(emitter);
        return;
    }

    void *ptr = backend->registers[register_].ptr;
    ir_type_t type = backend->registers[register_].type;

    emit_mov_r64_imm64(emitter, RAX, (intptr_t)ptr);
    load_value(emitter, instr->write.value, RCX);
    emit_mov_mN_rN(emitter, type.width, mem_indirect(RAX), RCX);
}

static void assemble_trunc(ir_recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    load_value(emitter, instr->cvt.value, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static void assemble_sext(ir_recompiler_backend_t const *backend,
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

static void assemble_zext(ir_recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    emit_xor_r64_r64(emitter, RAX, RAX);
    load_value(emitter, instr->cvt.value, RAX);
    store_value(emitter, instr->type, instr->res, RAX);
}

static const void (*assemble_callbacks[])(ir_recompiler_backend_t const *backend,
                                          code_buffer_t *emitter,
                                          ir_instr_t const *instr) = {
    [IR_EXIT]  = assemble_exit,
    [IR_BR]    = assemble_br,
    [IR_CALL]  = assemble_call,
    [IR_NOT]   = assemble_not,
    [IR_ADD]   = assemble_add,
    [IR_SUB]   = assemble_sub,
    [IR_UMUL]  = assemble_umul,
    [IR_SMUL]  = assemble_smul,
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

static void assemble_instr(ir_recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    return assemble_callbacks[instr->kind](backend, emitter, instr);
}

static void assemble_block(ir_recompiler_backend_t const *backend,
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
    for (unsigned nr = 0; nr < graph->nr_blocks; nr++) {
        ir_instr_t const *instr = graph->blocks[nr].instrs;
        for (; instr != NULL; instr = instr->next) {
            if (ir_is_void_instr(instr))
                continue;

            // Align the offset to the return type size.
            unsigned width = round_up_to_power2(instr->type.width);
            offset = (offset + width - 1) & ~(width - 1);

            // Save the offset to the var metadata, update the current
            // stack offset.
            ir_var_context[instr->res].stack_offset = offset;
            offset = offset + width;
        }
    }

    return (offset + 15) & ~15u;
}

void *ir_x86_64_assemble(ir_recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_graph_t const *graph) {

    // Reset the emitter. Sets a catch point for exception generated by the
    // emit_* helpers. An negative return signifies a generation failure.
    if (reset_code_buffer(emitter) < 0) {
        return NULL;
    }

    // Clear the assembler context.
    ir_br_queue_len = 0;
    ir_exit_queue_len = 0;
    for (unsigned nr = 0; nr < IR_BLOCK_MAX; nr++) {
        ir_block_context[nr].start = NULL;
    }

    // Allocate the stack frame for the assembled graph.
    unsigned stack_size = alloc_vars(graph);

    // Generate the function prelude to enter into compiled code.
    // Because it is a dummy generator, no register is scratched.
    void *entry = emitter->ptr + emitter->length;
    emit_push_r64(emitter, RBP);
    emit_sub_r64_imm32(emitter, RSP, stack_size);
    emit_mov_r64_r64(emitter, RBP, RSP);

    // Start the assembly with the first block.
    ir_br_queue[0].rel32 = NULL;
    ir_br_queue[0].block = &graph->blocks[0];
    ir_br_queue_len = 1;

    // Loop until all instructions are compiled.
    while (ir_br_queue_len > 0) {
        ir_br_context_t context = ir_br_queue[--ir_br_queue_len];
        unsigned char *start = ir_block_context[context.block->label].start;
        if (start == NULL) {
            start = emitter->ptr + emitter->length;
            ir_block_context[context.block->label].start = start;
            assemble_block(backend, emitter, context.block);
        }
        patch_jmp_rel32(emitter, context.rel32, start);
    }

    // Generate the function postlude.
    unsigned char *exit_label = emitter->ptr + emitter->length;
    emit_add_r64_imm32(emitter, RSP, stack_size);
    emit_pop_r64(emitter, RBP);
    emit_ret(emitter);

    // Patch all exit instructions to jump to the exit label.
    for (unsigned nr = 0; nr < ir_exit_queue_len; nr++) {
        patch_jmp_rel32(emitter, ir_exit_queue[nr].rel32, exit_label);
    }

    // Return the address of the graph entry.
    return entry;
}
