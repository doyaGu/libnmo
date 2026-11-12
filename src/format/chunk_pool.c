/**
 * @file chunk_pool.c
 * @brief Chunk memory pool implementation
 *
 * Available chunks form a free list through their entries, and an
 * open-addressed table maps each chunk's address to its entry, so acquiring,
 * releasing and validating a chunk take constant time however many chunks the
 * pool holds. Everything lives in the pool's arena: a grown array leaves the
 * old one there until the arena goes.
 */

#include "format/nmo_chunk_pool.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define NMO_POOL_NONE SIZE_MAX

/**
 * @brief Pool entry for tracking chunks
 */
typedef struct nmo_pool_entry {
    nmo_chunk_t *chunk;     /**< Chunk pointer */
    size_t next_free;       /**< Next available entry, NMO_POOL_NONE at the end */
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
    size_t free_head;           /**< First available entry, NMO_POOL_NONE if none */
    size_t *slots;              /**< Chunk address to entry index + 1; 0 is empty */
    size_t slot_mask;           /**< Slot count - 1; the count is a power of two */
    nmo_arena_t *arena;         /**< Arena for allocations */
};

static size_t pool_hash(const nmo_chunk_t *chunk) {
    uint64_t v = (uint64_t)(uintptr_t)chunk;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    return (size_t)v;
}

static void pool_slot_insert(nmo_chunk_pool_t *pool, size_t index) {
    size_t slot = pool_hash(pool->entries[index].chunk) & pool->slot_mask;
    while (pool->slots[slot] != 0) {
        slot = (slot + 1) & pool->slot_mask;
    }
    pool->slots[slot] = index + 1;
}

static size_t pool_find(const nmo_chunk_pool_t *pool, const nmo_chunk_t *chunk) {
    if (pool->slots == NULL) {
        return NMO_POOL_NONE;
    }
    size_t slot = pool_hash(chunk) & pool->slot_mask;
    while (pool->slots[slot] != 0) {
        size_t index = pool->slots[slot] - 1;
        if (pool->entries[index].chunk == chunk) {
            return index;
        }
        slot = (slot + 1) & pool->slot_mask;
    }
    return NMO_POOL_NONE;
}

/* Sizes the address table for `capacity` entries (at most half full) and
 * fills it with the entries the pool has. */
static int pool_build_slots(nmo_chunk_pool_t *pool, size_t capacity) {
    size_t slot_count = 16;
    while (slot_count / 2 < capacity) {
        if (slot_count > SIZE_MAX / 2 / sizeof(size_t)) {
            return 0;
        }
        slot_count *= 2;
    }
    size_t *slots = (size_t *)nmo_arena_alloc(
        pool->arena, slot_count * sizeof(size_t), _Alignof(size_t));
    if (slots == NULL) {
        return 0;
    }
    memset(slots, 0, slot_count * sizeof(size_t));
    pool->slots = slots;
    pool->slot_mask = slot_count - 1;
    for (size_t i = 0; i < pool->count; i++) {
        pool_slot_insert(pool, i);
    }
    return 1;
}

/**
 * @brief Create chunk pool
 */
NMO_API nmo_chunk_pool_t *nmo_chunk_pool_create(
    size_t initial_capacity,
    nmo_arena_t *arena
) {
    if (!arena || initial_capacity == 0 ||
        initial_capacity > SIZE_MAX / sizeof(nmo_pool_entry_t)) {
        return NULL;
    }

    nmo_chunk_pool_t *pool = (nmo_chunk_pool_t *)nmo_arena_alloc(
        arena,
        sizeof(nmo_chunk_pool_t),
        _Alignof(nmo_chunk_pool_t)
    );
    if (!pool) {
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));
    pool->arena = arena;
    pool->free_head = NMO_POOL_NONE;

    pool->entries = (nmo_pool_entry_t *)nmo_arena_alloc(
        arena,
        initial_capacity * sizeof(nmo_pool_entry_t),
        _Alignof(nmo_pool_entry_t)
    );
    if (!pool->entries || !pool_build_slots(pool, initial_capacity)) {
        return NULL;
    }
    pool->capacity = initial_capacity;
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

    if (pool->free_head != NMO_POOL_NONE) {
        nmo_pool_entry_t *entry = &pool->entries[pool->free_head];
        pool->free_head = entry->next_free;
        entry->next_free = NMO_POOL_NONE;
        entry->in_use = 1;
        pool->available--;
        reset_chunk(entry->chunk);
        return entry->chunk;
    }

    if (pool->count >= pool->capacity) {
        if (pool->capacity > SIZE_MAX / 2 / sizeof(nmo_pool_entry_t)) {
            return NULL;
        }
        size_t new_capacity = pool->capacity * 2;
        nmo_pool_entry_t *new_entries = (nmo_pool_entry_t *)nmo_arena_alloc(
            pool->arena,
            new_capacity * sizeof(nmo_pool_entry_t),
            _Alignof(nmo_pool_entry_t)
        );
        if (!new_entries) {
            return NULL;
        }
        memcpy(new_entries, pool->entries, pool->count * sizeof(nmo_pool_entry_t));
        nmo_pool_entry_t *old_entries = pool->entries;
        pool->entries = new_entries;
        if (!pool_build_slots(pool, new_capacity)) {
            pool->entries = old_entries;
            return NULL;
        }
        pool->capacity = new_capacity;
    }

    nmo_chunk_t *chunk = nmo_chunk_create(pool->arena);
    if (!chunk) {
        return NULL;
    }

    nmo_pool_entry_t *entry = &pool->entries[pool->count];
    entry->chunk = chunk;
    entry->next_free = NMO_POOL_NONE;
    entry->in_use = 1;
    pool_slot_insert(pool, pool->count);
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

    size_t index = pool_find(pool, chunk);
    if (index == NMO_POOL_NONE || !pool->entries[index].in_use) {
        /* Released twice, or never acquired from this pool. */
        assert(0 && "Chunk not found in pool or already released");
        return;
    }
    nmo_pool_entry_t *entry = &pool->entries[index];
    entry->in_use = 0;
    entry->next_free = pool->free_head;
    pool->free_head = index;
    pool->available++;
    reset_chunk(chunk);
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
    size_t index = pool_find(pool, chunk);
    return index != NMO_POOL_NONE ? pool->entries[index].in_use : 0;
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

    /* Every chunk becomes available, the first acquired first again. */
    for (size_t i = 0; i < pool->count; i++) {
        pool->entries[i].in_use = 0;
        pool->entries[i].next_free = i + 1 < pool->count ? i + 1 : NMO_POOL_NONE;
        reset_chunk(pool->entries[i].chunk);
    }
    pool->free_head = pool->count > 0 ? 0 : NMO_POOL_NONE;
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
        pool->slots = NULL;
        pool->count = 0;
        pool->available = 0;
        pool->capacity = 0;
        pool->free_head = NMO_POOL_NONE;
        pool->arena = NULL;
    }
}
