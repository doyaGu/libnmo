#ifndef NMO_ARENA_H
#define NMO_ARENA_H

#include "nmo_types.h"
#include "nmo_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_arena.h
 * @brief Arena (bump) allocator for fast bulk allocations
 *
 * Arena allocators are extremely fast for bulk allocations and allow
 * deallocation of all memory at once. Perfect for temporary allocations
 * during parsing or session-local data.
 *
 * Memory Layout:
 * ┌────────────┬────────────┬────────────┐
 * │  Chunk 1   │  Chunk 2   │  Chunk 3   │
 * └────────────┴────────────┴────────────┘
 *      ↑
 *    cursor (allocation pointer)
 *
 * Features:
 * - O(1) allocation
 * - O(1) bulk deallocation
 * - Automatic growth in chunks
 * - Alignment support
 */

/**
 * @brief Opaque arena allocator type
 */
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Arena configuration (Phase 5 optimization)
 */
typedef struct nmo_arena_config {
    size_t initial_block_size;    /**< Initial block size (default: 64KB) */
    size_t max_block_size;        /**< Maximum block size (default: 16MB, 0=unlimited) */
    float growth_factor;          /**< Growth factor for new blocks (default: 2.0) */
    size_t alignment;             /**< Default alignment (default: 16 bytes) */
} nmo_arena_config_t;

/**
 * @brief Get default arena configuration
 * 
 * @return Default configuration
 */
NMO_API nmo_arena_config_t nmo_arena_default_config(void);

/**
 * @brief Create arena allocator with configuration
 *
 * @param allocator Backing allocator (NULL for default)
 * @param config Arena configuration (NULL for defaults)
 * @return Arena allocator or NULL on failure
 */
NMO_API nmo_arena_t *nmo_arena_create_ex(nmo_allocator_t *allocator, const nmo_arena_config_t *config);

/**
 * @brief Create arena allocator
 *
 * @param allocator Backing allocator (NULL for default)
 * @param initial_size Initial chunk size in bytes (0 for default 64KB)
 * @return Arena allocator or NULL on failure
 */
NMO_API nmo_arena_t *nmo_arena_create(nmo_allocator_t *allocator, size_t initial_size);

/**
 * @brief Allocate memory from arena
 *
 * @param arena Arena allocator
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory or NULL on failure
 */
NMO_API void *nmo_arena_alloc(nmo_arena_t *arena, size_t size, size_t alignment);

/**
 * @brief Reset arena (free all allocations but keep chunks)
 *
 * This allows reuse of the arena for another batch of allocations
 * without deallocating the underlying memory.
 *
 * @param arena Arena allocator
 */
NMO_API void nmo_arena_reset(nmo_arena_t *arena);

/**
 * @brief Destroy arena (free all memory)
 *
 * @param arena Arena allocator
 */
NMO_API void nmo_arena_destroy(nmo_arena_t *arena);

/**
 * @brief Get total bytes allocated from system
 *
 * @param arena Arena allocator
 * @return Total bytes allocated
 */
NMO_API size_t nmo_arena_total_allocated(nmo_arena_t *arena);

/**
 * @brief Get bytes currently used
 *
 * @param arena Arena allocator
 * @return Bytes used
 */
NMO_API size_t nmo_arena_bytes_used(nmo_arena_t *arena);

/**
 * @brief Reserve capacity (preallocate memory) - Phase 5 optimization
 * 
 * Preallocates memory to avoid multiple allocations during usage.
 * Useful when total memory requirement is known in advance.
 *
 * @param arena Arena allocator
 * @param total_size Total size to reserve (in bytes)
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_arena_reserve(nmo_arena_t *arena, size_t total_size);

/**
 * @brief Get arena configuration
 * 
 * @param arena Arena allocator
 * @param config Output: configuration structure
 * @return NMO_OK on success
 */
NMO_API int nmo_arena_get_config(const nmo_arena_t *arena, nmo_arena_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // NMO_ARENA_H
