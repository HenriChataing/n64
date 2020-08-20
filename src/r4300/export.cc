
#include <r4300/export.h>

using namespace R4300;
namespace R4300 {

static void serialize_u32(FILE *of, u32 v) {
    u8 bytes[] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8),  (u8)(v >> 0), };
    std::fwrite(&bytes, 1, sizeof(bytes), of);
}

static void deserialize_u32(FILE *of, u32 &v) {
    u8 bytes[4] = { 0 };
    size_t res = std::fread(bytes, 1, sizeof(bytes), of);
    if (res == sizeof(bytes)) {
        v = ((u32)bytes[0] << 24) |
            ((u32)bytes[1] << 16) |
            ((u32)bytes[2] << 8) |
            ((u32)bytes[3] << 0);
    } else {
        v = 0;
    }
}

static void deserialize_u32(unsigned char const **ptr, u32 &v) {
    u8 const *bytes = *ptr;
    v = ((u32)bytes[0] << 24) |
        ((u32)bytes[1] << 16) |
        ((u32)bytes[2] << 8) |
        ((u32)bytes[3] << 0);
    *ptr = bytes + 4;
}

static void serialize_u64(FILE *of, u64 v) {
    u8 bytes[] = {
        (u8)(v >> 56), (u8)(v >> 48), (u8)(v >> 40), (u8)(v >> 32),
        (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8),  (u8)(v >> 0),
    };
    std::fwrite(&bytes, 1, sizeof(bytes), of);
}

static void deserialize_u64(FILE *of, u64 &v) {
    u8 bytes[8] = { 0 };
    size_t res = std::fread(bytes, 1, sizeof(bytes), of);
    if (res == sizeof(bytes)) {
        v = ((u64)bytes[0] << 56) | ((u64)bytes[1] << 48) |
            ((u64)bytes[2] << 40) | ((u64)bytes[3] << 32) |
            ((u64)bytes[4] << 24) | ((u64)bytes[5] << 16) |
            ((u64)bytes[6] << 8)  | ((u64)bytes[7] << 0);
    } else {
        v = 0;
    }
}

static void deserialize_u64(unsigned char const **ptr, u64 &v) {
    u8 const *bytes = *ptr;
    v = ((u64)bytes[0] << 56) | ((u64)bytes[1] << 48) |
        ((u64)bytes[2] << 40) | ((u64)bytes[3] << 32) |
        ((u64)bytes[4] << 24) | ((u64)bytes[5] << 16) |
        ((u64)bytes[6] << 8)  | ((u64)bytes[7] << 0);
    *ptr = bytes + 8;
}

size_t serializedCpuRegistersSize(void) {
    return 34*8;
}

size_t serializedCp0RegistersSize(void) {
    return 12*4 + 8*8;
}

size_t serializedCp1RegistersSize(void) {
    return 32*8 + 2*4;
}

void serializeCpuRegisters(FILE *of, struct cpureg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        serialize_u64(of, reg.gpr[r]);
    }
    serialize_u64(of, reg.multHi);
    serialize_u64(of, reg.multLo);
}

void serializeCp0Registers(FILE *of, struct cp0reg &reg) {
    serialize_u32(of, reg.index);
    serialize_u32(of, reg.random);
    serialize_u64(of, reg.entrylo0);
    serialize_u64(of, reg.entrylo1);
    serialize_u64(of, reg.context);
    serialize_u32(of, reg.pagemask);
    serialize_u32(of, reg.wired);
    serialize_u64(of, reg.badvaddr);
    serialize_u32(of, reg.count);
    serialize_u64(of, reg.entryhi);
    serialize_u32(of, reg.compare);
    serialize_u32(of, reg.sr);
    serialize_u32(of, reg.cause);
    serialize_u64(of, reg.epc);
    serialize_u32(of, reg.prid);
    serialize_u32(of, reg.config);
    serialize_u64(of, reg.xcontext);
    serialize_u32(of, reg.taglo);
    serialize_u32(of, reg.taghi);
    serialize_u64(of, reg.errorepc);
}

void serializeCp1Registers(FILE *of, struct cp1reg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        serialize_u64(of, reg.fpr[r]);
    }
    serialize_u32(of, reg.fcr0);
    serialize_u32(of, reg.fcr31);
}

void deserializeCpuRegisters(FILE *of, struct cpureg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize_u64(of, reg.gpr[r]);
    }
    deserialize_u64(of, reg.multHi);
    deserialize_u64(of, reg.multLo);
}

void deserializeCp0Registers(FILE *of, struct cp0reg &reg) {
    deserialize_u32(of, reg.index);
    deserialize_u32(of, reg.random);
    deserialize_u64(of, reg.entrylo0);
    deserialize_u64(of, reg.entrylo1);
    deserialize_u64(of, reg.context);
    deserialize_u32(of, reg.pagemask);
    deserialize_u32(of, reg.wired);
    deserialize_u64(of, reg.badvaddr);
    deserialize_u32(of, reg.count);
    deserialize_u64(of, reg.entryhi);
    deserialize_u32(of, reg.compare);
    deserialize_u32(of, reg.sr);
    deserialize_u32(of, reg.cause);
    deserialize_u64(of, reg.epc);
    deserialize_u32(of, reg.prid);
    deserialize_u32(of, reg.config);
    deserialize_u64(of, reg.xcontext);
    deserialize_u32(of, reg.taglo);
    deserialize_u32(of, reg.taghi);
    deserialize_u64(of, reg.errorepc);
}

void deserializeCp1Registers(FILE *of, struct cp1reg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize_u64(of, reg.fpr[r]);
    }
    deserialize_u32(of, reg.fcr0);
    deserialize_u32(of, reg.fcr31);
}

void deserializeCpuRegisters(unsigned char const *ptr, struct cpureg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize_u64(&ptr, reg.gpr[r]);
    }
    deserialize_u64(&ptr, reg.multHi);
    deserialize_u64(&ptr, reg.multLo);
}

void deserializeCp0Registers(unsigned char const *ptr, struct cp0reg &reg) {
    deserialize_u32(&ptr, reg.index);
    deserialize_u32(&ptr, reg.random);
    deserialize_u64(&ptr, reg.entrylo0);
    deserialize_u64(&ptr, reg.entrylo1);
    deserialize_u64(&ptr, reg.context);
    deserialize_u32(&ptr, reg.pagemask);
    deserialize_u32(&ptr, reg.wired);
    deserialize_u64(&ptr, reg.badvaddr);
    deserialize_u32(&ptr, reg.count);
    deserialize_u64(&ptr, reg.entryhi);
    deserialize_u32(&ptr, reg.compare);
    deserialize_u32(&ptr, reg.sr);
    deserialize_u32(&ptr, reg.cause);
    deserialize_u64(&ptr, reg.epc);
    deserialize_u32(&ptr, reg.prid);
    deserialize_u32(&ptr, reg.config);
    deserialize_u64(&ptr, reg.xcontext);
    deserialize_u32(&ptr, reg.taglo);
    deserialize_u32(&ptr, reg.taghi);
    deserialize_u64(&ptr, reg.errorepc);
}

void deserializeCp1Registers(unsigned char const *ptr, struct cp1reg &reg) {
    for (unsigned r = 0; r < 32; r++) {
        deserialize_u64(&ptr, reg.fpr[r]);
    }
    deserialize_u32(&ptr, reg.fcr0);
    deserialize_u32(&ptr, reg.fcr31);
}

}; /* namespace R4300 */
