
#include <iomanip>
#include <iostream>
#include <cstring>

#include <r4300/cpu.h>
#include <r4300/hw.h>
#include <r4300/state.h>
#include <assembly/disassembler.h>
#include <assembly/opcodes.h>
#include <assembly/registers.h>
#include <interpreter.h>

#include <memory.h>
#include <debugger.h>

using namespace R4300;
using namespace n64;

namespace interpreter::cpu {

/**
 * 1. Index
 *
 * The Index register has 6 bits to specifiy an entry into the
 * on-chip TLB. The higher order bit indicates the success of a
 * previous TLBP instruction.
 *
 *  + [31] P
 *      Result of last probe operation. Set to 1 if the last TLBP
 *      instruction was unsuccessful
 *  + [30:6] 0
 *  + [5:0] index
 *      Index to entry in TLB
 *
 * 2. Random
 *
 * 3. EntryLo0, EntryLo1
 *
 * The EntryLo0 and EntryLo1 are a pair of registers used to access an
 * on-chip TLB. EntryLo0 is used for even virtual pages while EntryLo1
 * is used for odd pages. They contain the Page Frame Number along
 * with some configuration bits.
 *
 *  + [31:30] 0
 *  + [29:6] PFN
 *      Physical Frame Number
 *  + [5:3] C
 *      Cache algorithm (011 = cached, 010 = uncached)
 *  + [2] D
 *      Dirty bit
 *  + [1] V
 *      Valid bit
 *  + [0] G
 *      Global bit. If set in both EntryLo0 and EntryLo1, then ignore
 *      ASID
 *
 * 4. Context
 *
 * 5. PageMask
 *
 * The PageMask register is used as source or destination when reading
 * or writing an on-chip TLB.
 *
 *  + [24:13] MASK
 *      Mask for virtual page number. For R4300i this is 0000_0000_0000
 *      for 4K pages, up to 1111_1111_1111 for 16M pages.
 *
 * 6. Wired
 *
 * Specifies the boundary between wired and random entries of the TLB.
 *
 * 7. BadVAddr
 *
 * 8. Count
 *
 * The Count register acts like a timer incrementing at a constant
 * rate (half the maximum instruction issue rate).
 *
 * 9. EntryHi
 *
 * Register used to access the TLB.
 *
 *  + [31:13] VPN2
 *      Virtual Page Number divided by 2
 *  + [12:8] 0
 *  + [7:0] ASID
 *      Address Space Identifier
 *
 * 10. Compare
 *
 * Compare register acts as a timer (see also the Count register).
 *
 * It maintains a stable value that does not change on its own.
 * When the value of the Count register equals the value of the Compare
 * register, interrupt bit IP(7) in the Cause register is set.
 * This causes an interrupt as soon as the interrupt is enabled.
 *
 * 11. Status
 *
 * 12. Cause
 *
 * The Cause register describes the cause of the most
 * recent exception.
 *
 *  + [31] BD
 *      Indicates whether the last exception taken occurred in a
 *      branch delay slot (1-> delay slot, 0 -> normal)
 *  + [30] 0
 *  + [29:28] CE
 *      Coprocessor unit referenced when a Coprocessor Unusable
 *      exception is taken
 *  + [27:16] 0
 *  + [15:8] IP0-7
 *      Indicates if an instruction is pending (1 -> pending, 0 -> no)
 *  + [7] 0
 *  + [6:2] ExcCode
 *      Exception code
 *  + [1:0] 0
 *
 * 13. EPC
 *
 * The EPC register contains the address at which instruction
 * processing may resume after servicing an exception.
 *
 * 14. PrId
 * 15. Config
 * 15. LLAddr
 * 15. WatchLo
 * 15. WatchHi
 * 15. XContext
 * 15. PErr
 * 15. CacheErr
 *
 * 16. TagLo, TagHi
 *
 * The TagLo and TagHi registers are 32-bit read/write registers that
 * hold either the primary cache tag and parity, or the secondary cache
 * tag and ECC during cache initialization, cache diagnostics, or cache
 * error processing.
 *
 * 17. ErrorEPC
 */

/** @brief Interpret a MFC0 instruction. */
void eval_MFC0(u32 instr) {
    using namespace assembly::cpu;
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 val;

    switch (rd) {
        case Index:     val = state.cp0reg.index; break;
        case Random:    val = state.cp0reg.random; break;
        case EntryLo0:  val = state.cp0reg.entrylo0; break;
        case EntryLo1:  val = state.cp0reg.entrylo1; break;
        case Context:   val = state.cp0reg.context; break;
        case PageMask:  val = state.cp0reg.pagemask; break;
        case Wired:     val = state.cp0reg.wired; break;
        case BadVAddr:  val = state.cp0reg.badvaddr; break;
        case Count:
            val = state.cp0reg.count +
                  (state.cycles - state.cp0reg.lastCounterUpdate) / 2;
            break;
        case EntryHi:   val = state.cp0reg.entryhi; break;
        case Compare:   val = state.cp0reg.compare; break;
        case SR:        val = state.cp0reg.sr; break;
        case Cause:     val = state.cp0reg.cause; break;
        case EPC:       val = state.cp0reg.epc; break;
        case PrId:
            val = state.cp0reg.prid;
            debugger::halt("MFC0 prid");
            break;
        case Config:
            val = state.cp0reg.config;
            debugger::halt("MFC0 config");
            break;
        case LLAddr:
            val = state.cp0reg.lladdr;
            debugger::halt("MFC0 lladdr");
            break;
        case WatchLo:
            val = state.cp0reg.watchlo;
            debugger::halt("MFC0 watchlo");
            break;
        case WatchHi:
            val = state.cp0reg.watchhi;
            debugger::halt("MFC0 watchhi");
            break;
        case XContext:
            val = state.cp0reg.xcontext;
            debugger::halt("MFC0 xcontext");
            break;
        case PErr:
            val = state.cp0reg.perr;
            debugger::halt("MFC0 perr");
            break;
        case CacheErr:
            val = state.cp0reg.cacheerr;
            debugger::halt("MFC0 cacheerr");
            break;
        case TagLo:     val = state.cp0reg.taglo; break;
        case TagHi:     val = state.cp0reg.taghi; break;
        case ErrorEPC:  val = state.cp0reg.errorepc; break;
        default:
            val = 0;
            std::string reason = "MFC0 ";
            debugger::halt(reason + assembly::cpu::Cop0RegisterNames[rd]);
            break;
    }

    debugger::info(Debugger::COP0, "{} -> {:08x}",
        assembly::cpu::Cop0RegisterNames[rd], val);
    state.reg.gpr[rt] = sign_extend<u64, u32>(val);
}

/** @brief Interpret a DMFC0 instruction. */
void eval_DMFC0(u32 instr) {
    using namespace assembly::cpu;
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u64 val = 0;

    switch (rd) {
        /* 64bit registers */
        case EntryLo0:  val = state.cp0reg.entrylo0; break;
        case EntryLo1:  val = state.cp0reg.entrylo1; break;
        case Context:
            val = state.cp0reg.context;
            debugger::halt("DMFC0 context");
            break;
        case BadVAddr:  val = state.cp0reg.badvaddr; break;
        case EntryHi:   val = state.cp0reg.entryhi; break;
        case EPC:       val = state.cp0reg.epc; break;
        case XContext:
            val = state.cp0reg.xcontext;
            debugger::halt("DMFC0 xcontext");
            break;
        case ErrorEPC:  val = state.cp0reg.errorepc; break;
        /* 32bit registers */
        case Count:
            val = zero_extend<u64, u32>(
                state.cp0reg.count +
                (state.cycles - state.cp0reg.lastCounterUpdate) / 2);
            break;
        default:
            val = 0;
            std::string reason = "DMFC0 ";
            debugger::halt(reason + assembly::cpu::Cop0RegisterNames[rd] +
                " (undefined)");
            break;
    }

    debugger::info(Debugger::COP0, "{} -> {:08x}",
        assembly::cpu::Cop0RegisterNames[rd], val);
    state.reg.gpr[rt] = val;
}

/** @brief Interpret a MTC0 instruction. */
void eval_MTC0(u32 instr) {
    using namespace assembly::cpu;
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u32 val = state.reg.gpr[rt];

    debugger::info(Debugger::COP0, "{} <- {:08x}",
        assembly::cpu::Cop0RegisterNames[rd], val);

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
            debugger::halt(reason + assembly::cpu::Cop0RegisterNames[rd]);
            break;
    }
}

