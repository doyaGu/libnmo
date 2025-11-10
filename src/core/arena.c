#include "core/nmo_arena.h"
#include <string.h>
#include <stdint.h>

#define NMO_ARENA_DEFAULT_CHUNK_SIZE (64 * 1024)  // 64 KB
#define NMO_ARENA_ALIGNMENT 16

// Arena chunk
typedef struct nmo_arena_chunk {
    struct nmo_arena_chunk* next;
    size_t size;
    size_t used;
    uint8_t data[];
} nmo_arena_chunk;

// Arena structure
struct nmo_arena {
    nmo_allocator allocator;
    nmo_arena_chunk* first;
    nmo_arena_chunk* current;
    size_t chunk_size;
    size_t total_allocated;
    size_t bytes_used;
};

static nmo_arena_chunk* arena_create_chunk(nmo_allocator* allocator, size_t size) {
    size_t total_size = sizeof(nmo_arena_chunk) + size;
    nmo_arena_chunk* chunk = (nmo_arena_chunk*)nmo_alloc(allocator, total_size, NMO_ARENA_ALIGNMENT);

    if (chunk == NULL) {
        return NULL;
    }

    chunk->next = NULL;
    chunk->size = size;
    chunk->used = 0;

    return chunk;
}

nmo_arena* nmo_arena_create(nmo_allocator* allocator, size_t initial_size) {
    // Use default allocator if none provided
    nmo_allocator alloc = allocator ? *allocator : nmo_allocator_default();

    // Use default chunk size if none provided
    if (initial_size == 0) {
        initial_size = NMO_ARENA_DEFAULT_CHUNK_SIZE;
    }

    // Allocate arena structure
    nmo_arena* arena = (nmo_arena*)nmo_alloc(&alloc, sizeof(nmo_arena), NMO_ARENA_ALIGNMENT);
    if (arena == NULL) {
        return NULL;
    }

    // Initialize arena
    arena->allocator = alloc;
    arena->chunk_size = initial_size;
    arena->total_allocated = 0;
    arena->bytes_used = 0;

    // Create first chunk
    arena->first = arena_create_chunk(&alloc, initial_size);
    if (arena->first == NULL) {
        nmo_free(&alloc, arena);
        return NULL;
    }

    arena->current = arena->first;
    arena->total_allocated = initial_size;

    return arena;
}

void* nmo_arena_alloc(nmo_arena* arena, size_t size, size_t alignment) {
    if (arena == NULL || size == 0) {
        return NULL;
    }

    // Ensure alignment is power of 2
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    // Default alignment
    if (alignment == 0) {
        alignment = NMO_ARENA_ALIGNMENT;
    }

    nmo_arena_chunk* chunk = arena->current;

    // Align current position
    size_t aligned_used = (chunk->used + alignment - 1) & ~(alignment - 1);
    size_t remaining = chunk->size - aligned_used;

    // Check if current chunk has enough space
    if (remaining < size) {
        // Need a new chunk
        size_t new_chunk_size = arena->chunk_size;

        // If requested size is larger than default chunk size, use larger chunk
        if (size > new_chunk_size) {
            new_chunk_size = (size + NMO_ARENA_ALIGNMENT - 1) & ~(NMO_ARENA_ALIGNMENT - 1);
        }

        nmo_arena_chunk* new_chunk = arena_create_chunk(&arena->allocator, new_chunk_size);
        if (new_chunk == NULL) {
            return NULL;
        }

        // Link new chunk
        chunk->next = new_chunk;
        arena->current = new_chunk;
        arena->total_allocated += new_chunk_size;

        chunk = new_chunk;
        aligned_used = 0;
    }

    // Allocate from current chunk
    void* ptr = chunk->data + aligned_used;
    chunk->used = aligned_used + size;
    arena->bytes_used += size;

    return ptr;
}

void nmo_arena_reset(nmo_arena* arena) {
    if (arena == NULL) {
        return;
    }

    // Reset all chunks
    for (nmo_arena_chunk* chunk = arena->first; chunk != NULL; chunk = chunk->next) {
        chunk->used = 0;
    }

    arena->current = arena->first;
    arena->bytes_used = 0;
}

void nmo_arena_destroy(nmo_arena* arena) {
    if (arena == NULL) {
        return;
    }

    // Free all chunks
    nmo_arena_chunk* chunk = arena->first;
    while (chunk != NULL) {
        nmo_arena_chunk* next = chunk->next;
        nmo_free(&arena->allocator, chunk);
        chunk = next;
    }

    // Free arena structure
    nmo_free(&arena->allocator, arena);
}

size_t nmo_arena_total_allocated(nmo_arena* arena) {
    return arena ? arena->total_allocated : 0;
}

size_t nmo_arena_bytes_used(nmo_arena* arena) {
    return arena ? arena->bytes_used : 0;
}
