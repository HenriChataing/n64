
#include <interpreter.h>
#include <assembly/registers.h>
#include <r4300/state.h>

#include <recompiler/config.h>
#include <recompiler/ir.h>
#include <recompiler/backend.h>
#include <recompiler/target/mips.h>

enum {
    /* General purpose registers. */
    REG_ZERO,       REG_AT,         REG_V0,         REG_V1,
    REG_A0,         REG_A1,         REG_A2,         REG_A3,
    REG_T0,         REG_T1,         REG_T2,         REG_T3,
    REG_T4,         REG_T5,         REG_T6,         REG_T7,
    REG_S0,         REG_S1,         REG_S2,         REG_S3,
    REG_S4,         REG_S5,         REG_S6,         REG_S7,
    REG_T8,         REG_T9,         REG_K0,         REG_K1,
    REG_GP,         REG_SP,         REG_FP,         REG_RA,
    /* Special registers. */
    REG_PC,         REG_MULTHI,     REG_MULTLO,
    /* COP0 registers. */
    REG_INDEX,      REG_RANDOM,     REG_ENTRYLO0,   REG_ENTRYLO1,
    REG_CONTEXT,    REG_PAGEMASK,   REG_WIRED,      REG_BADVADDR,
    REG_COUNT,      REG_ENTRYHI,    REG_COMPARE,    REG_SR,
    REG_CAUSE,      REG_EPC,        REG_PRID,       REG_CONFIG,
    REG_LLADDR,     REG_WATCHLO,    REG_WATCHHI,    REG_XCONTEXT,
    REG_PERR,       REG_CACHEERR,   REG_TAGLO,      REG_TAGHI,
    REG_ERROREPC,
    /* COP1 registers. */
    REG_FCR0,       REG_FCR31,
    /* State globals. */
    REG_CYCLES,
    REG_DELAY_SLOT,
    REG_MAX,
};

/* Stand-in interpreter, default callback when the instruction cannot
 * be translated to IR. */
extern "C" void interpret(uint32_t instr) {
    interpreter::cpu::eval_Instr(instr);
}

extern "C" void take_cop1_unusable_exception(void) {
    takeException(R4300::CoprocessorUnusable, 0, false, false, 1);
    R4300::state.reg.pc = R4300::state.cpu.nextPc;
}

extern "C" void take_cop2_unusable_exception(void) {
    takeException(R4300::CoprocessorUnusable, 0, false, false, 2);
    R4300::state.reg.pc = R4300::state.cpu.nextPc;
}

extern "C" void take_cop3_unusable_exception(void) {
    takeException(R4300::CoprocessorUnusable, 0, false, false, 3);
    R4300::state.reg.pc = R4300::state.cpu.nextPc;
}

#define _define_virt_load_uN(N) \
extern "C" bool virt_load_u##N(uint64_t virt_addr, uint##N##_t *value) { \
    uint64_t phys_addr; \
    uint64_t align_mask = N / 8 - 1; \
    R4300::Exception exn; \
    if ((virt_addr & align_mask) != 0) { \
        exn = R4300::AddressError; \
        goto take_exception; \
    } \
    exn = R4300::translateAddress(virt_addr, &phys_addr, false); \
    if (exn != R4300::Exception::None) { \
        goto take_exception; \
    } \
    if (!R4300::state.bus->load_u##N(phys_addr, value)) { \
        exn = R4300::BusError; \
        goto take_exception; \
    } \
    return R4300::state.cpu.nextAction != R4300::State::Jump; \
take_exception: \
    R4300::takeException(exn, virt_addr, false, true); \
    return false; \
}

_define_virt_load_uN(8);
_define_virt_load_uN(16);
_define_virt_load_uN(32);
_define_virt_load_uN(64);

#define _define_virt_store_uN(N) \
extern "C" bool virt_store_u##N(uint64_t virt_addr, uint##N##_t value) { \
    uint64_t phys_addr; \
    uint64_t align_mask = N / 8 - 1; \
    R4300::Exception exn; \
    if ((virt_addr & align_mask) != 0) { \
        exn = R4300::AddressError; \
        goto take_exception; \
    } \
    exn = R4300::translateAddress(virt_addr, &phys_addr, false); \
    if (exn != R4300::Exception::None) { \
        goto take_exception; \
    } \
    if (!R4300::state.bus->store_u##N(phys_addr, value)) { \
        exn = R4300::BusError; \
        goto take_exception; \
    } \
    return R4300::state.cpu.nextAction != R4300::State::Jump; \
take_exception: \
    R4300::takeException(exn, virt_addr, false, false); \
    return false; \
}

_define_virt_store_uN(8);
_define_virt_store_uN(16);
_define_virt_store_uN(32);
_define_virt_store_uN(64);

static inline uint32_t mips_get_rs(uint32_t instr) {
    return (instr >> 21) & 0x1flu;
}

static inline uint32_t mips_get_rt(uint32_t instr) {
    return (instr >> 16) & 0x1flu;
}

static inline uint32_t mips_get_rd(uint32_t instr) {
    return (instr >> 11) & 0x1flu;
}

static inline uint32_t mips_get_shamnt(uint32_t instr) {
    return (instr >> 6) & 0x1flu;
}

static inline uint16_t mips_get_imm_u16(uint32_t instr) {
    return instr & 0xfffflu;
}

static inline uint32_t mips_get_imm_u32(uint32_t instr) {
    uint16_t imm = mips_get_imm_u16(instr);
    return (uint32_t)(int32_t)(int16_t)imm;
}

static inline uint64_t mips_get_imm_u64(uint32_t instr) {
    uint16_t imm = mips_get_imm_u16(instr);
    return (uint64_t)(int64_t)(int16_t)imm;
}

static inline uint32_t mips_get_target(uint32_t instr) {
    return instr & 0x3fffffflu;
}

static inline
ir_value_t ir_mips_append_load(ir_instr_cont_t *c, unsigned width,
                               ir_callback_t load_func, ir_value_t addr) {
    ir_value_t value_ptr = ir_append_alloc(c, ir_make_iN(width));
    ir_value_t exn = ir_append_call(c, ir_make_iN(1),
        load_func, 2, addr, value_ptr);
    ir_append_assert(c, exn);
    return ir_append_load(c, ir_make_iN(width), value_ptr);
}

static inline
ir_value_t ir_mips_append_load_i8(ir_instr_cont_t *c, ir_value_t addr) {
    return ir_mips_append_load(c, 8, (ir_callback_t)virt_load_u8, addr);
}

static inline
ir_value_t ir_mips_append_load_i16(ir_instr_cont_t *c, ir_value_t addr) {
    return ir_mips_append_load(c, 16, (ir_callback_t)virt_load_u16, addr);
}

static inline
ir_value_t ir_mips_append_load_i32(ir_instr_cont_t *c, ir_value_t addr) {
    return ir_mips_append_load(c, 32, (ir_callback_t)virt_load_u32, addr);
}

static inline
ir_value_t ir_mips_append_load_i64(ir_instr_cont_t *c, ir_value_t addr) {
    return ir_mips_append_load(c, 64, (ir_callback_t)virt_load_u64, addr);
}

static inline
void ir_mips_append_store(ir_instr_cont_t *c, unsigned width,
                          ir_callback_t store_func, ir_value_t addr,
                          ir_value_t value) {
    ir_value_t exn = ir_append_call(c, ir_make_iN(1),
        store_func, 2, addr, value);
    ir_append_assert(c, exn);
}

static inline
void ir_mips_append_store_i8(ir_instr_cont_t *c, ir_value_t addr,
                             ir_value_t value) {
    return ir_mips_append_store(c, 8,
        (ir_callback_t)virt_store_u8, addr, value);
}

static inline
void ir_mips_append_store_i16(ir_instr_cont_t *c, ir_value_t addr,
                              ir_value_t value) {
    return ir_mips_append_store(c, 16,
        (ir_callback_t)virt_store_u16, addr, value);
}

