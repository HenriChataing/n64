
#ifndef _TEST_RECOMPILER_TEST_BLOCKS_H_INCLUDED_
#define _TEST_RECOMPILER_TEST_BLOCKS_H_INCLUDED_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct ir_block {
    uint64_t address;
    unsigned char *ptr;
    size_t len;
} ir_block_t;

typedef struct ir_block_list {
    unsigned count;
    ir_block_t *blocks;
} ir_block_list_t;

extern ir_block_list_t ir_mips_recompiler_tests;

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _TEST_RECOMPILER_TEST_BLOCKS_H_INCLUDED_ */
