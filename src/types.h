
#ifndef _TYPE_H_INCLUDED_
#define _TYPE_H_INCLUDED_

#include <cstdint>

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

#endif /* _TYPE_H_INCLUDED_ */