static inline
void ir_mips_append_store_i32(ir_instr_cont_t *c, ir_value_t addr,
                              ir_value_t value) {
    return ir_mips_append_store(c, 32,
        (ir_callback_t)virt_store_u32, addr, value);
}

static inline
void ir_mips_append_store_i64(ir_instr_cont_t *c, ir_value_t addr,
                              ir_value_t value) {
    return ir_mips_append_store(c, 64,
        (ir_callback_t)virt_store_u64, addr, value);
}

static inline ir_value_t ir_mips_append_read(ir_instr_cont_t *c,
                                             ir_global_t global) {
    return global ? ir_append_read_i64(c, global)
                     : ir_make_const_u64(0);
}

static inline void ir_mips_append_write(ir_instr_cont_t *c,
                                        ir_global_t global,
                                        ir_value_t value) {
    if (global) ir_append_write_i64(c, global, value);
}

/** Number of cycle increments applied so far. */
static unsigned ir_disas_cycles;
/** Whether the COP1 coprocessor guard was generated or not. */
static bool ir_cop1_guard_generated;

static inline void ir_mips_incr_cycles(void) {
    ir_disas_cycles++;
}

static inline void ir_mips_commit_cycles(ir_instr_cont_t *c) {
    if (ir_disas_cycles) {
        ir_value_t v = ir_append_read_i64(c, REG_CYCLES);
        ir_append_write_i64(c, REG_CYCLES,
            ir_append_binop(c, IR_ADD, v, ir_make_const_u64(ir_disas_cycles)));
        ir_disas_cycles = 0;
    }
}

static inline void ir_mips_append_interpreter(ir_instr_cont_t *c,
                                              uint64_t address,
                                              uint32_t instr) {
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    ir_append_call(c, ir_make_iN(0), (ir_callback_t)interpret,
                   1, ir_make_const_u32(instr));
}

typedef struct ir_disas_entrypoint {
    uint64_t address;
    ir_instr_cont_t cont;
} ir_disas_entrypoint_t;

/** Store the information relevant to the current disassembly task. */
static struct {
    uint64_t start;
    uint64_t end;
    unsigned char *ptr;
} ir_disas_region;

/** Queue containing current disassembly entry points. */
static struct {
    ir_disas_entrypoint_t queue[RECOMPILER_BLOCK_MAX];
    unsigned length;
} ir_disas_queue;

/** Map address offsets to disassembled instructions. */
static struct {
    ir_instr_t     *map[RECOMPILER_INSTR_MAX];
    unsigned        base;
} ir_disas_map;

/** Whether the current instruction is in a delay slot. */
static bool ir_disas_delay_slot;

static ir_instr_t *disas_instr(ir_instr_cont_t *c, uint64_t address,
                               uint32_t instr, bool delay_slot);
static void append_delay_instr(ir_instr_cont_t *c, uint64_t address,
                               uint32_t instr);

static void disas_push (uint64_t address, ir_instr_cont_t c) {
    ir_disas_queue.queue[ir_disas_queue.length++] =
        (ir_disas_entrypoint_t){ .address = address, .cont = c };
}

static void disas_pop  (uint64_t *address, ir_instr_cont_t *c) {
    ir_disas_queue.length--;
    *address = ir_disas_queue.queue[ir_disas_queue.length].address;
    *c = ir_disas_queue.queue[ir_disas_queue.length].cont;
}

static void disas_map  (uint64_t address, ir_instr_t *instr) {
    unsigned offset = (address - ir_disas_region.start) / 4;
    ir_disas_map.map[offset] = instr;
}

static bool disas_fetch(uint64_t address, ir_instr_cont_t cont) {
#if 0
    unsigned offset = (address - ir_disas_region.start) / 4;
    if (ir_disas_map.map[offset]) {
        *cont = ir_disas_map.map[offset];
        return true;
    } else {
        return false;
    }
#endif
    return false;
}

static bool disas_check_address(uint64_t address) {
    return address >= ir_disas_region.start &&
           (address + 4) <= ir_disas_region.end;
}

/** Check whether the delay instruction address is inside the
 * disassembly region. Generates IR bytecode to exit the recompiled code
 * before the branch instruction if the address is invalid. */
static bool disas_guard_branch_delay(ir_instr_cont_t *c, uint64_t address) {
    if (disas_check_address(address + 4)) {
        return true;
    } else {
        /* The address is outside the specified region,
         * emit a emulation exit to return to the interpreter. */
        ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
        ir_mips_commit_cycles(c);
        ir_append_exit(c);
        return false;
    }
}

/** Check whether an instruction requires a COP1 coprocessor guard. */
static bool cop1_guard_required(uint32_t instr) {
    uint32_t opcode = (instr >> 26) & 0x3fu;
    return opcode == 0x11 || // COP1
           opcode == 0x31 || // LWC1
           opcode == 0x35 || // LDC1
           opcode == 0x39 || // SWC1
           opcode == 0x3d;   // SDC1
}

/**
 * Generate a guard checking a COP1 coprocessor access rights.
 * The guard is only generated for a first access, or after a write
 * to the COP0 status register.
 * The code generated returns after triggering a CoprocessorUnusable
 * exception.
 */
static void generate_cop1_guard(ir_instr_cont_t *c, uint64_t address) {
    if (ir_cop1_guard_generated)
        return;

    ir_instr_cont_t br_true;
    ir_value_t cu1 = ir_append_binop(c, IR_AND,
        ir_append_read_i32(c, REG_SR), ir_make_const_u32(STATUS_CU1));
    ir_append_br(c, ir_append_icmp(c, IR_EQ, cu1, ir_make_const_u32(0)),
        c, &br_true);

    // Backup the current cycle counter.
    // The commit here only accounts for the case where an exception
    // is taken. Furthermore, in the case of a delay slot instruction,
    // the count is incremented to account for the yet to be disassembed
    // instruction.
    unsigned cycles = ir_disas_cycles;
    ir_append_write_i64(&br_true, REG_PC,
        ir_make_const_u64(address));
    if (ir_disas_delay_slot) {
        // The value of the elay slot always defaults to 0.
        // The recompiled code execution stops after any branching
        // instruction so the value never needs to be rewritten to 0.
        ir_append_write_i8(&br_true, REG_DELAY_SLOT,
            ir_make_const_u8(ir_disas_delay_slot));
    }
    ir_mips_commit_cycles(&br_true);
    ir_append_call(&br_true, ir_make_iN(0), take_cop1_unusable_exception, 0);
    ir_append_exit(&br_true);
    ir_cop1_guard_generated = true;

    // Restore the cycles count.
    ir_disas_cycles = cycles;
}

static uint32_t disas_read_instr(uint64_t address) {
    unsigned char *ptr = ir_disas_region.ptr + (address - ir_disas_region.start);
    return ((uint32_t)ptr[0] << 24) |
           ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8)  |
           ((uint32_t)ptr[3] << 0);
}

/** Generates the IR bytecode for a branch instruction.
 * The generated graph has the following shape :
 *
 *  cond = .. --> [delay] --> br cond --{true}--> target
 *                              |
 *                              `-----{false}--> next
 */
static void disas_branch(ir_instr_cont_t *c, ir_value_t cond, uint64_t address,
                         uint32_t instr) {

    ir_instr_cont_t br_false, br_true;
    uint32_t delay_instr = disas_read_instr(address + 4);
    uint64_t imm = mips_get_imm_u64(instr);
    append_delay_instr(c, address + 4, delay_instr);
    ir_mips_commit_cycles(c);
    ir_append_br(c, cond, &br_false, &br_true);

#if RECOMPILER_DISAS_BRANCH_ENABLE
    disas_push(address + 4 + (imm << 2), br_true);
    disas_push(address + 8, br_false);
#else
    ir_append_write_i64(&br_false, REG_PC,
        ir_make_const_u64(address + 8));
    ir_append_exit(&br_false);

    ir_append_write_i64(&br_true, REG_PC,
        ir_make_const_u64(address + 4 + (imm << 2)));
    ir_append_exit(&br_true);
#endif /* DISAS_BRANCH_ENABLE */
}

