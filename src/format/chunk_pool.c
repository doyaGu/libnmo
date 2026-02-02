/**
 * @file chunk_pool.c
 * @brief Chunk memory pool implementation
 */

#include "format/nmo_chunk_pool.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <assert.h>

/**
 * @brief Pool entry for tracking chunks
 */
typedef struct nmo_pool_entry {
    nmo_chunk_t *chunk;     /**< Chunk pointer */
    int in_use;             /**< 1 if acquired, 0 if available */
} nmo_pool_entry_t;

/**
 * @brief Chunk memory pool structure
 */
struct nmo_chunk_pool {
    nmo_pool_entry_t *entries;  /**< Array of pool entries */
    size_t capacity;            /**< Total capacity */
    size_t count;               /**< Number of entries (total chunks) */
    size_t available;           /**< Number of available chunks */
    nmo_arena_t *arena;         /**< Arena for allocations */
};

/**
 * @brief Create chunk pool
 */
NMO_API nmo_chunk_pool_t *nmo_chunk_pool_create(
    size_t initial_capacity,
    nmo_arena_t *arena
) {
    if (!arena || initial_capacity == 0) {
        return NULL;
    }

    // Allocate pool structure
    nmo_chunk_pool_t *pool = (nmo_chunk_pool_t *)nmo_arena_alloc(
        arena,
        sizeof(nmo_chunk_pool_t),
        _Alignof(nmo_chunk_pool_t)
    );
    if (!pool) {
        return NULL;
    }

    // Allocate entries array
    pool->entries = (nmo_pool_entry_t *)nmo_arena_alloc(
        arena,
        initial_capacity * sizeof(nmo_pool_entry_t),
        _Alignof(nmo_pool_entry_t)
    );
    if (!pool->entries) {
        return NULL;
    }

    // Initialize pool state
    memset(pool->entries, 0, initial_capacity * sizeof(nmo_pool_entry_t));
    pool->capacity = initial_capacity;
    pool->count = 0;
    pool->available = 0;
    pool->arena = arena;
    return pool;
}

/**
 * @brief Reset chunk to initial state
 */
static void reset_chunk(nmo_chunk_t *chunk) {
    if (!chunk) return;

    // Clear data buffer (keep capacity)
    chunk->data.count = 0;
    if (chunk->data.data) {
        memset(chunk->data.data, 0, chunk->data.capacity * chunk->data.element_size);
    }

    // Clear tracking lists
    chunk->ids.count = 0;
    chunk->chunks.count = 0;
    chunk->chunk_refs.count = 0;
    chunk->managers.count = 0;

    // Reset metadata
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 7; // Default VERSION4
    chunk->chunk_class_id = 0;
    chunk->chunk_options = 0;

    // Clear compression info
    chunk->uncompressed_size = 0;
    chunk->compressed_size = 0;
    chunk->is_compressed = 0;
    chunk->unpack_size = 0;

    // Clear raw data
    chunk->raw_data = NULL;
    chunk->raw_size = 0;

    // Clear parser state
    if (chunk->parser_state) {
        memset(chunk->parser_state, 0, sizeof(nmo_chunk_parser_state_t));
    }
}

/**
 * @brief Acquire chunk from pool
 */
