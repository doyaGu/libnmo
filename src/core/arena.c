#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

#define NMO_ARENA_DEFAULT_CHUNK_SIZE (64 * 1024)   // 64 KB
#define NMO_ARENA_DEFAULT_MAX_CHUNK (16 * 1024 * 1024)  // 16 MB
#define NMO_ARENA_DEFAULT_GROWTH_FACTOR 2.0f
#define NMO_ARENA_ALIGNMENT 16

// Arena chunk
typedef struct nmo_arena_chunk {
    struct nmo_arena_chunk *next;
    size_t size;
    size_t used;
    uint8_t data[];
} nmo_arena_chunk_t;

// Arena structure (Phase 5: added config fields)
typedef struct nmo_arena {
    nmo_allocator_t allocator;
    nmo_arena_chunk_t *first;
    nmo_arena_chunk_t *current;
    
    /* Configuration (Phase 5) */
    size_t initial_chunk_size;
    size_t next_chunk_size;        /**< Size for next chunk allocation */
    size_t max_chunk_size;
    float growth_factor;
    size_t default_alignment;
    
    /* Statistics */
    size_t total_allocated;
    size_t bytes_used;
} nmo_arena_t;

static nmo_arena_chunk_t *arena_create_chunk(nmo_allocator_t *allocator, size_t size) {
    size_t total_size = sizeof(nmo_arena_chunk_t) + size;
    nmo_arena_chunk_t *chunk = (nmo_arena_chunk_t *) nmo_alloc(allocator, total_size, NMO_ARENA_ALIGNMENT);

    if (chunk == NULL) {
        return NULL;
    }

    chunk->next = NULL;
    chunk->size = size;
    chunk->used = 0;

    return chunk;
}

/**
 * Get default arena configuration (Phase 5)
 */
nmo_arena_config_t nmo_arena_default_config(void) {
    nmo_arena_config_t config;
    config.initial_block_size = NMO_ARENA_DEFAULT_CHUNK_SIZE;
    config.max_block_size = NMO_ARENA_DEFAULT_MAX_CHUNK;
    config.growth_factor = NMO_ARENA_DEFAULT_GROWTH_FACTOR;
    config.alignment = NMO_ARENA_ALIGNMENT;
    return config;
}

/**
 * Create arena with configuration
 */
nmo_arena_t *nmo_arena_create_ex(nmo_allocator_t *allocator, const nmo_arena_config_t *config) {
    // Use default allocator if none provided
    nmo_allocator_t alloc = allocator ? *allocator : nmo_allocator_default();
    
    // Use default config if none provided
    nmo_arena_config_t cfg = config ? *config : nmo_arena_default_config();
    
    // Validate config
    if (cfg.initial_block_size == 0) {
        cfg.initial_block_size = NMO_ARENA_DEFAULT_CHUNK_SIZE;
    }
    if (cfg.growth_factor < 1.0f) {
        cfg.growth_factor = NMO_ARENA_DEFAULT_GROWTH_FACTOR;
    }
    if (cfg.alignment == 0) {
        cfg.alignment = NMO_ARENA_ALIGNMENT;
    }
    
    // Allocate arena structure
    nmo_arena_t *arena = (nmo_arena_t *) nmo_alloc(&alloc, sizeof(nmo_arena_t), cfg.alignment);
    if (arena == NULL) {
        return NULL;
    }

    // Initialize arena with config
    arena->allocator = alloc;
    arena->initial_chunk_size = cfg.initial_block_size;
    arena->next_chunk_size = cfg.initial_block_size;
    arena->max_chunk_size = cfg.max_block_size;
    arena->growth_factor = cfg.growth_factor;
    arena->default_alignment = cfg.alignment;
    arena->total_allocated = 0;
    arena->bytes_used = 0;

    // Create first chunk
    arena->first = arena_create_chunk(&alloc, cfg.initial_block_size);
    if (arena->first == NULL) {
        nmo_free(&alloc, arena);
        return NULL;
    }

    arena->current = arena->first;
    arena->total_allocated = cfg.initial_block_size;

    return arena;
}

/**
 * Create arena
 */
nmo_arena_t *nmo_arena_create(nmo_allocator_t *allocator, size_t initial_size) {
    nmo_arena_config_t config = nmo_arena_default_config();
    if (initial_size > 0) {
        config.initial_block_size = initial_size;
    }
    return nmo_arena_create_ex(allocator, &config);
}

