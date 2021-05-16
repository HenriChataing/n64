
#ifndef _TYPE_H_INCLUDED_
#define _TYPE_H_INCLUDED_

#include <cstdint>
#include <limits>
#include <type_traits>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned int uint;
typedef unsigned long ulong;

#define U64_2GB     (0x80000000llu)
#define U64_2_5GB   (0xa0000000llu)
#define U64_3GB     (0xc0000000llu)
#define U64_1TB     (0x10000000000llu)

template<typename T, typename U>
static inline T sign_extend(U x) {
    static_assert(std::is_unsigned<T>::value && std::is_unsigned<U>::value,
                  "sign_extend expects unsigned integral types");
    typename std::make_signed<U>::type y = x;
    typename std::make_signed<T>::type sy = y;
    return (T)sy;
}

static inline u32 i7_to_i32(u32 val) {
    bool neg = (val & (1lu << 6)) != 0;
    return neg ? (val | 0xffffff80lu) : val;
}

template<typename T, typename U>
static inline T zero_extend(U x) {
    static_assert(std::is_unsigned<T>::value && std::is_unsigned<U>::value,
                  "zero_extend expects unsigned integral types");
    return (T)x;
}

template<typename T, typename U>
static inline T clamp(U x) {
    static_assert(std::numeric_limits<T>::max() <=
                      std::numeric_limits<U>::max() &&
                  std::numeric_limits<T>::min() >=
                      std::numeric_limits<U>::min(),
                  "clamp expects that the range of values of the return type"
                  " be included in the range of values of the input type");

    return x > std::numeric_limits<T>::max() ? std::numeric_limits<T>::max() :
           x < std::numeric_limits<T>::min() ? std::numeric_limits<T>::min() :
           (T)x;
}

template<typename T>
static inline T read_be(uint8_t const *bytes, unsigned nr_bytes = sizeof(T)) {
    static_assert(std::is_unsigned<T>::value,
                  "read_be expects an unsigned integral type");
    T val = 0;
    for (unsigned n = 0; n < nr_bytes; n++) {
        val = (val << 8) | (T)bytes[n];
    }
    return val;
}

template<typename T>
static inline void write_be(uint8_t *bytes, T val, unsigned nr_bytes = sizeof(T)) {
    static_assert(std::is_unsigned<T>::value,
                  "read_be expects an unsigned integral type");
    for (unsigned n = 0; n < nr_bytes; n++) {
        bytes[n] = val >> (8 * (nr_bytes - n - 1));
    }
}

#endif /* _TYPE_H_INCLUDED_ */
