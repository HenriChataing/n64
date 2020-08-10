
#include <recompiler/ir.h>
#include <recompiler/target/mips.h>
#include <r4300/eval.h>
#include <r4300/state.h>

#define IR_BLOCK_MAX 16
#define IR_INSTR_MAX 1024

/* Stand-in interpreter, default callback when the instruction cannot
 * be translated to IR. */
extern "C" void interpreter(uint32_t instr) {
    R4300::Eval::eval_Instr(instr);
}

extern "C" bool cpu_load_u8(uintmax_t vAddr, uint8_t *value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->load_u8(pAddr, value);
}

extern "C" bool cpu_load_u16(uintmax_t vAddr, uint16_t *value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->load_u16(pAddr, value);
}

extern "C" bool cpu_load_u32(uintmax_t vAddr, uint32_t *value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->load_u32(pAddr, value);
}

extern "C" bool cpu_load_u64(uintmax_t vAddr, uint64_t *value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->load_u64(pAddr, value);
}

extern "C" bool cpu_store_u8(uintmax_t vAddr, uint8_t value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->store_u8(pAddr, value);
}

extern "C" bool cpu_store_u16(uintmax_t vAddr, uint16_t value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->store_u16(pAddr, value);
}

extern "C" bool cpu_store_u32(uintmax_t vAddr, uint32_t value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->store_u32(pAddr, value);
}

extern "C" bool cpu_store_u64(uintmax_t vAddr, uint64_t value) {
    uint64_t pAddr;
    return R4300::translateAddress(vAddr, &pAddr, false) != R4300::Exception::None &&
           R4300::state.bus->store_u64(pAddr, value);
}

#define IR_DISAS_BRANCH_ENABLE 0
#define IR_DISAS_QUEUE_SIZE    32
#define IR_DISAS_MAP_SIZE      1024

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

static inline ir_value_t ir_mips_append_read(ir_instr_cont_t *c,
                                             ir_register_t register_) {
    return register_ ? ir_append_read(c, ir_make_iN(64), register_)
                     : ir_make_const_u64(0);
}

static inline void ir_mips_append_write(ir_instr_cont_t *c,
                                        ir_register_t register_,
                                        ir_value_t value) {
    if (register_) ir_append_write(c, register_, value);
}

static inline void ir_mips_append_interpreter(ir_instr_cont_t *c,
                                              uint64_t address,
                                              uint32_t instr) {
    ir_append_write(c, REG_PC, ir_make_const_u64(address));
    ir_append_call(c, ir_make_iN(0), (ir_callback_t)interpreter,
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
    ir_disas_entrypoint_t queue[IR_DISAS_QUEUE_SIZE];
    unsigned length;
} ir_disas_queue;

/** Map address offsets to disassembled instructions. */
static struct {
    ir_instr_t     *map[IR_DISAS_MAP_SIZE];
    unsigned        base;
} ir_disas_map;

static ir_instr_t *disas_instr(ir_instr_cont_t *c, uint64_t address, uint32_t instr);
static void append_instr(ir_instr_cont_t *c, uint64_t address, uint32_t instr);

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
        ir_mips_append_write(c, REG_PC, ir_make_const_u64(address));
        ir_append_exit(c);
        return false;
    }
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
static void disas_branch(ir_instr_cont_t *c, ir_value_t cond,
                         uint64_t address, uint32_t instr) {
    uint64_t imm = mips_get_imm_u64(instr);
    ir_instr_cont_t br;
    append_instr(c, address + 4, disas_read_instr(address + 4));
    ir_append_br(c, cond, &br);
#if IR_DISAS_BRANCH_ENABLE
    disas_push(address + 4 + (imm << 2), br);
    disas_push(address + 8, *c);
#else
    ir_append_write(c, REG_PC,
        ir_make_const_u64(address + 8));
    ir_append_exit(c);
    ir_append_write(&br, REG_PC,
        ir_make_const_u64(address + 4 + (imm << 2)));
    ir_append_exit(&br);
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
    ir_instr_cont_t br;
    ir_append_br(c, cond, &br);
    append_instr(&br, address + 4, disas_read_instr(address + 4));

#if IR_DISAS_BRANCH_ENABLE
    disas_push(address + 4 + (imm << 2), br);
    disas_push(address + 8, *c);
#else
    ir_append_write(c, REG_PC,
        ir_make_const_u64(address + 8));
    ir_append_exit(c);
    ir_append_write(&br, REG_PC,
        ir_make_const_u64(address + 4 + (imm << 2)));
    ir_append_exit(&br);
#endif /* DISAS_BRANCH_ENABLE */
}

/* Reserved opcodes */

static void disas_Reserved(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

/* SPECIAL opcodes */

static void disas_ADD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow exception
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    vd = ir_append_binop(c, IR_ADD, vs, vt);
    vd = ir_append_sext(c,  ir_make_iN(64), vd);
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
    (void)instr; (void)c;
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
    append_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, vs);
    ir_append_exit(c);
}

