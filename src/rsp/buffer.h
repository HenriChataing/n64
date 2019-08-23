
#ifndef _RSP_BUFFER_H_INCLUDED_
#define _RSP_BUFFER_H_INCLUDED_

#include <stdlib.h>
#include "types.h"

namespace RSP {

struct buffer {
    unsigned char *ptr;
    size_t capacity;
    size_t len;

    buffer(unsigned char *ptr, size_t capacity);
    void clear();
    void append(char const *str);
    void appendU8(u8 val);
    void appendU16(u16 val);
    void appendU32(u32 val);
    void appendU64(u64 val);
};

};

#endif /* _RSP_BUFFER_H_INCLUDED_ */