/** Generates the IR bytecode for a branch _likely_ instruction.
 * The generated graph has the following shape :
 *
 *  cond = .. --> br cond --{true}--> [delay] -->  target
 *                   |
 *                   `-----{false}--> next
 */
static void disas_branch_likely(ir_instr_cont_t *c, ir_value_t cond,
                                uint64_t address, uint32_t instr) {
    uint64_t imm = mips_get_imm_u64(instr);
    ir_instr_cont_t br_false, br_true;
    ir_mips_commit_cycles(c);
    ir_append_br(c, cond, &br_false, &br_true);
    append_delay_instr(&br_true, address + 4, disas_read_instr(address + 4));

#if RECOMPILER_DISAS_BRANCH_ENABLE
    disas_push(address + 4 + (imm << 2), br_true);
    disas_push(address + 8, br_false);
#else
    ir_append_write_i64(&br_false, REG_PC,
        ir_make_const_u64(address + 8));
    ir_append_exit(&br_false);

    ir_append_write_i64(&br_true, REG_PC,
        ir_make_const_u64(address + 4 + (imm << 2)));
    ir_mips_commit_cycles(&br_true);
    ir_append_exit(&br_true);
#endif /* DISAS_BRANCH_ENABLE */
}

/* Reserved opcodes */

static void disas_Reserved(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

/* SPECIAL opcodes */

static void disas_ADD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow exception
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    vd = ir_append_binop(c, IR_ADD, vs, vt);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_ADDU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_ADD(c, address, instr);
}

static void disas_AND(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_AND, vs, vt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_BREAK(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_DADD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_ADD, vs, vt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DADDU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_DADD(c, address, instr);
}

static void disas_DDIV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // i64 lo = (i64)state.reg.gpr[rs] / (i64)state.reg.gpr[rt];
    // i64 hi = (i64)state.reg.gpr[rs] % (i64)state.reg.gpr[rt];
    // state.reg.multLo = (uint64_t)lo;
    // state.reg.multHi = (uint64_t)hi;
}

static void disas_DDIVU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // state.reg.multLo = state.reg.gpr[rs] / state.reg.gpr[rt];
    // state.reg.multHi = state.reg.gpr[rs] % state.reg.gpr[rt];
}

static void disas_DIV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // // Must use 64bit integers here to prevent signed overflow.
    // i64 num = (i32)state.reg.gpr[rs];
    // i64 denum = (i32)state.reg.gpr[rt];
    // if (denum != 0) {
    //     state.reg.multLo = sign_extend<uint64_t, uint32_t>((uint64_t)(num / denum));
    //     state.reg.multHi = sign_extend<uint64_t, uint32_t>((uint64_t)(num % denum));
    // } else {
    //     debugger::undefined("Divide by 0 (DIV)");
    //     // Undefined behaviour here according to the reference
    //     // manual. The machine behaviour is as implemented.
    //     state.reg.multLo = num < 0 ? 1 : UINT64_C(-1);
    //     state.reg.multHi = sign_extend<uint64_t, uint32_t>((uint64_t)num);
    // }
}

static void disas_DIVU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // uint32_t num = state.reg.gpr[rs];
    // uint32_t denum = state.reg.gpr[rt];
    // if (denum != 0) {
    //     state.reg.multLo = sign_extend<uint64_t, uint32_t>(num / denum);
    //     state.reg.multHi = sign_extend<uint64_t, uint32_t>(num % denum);
    // } else {
    //     debugger::undefined("Divide by 0 (DIVU)");
    //     // Undefined behaviour here according to the reference
    //     // manual. The machine behaviour is as implemented.
    //     state.reg.multLo = UINT64_C(-1);
    //     state.reg.multHi = sign_extend<uint64_t, uint32_t>(num);
    // }
}

static void disas_DMULT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // mult_uint64_t(state.reg.gpr[rs], state.reg.gpr[rt],
    //          &state.reg.multHi, &state.reg.multLo);
    // if ((i64)state.reg.gpr[rs] < 0)
    //     state.reg.multHi -= state.reg.gpr[rt];
    // if ((i64)state.reg.gpr[rt] < 0)
    //     state.reg.multHi -= state.reg.gpr[rs];
}

static void disas_DMULTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // mult_uint64_t(state.reg.gpr[rs], state.reg.gpr[rt],
    //          &state.reg.multHi, &state.reg.multLo);
}

static void disas_DSLL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SLL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSLL32(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(32 + mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SLL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSLLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    shamnt = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0x3f));
    vd = ir_append_binop(c, IR_SLL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRA(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRA, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRA32(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(32 + mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRA, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRAV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    shamnt = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0x3f));
    vd = ir_append_binop(c, IR_SRA, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRL32(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    shamnt = ir_make_const_u64(32 + mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSRLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    shamnt = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0x3f));
    vd = ir_append_binop(c, IR_SRL, vt, shamnt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSUB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_SUB, vs, vt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_DSUBU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_DSUB(c, address, instr);
}

static void disas_JALR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    ir_mips_append_write(c, mips_get_rd(instr), ir_make_const_u64(address + 8));
    append_delay_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, vs);
    ir_mips_commit_cycles(c);
    ir_append_exit(c);
}

static void disas_JR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    append_delay_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, vs);
    ir_mips_commit_cycles(c);
    ir_append_exit(c);
}

static void disas_MFHI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vd;
    vd = ir_append_read_i64(c, REG_MULTHI);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_MFLO(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vd;
    vd = ir_append_read_i64(c, REG_MULTLO);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_MOVN(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // debugger::halt("MOVN");
}

static void disas_MOVZ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // debugger::halt("MOVZ");
}

static void disas_MTHI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    ir_mips_append_write(c, REG_MULTHI, vs);
    disas_push(address + 4, *c);
}

static void disas_MTLO(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    ir_mips_append_write(c, REG_MULTLO, vs);
    disas_push(address + 4, *c);
}

static void disas_MULT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd, multhi, multlo;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0xfffffffflu));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_binop(c, IR_AND, vt, ir_make_const_u64(0xfffffffflu));
    vd = ir_append_binop(c, IR_MUL, vs, vt);

    multhi = ir_append_binop(c, IR_SRL, vd, ir_make_const_u64(32));
    multhi = ir_append_trunc_i32(c, multhi);
    multhi = ir_append_sext_i64(c, multhi);
    ir_mips_append_write(c, REG_MULTHI, multhi);

    multlo = ir_append_trunc_i32(c, vd);
    multlo = ir_append_sext_i64(c, multlo);
    ir_mips_append_write(c, REG_MULTLO, multlo);
    disas_push(address + 4, *c);
}

static void disas_MULTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd, multhi, multlo;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0xfffffffflu));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_binop(c, IR_AND, vt, ir_make_const_u64(0xfffffffflu));
    vd = ir_append_binop(c, IR_MUL, vs, vt);

    multhi = ir_append_binop(c, IR_SRL, vd, ir_make_const_u64(32));
    multhi = ir_append_trunc_i32(c, multhi);
    multhi = ir_append_sext_i64(c, multhi);
    ir_mips_append_write(c, REG_MULTHI, multhi);

    multlo = ir_append_trunc_i32(c, vd);
    multlo = ir_append_sext_i64(c, multlo);
    ir_mips_append_write(c, REG_MULTLO, multlo);
    disas_push(address + 4, *c);
}

static void disas_NOR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_OR, vs, vt);
    vd = ir_append_unop(c,  IR_NOT, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_OR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_OR, vs, vt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SLL, vt, shamnt);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    vd = ir_append_binop(c, IR_SLL, vt, vs);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_icmp(c, IR_SLT, vs, vt);
    vd = ir_append_zext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_icmp(c, IR_ULT, vs, vt);
    vd = ir_append_zext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRA(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRA, vt, shamnt);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRAV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    vd = ir_append_binop(c, IR_SRA, vt, vs);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRL, vt, shamnt);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    vd = ir_append_binop(c, IR_SRL, vt, vs);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SUB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow exception
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    vd = ir_append_binop(c, IR_SUB, vs, vt);
    vd = ir_append_sext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SUBU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_SUB(c, address, instr);
}

