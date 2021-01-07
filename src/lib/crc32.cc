
#include <stdbool.h>
#include "crc32.h"

/** Lookup table. */
static bool poly8_lookup_init;
static uint32_t poly8_lookup[256];

/** Fill the lookup table. */
void fill_poly8_lookup(void) {
    if (poly8_lookup_init) {
        return;
    }

    for (unsigned i = 0; i < 256; i++) {
        uint32_t rem = i;  /* remainder from polynomial division */
        for (unsigned j = 0; j < 8; j++) {
            if (rem & 1) {
                rem >>= 1;
                rem ^= UINT32_C(0xedb88320);
            } else {
                rem >>= 1;
            }
        }
        poly8_lookup[i] = rem;
    }

    poly8_lookup_init = true;
}

/**
 * Calculate the checksum of a buffer.
 * @param ptr       Pointer to the input buffer.
 * @param len       Length in bytes of the input buffer.
 */
uint32_t calculate_crc32(unsigned char *ptr, size_t len) {
    fill_poly8_lookup();

    uint32_t crc = ~UINT32_C(0xffffffff);
    for (unsigned i = 0; i < len; i++) {
        crc = poly8_lookup[(crc & 0xff) ^ ptr[i]] ^ (crc >> 8);
    }

    return ~crc;
}