static void disas_JR(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    if (!disas_guard_branch_delay(c, address))
        return;
    ir_value_t vs;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    append_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, vs);
    ir_append_exit(c);
}

static void disas_MFHI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vd;
    vd = ir_append_read(c, ir_make_iN(64), REG_MULTHI);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_MFLO(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vd;
    vd = ir_append_read(c, ir_make_iN(64), REG_MULTLO);
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
    vd = ir_append_binop(c, IR_SMUL, vs, vt);

    multhi = ir_append_binop(c, IR_SRL, vd, ir_make_const_u64(32));
    multhi = ir_append_trunc(c, ir_make_iN(32), multhi);
    multhi = ir_append_zext(c, ir_make_iN(64), multhi);
    ir_mips_append_write(c, REG_MULTHI, multhi);

    multlo = ir_append_trunc(c, ir_make_iN(32), vd);
    multlo = ir_append_zext(c, ir_make_iN(64), multlo);
    ir_mips_append_write(c, REG_MULTLO, multlo);
    disas_push(address + 4, *c);
}

static void disas_MULTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd, multhi, multlo;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u64(0xfffffffflu));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_binop(c, IR_AND, vt, ir_make_const_u64(0xfffffffflu));
    vd = ir_append_binop(c, IR_UMUL, vs, vt);

    multhi = ir_append_binop(c, IR_SRL, vd, ir_make_const_u64(32));
    multhi = ir_append_trunc(c, ir_make_iN(32), multhi);
    multhi = ir_append_zext(c, ir_make_iN(64), multhi);
    ir_mips_append_write(c, REG_MULTHI, multhi);

    multlo = ir_append_trunc(c, ir_make_iN(32), vd);
    multlo = ir_append_zext(c, ir_make_iN(64), multlo);
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
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SLL, vt, shamnt);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    vd = ir_append_binop(c, IR_SLL, vt, vs);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_icmp(c, IR_SLT, vs, vt);
    vd = ir_append_zext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SLTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vd = ir_append_icmp(c, IR_ULT, vs, vt);
    vd = ir_append_zext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRA(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRA, vt, shamnt);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRAV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    vd = ir_append_binop(c, IR_SRA, vt, vs);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vt, shamnt, vd;
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    shamnt = ir_make_const_u32(mips_get_shamnt(instr));
    vd = ir_append_binop(c, IR_SRL, vt, shamnt);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SRLV(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    vs = ir_append_binop(c, IR_AND, vs, ir_make_const_u32(0x1f));
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    vd = ir_append_binop(c, IR_SRL, vt, vs);
    vd = ir_append_sext(c, ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SUB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow exception
    ir_value_t vs, vt, vd;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    vd = ir_append_binop(c, IR_SUB, vs, vt);
    vd = ir_append_sext(c,  ir_make_iN(64), vd);
    ir_mips_append_write(c, mips_get_rd(instr), vd);
    disas_push(address + 4, *c);
}

static void disas_SUBU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    disas_SUB(c, address, instr);
}

static void disas_SYNC(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_SYSCALL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // RType(instr);
    // takeR4300::Exception(SystemCall, 0, false, false, 0);
}

static void disas_TEQ(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TGE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TGEU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TLT(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TLTU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TNE(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
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
    (void)instr; (void)c;
}

static void disas_TGEI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TGEIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TLTI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TLTIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

static void disas_TNEI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    (void)instr; (void)c;
}

/* Other opcodes */

static void disas_ADDI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    // TODO integer overflow
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    vs = ir_append_trunc(c, ir_make_iN(32), vs);
    imm = ir_make_const_u32(mips_get_imm_u32(instr));
    vt = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_sext(c,  ir_make_iN(64), vt);
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
    (void)instr; (void)c;
}

static void disas_COP0(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 2);
}

static void disas_COP1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 3);
}

static void disas_COP2(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 2);
}

static void disas_COP3(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_mips_append_interpreter(c, address, instr);
    disas_push(address + 4, *c);
    // takeR4300::Exception(CoprocessorUnusable, 0, false, false, 3);
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
    uint64_t target = mips_get_target(instr);
    target = (address & 0xfffffffff0000000llu) | (target << 2);
    append_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(target));
    ir_append_exit(c);
}

static void disas_JAL(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    uint64_t target = mips_get_target(instr);
    target = (address & 0xfffffffff0000000llu) | (target << 2);
    ir_mips_append_write(c, REG_RA, ir_make_const_u64(address + 8));
    append_instr(c, address + 4, disas_read_instr(address + 4));
    ir_mips_append_write(c, REG_PC, ir_make_const_u64(target));
    ir_append_exit(c);
}