static void disas_SYNC(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_SYSCALL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // takeR4300::Exception(SystemCall, 0, false, false, 0);
}

static void disas_TEQ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TGE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TGEU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TLT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TLTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TNE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_XOR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_binop(c, IR_XOR, vs, vt);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

/* REGIMM opcodes */

static void disas_BGEZ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGE, vs, ir_make_const_u64(0));
    disas_branch(c, cond, address, instr);
}

static void disas_BGEZL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGE, vs, ir_make_const_u64(0));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BLTZ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLT, vs, ir_make_const_u64(0));
    disas_branch(c, cond, address, instr);
}

static void disas_BLTZL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLT, vs, ir_make_const_u64(0));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BGEZAL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGE, vs, ir_make_const_u64(0));
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    disas_branch(c, cond, address, instr);
}

static void disas_BGEZALL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGE, vs, ir_make_const_u64(0));
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BLTZAL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLT, vs, ir_make_const_u64(0));
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    disas_branch(c, cond, address, instr);
}

static void disas_BLTZALL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLT, vs, ir_make_const_u64(0));
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_TEQI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TGEI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TGEIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TLTI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TLTIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_TNEI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

/* Other opcodes */

static void disas_ADDI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc_i32(c, vs);
    imm = ir_make_const_u32(mips_get_imm_u32(instr));
    vt = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_sext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_ADDIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_ADDI(c, address, instr);
}

static void disas_ANDI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u16(instr));
    vt = ir_append_binop(c, IR_AND, vs, imm);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_BEQ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, vt, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    cond = ir_append_icmp(c, IR_EQ, vs, vt);
    disas_branch(c, cond, address, instr);
}

static void disas_BEQL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, vt, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    cond = ir_append_icmp(c, IR_EQ, vs, vt);
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BGTZ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGT, vs, ir_make_const_u64(0));
    disas_branch(c, cond, address, instr);
}

static void disas_BGTZL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SGT, vs, ir_make_const_u64(0));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BLEZ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLE, vs, ir_make_const_u64(0));
    disas_branch(c, cond, address, instr);
}

static void disas_BLEZL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    cond = ir_append_icmp(c, IR_SLE, vs, ir_make_const_u64(0));
    disas_branch_likely(c, cond, address, instr);
}

static void disas_BNE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, vt, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    cond = ir_append_icmp(c, IR_NE, vs, vt);
    disas_branch(c, cond, address, instr);
}

static void disas_BNEL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs, vt, cond;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    cond = ir_append_icmp(c, IR_NE, vs, vt);
    disas_branch_likely(c, cond, address, instr);
}

static void disas_CACHE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

/** Specific helper to read the current count value. */
extern "C" uint32_t eval_MFC0_Count(void) {
    return R4300::state.cp0reg.count +
        (R4300::state.cycles - R4300::state.cp0reg.lastCounterUpdate) / 2;
}

static void disas_MFC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    using namespace n64::assembly::cpu;
    ir_value_t vd;
    switch (mips_get_rd(instr)) {
    case Index:     vd = ir_append_read_i32(c, REG_INDEX); break;
    case Random:    vd = ir_append_read_i32(c, REG_RANDOM); break;
    case EntryLo0:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_ENTRYLO0));
        break;
    case EntryLo1:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_ENTRYLO1));
        break;
    case Context:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_CONTEXT));
        break;
    case PageMask:  vd = ir_append_read_i32(c, REG_PAGEMASK); break;
    case Wired:     vd = ir_append_read_i32(c, REG_WIRED); break;
    case BadVAddr:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_BADVADDR));
        break;
    case Count:
        ir_mips_commit_cycles(c);
        vd = ir_append_call(c, ir_make_iN(32),
            (ir_callback_t)eval_MFC0_Count, 0);
        break;
    case EntryHi:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_ENTRYHI));
        break;
    case Compare:   vd = ir_append_read_i32(c, REG_COMPARE); break;
    case SR:        vd = ir_append_read_i32(c, REG_SR); break;
    case Cause:     vd = ir_append_read_i32(c, REG_CAUSE); break;
    case EPC:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_EPC));
        break;
    case PrId:      vd = ir_append_read_i32(c, REG_PRID); break;
    case Config:    vd = ir_append_read_i32(c, REG_CONFIG); break;
    case LLAddr:    vd = ir_append_read_i32(c, REG_LLADDR); break;
    case WatchLo:   vd = ir_append_read_i32(c, REG_WATCHLO); break;
    case WatchHi:   vd = ir_append_read_i32(c, REG_WATCHHI); break;
    case XContext:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_XCONTEXT));
        break;
    case PErr:      vd = ir_append_read_i32(c, REG_PERR); break;
    case CacheErr:  vd = ir_append_read_i32(c, REG_CACHEERR); break;
    case TagLo:     vd = ir_append_read_i32(c, REG_TAGLO); break;
    case TagHi:     vd = ir_append_read_i32(c, REG_TAGHI); break;
    case ErrorEPC:
        vd = ir_append_trunc_i32(c,
             ir_append_read_i64(c, REG_ERROREPC));
        break;
    default:
        vd = ir_make_const_u32(0);
        break;
    }

    ir_mips_append_write(c, mips_get_rt(instr), ir_append_sext_i64(c, vd));
    disas_push(address + 4, *c);
}

static void disas_DMFC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    using namespace n64::assembly::cpu;
    ir_value_t vd;
    switch (mips_get_rd(instr)) {
    /* 64 bit registers */
    case EntryLo0:  vd = ir_append_read_i64(c, REG_ENTRYLO0); break;
    case EntryLo1:  vd = ir_append_read_i64(c, REG_ENTRYLO1); break;
    case Context:   vd = ir_append_read_i64(c, REG_CONTEXT); break;
    case BadVAddr:  vd = ir_append_read_i64(c, REG_BADVADDR); break;
    case EntryHi:   vd = ir_append_read_i64(c, REG_ENTRYHI); break;
    case EPC:       vd = ir_append_read_i64(c, REG_EPC); break;
    case XContext:  vd = ir_append_read_i64(c, REG_XCONTEXT); break;
    case ErrorEPC:  vd = ir_append_read_i64(c, REG_ERROREPC); break;
    /* 32-bit registers */
    case Count:
        ir_mips_commit_cycles(c);
        vd = ir_append_zext_i64(c,
             ir_append_call(c, ir_make_iN(32),
                (ir_callback_t)eval_MFC0_Count, 0));
        break;
    default:
        vd = ir_make_const_u64(0);
        break;
    }

    ir_mips_append_write(c, mips_get_rt(instr), vd);
    disas_push(address + 4, *c);
}

