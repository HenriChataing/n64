
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
    unsigned register_;
    bool allocated;
    bool spilled;
    unsigned liveness_end;
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

/**
 * Convert a typed variable to an operand:
 * direct register access for register allocated variables,
 * indirect stack frame access otherwise.
 * \p value cannot be an allocated variable.
 */
static x86_64_operand_t op_var(ir_var_t var, ir_type_t type) {
    unsigned size = round_up_to_power2(type.width);
    if (ir_var_context[var].allocated) {
        // The value of allocated variables cannot be taken.
        return op_imm(size, 0);
    } else if (ir_var_context[var].spilled) {
        return op_mem_indirect_disp(size, RBP,
            ir_var_context[var].stack_offset);
    } else {
        return op_reg(size, ir_var_context[var].register_);
    }
}

/**
 * Convert a value to an operand: immediate for constant values,
 * direct register access for register allocated variables,
 * indirect stack frame access otherwise.
 * \p value cannot be an allocated variable.
 */
static x86_64_operand_t op_value(ir_value_t const *value) {
    unsigned size = round_up_to_power2(value->type.width);
    if (value->kind == IR_CONST) {
        return op_imm(size, value->const_.int_);
    } else if (ir_var_context[value->var].allocated) {
        // The value of allocated variables cannot be taken.
        return op_imm(size, 0);
    } else if (ir_var_context[value->var].spilled) {
        return op_mem_indirect_disp(size, RBP,
            ir_var_context[value->var].stack_offset);
    } else {
        return op_reg(size, ir_var_context[value->var].register_);
    }
}

/**
 * Generate the code to move the input value to the selected location.
 * If \p value is an allocated variable, the pointer to the allocated location
 * is moved instead of the content itself.
 * @param emitter       Selected code emitter.
 * @param dst           Pointer to the destination operand.
 * @param value         Pointer to the source value.
 */
static void emit_mov_dst_val(code_buffer_t *emitter, x86_64_operand_t *dst,
                             ir_value_t const *value) {
    if (value->kind == IR_VAR && ir_var_context[value->var].allocated) {
        emit_lea_dst_m(emitter, dst,
            mem_indirect_disp(RBP, ir_var_context[value->var].stack_offset));
    } else {
        x86_64_operand_t src = op_value(value);
        emit_mov_dst_src0(emitter, dst, &src);
    }
}

/**
 * Generate the code to move the selected operand to the output value.
 * If \p value cannot be an allocated variable, since the value (stack offset)
 * is known at compile time and never changed.
 * @param emitter       Selected code emitter.
 * @param dst           Pointer to the destination operand.
 * @param value         Pointer to the source value.
 */
static void emit_mov_val_src(code_buffer_t *emitter,
                             ir_var_t var, ir_type_t type,
                             x86_64_operand_t *src) {
    if (!ir_var_context[var].allocated) {
        x86_64_operand_t dst = op_var(var, type);
        emit_mov_dst_src0(emitter, &dst, src);
    }
}


static void assemble_exit(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    ir_exit_queue[ir_exit_queue_len].rel32 = emit_jmp_rel32(emitter, 0);
    ir_exit_queue_len++;
}

static void assemble_assert(recompiler_backend_t const *backend,
                            code_buffer_t *emitter,
                            ir_instr_t const *instr) {
    // The assert jumps to the exit label if the condition is false,
    // otherwise continues with normal instruction flow.
    x86_64_operand_t src0 = op_value(&instr->assert_.cond);
    x86_64_operand_t src1 = op_imm(8, 1);

    emit_test_src0_src1(emitter, &src0, &src1);
    ir_exit_queue[ir_exit_queue_len].rel32 = emit_je_rel32(emitter, 0);
    ir_exit_queue_len++;
}