static void disas_LB(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_load(c, ir_make_iN(8), vs);
    vt = ir_append_sext(c, ir_make_iN(64), vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LBU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_load(c, ir_make_iN(8), vs);
    vt = ir_append_zext(c, ir_make_iN(64), vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LD(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_load(c, ir_make_iN(64), vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LDC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
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
    vt = ir_append_load(c, ir_make_iN(16), vs);
    vt = ir_append_sext(c, ir_make_iN(64), vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LHU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_append_load(c, ir_make_iN(16), vs);
    vt = ir_append_zext(c, ir_make_iN(64), vs);
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
    vt = ir_append_load(c, ir_make_iN(32), vs);
    vt = ir_append_sext(c, ir_make_iN(64), vs);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_LWC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
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
    vt = ir_append_load(c, ir_make_iN(64), vs);
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
    vt = ir_append_trunc(c, ir_make_iN(8), vt);
    ir_append_store(c, vs, vt);
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
    ir_append_store(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SDC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
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
    vt = ir_append_trunc(c, ir_make_iN(16), vt);
    ir_append_store(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SLTI(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vt = ir_append_icmp(c, IR_SLT, vs, imm);
    vt = ir_append_zext(c, ir_make_iN(64), vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_SLTIU(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vt = ir_append_icmp(c, IR_ULT, vs, imm);
    vt = ir_append_zext(c, ir_make_iN(64), vt);
    ir_mips_append_write(c, mips_get_rt(instr), vt);
    disas_push(address + 4, *c);
}

static void disas_SW(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_value_t vs, imm, vt;
    vs = ir_mips_append_read(c, mips_get_rs(instr));
    imm = ir_make_const_u64(mips_get_imm_u64(instr));
    vs = ir_append_binop(c, IR_ADD, vs, imm);
    vt = ir_mips_append_read(c, mips_get_rt(instr));
    vt = ir_append_trunc(c, ir_make_iN(32), vt);
    ir_append_store(c, vs, vt);
    disas_push(address + 4, *c);
}

static void disas_SWC1(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
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

static ir_instr_t *disas_instr(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    ir_instr_t *entry = NULL;
    ir_instr_cont_t entryc = { c->backend, c->block, &entry };
    CPU_callbacks[(instr >> 26) & 0x3fu](&entryc, address, instr);
    *c->next = entry;
    c->next = entryc.next;
    return entry;
}

static void append_instr(ir_instr_cont_t *c, uint64_t address, uint32_t instr) {
    unsigned prev_length = ir_disas_queue.length;
    (void)disas_instr(c, address, instr);
    ir_disas_queue.length = prev_length;
}

ir_recompiler_backend_t *ir_mips_recompiler_backend(void) {
    ir_memory_backend_t memory_backend = {
        cpu_load_u8,
        cpu_load_u16,
        cpu_load_u32,
        cpu_load_u64,
        cpu_store_u8,
        cpu_store_u16,
        cpu_store_u32,
        cpu_store_u64,
    };
    ir_recompiler_backend_t *backend =
        ir_create_recompiler_backend(&memory_backend, REG_MAX,
                                     IR_BLOCK_MAX, IR_INSTR_MAX);
    for (unsigned i = 1; i < 32; i++) {
        ir_bind_register_u64(backend, i, &R4300::state.reg.gpr[i]);
    }
    ir_bind_register_u64(backend, REG_PC, &R4300::state.reg.pc);
    ir_bind_register_u64(backend, REG_MULTHI, &R4300::state.reg.multHi);
    ir_bind_register_u64(backend, REG_MULTLO, &R4300::state.reg.multLo);
    return backend;
}

ir_graph_t *ir_mips_disassemble(ir_recompiler_backend_t *backend,
                                uint64_t address, unsigned char *ptr, size_t len) {
    if (len > (IR_DISAS_MAP_SIZE * sizeof(uint32_t)))
        len = (IR_DISAS_MAP_SIZE * sizeof(uint32_t));

    ir_disas_region.start = address;
    ir_disas_region.end   = address + len;
    ir_disas_region.ptr   = ptr;

    ir_block_t *block      = ir_alloc_block(backend);
    ir_instr_cont_t cont = { backend, block, &block->instrs };

    disas_push(address, cont);
    while (ir_disas_queue.length) {
        disas_pop(&address, &cont);
        if (!disas_check_address(address)) {
            /* The address is outside the specified region,
             * emit a emulation exit to return to the interpreter. */
            ir_append_write(&cont, REG_PC, ir_make_const_u64(address));
            ir_append_exit(&cont);
        }
        else if (!disas_fetch(address, cont)) {
            /* The continuation is automatically filled if the instruction
             * was already disassembled. Otherwise read the instruction word
             * and procude the IR. NB: delay instructions, which are
             * disassembled directly in the branch handlers, are purposefully
             * not added to the map as the control flow would be incorrect. */
            uint32_t instr = disas_read_instr(address);
            ir_instr_t *entry = disas_instr(&cont, address, instr);
            disas_map(address, entry);
        }
    }

    return ir_make_graph(backend);
}
