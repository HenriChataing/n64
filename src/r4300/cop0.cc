
#include <iomanip>
#include <iostream>
#include <cstring>

#include <memory.h>
#include <mips/asm.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include "cpu.h"

using namespace R4300;

namespace R4300 {

class Cop0 : public Coprocessor
{
public:
    enum Register {
        /**
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
         */
        Index = 0,
        Random = 1,
        /**
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
         */
        EntryLo0 = 2,
        EntryLo1 = 3,
        Context = 4,
        /**
         * The PageMask register is used as source or destination when reading
         * or writing an on-chip TLB.
         *
         *  + [24:13] MASK
         *      Mask for virtual page number. For R4300i this is 0000_0000_0000
         *      for 4K pages, up to 1111_1111_1111 for 16M pages.
         */
        PageMask = 5,
        Wired = 6,
        CPR7 = 7,
        BadVAddr = 8,
        /**
         * The Count register acts like a timer incrementing at a constant
         * rate (half the maximum instruction issue rate).
         */
        Count = 9,
        /**
         * Register used to access the TLB.
         *
         *  + [31:13] VPN2
         *      Virtual Page Number divided by 2
         *  + [12:8] 0
         *  + [7:0] ASID
         *      Address Space Identifier
         */
        EntryHi = 10,
        /**
         * Compare register acts as a timer (see also the Count register).
         *
         * It maintains a stable value that does not change on its own.
         * When the value of the Count register equals the value of the Compare
         * register, interrupt bit IP(7) in the Cause register is set.
         * This causes an interrupt as soon as the interrupt is enabled.
         */
        Compare = 11,
        SR = 12,
        /**
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
         */
        Cause = 13,
        /**
         * The EPC register contains the address at which instruction
         * processing may resume after servicing an exception.
         */
        EPC = 14,
        PrId = 15,
        Config = 16,
        LLAddr = 17,
        WatchLo = 18,
        WatchHi = 19,
        XContext = 20,
        CPR21 = 21,
        CPR22 = 22,
        CPR23 = 23,
        CPR24 = 24,
        CPR25 = 25,
        PErr = 26,
        CacheErr = 27,
        /*
         * The TagLo and TagHi registers are 32-bit read/write registers that
         * hold either the primary cache tag and parity, or the secondary cache
         * tag and ECC during cache initialization, cache diagnostics, or cache
         * error processing.
         */
        TagLo = 28,
        TagHi = 29,
        ErrorEPC = 30,
        CPR31 = 31,
    };

    Cop0() {}
    ~Cop0() {}

    static inline void logWrite(const char *tag, u64 value)
    {
#if 1
        std::cerr << "\x1b[34;1m"; // blue
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << " <- " << std::hex << value;
        std::cerr << "\x1b[0m" << std::endl;
#endif
    }

    static inline void logRead(const char *tag, u64 value)
    {
#if 0
        std::cerr << "\x1b[34;1m"; // blue
        std::cerr << std::left << std::setfill(' ') << std::setw(32);
        std::cerr << tag << " -> " << std::hex << value;
        std::cerr << "\x1b[0m" << std::endl;
#endif
    }

    virtual void cofun(u32 instr)
    {
        // std::cerr << "COP0 COFUN " << instr << std::endl;
        uint i;
        u32 op = Mips::getFunct(instr);
        switch (op) {
            case Mips::Cop0::TLBR:
                i = state.cp0reg.index & 0x3flu;
                if (i > tlbEntryCount)
                    break;
                state.cp0reg.pageMask = state.tlb[i].pageMask;
                state.cp0reg.entryHi = state.tlb[i].entryHi;
                state.cp0reg.entryLo0 = state.tlb[i].entryLo0;
                state.cp0reg.entryLo1 = state.tlb[i].entryLo1;
                break;

            case Mips::Cop0::TLBWI:
            case Mips::Cop0::TLBWR:
                if (op == Mips::Cop0::TLBWI) {
                    i = state.cp0reg.index & 0x3flu;
                    if (i > tlbEntryCount)
                        break;
                } else {
                    i = state.cp0reg.random;
                    state.cp0reg.random = (i + tlbEntryCount - 1) % tlbEntryCount;
                }

                state.tlb[i].pageMask = state.cp0reg.pageMask & 0x1ffe000llu;
                state.tlb[i].entryHi = state.cp0reg.entryHi;
                state.tlb[i].entryLo0 = state.cp0reg.entryLo0;
                state.tlb[i].entryLo1 = state.cp0reg.entryLo1;
                state.tlb[i].asid = state.cp0reg.entryHi & 0xfflu;
                state.tlb[i].global =
                    (state.cp0reg.entryLo0 & 1lu) &&
                    (state.cp0reg.entryLo1 & 1lu);
                break;

            case Mips::Cop0::TLBP:
                state.cp0reg.index = 0x80000000lu;
                if (probeTLB(state.cp0reg.entryHi, &i))
                    state.cp0reg.index = i;
                break;

            case Mips::Cop0::ERET:
                if (ERL()) {
                    state.reg.pc = state.cp0reg.errorEpc;
                    state.cp0reg.sr &= ~(1lu << 2);
                    state.branch = true;
                } else {
                    state.reg.pc = state.cp0reg.epc;
                    state.cp0reg.sr &= ~(1lu << 1);
                    state.branch = true;
                }
                break;

            default:
                throw "UndefinedCopO";
                break;
        }
    }

