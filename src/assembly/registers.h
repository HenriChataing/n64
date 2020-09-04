
#ifndef _ASSEMBLY_REGISTERS_H_INCLUDED_
#define _ASSEMBLY_REGISTERS_H_INCLUDED_

namespace n64::assembly {
namespace cpu {

enum Cop0Register {
    Index = 0,
    Random = 1,
    EntryLo0 = 2,
    EntryLo1 = 3,
    Context = 4,
    PageMask = 5,
    Wired = 6,
    BadVAddr = 8,
    Count = 9,
    EntryHi = 10,
    Compare = 11,
    SR = 12,
    Cause = 13,
    EPC = 14,
    PrId = 15,
    Config = 16,
    LLAddr = 17,
    WatchLo = 18,
    WatchHi = 19,
    XContext = 20,
    PErr = 26,
    CacheErr = 27,
    TagLo = 28,
    TagHi = 29,
    ErrorEPC = 30,
};

extern const char *RegisterNames[32];
extern const char *Cop0RegisterNames[32];

static inline const char *getRegisterName(unsigned nr) {
    return nr >= 32 ? "$?" : RegisterNames[nr];
}
static inline const char *getCop0RegisterName(unsigned nr) {
    return nr >= 32 ? "$c?" : Cop0RegisterNames[nr];
}

}; /* namespace cpu */

namespace rsp {

extern const char *Cop0RegisterNames[32];

static inline const char *getCop0RegisterName(unsigned nr) {
    return nr >= 32 ? "$c?" : Cop0RegisterNames[nr];
}

}; /* namespace rsp */
}; /* namespace n64::assembly */

#endif /* _ASSEMBLY_REGISTERS_H_INCLUDED_ */
