
#include <cstring>
#include <cstdarg>
#include "buffer.h"

namespace RSP {

buffer::buffer(unsigned char *ptr, size_t capacity)
    : ptr(ptr), capacity(capacity), len(0)
{
}

void buffer::clear()
{
    len = 0;
}

void buffer::append(char const *str)
{
    size_t strLen = strlen(str);
    if (strLen > capacity - len)
        return; // throw

    memcpy(&ptr[len], str, strLen);
    len += strLen;
}

// void buffer::append(char const *fmt, ...)
// {
// }

static unsigned char encodeHexDigit(unsigned char digit)
{
    return (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
}

void buffer::appendU8(u8 val)
{
    if (2 > capacity - len)
        return; // throw

    ptr[len++] = encodeHexDigit(val / 16);
    ptr[len++] = encodeHexDigit(val % 16);
}

void buffer::appendU16(u16 val)
{
    appendU8(val >> 8);
    appendU8(val);
}

void buffer::appendU32(u32 val)
{
    appendU16(val >> 16);
    appendU16(val);
}

void buffer::appendU64(u64 val)
{
    appendU32(val >> 32);
    appendU32(val);
}

};
