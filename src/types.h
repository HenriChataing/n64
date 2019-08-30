
#ifndef _TYPE_H_INCLUDED_
#define _TYPE_H_INCLUDED_

#include <cstdint>
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

template<typename T, typename U>
static inline T zero_extend(U x) {
    static_assert(std::is_unsigned<T>::value && std::is_unsigned<U>::value,
                  "zero_extend expects unsigned integral types");
    return (T)x;
}

#endif /* _TYPE_H_INCLUDED_ */