#if 0
static void disas_MTC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u32 val = state.reg.gpr[rt];

    debugger::info(Debugger::COP0, "{} <- {:08x}", Cop0RegisterNames[rd], val);

    switch (rd) {
        case Index:     state.cp0reg.index = val & UINT32_C(0x3f); break;
        case Random:
            state.cp0reg.random = val;
            debugger::halt("MTC0 random");
            break;
        case EntryLo0:  state.cp0reg.entrylo0 = sign_extend<u64, u32>(val); break;
        case EntryLo1:  state.cp0reg.entrylo1 = sign_extend<u64, u32>(val); break;
        case Context:
            state.cp0reg.context = sign_extend<u64, u32>(val);
            break;
        case PageMask:  state.cp0reg.pagemask = val & UINT32_C(0x01ffe000); break;
        case Wired:
            state.cp0reg.wired = val & UINT32_C(0x3f);
            if (state.cp0reg.wired >= tlbEntryCount)
                debugger::halt("COP0::wired invalid value");
            state.cp0reg.random = tlbEntryCount - 1;
            break;
        case BadVAddr:  state.cp0reg.badvaddr = sign_extend<u64, u32>(val); break;
        case Count:
            state.cp0reg.count = val;
            state.cp0reg.lastCounterUpdate = state.cycles;
            scheduleCounterEvent();
            break;
        case EntryHi:   state.cp0reg.entryhi = sign_extend<u64, u32>(val); break;
        case Compare:
            state.cp0reg.compare = val;
            state.cp0reg.cause &= ~CAUSE_IP7;
            scheduleCounterEvent();
            break;
        case SR:
            if ((val & STATUS_FR) != (state.cp0reg.sr & STATUS_FR)) {
                state.cp1reg.setFprAliases((val & STATUS_FR) != 0);
            }
            if (val & STATUS_RE) {
                debugger::halt("COP0::sr RE bit set");
            }
            // TODO check all config bits
            state.cp0reg.sr = val;
            checkInterrupt();
            break;
        case Cause:
            state.cp0reg.cause =
                (state.cp0reg.cause & ~CAUSE_IP_MASK) |
                (val                &  CAUSE_IP_MASK);
            // Interrupts bit 0 and 1 can be used to raise
            // software interrupts.
            // TODO mask unimplemented bits (only NMI, IRQ, SWI0, SWI1
            // are actually writable).
            checkInterrupt();
            break;
        case EPC:       state.cp0reg.epc = sign_extend<u64, u32>(val); break;
        case PrId:
            state.cp0reg.prid = val;
            debugger::halt("MTC0 prid");
            break;
        case Config:
            state.cp0reg.config = val;
            debugger::halt("MTC0 config");
            break;
        case LLAddr:
            state.cp0reg.lladdr = val;
            debugger::halt("MTC0 lladdr");
            break;
        case WatchLo:
            state.cp0reg.watchlo = val;
            debugger::halt("MTC0 watchlo");
            break;
        case WatchHi:
            state.cp0reg.watchhi = val;
            debugger::halt("MTC0 watchhi");
            break;
        case XContext:
            state.cp0reg.xcontext = sign_extend<u64, u32>(val);
            debugger::halt("MTC0 xcontext");
            break;
        case PErr:
            state.cp0reg.perr = val;
            debugger::halt("MTC0 perr");
            break;
        case CacheErr:
            state.cp0reg.cacheerr = val;
            debugger::halt("MTC0 cacheerr");
            break;
        case TagLo:     state.cp0reg.taglo = val; break;
        case TagHi:     state.cp0reg.taghi = val; break;
        case ErrorEPC:  state.cp0reg.errorepc = sign_extend<u64, u32>(val); break;
        default:
            std::string reason = "MTC0 ";
            debugger::halt(reason + Cop0RegisterNames[rd]);
            break;
    }
}

static void disas_DMTC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    u32 rt = Mips::getRt(instr);
    u32 rd = Mips::getRd(instr);
    u64 val = state.reg.gpr[rt];

    debugger::info(Debugger::COP0, "{} <- {:08x}", Cop0RegisterNames[rd], val);

    switch (rd) {
        case EntryLo0:  state.cp0reg.entrylo0 = val; break;
        case EntryLo1:  state.cp0reg.entrylo1 = val; break;
        case Context:
            state.cp0reg.context = val;
            debugger::halt("DMTC0 context");
            break;
        case BadVAddr:  state.cp0reg.badvaddr = val; break;
        case EntryHi:   state.cp0reg.entryhi = val; break;
        case EPC:       state.cp0reg.epc = val; break;
        case XContext:
            state.cp0reg.xcontext = val;
            debugger::halt("DMTC0 xcontext");
            break;
        case ErrorEPC:  state.cp0reg.errorepc = val; break;
        default:
            std::string reason = "DMTC0 ";
            debugger::halt(reason + Cop0RegisterNames[rd] + " (undefined)");
            break;
    }
}

#endif

static void disas_CFC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

static void disas_CTC0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    disas_push(address + 4, *c);
}

#if 0
static void disas_TLBR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    unsigned index = state.cp0reg.index & UINT32_C(0x3f);
    if (index >= tlbEntryCount) {
        debugger::halt("TLBR bad index");
        return;
    }
    state.cp0reg.pagemask = state.tlb[index].pageMask & UINT32_C(0x01ffe000);
    state.cp0reg.entryhi = state.tlb[index].entryHi;
    state.cp0reg.entrylo0 = state.tlb[index].entryLo0;
    state.cp0reg.entrylo1 = state.tlb[index].entryLo1;
}

static void disas_TLBW(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    u32 funct = Mips::getFunct(instr);
    unsigned index;

    if (funct == Mips::Cop0::TLBWI) {
        index = state.cp0reg.index & UINT32_C(0x3f);
        if (index >= tlbEntryCount) {
            debugger::halt("TLBWI bad index");
            return;
        }
    } else {
        index = state.cp0reg.random;
        state.cp0reg.random =
            index == state.cp0reg.wired ? tlbEntryCount - 1 : index - 1;
    }

    state.tlb[index].pageMask = state.cp0reg.pagemask;
    state.tlb[index].entryHi = state.cp0reg.entryhi;
    state.tlb[index].entryLo0 = state.cp0reg.entrylo0;
    state.tlb[index].entryLo1 = state.cp0reg.entrylo1;

    state.tlb[index].asid = state.cp0reg.entryhi & UINT64_C(0xff);
    state.tlb[index].global =
        (state.cp0reg.entrylo0 & UINT64_C(1)) &&
        (state.cp0reg.entrylo1 & UINT64_C(1));
}

static void disas_TLBP(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr;
    unsigned index;
    state.cp0reg.index = INDEX_P;
    if (probeTLB(state.cp0reg.entryhi, &index)) {
        state.cp0reg.index = index;
    }
}

#endif

static void disas_ERET(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t  sr, erl, pc;
    ir_instr_cont_t br_erl, br_exl;

    sr  = ir_append_read_i32(c, REG_SR);
    erl = ir_append_binop(c, IR_AND, sr, ir_make_const_u32(STATUS_ERL));
    ir_mips_commit_cycles(c);
    ir_append_br(c,
        ir_append_icmp(c, IR_EQ, erl, ir_make_const_u32(0)), &br_erl, &br_exl);

    /* ERL == 1 */
    ir_append_write_i32(&br_erl, REG_SR,
        ir_append_binop(&br_erl, IR_AND, sr, ir_make_const_u32(~STATUS_ERL)));
    pc = ir_append_read_i64(&br_erl, REG_ERROREPC);
    ir_append_write_i64(&br_erl, REG_PC, pc);
    ir_append_exit(&br_erl);

    /* ERL == 0 */
    ir_append_write_i32(&br_exl, REG_SR,
        ir_append_binop(&br_exl, IR_AND, sr, ir_make_const_u32(~STATUS_EXL)));
    pc = ir_append_read_i64(&br_exl, REG_EPC);
    ir_append_write_i64(&br_exl, REG_PC, pc);
    ir_append_exit(&br_exl);
}

static void disas_COP0(ir_instr_cont_t *c, uint64_t address, uint32_t instr)
{
    switch (mips_get_rs(instr)) {
    case 0:  disas_MFC0(c, address, instr); break;
    case 1:  disas_DMFC0(c, address, instr); break;
    // case 2:    eval_CFC0(instr); break;
    // case 4:    eval_MTC0(instr); break;
    // case 5 :   eval_DMTC0(instr); break;
    // case 6:    eval_CTC0(instr); break;
    case 0x10u:
        switch (instr & 0x3fu) {
        // case 1: eval_TLBR(instr); break;
        // case 2: eval_TLBW(instr); break;
        // case 6: eval_TLBW(instr); break;
        // case 8: eval_TLBP(instr); break;
        case 0x18:  disas_ERET(c, address, instr); break;
        default:
            ir_mips_append_interpreter(c, address, instr);
            disas_push(address + 4, *c);
            break;
        }
        break;
    default:
        // TODO only required when writing the SR register.
        ir_cop1_guard_generated = false;
        ir_mips_append_interpreter(c, address, instr);
        disas_push(address + 4, *c);
        break;
    }
}