static void assemble_br(recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {
    x86_64_operand_t src0 = op_value(&instr->br.cond);
    x86_64_operand_t src1 = op_imm(8, 1);
    unsigned char *rel32;

    emit_test_src0_src1(emitter, &src0, &src1);
    rel32 = emit_jne_rel32(emitter, 0);
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

    // Save R8, R9 if necessary: these registers are used for pseudo
    // variable storage.
    if (instr->call.nr_params >= 5) {
        emit_push_r64(emitter, R8);
        emit_push_r64(emitter, R9);
    }

    // TODO potential issue if the a parameter is saved to R8: its value
    // might be overwritten...
    for (unsigned nr = 5; nr < instr->call.nr_params; nr++) {
        if (instr->call.params[nr].kind == IR_VAR &&
            !ir_var_context[instr->call.params[nr].var].spilled &&
            ir_var_context[instr->call.params[nr].var].register_ == R8) {
            printf("assemble_call: cannot assemble function call");
            fail_code_buffer(emitter);
        }
    }

    // Load first six parameters to registers.
    for (unsigned nr = 0; nr < instr->call.nr_params && nr < 6; nr++) {
        ir_value_t *param = instr->call.params + nr;
        unsigned size = round_up_to_power2(param->type.width);

        if (size == 8 && nr < 3) {
            // RDI, RSI cannot be written with byte access.
            x86_64_operand_t tmp = op_reg(8, AL);
            emit_mov_dst_val(emitter, &tmp, param);
            emit_mov_r64_r64(emitter, register_parameters[nr], RAX);
        } else {
            x86_64_operand_t dst = op_reg(size, register_parameters[nr]);
            emit_mov_dst_val(emitter, &dst, param);
        }
    }

    // Allocate call frame if the function has more than six parameters.
    // RBX is used here to save the frame address.
    if (instr->call.nr_params >= 6) {
        emit_push_r64(emitter, RBX);
        emit_push_r64(emitter, RBP);
        frame_size = 8 * (instr->call.nr_params - 6);
        frame_size = (frame_size + 15) / 16;
        frame_size = frame_size * 16;
        emit_sub_r64_imm32(emitter, RSP, frame_size);
        emit_mov_r64_r64(emitter, RBX, RSP);
    }

    // Load subsequent parameters to the call frame.
    for (unsigned nr = 6; nr < instr->call.nr_params; nr++) {
        ir_value_t *param = instr->call.params + nr;
        unsigned size = round_up_to_power2(param->type.width);
        x86_64_operand_t dst = op_mem_indirect_disp(size, RBX, 8 * (nr - 6));
        emit_mov_dst_val(emitter, &dst, param);
    }

    // Emit the function call.
    emit_call(emitter, instr->call.func, RAX);

    // Trash call frame, restored saved registers.
    if (instr->call.nr_params >= 6) {
        emit_add_r64_imm32(emitter, RSP, frame_size);
        emit_pop_r64(emitter, RBP);
        emit_pop_r64(emitter, RBX);
    }

    // Restore R8, R9 if they were saved.
    if (instr->call.nr_params >= 5) {
        emit_pop_r64(emitter, R9);
        emit_pop_r64(emitter, R8);
    }

    // Save the function return value.
    if (instr->type.width > 0) {
        unsigned size = round_up_to_power2(instr->type.width);
        x86_64_operand_t src = op_reg(size, RAX);
        emit_mov_val_src(emitter, instr->res, instr->type, &src);
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

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->unop.value);

    emit_not_dst_src0(emitter, &dst, &src0);
}

static void assemble_add(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_add_dst_src0_src1(emitter, &dst, &src0, &src1);
}


static void assemble_sub(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_sub_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_mul(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t tmp0 = op_reg(instr->type.width, RAX);
    x86_64_operand_t tmp1 = op_reg(instr->type.width, RCX);
    emit_mov_dst_val(emitter, &tmp0, &instr->binop.left);
    emit_mov_dst_val(emitter, &tmp1, &instr->binop.right);
    emit_imul_rN_rN(emitter, instr->type.width, RAX, RCX);
    emit_mov_val_src(emitter, instr->res, instr->type, &tmp0);
}

static void assemble_udiv(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    x86_64_operand_t tmp0 = op_reg(instr->type.width, RAX);
    x86_64_operand_t tmp1 = op_reg(instr->type.width, RCX);
    emit_mov_dst_val(emitter, &tmp0, &instr->binop.left);
    emit_mov_dst_val(emitter, &tmp1, &instr->binop.right);
    emit_xor_r64_r64(emitter, RDX, RDX);

    switch (instr->type.width) {
    case 32: emit_div_edx_eax_r32(emitter, ECX);
    case 64: emit_div_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    emit_mov_val_src(emitter, instr->res, instr->type, &tmp0);
}

static void assemble_sdiv(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    x86_64_operand_t tmp0 = op_reg(instr->type.width, RAX);
    x86_64_operand_t tmp1 = op_reg(instr->type.width, RCX);
    emit_mov_dst_val(emitter, &tmp0, &instr->binop.left);
    emit_mov_dst_val(emitter, &tmp1, &instr->binop.right);

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

    emit_mov_val_src(emitter, instr->res, instr->type, &tmp0);
}

static void assemble_urem(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    x86_64_operand_t tmp0 = op_reg(instr->type.width, RAX);
    x86_64_operand_t tmp1 = op_reg(instr->type.width, RCX);
    emit_mov_dst_val(emitter, &tmp0, &instr->binop.left);
    emit_mov_dst_val(emitter, &tmp1, &instr->binop.right);
    emit_xor_r64_r64(emitter, RDX, RDX);

    switch (instr->type.width) {
    case 32: emit_div_edx_eax_r32(emitter, ECX);
    case 64: emit_div_rdx_rax_r64(emitter, RCX);
        break;
    default:
        fail_code_buffer(emitter);
        break;
    }

    x86_64_operand_t src = op_reg(instr->type.width, RDX);
    emit_mov_val_src(emitter, instr->res, instr->type, &src);
}

static void assemble_srem(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    x86_64_operand_t tmp0 = op_reg(instr->type.width, RAX);
    x86_64_operand_t tmp1 = op_reg(instr->type.width, RCX);
    emit_mov_dst_val(emitter, &tmp0, &instr->binop.left);
    emit_mov_dst_val(emitter, &tmp1, &instr->binop.right);

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

    x86_64_operand_t src = op_reg(instr->type.width, RDX);
    emit_mov_val_src(emitter, instr->res, instr->type, &src);
}

static void assemble_and(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_and_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_or(recompiler_backend_t const *backend,
                        code_buffer_t *emitter,
                        ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_or_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_xor(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_xor_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_sll(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_shl_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_srl(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_shr_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_sra(recompiler_backend_t const *backend,
                         code_buffer_t *emitter,
                         ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->binop.left);
    x86_64_operand_t src1 = op_value(&instr->binop.right);

    emit_sra_dst_src0_src1(emitter, &dst, &src0, &src1);
}

static void assemble_icmp(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src0 = op_value(&instr->icmp.left);
    x86_64_operand_t src1 = op_value(&instr->icmp.right);

    emit_cmp_src0_src1(emitter, &src0, &src1);

    switch (instr->icmp.op) {
    case IR_EQ:  emit_sete_op0(emitter, &dst); break;
    case IR_NE:  emit_setne_op0(emitter, &dst); break;
    case IR_UGT: emit_seta_op0(emitter, &dst); break;
    case IR_UGE: emit_setae_op0(emitter, &dst); break;
    case IR_ULT: emit_setb_op0(emitter, &dst); break;
    case IR_ULE: emit_setbe_op0(emitter, &dst); break;
    case IR_SGT: emit_setg_op0(emitter, &dst); break;
    case IR_SGE: emit_setge_op0(emitter, &dst); break;
    case IR_SLT: emit_setl_op0(emitter, &dst); break;
    case IR_SLE: emit_setle_op0(emitter, &dst); break;
    default:     fail_code_buffer(emitter);
    }
}

static void assemble_load(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    unsigned size = round_up_to_power2(instr->type.width);

    if (instr->load.address.kind == IR_VAR &&
        ir_var_context[instr->load.address.var].allocated) {

        x86_64_operand_t src = op_mem_indirect_disp(size, RBP,
            ir_var_context[instr->load.address.var].stack_offset);
        emit_mov_val_src(emitter, instr->res, instr->type, &src);
    } else {
        x86_64_operand_t src = op_value(&instr->load.address);
        x86_64_operand_t tmp = op_reg(src.size, RAX);
        x86_64_operand_t tmp_deref = op_mem_indirect(size, RAX);
        emit_mov_op0_op1(emitter, &tmp, &src);
        emit_mov_val_src(emitter, instr->res, instr->type, &tmp_deref);
    }
}

static void assemble_store(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {

    unsigned size = round_up_to_power2(instr->type.width);

    if (instr->store.address.kind == IR_VAR &&
        ir_var_context[instr->store.address.var].allocated) {

        x86_64_operand_t dst = op_mem_indirect_disp(size, RBP,
            ir_var_context[instr->store.address.var].stack_offset);
        emit_mov_dst_val(emitter, &dst, &instr->store.value);
    } else {
        x86_64_operand_t dst = op_value(&instr->store.address);
        x86_64_operand_t tmp = op_reg(instr->type.width, RAX);
        x86_64_operand_t tmp_deref = op_mem_indirect(size, RAX);
        emit_mov_op0_op1(emitter, &tmp, &dst);
        emit_mov_dst_val(emitter, &tmp_deref, &instr->store.value);
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
    x86_64_operand_t dst = op_var(instr->res, instr->type);
    x86_64_operand_t src = op_mem_indirect(dst.size, RCX);

    emit_mov_r64_imm64(emitter, RCX, (intptr_t)ptr);
    emit_mov_dst_src0(emitter, &dst, &src);
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
    x86_64_operand_t dst = op_mem_indirect(instr->type.width, RCX);

    emit_mov_r64_imm64(emitter, RCX, (intptr_t)ptr);
    emit_mov_dst_val(emitter, &dst, &instr->write.value);
}

static void assemble_trunc(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {

    x86_64_operand_t src = op_value(&instr->cvt.value);
    src.size = instr->type.width;
    emit_mov_val_src(emitter, instr->res, instr->type, &src);
}

static void assemble_sext(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {
    unsigned from_size = round_up_to_power2(instr->cvt.value.type.width);
    unsigned to_size   = round_up_to_power2(instr->type.width);
    x86_64_operand_t tmp0 = op_reg(from_size, RAX);
    x86_64_operand_t tmp1 = op_reg(to_size, RAX);

    emit_mov_dst_val(emitter, &tmp0, &instr->cvt.value);

    if (from_size <=  8 && to_size >  8) {
        emit_cbw(emitter);
    }
    if (from_size <= 16 && to_size > 16) {
        emit_cwde(emitter);
    }
    if (from_size <= 32 && to_size > 32) {
        emit_cdqe(emitter);
    }

    emit_mov_val_src(emitter, instr->res, instr->type, &tmp1);
}

static void assemble_zext(recompiler_backend_t const *backend,
                          code_buffer_t *emitter,
                          ir_instr_t const *instr) {

    unsigned from_size = round_up_to_power2(instr->cvt.value.type.width);
    unsigned to_size   = round_up_to_power2(instr->type.width);
    x86_64_operand_t tmp0 = op_reg(from_size, RAX);
    x86_64_operand_t tmp1 = op_reg(to_size, RAX);

    emit_xor_r64_r64(emitter, RAX, RAX);
    emit_mov_dst_val(emitter, &tmp0, &instr->cvt.value);
    emit_mov_val_src(emitter, instr->res, instr->type, &tmp1);
}

static const void (*assemble_callbacks[])(recompiler_backend_t const *backend,
                                          code_buffer_t *emitter,
                                          ir_instr_t const *instr) = {
    [IR_EXIT]   = assemble_exit,
    [IR_ASSERT] = assemble_assert,
    [IR_BR]     = assemble_br,
    [IR_CALL]   = assemble_call,
    [IR_ALLOC]  = assemble_alloc,
    [IR_NOT]    = assemble_not,
    [IR_ADD]    = assemble_add,
    [IR_SUB]    = assemble_sub,
    [IR_MUL]    = assemble_mul,
    [IR_UDIV]   = assemble_udiv,
    [IR_SDIV]   = assemble_sdiv,
    [IR_UREM]   = assemble_urem,
    [IR_SREM]   = assemble_srem,
    [IR_SLL]    = assemble_sll,
    [IR_SRL]    = assemble_srl,
    [IR_SRA]    = assemble_sra,
    [IR_AND]    = assemble_and,
    [IR_OR]     = assemble_or,
    [IR_XOR]    = assemble_xor,
    [IR_ICMP]   = assemble_icmp,
    [IR_LOAD]   = assemble_load,
    [IR_STORE]  = assemble_store,
    [IR_READ]   = assemble_read,
    [IR_WRITE]  = assemble_write,
    [IR_TRUNC]  = assemble_trunc,
    [IR_SEXT]   = assemble_sext,
    [IR_ZEXT]   = assemble_zext,
};

_Static_assert(IR_ZEXT == 26,
    "IR instruction set changed, code may need to be updated");

static void assemble_instr(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_instr_t const *instr) {
    return assemble_callbacks[instr->kind](backend, emitter, instr);
}

static void assemble_block(recompiler_backend_t const *backend,
                           code_buffer_t *emitter,
                           ir_block_t const *block) {
    ir_instr_t const *instr = block->entry;
    for (; instr != NULL; instr = instr->next) {
        assemble_instr(backend, emitter, instr);
    }
}

static void set_liveness_end(void *index, ir_value_t const *value) {
    if (value->kind == IR_VAR) {
        ir_var_context[value->var].liveness_end = *(unsigned *)index;
    }
}

/**
 * Compute the liveness interval of each intermediate variable.
 * The interval starts at the variable declaration (unique because variables
 * are in SSA form) and ends with the last occurence.
 * The blocks must have been generated following the domination graph:
 * each block precedes all blocks inheriting its scope.
 */
static void compute_liveness(ir_graph_t const *graph) {
    unsigned index = 0;
    for (unsigned label = 0; label < graph->nr_blocks; label++) {
        ir_block_t const *block = graph->blocks + label;
        ir_instr_t const *instr = block->entry;

        for (; instr != NULL; instr = instr->next, index++) {
            ir_iter_values(instr, set_liveness_end, &index);
        }
    }
}

struct release_register_params {
    unsigned index;
    unsigned register_bitmap;
    int preferred_register;
};

/**
 * Release the register used by a pseudo variable if the liveness
 * interval ends on the current instruction.
 */
static void release_register(void *params_, ir_value_t const *value) {

    struct release_register_params *params = params_;
    if (value->kind != IR_VAR) {
        return;
    }

    ir_var_context_t *ctx = ir_var_context + value->var;
    if (ctx->liveness_end != params->index || ctx->spilled) {
        return;
    }

    params->register_bitmap |= 1u << ctx->register_;
    if (params->preferred_register < 0) {
        params->preferred_register = ctx->register_;
    }
}

/**
 * Release the registers used by pseudo variables if the liveness
 * interval ends on the current instruction.
 */
static int release_registers(ir_instr_t const *instr, unsigned index,
                             unsigned *register_bitmap) {
    struct release_register_params params = {
        .index = index,
        .register_bitmap = *register_bitmap,
        .preferred_register = -1,
    };

    ir_iter_values(instr, release_register, &params);
    *register_bitmap = params.register_bitmap;
    return params.preferred_register;
}

/**
 * Alloc a register from the unused bitmap.
 * \p register_bitmap must not be zero.
 * If \p preferred_register is strictly positive,
 * it will be directly allocated.
 */
static unsigned alloc_register(unsigned *register_bitmap,
                               int preferred_register) {
    unsigned r = preferred_register < 0 ?
        __builtin_ctz(*register_bitmap) : preferred_register;
    *register_bitmap &= ~(1u << r);
    return r;
}

/**
 * Allocate the stack frame for storing all intermediate variables.
 * The allocation spills all variables, and ignores lifetime.
 * Returns the required stack frame size.
 */
static unsigned alloc_vars(ir_graph_t const *graph,
                           unsigned *used_register_bitmap_ptr) {
    /* Current stack frame offset.  */
    unsigned stack_offset = 0;
    /* Bitmap of unused registers. */
    unsigned register_bitmap = 0xff00;
    unsigned used_register_bitmap = 0x0;
    /* Instruction index. */
    unsigned index = 0;

    /* Determine the liveness of each variable first. */
    compute_liveness(graph);

    /* Iterate through instructions to allocate variables.  */
    for (unsigned label = 0; label < graph->nr_blocks; label++) {
        ir_block_t const *block = graph->blocks + label;
        ir_instr_t const *instr = block->entry;

        for (; instr != NULL; instr = instr->next, index++) {
            // Update register bitmap with liveness interval ends.
            int reg = release_registers(instr, index, &register_bitmap);

            // No variable to allocate.
            if (ir_is_void_instr(instr))
                continue;

            // Allocated variables are phantom, only the requested memory
            // is allocated, not the variable slot.
            bool alloc = instr->kind == IR_ALLOC;

            if (!alloc && register_bitmap != 0) {
                // Allocate a register.
                reg = alloc_register(&register_bitmap, reg);
                ir_var_context[instr->res].spilled = false;
                ir_var_context[instr->res].register_ = reg;
                ir_var_context[instr->res].allocated = false;
                used_register_bitmap |= (1u << reg);
            } else {
                // Spill the variable.
                // Align the offset to the return type size.
                unsigned width = alloc ? instr->alloc.type.width : instr->type.width;
                width = round_up_to_power2(width) / 8;
                stack_offset = (stack_offset + width - 1) & ~(width - 1);
                stack_offset = stack_offset + width;
                ir_var_context[instr->res].spilled = true;
                ir_var_context[instr->res].stack_offset = -stack_offset;
                ir_var_context[instr->res].allocated = alloc;
            }
        }
    }

    // Return used registers.
    if (used_register_bitmap_ptr) {
        *used_register_bitmap_ptr = used_register_bitmap;
    }

    // Round to 16 to preserve the stack alignment on function calls.
    return (stack_offset + 15) & ~15u;
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
    unsigned used_register_bitmap = 0;
    unsigned stack_size = alloc_vars(graph, &used_register_bitmap);
    emit_push_r64(emitter, RBP);
    emit_push_r64(emitter, R12);
    emit_push_r64(emitter, R13);
    emit_push_r64(emitter, R14);
    emit_push_r64(emitter, R15);
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
    emit_pop_r64(emitter, R15);
    emit_pop_r64(emitter, R14);
    emit_pop_r64(emitter, R13);
    emit_pop_r64(emitter, R12);
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
