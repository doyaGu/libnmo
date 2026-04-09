#ifndef NMO_POOL_H
#define NMO_POOL_H

#include "nmo_types.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_pool.h
 * @brief Fixed-size memory pool allocator for frequent small allocations.
 *
 * @note Thread safety: This module is not thread-safe. Synchronize access.
 */

/**
 * @brief Pool statistics snapshot.
 */
typedef struct nmo_pool_stats {
    size_t block_size;    /**< Size of each block in bytes. */
    size_t capacity;      /**< Total number of blocks allocated. */
    size_t in_use;        /**< Blocks currently checked out. */
    size_t peak_in_use;   /**< Peak in-use count. */
} nmo_pool_stats_t;

/**
 * @brief Opaque pool type.
 */
typedef struct nmo_pool nmo_pool_t;

/**
 * @brief Create a fixed-size pool.
 *
 * @param allocator Allocator for pool storage (NULL for default).
 * @param block_size Size of each block in bytes.
 * @param initial_capacity Initial number of blocks to allocate.
 * @return Pool instance or NULL on failure.
 * @ownership owned
 */
NMO_API nmo_pool_t *nmo_pool_create(const nmo_allocator_t *allocator,
                                    size_t block_size,
                                    size_t initial_capacity);

/**
 * @brief Destroy a pool and release all memory.
 */
NMO_API void nmo_pool_destroy(nmo_pool_t *pool);

/**
 * @brief Allocate a block from the pool.
 *
 * @return Pointer to block or NULL on failure.
 * @ownership borrowed
 */
NMO_API void *nmo_pool_alloc(nmo_pool_t *pool);

/**
 * @brief Return a block to the pool.
 */
NMO_API void nmo_pool_free(nmo_pool_t *pool, void *ptr);

/**
 * @brief Reset the pool, returning all blocks to the free list.
 */
NMO_API void nmo_pool_reset(nmo_pool_t *pool);

/**
 * @brief Fetch pool statistics.
 */
NMO_API nmo_pool_stats_t nmo_pool_get_stats(const nmo_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* NMO_POOL_H */
