
#ifndef _RECOMPILER_CACHE_H_INCLUDED_
#define _RECOMPILER_CACHE_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>

#include <recompiler/code_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct recompiler_cache recompiler_cache_t;

/**
 * @brief Allocate a recompiler cache.
 * @param page_size
 *      Size in bytes of recompiler cache buckets. Must be a power of two
 *      larger than 4096. Code blocks which fall into the same bucket
 *      are recompiled to the same code buffer. Buckets are invalidated
 *      as a whole.
 * @param page_count
 *      Size in pages of the valid memory address range.
 * @param code_buffer_size
 *      Size in bytes of the code buffer allocated for each bucket.
 * @param map_size
 *      Size in number of elements of the hash map allocated to lookup
 *      code addresses within each bucket.
 */
recompiler_cache_t *alloc_recompiler_cache(size_t page_size,
                                           size_t page_count,
                                           size_t code_buffer_size,
                                           size_t map_size);

/**
 * @brief Destroy a recompiler cache.
 * @param cache         Code cache to destroy
 */
void free_recompiler_cache(recompiler_cache_t *cache);

/**
 * @brief Push code to the code cache.
 * @param cache         Code cache
 * @param address       Address of the newly compiled code block
 * @param entry         Entry address of the newly compiled code block
 * @return              -1 on failure, 0 on success
 */
int update_recompiler_cache(recompiler_cache_t *cache,
                            uint64_t address,
                            code_entry_t entry);

/**
 * @brief Invalidate a segment of the code cache.
 * Note that addresses outside the selected interval can
 * also be invalidated depending on the cache geometry.
 * @param cache         Code cache
 * @param start_address Address to the first memory location being invalidated
 * @param end_address   Address to the memory location directly following the
 *                      memory being invalidated
 */
void invalidate_recompiler_cache(recompiler_cache_t *cache,
                                 uint64_t start_address,
                                 uint64_t end_address);

/**
 * @brief Query the code cache.
 * @param cache         Code cache
 * @param address       Address to query
 * @param emitter
 *      Returns the emitter to use on success.
 *      The emitter associated to a code address is always the same.
 * @return
 *      The entry address of the pre-compiled binary code for the code block
 *      starting at address \p address if present, NULL otherwise.
 *      The pointer to the dedicated code emitter is always returned
 *      in \p emitter.
 */
code_entry_t query_recompiler_cache(recompiler_cache_t *cache,
                                    uint64_t address,
                                    code_buffer_t **emitter);

#ifdef __cplusplus
}; /* extern "C" */
#endif /* __cplusplus */

#endif /* _RECOMPILER_CACHE_H_INCLUDED_ */