NMO_API nmo_chunk_t *nmo_chunk_pool_acquire(
    nmo_chunk_pool_t *pool
) {
    if (!pool) {
        return NULL;
    }

    // First, try to find an available chunk
    for (size_t i = 0; i < pool->count; i++) {
        if (!pool->entries[i].in_use) {
            pool->entries[i].in_use = 1;
            pool->available--;

            nmo_chunk_t *chunk = pool->entries[i].chunk;
            reset_chunk(chunk);
            return chunk;
        }
    }

    // No available chunk, need to allocate a new one
    if (pool->count >= pool->capacity) {
        // Pool is full, need to expand
        // NOTE: Since we use arena allocation, the old entries array cannot be freed.
        // This is a known memory leak in the arena allocator pattern. The memory will
        // be reclaimed when the arena itself is destroyed. For production use, consider
        // using malloc/free for the entries array or implement a bump allocator with
        // explicit free support.
        size_t new_capacity = pool->capacity * 2;
        nmo_pool_entry_t *new_entries = (nmo_pool_entry_t *)nmo_arena_alloc(
            pool->arena,
            new_capacity * sizeof(nmo_pool_entry_t),
            _Alignof(nmo_pool_entry_t)
        );
        if (!new_entries) {
            return NULL; // Out of memory
        }

        // Copy old entries
        memcpy(new_entries, pool->entries,
               pool->count * sizeof(nmo_pool_entry_t));

        // Clear new entries
        memset(new_entries + pool->count, 0,
               (new_capacity - pool->count) * sizeof(nmo_pool_entry_t));

        pool->entries = new_entries;
        pool->capacity = new_capacity;
    }

    // Allocate new chunk
    nmo_chunk_t *chunk = nmo_chunk_create(pool->arena);
    if (!chunk) {
        return NULL;
    }

    // Add to pool
    pool->entries[pool->count].chunk = chunk;
    pool->entries[pool->count].in_use = 1;
    pool->count++;

    return chunk;
}

/**
 * @brief Release chunk back to pool
 */
NMO_API void nmo_chunk_pool_release(
    nmo_chunk_pool_t *pool,
    nmo_chunk_t *chunk
) {
    if (!pool || !chunk) {
        return;
    }

    // Find the chunk in the pool
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->entries[i].chunk == chunk && pool->entries[i].in_use) {
            pool->entries[i].in_use = 0;
            pool->available++;
            reset_chunk(chunk);
            return;
        }
    }

    // Chunk not found in pool - this is an error condition
    // The chunk may have been already released or was never acquired from this pool
    assert(0 && "Chunk not found in pool or already released");
}

/**
 * @brief Validate chunk pointer (detect use-after-release)
 *
 * @param pool Pool to validate against
 * @param chunk Chunk to validate
 * @return 1 if chunk is valid and in use, 0 if released or invalid
 */
NMO_API int nmo_chunk_pool_validate(
    const nmo_chunk_pool_t *pool,
    const nmo_chunk_t *chunk
) {
    if (!pool || !chunk) {
        return 0;
    }

    for (size_t i = 0; i < pool->count; i++) {
        if (pool->entries[i].chunk == chunk) {
            return pool->entries[i].in_use;
        }
    }

    return 0; // Not found in pool
}

/**
 * @brief Get pool statistics
 */
NMO_API void nmo_chunk_pool_get_stats(
    const nmo_chunk_pool_t *pool,
    size_t *out_total,
    size_t *out_available,
    size_t *out_in_use
) {
    if (!pool) {
        if (out_total) *out_total = 0;
        if (out_available) *out_available = 0;
        if (out_in_use) *out_in_use = 0;
        return;
    }

    if (out_total) {
        *out_total = pool->count;
    }
    if (out_available) {
        *out_available = pool->available;
    }
    if (out_in_use) {
        *out_in_use = pool->count - pool->available;
    }
}

/**
 * @brief Clear all chunks in pool
 */
NMO_API void nmo_chunk_pool_clear(
    nmo_chunk_pool_t *pool
) {
    if (!pool) {
        return;
    }

    // Mark all chunks as available
    for (size_t i = 0; i < pool->count; i++) {
        pool->entries[i].in_use = 0;
        reset_chunk(pool->entries[i].chunk);
    }

    pool->available = pool->count;
}

/**
 * @brief Destroy pool
 */
NMO_API void nmo_chunk_pool_destroy(
    nmo_chunk_pool_t *pool
) {
    // Since we use arena allocation, just clear references
    // The arena itself will handle memory cleanup
    if (pool) {
        pool->entries = NULL;
        pool->count = 0;
        pool->available = 0;
        pool->capacity = 0;
        pool->arena = NULL;
    }
}