/** @brief Interpret a DMTC0 instruction. */
void eval_DMTC0(u32 instr) {
    using namespace assembly::cpu;
    u32 rt = assembly::getRt(instr);
    u32 rd = assembly::getRd(instr);
    u64 val = state.reg.gpr[rt];

    debugger::info(Debugger::COP0, "{} <- {:08x}",
        assembly::cpu::Cop0RegisterNames[rd], val);

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
            debugger::halt(reason + assembly::cpu::Cop0RegisterNames[rd] +
                " (undefined)");
            break;
    }
}

/** @brief Interpret a CFC0 instruction. */
void eval_CFC0(u32 instr) {
    (void)instr;
    debugger::halt("CFC0");
}

/** @brief Interpret a CTC0 instruction. */
void eval_CTC0(u32 instr) {
    (void)instr;
    debugger::halt("CTC0");
}

/** @brief Interpret the TLBR instruction. */
void eval_TLBR(u32 instr) {
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

/** @brief Interpret the TLBWI or TLBWR instruction. */
void eval_TLBW(u32 instr) {
    u32 funct = assembly::getFunct(instr);
    unsigned index;

    if (funct == assembly::TLBWI) {
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

/** @brief Interpret the TLBP instruction. */
void eval_TLBP(u32 instr) {
    (void)instr;
    unsigned index;
    state.cp0reg.index = INDEX_P;
    if (probeTLB(state.cp0reg.entryhi, &index)) {
        state.cp0reg.index = index;
    }
}

/** @brief Interpret the ERET instruction. */
void eval_ERET(u32 instr) {
    if (state.cp0reg.ERL()) {
        state.cpu.nextAction = State::Action::Jump;
        state.cpu.nextPc = state.cp0reg.errorepc;
        state.cp0reg.sr &= ~STATUS_ERL;
    } else {
        state.cpu.nextAction = State::Action::Jump;
        state.cpu.nextPc = state.cp0reg.epc;
        state.cp0reg.sr &= ~STATUS_EXL;
    }
    // Clearing the exception flag may have unmasked a
    // pending interrupt.
    checkInterrupt();
}

void eval_COP0(u32 instr)
{
    switch (assembly::getRs(instr)) {
        case assembly::MFCz:          eval_MFC0(instr); break;
        case assembly::DMFCz:         eval_DMFC0(instr); break;
        case assembly::MTCz:          eval_MTC0(instr); break;
        case assembly::DMTCz:         eval_DMTC0(instr); break;
        case assembly::CFCz:          eval_CFC0(instr); break;
        case assembly::CTCz:          eval_CTC0(instr); break;
        case 0x10u:
            switch (assembly::getFunct(instr)) {
                case assembly::TLBR:  eval_TLBR(instr); break;
                case assembly::TLBWI: eval_TLBW(instr); break;
                case assembly::TLBWR: eval_TLBW(instr); break;
                case assembly::TLBP:  eval_TLBP(instr); break;
                case assembly::ERET:  eval_ERET(instr); break;
                default:
                    debugger::halt("COP0 unsupported COFUN instruction");
                    break;
            }
            break;
        default:
            debugger::halt("COP0 unsupported instruction");
            break;
    }
}

}; /* namespace interpreter::cpu */
