
#include <iostream>
#include <r4300/export.h>

using namespace R4300;
namespace R4300 {

template <typename T>
static void serialize(std::ostream &os, T v) {
    static_assert(std::is_unsigned<T>::value,
                  "serialize expects unsigned integral types");
    size_t vs = sizeof(v);
    char bytes[vs];
    for (unsigned b = 0; b < vs; b++) {
        bytes[b] = (char)(v >> ((vs - b - 1) * 8));
    }
    os.write(bytes, vs);
}

template <typename T>
static void deserialize(std::istream &is, T &v) {
    static_assert(std::is_unsigned<T>::value,
                  "deserialize expects unsigned integral types");
    v = 0;
    size_t vs = sizeof(v);
    char bytes[vs];
    is.read(bytes, vs);
    for (unsigned b = 0; b < vs; b++) {
        v |= (T)(unsigned char)bytes[b] << ((vs - b - 1) * 8);
    }
}

void serialize(std::ostream &os, const struct cpureg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        serialize<u64>(os, reg.gpr[r]);
    }
    serialize<u64>(os, reg.multHi);
    serialize<u64>(os, reg.multLo);
}

void serialize(std::ostream &os, const struct cp0reg &reg) {
    serialize<u32>(os, reg.index);
    serialize<u32>(os, reg.random);
    serialize<u64>(os, reg.entrylo0);
    serialize<u64>(os, reg.entrylo1);
    serialize<u64>(os, reg.context);
    serialize<u32>(os, reg.pagemask);
    serialize<u32>(os, reg.wired);
    serialize<u64>(os, reg.badvaddr);
    serialize<u32>(os, reg.count);
    serialize<u64>(os, reg.entryhi);
    serialize<u32>(os, reg.compare);
    serialize<u32>(os, reg.sr);
    serialize<u32>(os, reg.cause);
    serialize<u64>(os, reg.epc);
    serialize<u32>(os, reg.prid);
    serialize<u32>(os, reg.config);
    serialize<u64>(os, reg.xcontext);
    serialize<u32>(os, reg.taglo);
    serialize<u32>(os, reg.taghi);
    serialize<u64>(os, reg.errorepc);
}

void serialize(std::ostream &os, const struct cp1reg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        serialize<u64>(os, reg.fpr[r]);
    }
    serialize<u32>(os, reg.fcr0);
    serialize<u32>(os, reg.fcr31);
}

void deserialize(std::istream &is, struct cpureg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize<u64>(is, reg.gpr[r]);
    }
    deserialize<u64>(is, reg.multHi);
    deserialize<u64>(is, reg.multLo);
}

void deserialize(std::istream &is, struct cp0reg &reg) {
    deserialize<u32>(is, reg.index);
    deserialize<u32>(is, reg.random);
    deserialize<u64>(is, reg.entrylo0);
    deserialize<u64>(is, reg.entrylo1);
    deserialize<u64>(is, reg.context);
    deserialize<u32>(is, reg.pagemask);
    deserialize<u32>(is, reg.wired);
    deserialize<u64>(is, reg.badvaddr);
    deserialize<u32>(is, reg.count);
    deserialize<u64>(is, reg.entryhi);
    deserialize<u32>(is, reg.compare);
    deserialize<u32>(is, reg.sr);
    deserialize<u32>(is, reg.cause);
    deserialize<u64>(is, reg.epc);
    deserialize<u32>(is, reg.prid);
    deserialize<u32>(is, reg.config);
    deserialize<u64>(is, reg.xcontext);
    deserialize<u32>(is, reg.taglo);
    deserialize<u32>(is, reg.taghi);
    deserialize<u64>(is, reg.errorepc);
}

void deserialize(std::istream &is, struct cp1reg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize<u64>(is, reg.fpr[r]);
    }
    deserialize<u32>(is, reg.fcr0);
    deserialize<u32>(is, reg.fcr31);
}

}; /* namespace R4300 */
