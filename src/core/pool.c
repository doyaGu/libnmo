/**
 * @file pool.c
 * @brief Fixed-size memory pool allocator.
 */

#include "core/nmo_pool.h"
#include <string.h>

typedef struct nmo_pool_chunk {
    struct nmo_pool_chunk *next;
} nmo_pool_chunk_t;

typedef struct nmo_pool_free_node {
    struct nmo_pool_free_node *next;
} nmo_pool_free_node_t;

struct nmo_pool {
    nmo_allocator_t allocator;
    size_t block_size;
    size_t block_stride;
    size_t blocks_per_chunk;
    size_t capacity;
    size_t in_use;
    size_t peak_in_use;
    nmo_pool_free_node_t *free_list;
    nmo_pool_chunk_t *chunks;
};

static size_t nmo_pool_align_up(size_t value, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return value;
    }
    size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

static size_t nmo_pool_block_stride(size_t block_size) {
    size_t min_size = block_size < sizeof(nmo_pool_free_node_t)
        ? sizeof(nmo_pool_free_node_t)
        : block_size;
    return nmo_pool_align_up(min_size, sizeof(void *));
}

static int nmo_pool_grow(nmo_pool_t *pool, size_t blocks) {
    if (blocks == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t header_size = nmo_pool_align_up(sizeof(nmo_pool_chunk_t), sizeof(void *));
    size_t chunk_bytes = header_size + (pool->block_stride * blocks);

    void *raw = nmo_alloc(&pool->allocator, chunk_bytes, sizeof(void *));
    if (!raw) {
        return NMO_ERR_NOMEM;
    }

    memset(raw, 0, header_size);
    nmo_pool_chunk_t *chunk = (nmo_pool_chunk_t *)raw;
    chunk->next = pool->chunks;
    pool->chunks = chunk;

    uint8_t *block_base = (uint8_t *)raw + header_size;
    for (size_t i = 0; i < blocks; ++i) {
        nmo_pool_free_node_t *node = (nmo_pool_free_node_t *)(block_base + (i * pool->block_stride));
        node->next = pool->free_list;
        pool->free_list = node;
    }

    pool->capacity += blocks;
    return NMO_OK;
}

nmo_pool_t *nmo_pool_create(const nmo_allocator_t *allocator,
                            size_t block_size,
                            size_t initial_capacity) {
    if (block_size == 0) {
        return NULL;
    }

    nmo_allocator_t effective_allocator =
        allocator ? *allocator : nmo_allocator_default();

    nmo_pool_t *pool = (nmo_pool_t *)nmo_alloc(&effective_allocator,
                                              sizeof(nmo_pool_t),
                                              sizeof(void *));
    if (!pool) {
        return NULL;
    }

    memset(pool, 0, sizeof(*pool));
    pool->allocator = effective_allocator;
    pool->block_size = block_size;
    pool->block_stride = nmo_pool_block_stride(block_size);
    pool->blocks_per_chunk = initial_capacity > 0 ? initial_capacity : 32;

    if (nmo_pool_grow(pool, pool->blocks_per_chunk) != NMO_OK) {
        nmo_free(&pool->allocator, pool);
        return NULL;
    }

    return pool;
}

void nmo_pool_destroy(nmo_pool_t *pool) {
    if (!pool) {
        return;
    }

    nmo_pool_chunk_t *chunk = pool->chunks;
    while (chunk) {
        nmo_pool_chunk_t *next = chunk->next;
        nmo_free(&pool->allocator, chunk);
        chunk = next;
    }

    nmo_free(&pool->allocator, pool);
}

void *nmo_pool_alloc(nmo_pool_t *pool) {
    if (!pool) {
        return NULL;
    }

    if (!pool->free_list) {
        if (nmo_pool_grow(pool, pool->blocks_per_chunk) != NMO_OK) {
            return NULL;
        }
    }

    nmo_pool_free_node_t *node = pool->free_list;
    pool->free_list = node->next;
    pool->in_use++;
    if (pool->in_use > pool->peak_in_use) {
        pool->peak_in_use = pool->in_use;
    }

    return node;
}

void nmo_pool_free(nmo_pool_t *pool, void *ptr) {
    if (!pool || !ptr) {
        return;
    }

    nmo_pool_free_node_t *node = (nmo_pool_free_node_t *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    if (pool->in_use > 0) {
        pool->in_use--;
    }
}

void nmo_pool_reset(nmo_pool_t *pool) {
    if (!pool) {
        return;
    }

    pool->free_list = NULL;
    pool->in_use = 0;

    nmo_pool_chunk_t *chunk = pool->chunks;
    size_t header_size = nmo_pool_align_up(sizeof(nmo_pool_chunk_t), sizeof(void *));
    while (chunk) {
        uint8_t *block_base = (uint8_t *)chunk + header_size;
        size_t blocks = pool->blocks_per_chunk;
        for (size_t i = 0; i < blocks; ++i) {
            nmo_pool_free_node_t *node = (nmo_pool_free_node_t *)(block_base + (i * pool->block_stride));
            node->next = pool->free_list;
            pool->free_list = node;
        }
        chunk = chunk->next;
    }
}

nmo_pool_stats_t nmo_pool_get_stats(const nmo_pool_t *pool) {
    nmo_pool_stats_t stats = {0, 0, 0, 0};
    if (!pool) {
        return stats;
    }
    stats.block_size = pool->block_size;
    stats.capacity = pool->capacity;
    stats.in_use = pool->in_use;
    stats.peak_in_use = pool->peak_in_use;
    return stats;
}