static void disas_CFC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_global_t rd;
    switch (mips_get_rd(instr)) {
    case 0:  rd = REG_FCR0; break;
    case 31: rd = REG_FCR31; break;
    default:
        disas_push(address + 4, *c);
        return;
    }

    ir_value_t vd = ir_append_read_i32(c, rd);
    vd = ir_append_zext_i64(c, vd);
    ir_mips_append_write(c, mips_get_rt(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_CTC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_global_t rd;
    switch (mips_get_rd(instr)) {
    case 0:  rd = REG_FCR0; break;
    case 31: rd = REG_FCR31; break;
    default:
        disas_push(address + 4, *c);
        return;
    }

    ir_value_t vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    ir_append_write_i32(c, rd, vt);
    disas_push(address + 4, *c);
}

static void disas_BC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;

    unsigned opcode = mips_get_rt(instr);
    ir_icmp_kind_t op;
    bool likely;

    switch (opcode) {
    case 0:
    case 2:
        op = IR_EQ;
        likely = opcode == 2;
        break;
    case 1:
    case 3:
        op = IR_NE;
        likely = opcode == 3;
        break;
    default:
        disas_push(address + 4, *c);
        return;
    }

    ir_value_t fcr31   = ir_append_read_i32(c, REG_FCR31);
    ir_value_t fcr31_c = ir_append_binop(c, IR_AND, fcr31, ir_make_const_u32(FCR31_C));
    ir_value_t cond    = ir_append_icmp(c, op, fcr31_c, ir_make_const_u32(0));

    if (likely) {
        disas_branch_likely(c, cond, address, instr);
    } else {
        disas_branch(c, cond, address, instr);
    }
}

static void disas_COP1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    generate_cop1_guard(c, address);
    unsigned fmt = mips_get_rs(instr);
    switch (fmt) {
    case 2: disas_CFC1(c, address, instr); break;
    case 6: disas_CTC1(c, address, instr); break;
    case 8: disas_BC1(c, address, instr); break;
    default:
        ir_mips_append_interpreter(c, address, instr);
        disas_push(address + 4, *c);
        break;
    }
}

static void disas_COP2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(address));
    ir_mips_append_write(c, REG_DELAY_SLOT, ir_make_const_u8(0)); // TODO
    ir_mips_commit_cycles(c);
    ir_append_call(c, ir_make_iN(0), take_cop2_unusable_exception, 0);
    ir_append_exit(c);
}

static void disas_COP3(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(address));
    ir_mips_append_write(c, REG_DELAY_SLOT, ir_make_const_u8(0)); // TODO
    ir_mips_commit_cycles(c);
    ir_append_call(c, ir_make_iN(0), take_cop3_unusable_exception, 0);
    ir_append_exit(c);
}

static void disas_DADDI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vt = ir_append_binop(c, IR_ADD, vs, imm);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_DADDIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_DADDI(c, address, instr);
}

static void disas_J(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    uint32_t delay_instr = disas_read_instr(address + 4);
    uint64_t target = mips_get_target(instr);
    target = (address & 0xfffffffff0000000llu) | (target << 2);
    append_delay_instr(c, address + 4, delay_instr);
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(target));
    ir_mips_commit_cycles(c);
    ir_append_exit(c);
}

static void disas_JAL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    uint32_t delay_instr = disas_read_instr(address + 4);
    uint64_t target = mips_get_target(instr);
    target = (address & 0xfffffffff0000000llu) | (target << 2);
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    append_delay_instr(c, address + 4, delay_instr);
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(target));
    ir_mips_commit_cycles(c);
    ir_append_exit(c);
}

static void disas_LB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i8(c, vs);
    vt = ir_append_sext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LBU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i8(c, vs);
    vt = ir_append_zext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i64(c, vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LDC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    generate_cop1_guard(c, address);
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr, val;

    // checkCop1Usable();
    // checkAddressAlignment(vAddr, 8, false, true);
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, true, 0);
    // checkR4300::Exception(
    //     state.bus->load(8, pAddr, &val) ? None : BusError,
    //     vAddr, false, true, 0);

    // state.cp1reg.fpr_d[rt]->l = val;
}

static void disas_LDC2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, true, 2);
    // debugger::halt("LDC2");
}

static void disas_LDL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, true),
    //     vAddr, false, false, 0);

    // size_t count = 8 - (pAddr % 8);
    // unsigned int shift = 56;
    // uint64_t mask = (1llu << (64 - 8 * count)) - 1u;
    // uint64_t val = 0;

    // for (size_t nr = 0; nr < count; nr++, shift -= 8) {
    //     uint64_t byte = 0;
    //     if (!state.bus->load(1, pAddr + nr, &byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false, 0);
    //         return;
    //     }
    //     val |= (byte << shift);
    // }

    // val = val | (state.reg.gpr[rt] & mask);
    // state.reg.gpr[rt] = val;
}

static void disas_LDR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, true),
    //     vAddr, false, false, 0);

    // size_t count = 1 + (pAddr % 8);
    // unsigned int shift = 0;
    // uint64_t mask = (1llu << (64 - 8 * count)) - 1u;
    // mask <<= 8 * count;
    // uint64_t val = 0;

    // for (size_t nr = 0; nr < count; nr++, shift += 8) {
    //     uint64_t byte = 0;
    //     if (!state.bus->load(1, pAddr - nr, &byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false);
    //         return;
    //     }
    //     val |= (byte << shift);
    // }

    // val = val | (state.reg.gpr[rt] & mask);
    // state.reg.gpr[rt] = val;
}

static void disas_LH(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i16(c, vs);
    vt = ir_append_sext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LHU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i16(c, vs);
    vt = ir_append_zext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // debugger::halt("LL");
}

static void disas_LLD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // debugger::halt("LLD");
}

static void disas_LUI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t imm;
    imm = ir_make_const_u64(mips_get_imm_u64(instr) << 16);
    ir_mips_append_write(c, mips_get_rt(instr), imm);
    disas_push(address + 4, *c);
}

static void disas_LW(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i32(c, vs);
    vt = ir_append_sext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LWC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    generate_cop1_guard(c, address);
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;
    // uint32_t val;

    // checkCop1Usable();
    // checkAddressAlignment(vAddr, 4, false, true);
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, true, 0);
    // checkR4300::Exception(
    //     loadw(pAddr, &val),
    //     vAddr, false, true, 0);

    // state.cp1reg.fpr_s[rt]->w = val;
}

static void disas_LWC2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, true, 2);
    // debugger::halt("LWC2");
}

static void disas_LWC3(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, true, 3);
    // debugger::halt("LWC3");
}

static void disas_LWL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, true),
    //     vAddr, false, false, 0);

    // size_t count = 4 - (pAddr % 4);
    // unsigned int shift = 24;
    // uint64_t mask = (1llu << (32 - 8 * count)) - 1u;
    // uint64_t val = 0;

    // for (size_t nr = 0; nr < count; nr++, shift -= 8) {
    //     uint64_t byte = 0;
    //     if (!state.bus->load(1, pAddr + nr, &byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false, 0);
    //         return;
    //     }
    //     val |= (byte << shift);
    // }

    // val = val | (state.reg.gpr[rt] & mask);
    // state.reg.gpr[rt] = sign_extend<uint64_t, uint32_t>(val);
}

static void disas_LWR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, true),
    //     vAddr, false, false, 0);

    // size_t count = 1 + (pAddr % 4);
    // unsigned int shift = 0;
    // uint64_t mask = (1llu << (32 - 8 * count)) - 1u;
    // mask <<= 8 * count;
    // uint64_t val = 0;

    // for (size_t nr = 0; nr < count; nr++, shift += 8) {
    //     uint64_t byte = 0;
    //     if (!state.bus->load(1, pAddr - nr, &byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false);
    //         return;
    //     }
    //     val |= (byte << shift);
    // }

    // val = val | (state.reg.gpr[rt] & mask);
    // state.reg.gpr[rt] = sign_extend<uint64_t, uint32_t>(val);
}

