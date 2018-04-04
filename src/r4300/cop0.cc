
#include <iostream>
#include <cstring>

#include <memory.h>
#include <r4300/hw.h>

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
        ErrPC = 30,
        CPR31 = 31,
    };

    Cop0() {}
    ~Cop0() {}

    virtual void cofun(u32 instr)
    {
        std::cerr << "COP0 COFUN " << instr << std::endl;
        throw "COPO COFUN";
    }

    virtual u64 read(uint bytes, u32 rd, bool ctrl)
    {
        if (ctrl)
            throw "Nonexisting COP0 control register";

        switch (rd) {
            case Index:
                std::cerr << "read Index" << std::endl;
                return state.cp0reg.index;
            case Random:
                std::cerr << "read Random" << std::endl;
                throw "ReadRandom";
                break;
            case EntryLo0:
                std::cerr << "read EntryLo0" << std::endl;
                return state.cp0reg.entryLo0;
            case EntryLo1:
                std::cerr << "read EntryLo1" << std::endl;
                return state.cp0reg.entryLo1;
            case Context:
                std::cerr << "read Context" << std::endl;
                throw "ReadContext";
                break;
            case PageMask:
                std::cerr << "read PageMask" << std::endl;
                return state.cp0reg.pageMask;
            case Wired:
                std::cerr << "read Wired" << std::endl;
                throw "ReadWired";
                break;
            case CPR7:
                std::cerr << "read CPR7" << std::endl;
                throw "ReadCPR7";
                break;
            case BadVAddr:
                std::cerr << "read BadVAddr" << std::endl;
                throw "ReadBadVAddr";
                break;
            case Count:
                return state.cp0reg.count;
            case EntryHi:
                std::cerr << "read EntryHi" << std::endl;
                return state.cp0reg.entryHi;
            case Compare:
                return state.cp0reg.compare;
            case SR:
                return state.cp0reg.sr;
            case Cause:
                std::cerr << "read Cause" << std::endl;
                return 0;
            case EPC:
                std::cerr << "read EPC" << std::endl;
                throw "ReadEPC";
                break;
            case PrId:
                std::cerr << "read PrId" << std::endl;
                throw "ReadPrId";
                break;
            case Config:
                std::cerr << "read Config" << std::endl;
                throw "ReadConfig";
                break;
            case LLAddr:
                std::cerr << "read LLAddr" << std::endl;
                throw "ReadLLAddr";
                break;
            case WatchLo:
                std::cerr << "read WatchLo" << std::endl;
                throw "ReadWatchLo";
                break;
            case WatchHi:
                std::cerr << "read WatchHi" << std::endl;
                throw "ReadWatchHi";
                break;
            case XContext:
                std::cerr << "read XContext" << std::endl;
                throw "ReadXContext";
                break;
            case CPR21:
                std::cerr << "read CPR21" << std::endl;
                throw "ReadCPR21";
                break;
            case CPR22:
                std::cerr << "read CPR22" << std::endl;
                throw "ReadCPR22";
                break;
            case CPR23:
                std::cerr << "read CPR23" << std::endl;
                throw "ReadCPR23";
                break;
            case CPR24:
                std::cerr << "read CPR24" << std::endl;
                throw "ReadCPR24";
                break;
            case CPR25:
                std::cerr << "read CPR25" << std::endl;
                throw "ReadCPR25";
                break;
            case PErr:
                std::cerr << "read PErr" << std::endl;
                throw "ReadPErr";
                break;
            case CacheErr:
                std::cerr << "read CacheErr" << std::endl;
                throw "ReadCacheErr";
                break;
            case TagLo:
                return state.cp0reg.tagLo;
            case TagHi:
                return state.cp0reg.tagHi;
            case ErrPC:
                std::cerr << "read ErrPC" << std::endl;
                throw "ReadErrPC";
                break;
            case CPR31:
                std::cerr << "read CPR31" << std::endl;
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
                std::cerr << "write Index" << std::endl;
                state.cp0reg.index |= imm & 0x3flu;
                break;
            case Random:
                std::cerr << "write Random" << std::endl;
                throw "WriteRandom";
                break;
            case EntryLo0:
                std::cerr << "write EntryLo0" << std::endl;
                state.cp0reg.entryLo0 = imm;
                break;
            case EntryLo1:
                std::cerr << "write EntryLo1" << std::endl;
                state.cp0reg.entryLo1 = imm;
                break;
            case Context:
                std::cerr << "write Context" << std::endl;
                throw "WriteContext";
                break;
            case PageMask:
                std::cerr << "write PageMask" << std::endl;
                state.cp0reg.pageMask = imm & 0xfff000lu;
                break;
            case Wired:
                std::cerr << "write Wired" << std::endl;
                throw "WriteWired";
                break;
            case CPR7:
                std::cerr << "write CPR7" << std::endl;
                throw "WriteCPR7";
                break;
            case BadVAddr:
                std::cerr << "write BadVAddr" << std::endl;
                throw "WriteBadVAddr";
                break;
            case Count:
                state.cp0reg.count = imm;
                break;
            case EntryHi:
                std::cerr << "write EntryHi" << std::endl;
                state.cp0reg.entryHi = imm;
                break;
            case Compare:
                state.cp0reg.compare = imm;
                break;
            case SR:
                std::cerr << "write SR" << std::endl;
                // @todo check written value and consequences
                state.cp0reg.sr = imm;
                break;
            case Cause:
                std::cerr << "write Cause" << std::endl;
                break;
            case EPC:
                std::cerr << "write EPC" << std::endl;
                throw "WriteEPC";
                break;
            case PrId:
                std::cerr << "write PrId" << std::endl;
                throw "WritePrId";
                break;
            case Config:
                std::cerr << "write Config" << std::endl;
                throw "WriteConfig";
                break;
            case LLAddr:
                std::cerr << "write LLAddr" << std::endl;
                throw "WriteLLAddr";
                break;
            case WatchLo:
                std::cerr << "write WatchLo" << std::endl;
                throw "WriteWatchLo";
                break;
            case WatchHi:
                std::cerr << "write WatchHi" << std::endl;
                throw "WriteWatchHi";
                break;
            case XContext:
                std::cerr << "write XContext" << std::endl;
                throw "WriteXContext";
                break;
            case CPR21:
                std::cerr << "write CPR21" << std::endl;
                throw "WriteCPR21";
                break;
            case CPR22:
                std::cerr << "write CPR22" << std::endl;
                throw "WriteCPR22";
                break;
            case CPR23:
                std::cerr << "write CPR23" << std::endl;
                throw "WriteCPR23";
                break;
            case CPR24:
                std::cerr << "write CPR24" << std::endl;
                throw "WriteCPR24";
                break;
            case CPR25:
                std::cerr << "write CPR25" << std::endl;
                throw "WriteCPR25";
                break;
            case PErr:
                std::cerr << "write PErr" << std::endl;
                throw "WritePErr";
                break;
            case CacheErr:
                std::cerr << "write CacheErr" << std::endl;
                throw "WriteCacheErr";
                break;
            case TagLo:
                std::cerr << "write TagLo" << std::endl;
                state.cp0reg.tagLo = imm;
                break;
            case TagHi:
                std::cerr << "write TagHi" << std::endl;
                state.cp0reg.tagHi = imm;
                break;
            case ErrPC:
                std::cerr << "write ErrPC" << std::endl;
                throw "WriteErrPC";
                break;
            case CPR31:
                std::cerr << "write CPR31" << std::endl;
                throw "WriteCPR31";
                break;
        }
    }
};

Cop0 cop0;

}; /* namespace R4300 */