    virtual u64 read(uint bytes, u32 rd, bool ctrl)
    {
        if (ctrl)
            throw "Nonexisting COP0 control register";

        switch (rd) {
            case Index:
                logRead("COP0_Index", state.cp0reg.index);
                return state.cp0reg.index;
            case Random:
                logRead("COP0_Random", state.cp0reg.random);
                return state.cp0reg.random;
            case EntryLo0:
                logRead("COP0_EntryLo0", state.cp0reg.entryLo0);
                return state.cp0reg.entryLo0;
            case EntryLo1:
                logRead("COP0_EntryLo1", state.cp0reg.entryLo1);
                return state.cp0reg.entryLo1;
            case Context:
                logRead("COP0_Context", state.cp0reg.context);
                throw "ReadContext";
                break;
            case PageMask:
                logRead("COP0_PageMask", state.cp0reg.pageMask);
                return state.cp0reg.pageMask;
            case Wired:
                logRead("COP0_Wired", state.cp0reg.wired);
                throw "ReadWired";
                break;
            case CPR7:
                logRead("COP0_CPR7", 0);
                throw "ReadCPR7";
                break;
            case BadVAddr:
                logRead("COP0_BadVAddr", state.cp0reg.badVAddr);
                return state.cp0reg.badVAddr;
            case Count:
                logRead("COP0_Count", state.cp0reg.count);
                return state.cp0reg.count;
            case EntryHi:
                logRead("COP0_EntryHi", state.cp0reg.entryHi);
                return state.cp0reg.entryHi;
            case Compare:
                logRead("COP0_Compare", state.cp0reg.compare);
                return state.cp0reg.compare;
            case SR:
                logRead("COP0_SR", state.cp0reg.sr);
                return state.cp0reg.sr;
            case Cause:
                logRead("COP0_Cause", state.cp0reg.cause);
                return state.cp0reg.cause;
            case EPC:
                logRead("COP0_EPC", state.cp0reg.epc);
                return state.cp0reg.epc;
            case PrId:
                logRead("COP0_PrId", state.cp0reg.prId);
                throw "ReadPrId";
                break;
            case Config:
                logRead("COP0_Config", state.cp0reg.config);
                throw "ReadConfig";
                break;
            case LLAddr:
                logRead("COP0_LLAddr", state.cp0reg.llAddr);
                throw "ReadLLAddr";
                break;
            case WatchLo:
                logRead("COP0_WatchLo", state.cp0reg.watchLo);
                throw "ReadWatchLo";
                break;
            case WatchHi:
                logRead("COP0_WatchHi", state.cp0reg.watchHi);
                throw "ReadWatchHi";
                break;
            case XContext:
                logRead("COP0_XContext", state.cp0reg.xContext);
                throw "ReadXContext";
                break;
            case CPR21:
                logRead("COP0_CPR21", 0);
                throw "ReadCPR21";
                break;
            case CPR22:
                logRead("COP0_CPR22", 0);
                throw "ReadCPR22";
                break;
            case CPR23:
                logRead("COP0_CPR23", 0);
                throw "ReadCPR23";
                break;
            case CPR24:
                logRead("COP0_CPR24", 0);
                throw "ReadCPR24";
                break;
            case CPR25:
                logRead("COP0_CPR25", 0);
                throw "ReadCPR25";
                break;
            case PErr:
                logRead("COP0_PErr", state.cp0reg.pErr);
                throw "ReadPErr";
                break;
            case CacheErr:
                logRead("COP0_CacheErr", state.cp0reg.cacheErr);
                throw "ReadCacheErr";
                break;
            case TagLo:
                logRead("COP0_TagLo", state.cp0reg.tagLo);
                return state.cp0reg.tagLo;
            case TagHi:
                logRead("COP0_TagHi", state.cp0reg.tagHi);
                return state.cp0reg.tagHi;
            case ErrorEPC:
                logRead("COP0_ErrorEPC", state.cp0reg.errorEpc);
                throw "ReadErrPC";
                break;
            case CPR31:
                logRead("COP0_CPR31", 0);
                throw "ReadCPR31";
                break;
        }
        return 0;
    }