static void disas_LWU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    vt = ir_mips_append_load_i32(c, vs);
    vt = ir_append_zext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_ORI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_make_const_u64(mips_get_imm_u16(instr));
    vd = ir_append_binop(c, IR_OR, vs, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i8(c, vt);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    ir_mips_append_store_i8(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SC(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // debugger::halt("SC");
}

static void disas_SCD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // debugger::halt("SCD");
}

static void disas_SD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    ir_mips_append_store_i64(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SDC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    generate_cop1_guard(c, address);
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // checkCop1Usable();
    // checkAddressAlignment(vAddr, 8, false, false);
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);
    // checkR4300::Exception(
    //     state.bus->store(8, pAddr, state.cp1reg.fpr_d[rt]->l) ? None : BusError,
    //     vAddr, false, false, 0);
}

static void disas_SDC2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, true, 2);
    // debugger::halt("SDC2");
}

static void disas_SDL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);

    // size_t count = 8 - (pAddr % 8);
    // uint64_t val = state.reg.gpr[rt];
    // unsigned int shift = 56;
    // for (size_t nr = 0; nr < count; nr++, shift -= 8) {
    //     uint64_t byte = (val >> shift) & 0xfflu;
    //     if (!state.bus->store(1, pAddr + nr, byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false, 0);
    //         return;
    //     }
    // }
}

static void disas_SDR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);

    // size_t count = 1 + (pAddr % 8);
    // uint64_t val = state.reg.gpr[rt];
    // unsigned int shift = 0;
    // for (size_t nr = 0; nr < count; nr++, shift += 8) {
    //     uint64_t byte = (val >> shift) & 0xfflu;
    //     if (!state.bus->store(1, pAddr - nr, byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false);
    //         return;
    //     }
    // }
}

static void disas_SH(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i16(c, vt);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    ir_mips_append_store_i16(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SLTI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vt = ir_append_icmp(c, IR_SLT, vs, imm);
    vt = ir_append_zext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_SLTIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vt = ir_append_icmp(c, IR_ULT, vs, imm);
    vt = ir_append_zext_i64(c, vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_SW(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc_i32(c, vt);
    ir_append_write_i64(c, REG_PC, ir_make_const_u64(address));
    ir_mips_commit_cycles(c);
    ir_mips_append_store_i32(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SWC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    generate_cop1_guard(c, address);
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);

    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // checkCop1Usable();
    // checkAddressAlignment(vAddr, 4, false, false);
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);
    // checkR4300::Exception(
    //     state.bus->store(4, pAddr, state.cp1reg.fpr_s[rt]->w) ? None : BusError,
    //     vAddr, false, false, 0);
}

static void disas_SWC2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 2);
    // debugger::halt("SWC2");
}

static void disas_SWC3(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 2);
    // debugger::halt("SWC3");
}

static void disas_SWL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // // debugger::halt("SWL instruction");
    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);

    // size_t count = 4 - (pAddr % 4);
    // uint32_t val = state.reg.gpr[rt];
    // unsigned int shift = 24;
    // for (size_t nr = 0; nr < count; nr++, shift -= 8) {
    //     uint64_t byte = (val >> shift) & 0xfflu;
    //     if (!state.bus->store(1, pAddr + nr, byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false, 0);
    //         return;
    //     }
    // }
}

static void disas_SWR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // IType(instr, sign_extend);
    // // debugger::halt("SWR instruction");
    // // @todo only BigEndianMem & !ReverseEndian for now
    // uint64_t vAddr = state.reg.gpr[rs] + imm;
    // uint64_t pAddr;

    // // Not calling checkAddressAlignment:
    // // this instruction specifically ignores the alignment
    // checkR4300::Exception(
    //     translateAddress(vAddr, &pAddr, false),
    //     vAddr, false, false, 0);

    // size_t count = 1 + (pAddr % 4);
    // uint32_t val = state.reg.gpr[rt];
    // unsigned int shift = 0;
    // for (size_t nr = 0; nr < count; nr++, shift += 8) {
    //     uint64_t byte = (val >> shift) & 0xfflu;
    //     if (!state.bus->store(1, pAddr - nr, byte)) {
    //         takeR4300::Exception(BusError, vAddr, false, false);
    //         return;
    //     }
    // }
}

static void disas_XORI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u16(instr));
    vt = ir_append_binop(c, IR_XOR, vs, imm);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void (*SPECIAL_callbacks[64])(ir_instr_cont_t *, uint64_t, uint32_t) = {
    disas_SLL,       disas_Reserved,  disas_SRL,       disas_SRA,
    disas_SLLV,      disas_Reserved,  disas_SRLV,      disas_SRAV,
    disas_JR,        disas_JALR,      disas_MOVZ,      disas_MOVN,
    disas_SYSCALL,   disas_BREAK,     disas_Reserved,  disas_SYNC,
    disas_MFHI,      disas_MTHI,      disas_MFLO,      disas_MTLO,
    disas_DSLLV,     disas_Reserved,  disas_DSRLV,     disas_DSRAV,
    disas_MULT,      disas_MULTU,     disas_DIV,       disas_DIVU,
    disas_DMULT,     disas_DMULTU,    disas_DDIV,      disas_DDIVU,
    disas_ADD,       disas_ADDU,      disas_SUB,       disas_SUBU,
    disas_AND,       disas_OR,        disas_XOR,       disas_NOR,
    disas_Reserved,  disas_Reserved,  disas_SLT,       disas_SLTU,
    disas_DADD,      disas_DADDU,     disas_DSUB,      disas_DSUBU,
    disas_TGE,       disas_TGEU,      disas_TLT,       disas_TLTU,
    disas_TEQ,       disas_Reserved,  disas_TNE,       disas_Reserved,
    disas_DSLL,      disas_Reserved,  disas_DSRL,      disas_DSRA,
    disas_DSLL32,    disas_Reserved,  disas_DSRL32,    disas_DSRA32,
};

static void disas_SPECIAL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    SPECIAL_callbacks[instr & 0x3fu](c, address, instr);
}

static void (*REGIMM_callbacks[32])(ir_instr_cont_t *, uint64_t, uint32_t) = {
    disas_BLTZ,      disas_BGEZ,      disas_BLTZL,     disas_BGEZL,
    disas_Reserved,  disas_Reserved,  disas_Reserved,  disas_Reserved,
    disas_TGEI,      disas_TGEIU,     disas_TLTI,      disas_TLTIU,
    disas_TEQI,      disas_Reserved,  disas_TNEI,      disas_Reserved,
    disas_BLTZAL,    disas_BGEZAL,    disas_BLTZALL,   disas_BGEZALL,
    disas_Reserved,  disas_Reserved,  disas_Reserved,  disas_Reserved,
    disas_Reserved,  disas_Reserved,  disas_Reserved,  disas_Reserved,
    disas_Reserved,  disas_Reserved,  disas_Reserved,  disas_Reserved,
};

static void disas_REGIMM(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    REGIMM_callbacks[mips_get_rt(instr)](c, address, instr);
}

static void (*CPU_callbacks[64])(ir_instr_cont_t *, uint64_t, uint32_t) = {
    disas_SPECIAL,   disas_REGIMM,    disas_J,         disas_JAL,
    disas_BEQ,       disas_BNE,       disas_BLEZ,      disas_BGTZ,
    disas_ADDI,      disas_ADDIU,     disas_SLTI,      disas_SLTIU,
    disas_ANDI,      disas_ORI,       disas_XORI,      disas_LUI,
    disas_COP0,      disas_COP1,      disas_COP2,      disas_COP3,
    disas_BEQL,      disas_BNEL,      disas_BLEZL,     disas_BGTZL,
    disas_DADDI,     disas_DADDIU,    disas_LDL,       disas_LDR,
    disas_Reserved,  disas_Reserved,  disas_Reserved,  disas_Reserved,
    disas_LB,        disas_LH,        disas_LWL,       disas_LW,
    disas_LBU,       disas_LHU,       disas_LWR,       disas_LWU,
    disas_SB,        disas_SH,        disas_SWL,       disas_SW,
    disas_SDL,       disas_SDR,       disas_SWR,       disas_CACHE,
    disas_LL,        disas_LWC1,      disas_LWC2,      disas_LWC3,
    disas_LLD,       disas_LDC1,      disas_LDC2,      disas_LD,
    disas_SC,        disas_SWC1,      disas_SWC2,      disas_SWC3,
    disas_SCD,       disas_SDC1,      disas_SDC2,      disas_SD,
};

