
#include <iostream>

#include <fmt/color.h>
#include <fmt/format.h>

#include <r4300/state.h>
#include "core.h"
#include "memory.h"
#include "trace.h"

/**
 * Create a RecordBus instance whereby memory accesses are serialized to
 * the output stream \p os.
 */
RecordBus::RecordBus(unsigned bits, std::ostream *os)
    : Memory::Bus(bits), _os(os) {}

static inline void serialize(uint64_t value, unsigned char *ptr) {
    ptr[0] = (unsigned char)(value >> 56);
    ptr[1] = (unsigned char)(value >> 48);
    ptr[2] = (unsigned char)(value >> 40);
    ptr[3] = (unsigned char)(value >> 32);
    ptr[4] = (unsigned char)(value >> 24);
    ptr[5] = (unsigned char)(value >> 16);
    ptr[6] = (unsigned char)(value >> 8);
    ptr[7] = (unsigned char)(value >> 0);
}

bool RecordBus::load(unsigned bytes, u64 address, u64 *value) {
    uint64_t pc = R4300::state.reg.pc;
    uint64_t cycles = R4300::state.cycles;
    bool res = root.load(bytes, address, value);

    unsigned char buf[35];
    buf[0] = 0;
    buf[1] = bytes;
    buf[2] = res;
    serialize(address, buf + 3);
    serialize(*value, buf + 11);
    serialize(pc, buf + 19);
    serialize(cycles, buf + 27);

    _os->write((char *)buf, sizeof(buf));
    if (_os->bad()) {
        fmt::print(fmt::fg(fmt::color::tomato),
            "RecordBus::load: failed to write {} bytes to output stream\n",
            sizeof(buf));
    }

    return res;
}

bool RecordBus::store(unsigned bytes, u64 address, u64 value) {
    uint64_t pc = R4300::state.reg.pc;
    uint64_t cycles = R4300::state.cycles;
    bool res = root.store(bytes, address, value);

    unsigned char buf[35];
    buf[0] = 1;
    buf[1] = bytes;
    buf[2] = res;
    serialize(address, buf + 3);
    serialize(value, buf + 11);
    serialize(pc, buf + 19);
    serialize(cycles, buf + 27);

    _os->write((char *)buf, sizeof(buf));
    if (_os->bad()) {
        fmt::print(fmt::fg(fmt::color::tomato),
            "RecordBus::store: failed to write {} bytes to output stream\n",
            sizeof(buf));
    }

    return res;
}

/**
 * Create a ReplayBus instance whereby memory accesses are matched
 * against access deserialized from the input stream \p os.
 */
ReplayBus::ReplayBus(unsigned bits, std::istream *is)
    : Memory::Bus(bits), _is(is) {}

static inline uint64_t deserialize(unsigned char *ptr) {
    return ((uint64_t)ptr[0] << 56) |
        ((uint64_t)ptr[1] << 48) |
        ((uint64_t)ptr[2] << 40) |
        ((uint64_t)ptr[3] << 32) |
        ((uint64_t)ptr[4] << 24) |
        ((uint64_t)ptr[5] << 16) |
        ((uint64_t)ptr[6] << 8) |
        ((uint64_t)ptr[7] << 0);
}

bool ReplayBus::load(unsigned bytes, u64 address, u64 *value) {
    uint64_t pc = R4300::state.reg.pc;
    uint64_t cycles = R4300::state.cycles;
    bool res = root.load(bytes, address, value);

    unsigned char buf[35];
    _is->read((char *)buf, sizeof(buf));
    if (_is->gcount() != sizeof(buf)) {
        fmt::print(fmt::fg(fmt::color::tomato),
            "ReplayBus::load: failed to read {} bytes from input stream\n",
            sizeof(buf));
        core::halt("end of memory trace");
        return res;
    }

    uint64_t recorded_address = deserialize(buf + 3);
    uint64_t recorded_value = deserialize(buf + 11);
    uint64_t recorded_pc = deserialize(buf + 19);
    uint64_t recorded_cycles = deserialize(buf + 27);

    if (buf[0] != 0 ||
        buf[1] != bytes ||
        buf[2] != res ||
        recorded_address != address ||
        recorded_value != *value ||
        recorded_pc != pc ||
        recorded_cycles != cycles)
    {
        fmt::print(fmt::emphasis::italic,
            "ReplayBus::load: unexpected memory access:\n");
        fmt::print(fmt::emphasis::italic,
            "    played:  load_u{}(0x{:x}) -> {}, 0x{:x} @ 0x{:x}, {}\n",
            bytes * 8, address, res, *value, pc, cycles);

        if (buf[0] == 0) {
            fmt::print(fmt::emphasis::italic,
                "    expected: load_u{}(0x{:x}) -> {}, 0x{:x} @ 0x{:x}, {}\n",
                buf[1] * 8, recorded_address, (bool)buf[2], recorded_value,
                recorded_pc, recorded_cycles);
        } else if (buf[0] == 1) {
            fmt::print(fmt::emphasis::italic,
                "    expected: store_u{}(0x{:x}, 0x{:x}) -> {} @ 0x{:x}, {}\n",
                buf[1] * 8, recorded_address, recorded_value,
                (bool)buf[2], recorded_pc, recorded_cycles);
        } else {
            fmt::print(fmt::emphasis::italic,
                "    expected: unrelated event {}\n", buf[0]);
        }
        core::halt("unexpected load access");
    }

    return res;
}

bool ReplayBus::store(unsigned bytes, u64 address, u64 value) {
    uint64_t pc = R4300::state.reg.pc;
    uint64_t cycles = R4300::state.cycles;
    bool res = root.store(bytes, address, value);

    unsigned char buf[35];
    _is->read((char *)buf, sizeof(buf));
    if (_is->gcount() != sizeof(buf)) {
        fmt::print(fmt::fg(fmt::color::tomato),
            "ReplayBus::load: failed to read {} bytes from input stream\n",
            sizeof(buf));
        core::halt("end of memory trace");
        return res;
    }

    uint64_t recorded_address = deserialize(buf + 3);
    uint64_t recorded_value = deserialize(buf + 11);
    uint64_t recorded_pc = deserialize(buf + 19);
    uint64_t recorded_cycles = deserialize(buf + 27);

    if (buf[0] != 1 ||
        buf[1] != bytes ||
        buf[2] != res ||
        recorded_address != address ||
        recorded_value != value ||
        recorded_pc != pc ||
        recorded_cycles != cycles)
    {
        fmt::print(fmt::emphasis::italic,
            "ReplayBus::store: unexpected memory access:\n");
        fmt::print(fmt::emphasis::italic,
            "    played:  store_u{}(0x{:x}, 0x{:x}) -> {} @ 0x{:x}, {}\n",
            bytes * 8, address, value, res, pc, cycles);

        if (buf[0] == 0) {
            fmt::print(fmt::emphasis::italic,
                "    expected: load_u{}(0x{:x}) -> {}, 0x{:x} @ 0x{:x}, {}\n",
                buf[1] * 8, recorded_address, (bool)buf[2], recorded_value,
                recorded_pc, recorded_cycles);
        } else if (buf[0] == 1) {
            fmt::print(fmt::emphasis::italic,
                "    expected: store_u{}(0x{:x}, 0x{:x}) -> {} @ 0x{:x}, {}\n",
                buf[1] * 8, recorded_address, recorded_value,
                (bool)buf[2], recorded_pc, recorded_cycles);
        } else {
            fmt::print(fmt::emphasis::italic,
                "    expected: unrelated event {}\n", buf[0]);
        }
        core::halt("unexpected store access");
    }

    return res;
}