    virtual void write(uint bytes, u32 rd, u64 imm, bool ctrl)
    {
        if (ctrl)
            throw "Nonexisting COP0 control register";
        switch (rd) {
            case Index:
                logWrite("COP0_Index", imm);
                state.cp0reg.index |= imm & 0x3flu;
                break;
            case Random:
                logWrite("COP0_Random", imm);
                throw "WriteRandom";
                break;
            case EntryLo0:
                logWrite("COP0_EntryLo0", imm);
                state.cp0reg.entryLo0 = imm;
                break;
            case EntryLo1:
                logWrite("COP0_EntryLo1", imm);
                state.cp0reg.entryLo1 = imm;
                break;
            case Context:
                logWrite("COP0_Context", imm);
                throw "WriteContext";
                break;
            case PageMask:
                logWrite("COP0_PageMask", imm);
                state.cp0reg.pageMask = imm & 0x1ffe000lu;
                break;
            case Wired:
                logWrite("COP0_Wired", imm);
                throw "WriteWired";
                break;
            case CPR7:
                logWrite("COP0_CPR7", imm);
                throw "WriteCPR7";
                break;
            case BadVAddr:
                logWrite("COP0_BadVAddr", imm);
                throw "WriteBadVAddr";
                break;
            case Count:
                logWrite("COP0_Count", imm);
                state.cp0reg.count = imm;
                break;
            case EntryHi:
                logWrite("COP0_EntryHi", imm);
                state.cp0reg.entryHi = imm;
                break;
            case Compare:
                logWrite("COP0_Compare", imm);
                state.cp0reg.compare = imm;
                break;
            case SR:
                logWrite("COP0_SR", imm);
                state.cp0reg.sr = imm;
                // @todo check written value and consequences
                break;
            case Cause:
                logWrite("COP0_Cause", imm);
                break;
            case EPC:
                logWrite("COP0_EPC", imm);
                state.cp0reg.epc = imm;
                break;
            case PrId:
                logWrite("COP0_PrId", imm);
                throw "WritePrId";
                break;
            case Config:
                logWrite("COP0_Config", imm);
                throw "WriteConfig";
                break;
            case LLAddr:
                logWrite("COP0_LLAddr", imm);
                throw "WriteLLAddr";
                break;
            case WatchLo:
                logWrite("COP0_WatchLo", imm);
                throw "WriteWatchLo";
                break;
            case WatchHi:
                logWrite("COP0_WatchHi", imm);
                throw "WriteWatchHi";
                break;
            case XContext:
                logWrite("COP0_XContext", imm);
                throw "WriteXContext";
                break;
            case CPR21:
                logWrite("COP0_CPR21", imm);
                throw "WriteCPR21";
                break;
            case CPR22:
                logWrite("COP0_CPR22", imm);
                throw "WriteCPR22";
                break;
            case CPR23:
                logWrite("COP0_CPR23", imm);
                throw "WriteCPR23";
                break;
            case CPR24:
                logWrite("COP0_CPR24", imm);
                throw "WriteCPR24";
                break;
            case CPR25:
                logWrite("COP0_CPR25", imm);
                throw "WriteCPR25";
                break;
            case PErr:
                logWrite("COP0_PErr", imm);
                throw "WritePErr";
                break;
            case CacheErr:
                logWrite("COP0_CacheErr", imm);
                throw "WriteCacheErr";
                break;
            case TagLo:
                logWrite("COP0_TagLo", imm);
                state.cp0reg.tagLo = imm;
                break;
            case TagHi:
                logWrite("COP0_TagHi", imm);
                state.cp0reg.tagHi = imm;
                break;
            case ErrorEPC:
                logWrite("COP0_ErrorEPC", imm);
                throw "WriteErrPC";
                break;
            case CPR31:
                logWrite("COP0_CPR31", imm);
                throw "WriteCPR31";
                break;
        }
    }
};

Cop0 cop0;

}; /* namespace R4300 */