static ir_instr_t *disas_instr(ir_instr_cont_t *c, uint64_t address,
                               uint32_t instr, bool delay_slot) {
    ir_mips_incr_cycles();
    ir_instr_t *entry = NULL;
    ir_instr_cont_t entryc = { c->backend, c->block, &entry };
    ir_disas_delay_slot = delay_slot;
    CPU_callbacks[(instr >> 26) & 0x3fu](&entryc, address, instr);
    if (entry == NULL) {
        /* The instruction is void, edit the last queue entry
         * (there would be a continuation in this case) to put back the
         * correct pointer */
        ir_disas_queue.queue[ir_disas_queue.length - 1].cont = *c;
    } else {
        *c->next = entry;
        c->next = entryc.next;
    }
    return entry;
}

static void append_delay_instr(ir_instr_cont_t *c, uint64_t address,
                               uint32_t instr) {
    unsigned prev_length = ir_disas_queue.length;
    (void)disas_instr(c, address, instr, true);
    ir_disas_queue.length = prev_length;
}

recompiler_backend_t *ir_mips_recompiler_backend(void) {
    ir_global_definition_t global_definitions[REG_MAX];
    global_definitions[REG_PC] = { ir_make_i64(), "pc", &R4300::state.reg.pc };
    global_definitions[REG_MULTHI] = { ir_make_i64(), "multhi", &R4300::state.reg.multHi };
    global_definitions[REG_MULTLO] = { ir_make_i64(), "multlo", &R4300::state.reg.multLo };
    global_definitions[REG_INDEX] = { ir_make_i32(), "index", &R4300::state.cp0reg.index };
    global_definitions[REG_RANDOM] = { ir_make_i32(), "random", &R4300::state.cp0reg.random };
    global_definitions[REG_ENTRYLO0] = { ir_make_i64(), "entrylo0", &R4300::state.cp0reg.entrylo0 };
    global_definitions[REG_ENTRYLO1] = { ir_make_i64(), "entrylo1", &R4300::state.cp0reg.entrylo1 };
    global_definitions[REG_CONTEXT] = { ir_make_i64(), "context", &R4300::state.cp0reg.context };
    global_definitions[REG_PAGEMASK] = { ir_make_i32(), "pagemask", &R4300::state.cp0reg.pagemask };
    global_definitions[REG_WIRED] = { ir_make_i32(), "wired", &R4300::state.cp0reg.wired };
    global_definitions[REG_BADVADDR] = { ir_make_i64(), "badvaddr", &R4300::state.cp0reg.badvaddr };
    global_definitions[REG_COUNT] = { ir_make_i32(), "count", &R4300::state.cp0reg.count };
    global_definitions[REG_ENTRYHI] = { ir_make_i64(), "entryhi", &R4300::state.cp0reg.entryhi };
    global_definitions[REG_COMPARE] = { ir_make_i32(), "compare", &R4300::state.cp0reg.compare };
    global_definitions[REG_SR] = { ir_make_i32(), "sr", &R4300::state.cp0reg.sr };
    global_definitions[REG_CAUSE] = { ir_make_i32(), "cause", &R4300::state.cp0reg.cause };
    global_definitions[REG_EPC] = { ir_make_i64(), "epc", &R4300::state.cp0reg.epc };
    global_definitions[REG_PRID] = { ir_make_i32(), "prid", &R4300::state.cp0reg.prid };
    global_definitions[REG_CONFIG] = { ir_make_i32(), "config", &R4300::state.cp0reg.config };
    global_definitions[REG_LLADDR] = { ir_make_i32(), "lladdr", &R4300::state.cp0reg.lladdr };
    global_definitions[REG_WATCHLO] = { ir_make_i32(), "watchlo", &R4300::state.cp0reg.watchlo };
    global_definitions[REG_WATCHHI] = { ir_make_i32(), "watchhi", &R4300::state.cp0reg.watchhi };
    global_definitions[REG_XCONTEXT] = { ir_make_i64(), "xcontext", &R4300::state.cp0reg.xcontext };
    global_definitions[REG_PERR] = { ir_make_i32(), "perr", &R4300::state.cp0reg.perr };
    global_definitions[REG_CACHEERR] = { ir_make_i32(), "cacheerr", &R4300::state.cp0reg.cacheerr };
    global_definitions[REG_TAGLO] = { ir_make_i32(), "taglo", &R4300::state.cp0reg.taglo };
    global_definitions[REG_TAGHI] = { ir_make_i32(), "taghi", &R4300::state.cp0reg.taghi };
    global_definitions[REG_ERROREPC] = { ir_make_i64(), "errorepc", &R4300::state.cp0reg.errorepc };
    global_definitions[REG_FCR0] = { ir_make_i32(), "fcr0", &R4300::state.cp1reg.fcr0 };
    global_definitions[REG_FCR31] = { ir_make_i32(), "fcr31", &R4300::state.cp1reg.fcr31 };
    global_definitions[REG_CYCLES] = { ir_make_i64(), "cycles", &R4300::state.cycles };
    global_definitions[REG_DELAY_SLOT] = { ir_make_i8(), "delay_slot", &R4300::state.cpu.delaySlot };

    for (unsigned i = 1; i < 32; i++) {
        global_definitions[i] = {
            ir_make_i64(), n64::assembly::cpu::RegisterNames[i],
            &R4300::state.reg.gpr[i],
        };
    }

    return create_recompiler_backend(global_definitions, REG_MAX,
                                     RECOMPILER_BLOCK_MAX,
                                     RECOMPILER_INSTR_MAX,
                                     RECOMPILER_PARAM_MAX);
}

ir_graph_t *ir_mips_disassemble(recompiler_backend_t *backend,
                                uint64_t address, unsigned char *ptr, size_t len) {
    /* Catch recompiler allocation errors. */
    if (reset_recompiler_backend(backend) < 0) {
        return NULL;
    }

    if (len > (RECOMPILER_INSTR_MAX * sizeof(uint32_t)))
        len = (RECOMPILER_INSTR_MAX * sizeof(uint32_t));

    ir_disas_region.start = address;
    ir_disas_region.end   = address + len;
    ir_disas_region.ptr   = ptr;

    ir_block_t *block      = ir_alloc_block(backend);
    ir_instr_cont_t cont = { backend, block, &block->instrs };

    ir_cop1_guard_generated = false;

    disas_push(address, cont);
    while (ir_disas_queue.length) {
        disas_pop(&address, &cont);
        if (!disas_check_address(address)) {
            /* The address is outside the specified region,
             * emit a emulation exit to return to the interpreter. */
            ir_append_write_i64(&cont, REG_PC, ir_make_const_u64(address));
            ir_mips_commit_cycles(&cont);
            ir_append_exit(&cont);
        }
        else if (!disas_fetch(address, cont)) {
            /* The continuation is automatically filled if the instruction
             * was already disassembled. Otherwise read the instruction word
             * and procude the IR. NB: delay instructions, which are
             * disassembled directly in the branch handlers, are purposefully
             * not added to the map as the control flow would be incorrect. */
            uint32_t instr = disas_read_instr(address);
            ir_instr_t *entry = disas_instr(&cont, address, instr, false);
            disas_map(address, entry);
        }
    }

    return ir_make_graph(backend);
}
