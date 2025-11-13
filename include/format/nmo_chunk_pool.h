#ifndef NMO_CHUNK_POOL_H
#define NMO_CHUNK_POOL_H

#include "nmo_chunk.h"
#include "core/nmo_arena.h"

/**
 * @file nmo_chunk_pool.h
 * @brief Chunk memory pool for efficient chunk reuse
 *
 * Provides a memory pool for nmo_chunk_t objects to reduce allocation
 * overhead when creating/destroying many chunks. Useful for:
 * - Loading files with many small chunks
 * - Temporary chunk operations
 * - High-frequency chunk creation/destruction
 *
 * Reference: VxMemoryPool.cpp, CKMemoryPool.h
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct nmo_chunk_pool nmo_chunk_pool_t;

/**
 * @brief Create chunk pool
 *
 * Creates a pool that can hold up to initial_capacity chunks.
 * Pool will grow automatically if needed.
 *
 * @param initial_capacity Initial pool capacity (recommended: 64-256)
 * @param arena Arena for pool memory allocation
 * @return Pool or NULL on error
 */
NMO_API nmo_chunk_pool_t *nmo_chunk_pool_create(
    size_t initial_capacity,
    nmo_arena_t *arena
);

/**
 * @brief Acquire chunk from pool
 *
 * Gets a pre-allocated chunk from the pool, or allocates a new one
 * if pool is empty. Chunk is initialized to zero state.
 *
 * @param pool Pool to acquire from
 * @return Chunk or NULL on allocation failure
 */
NMO_API nmo_chunk_t *nmo_chunk_pool_acquire(
    nmo_chunk_pool_t *pool
);

/**
 * @brief Release chunk back to pool
 *
 * Returns a chunk to the pool for reuse. The chunk's memory is NOT freed,
 * but it's marked as available for reuse. Do not use the chunk after
 * releasing it.
 *
 * Note: Chunk contents are reset to zero state for next acquire.
 *
 * @param pool Pool to release to
 * @param chunk Chunk to release (must have been acquired from this pool)
 */
NMO_API void nmo_chunk_pool_release(
    nmo_chunk_pool_t *pool,
    nmo_chunk_t *chunk
);

/**
 * @brief Get pool statistics
 *
 * @param pool Pool to query
 * @param out_total Output total chunks allocated
 * @param out_available Output available chunks in pool
 * @param out_in_use Output chunks currently in use
 */
NMO_API void nmo_chunk_pool_get_stats(
    const nmo_chunk_pool_t *pool,
    size_t *out_total,
    size_t *out_available,
    size_t *out_in_use
);

/**
 * @brief Clear all chunks in pool
 *
 * Marks all chunks as available for reuse. Does not free memory.
 * Useful when you know all acquired chunks are no longer in use.
 *
 * @param pool Pool to clear
 */
NMO_API void nmo_chunk_pool_clear(
    nmo_chunk_pool_t *pool
);

/**
 * @brief Destroy pool
 *
 * Since pool uses arena allocation, this is mostly a no-op.
 * Arena cleanup will handle memory deallocation.
 *
 * @param pool Pool to destroy
 */
NMO_API void nmo_chunk_pool_destroy(
    nmo_chunk_pool_t *pool
);

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_POOL_H