void *nmo_arena_alloc(nmo_arena_t *arena, size_t size, size_t alignment) {
    if (arena == NULL || size == 0) {
        return NULL;
    }

    // Ensure alignment is power of 2
    if (alignment > 0 && (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    // Default alignment
    if (alignment == 0) {
        alignment = arena->default_alignment;
    }

    nmo_arena_chunk_t *chunk = arena->current;

    // Align current position
    size_t aligned_used = (chunk->used + alignment - 1) & ~(alignment - 1);
    size_t remaining = chunk->size - aligned_used;

    // Check if current chunk has enough space
    if (remaining < size) {
        // Calculate next chunk size with growth factor (Phase 5)
        size_t new_chunk_size = (size_t)(arena->next_chunk_size * arena->growth_factor);
        
        // Clamp to max_chunk_size if set
        if (arena->max_chunk_size > 0 && new_chunk_size > arena->max_chunk_size) {
            new_chunk_size = arena->max_chunk_size;
        }

        // If requested size is larger than calculated chunk size, use larger chunk
        if (size > new_chunk_size) {
            new_chunk_size = (size + alignment - 1) & ~(alignment - 1);
        }

        nmo_arena_chunk_t *new_chunk = arena_create_chunk(&arena->allocator, new_chunk_size);
        if (new_chunk == NULL) {
            return NULL;
        }

        // Link new chunk
        chunk->next = new_chunk;
        arena->current = new_chunk;
        arena->total_allocated += new_chunk_size;
        arena->next_chunk_size = new_chunk_size;  /* Update for next growth */

        chunk = new_chunk;
        aligned_used = 0;
    }

    // Allocate from current chunk
    void *ptr = chunk->data + aligned_used;
    chunk->used = aligned_used + size;
    arena->bytes_used += size;

    return ptr;
}

void nmo_arena_reset(nmo_arena_t *arena) {
    if (arena == NULL) {
        return;
    }

    // Reset all chunks
    for (nmo_arena_chunk_t *chunk = arena->first; chunk != NULL; chunk = chunk->next) {
        chunk->used = 0;
    }

    arena->current = arena->first;
    arena->bytes_used = 0;
}

void nmo_arena_destroy(nmo_arena_t *arena) {
    if (arena == NULL) {
        return;
    }

    // Free all chunks
    nmo_arena_chunk_t *chunk = arena->first;
    while (chunk != NULL) {
        nmo_arena_chunk_t *next = chunk->next;
        nmo_free(&arena->allocator, chunk);
        chunk = next;
    }

    // Free arena structure
    nmo_free(&arena->allocator, arena);
}

size_t nmo_arena_total_allocated(nmo_arena_t *arena) {
    return arena ? arena->total_allocated : 0;
}

size_t nmo_arena_bytes_used(nmo_arena_t *arena) {
    return arena ? arena->bytes_used : 0;
}

/**
 * Reserve capacity (preallocate memory) - Phase 5
 */
int nmo_arena_reserve(nmo_arena_t *arena, size_t total_size) {
    if (arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    // Calculate how much more we need
    size_t current_total = arena->total_allocated;
    if (current_total >= total_size) {
        return NMO_OK;  /* Already have enough */
    }
    
    size_t needed = total_size - current_total;
    
    // Allocate in chunks following growth pattern
    while (needed > 0) {
        size_t chunk_size = arena->next_chunk_size;
        
        // Apply growth factor
        chunk_size = (size_t)(chunk_size * arena->growth_factor);
        
        // Clamp to max
        if (arena->max_chunk_size > 0 && chunk_size > arena->max_chunk_size) {
            chunk_size = arena->max_chunk_size;
        }
        
        // Don't allocate more than needed
        if (chunk_size > needed) {
            chunk_size = needed;
        }
        
        // Create chunk
        nmo_arena_chunk_t *new_chunk = arena_create_chunk(&arena->allocator, chunk_size);
        if (new_chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
        
        // Link chunk at end of chain
        nmo_arena_chunk_t *last = arena->first;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = new_chunk;
        
        arena->total_allocated += chunk_size;
        arena->next_chunk_size = chunk_size;
        needed -= chunk_size;
    }
    
    return NMO_OK;
}

/**
 * Get arena configuration
 */
int nmo_arena_get_config(const nmo_arena_t *arena, nmo_arena_config_t *config) {
    if (arena == NULL || config == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    config->initial_block_size = arena->initial_chunk_size;
    config->max_block_size = arena->max_chunk_size;
    config->growth_factor = arena->growth_factor;
    config->alignment = arena->default_alignment;
    
    return NMO_OK;
}
