
#include <stdlib.h>
#include <string.h>

#include <recompiler/cache.h>

typedef struct address_map {
    uint64_t address;
    code_entry_t entry;
} address_map_t;

struct recompiler_cache {
    size_t page_size;
    size_t page_shift;
    size_t page_count;
    size_t map_size;

    code_buffer_t *code_buffers;

    /** Hash table implemented with open addressing and linear probing. */
    address_map_t *address_maps;
};

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
                                           size_t map_size) {
    recompiler_cache_t *cache;
    code_buffer_t *code_buffers;
    address_map_t *address_maps;
    unsigned page_shift;

    if (page_size == 0) {
        return NULL;
    }

    page_shift = __builtin_ctz(page_size);
    if (page_size != (1u << page_shift)) {
        return NULL;
    }

    /* Allocate recompiler cache object. */
    cache = malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }

    /* Allocate the array of code buffers. */
    code_buffers = alloc_code_buffer_array(page_count, code_buffer_size);
    if (code_buffers == NULL) {
        free(cache);
        return NULL;
    }

    /* Allocate the array of address maps. */
    address_maps = calloc(page_count, map_size * sizeof(*address_maps));
    if (address_maps == NULL) {
        free(cache);
        free_code_buffer_array(code_buffers);
        return NULL;
    }

    /* Setup the cache object. */
    cache->page_shift = page_shift;
    cache->page_size = page_size;
    cache->page_count = page_count;
    cache->map_size = map_size;
    cache->code_buffers = code_buffers;
    cache->address_maps = address_maps;
    return cache;
}

/**
 * @brief Destroy a recompiler cache.
 * @param cache         Code cache to destroy
 */
void free_recompiler_cache(recompiler_cache_t *cache) {
    if (cache == NULL) {
        return;
    }

    free_code_buffer_array(cache->code_buffers);
    free(cache->address_maps);
    free(cache);
}

/**
 * @brief Push code to the code cache.
 * @param cache         Code cache
 * @param address       Address of the newly compiled code block
 * @param entry         Entry address of the newly compiled code block
 */
int update_recompiler_cache(recompiler_cache_t *cache,
                            uint64_t address,
                            code_entry_t entry) {
    uint64_t page_nr = address >> cache->page_shift;
    if (page_nr > cache->page_count) {
        return -1;
    }

    address_map_t *map = cache->address_maps + page_nr * cache->map_size;
    unsigned hash = (address >> 2) % cache->map_size;
    for (unsigned nr = 0; nr < cache->map_size; nr++) {
        unsigned index = (nr + hash) % cache->map_size;
        if (map[index].entry == NULL || map[index].address == address) {
            map[index].entry = entry;
            return 0;
        }
    }

    /* Map is full */
    return -1;
}

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
                                 uint64_t end_address) {
    uint64_t start_page = start_address >> cache->page_shift;
    uint64_t end_page =
        (end_address + cache->page_size - 1) >> cache->page_shift;

    if (end_page >= cache->page_count) {
        end_page = cache->page_count;
    }

    for (size_t page_nr = start_page; page_nr < end_page; page_nr++) {
        clear_code_buffer(cache->code_buffers + page_nr);
    }
    memset(cache->address_maps + start_page * cache->map_size, 0,
           (end_page - start_page) * cache->map_size * sizeof(address_map_t));
}

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
                                    code_buffer_t **emitter) {
    uint64_t page_nr = address >> cache->page_shift;
    if (page_nr > cache->page_count) {
        *emitter = NULL;
        return NULL;
    }

    address_map_t *map = cache->address_maps + page_nr * cache->map_size;
    unsigned hash = (address >> 2) % cache->map_size;
    *emitter = cache->code_buffers + page_nr;
    for (unsigned nr = 0; nr < cache->map_size; nr++) {
        unsigned index = (nr + hash) % cache->map_size;
        if (map[index].entry == NULL) {
            return NULL;
        }
        if (map[index].address == address) {
            return map[index].entry;
        }
    }

    /* Map is full, but the address is not present. */
    return NULL;
}
