#ifndef NMO_ALLOCATOR_H
#define NMO_ALLOCATOR_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_allocator.h
 * @brief Memory allocation interface
 *
 * Provides a customizable memory allocation interface that allows
 * users to plug in their own allocators or use the default system allocator.
 *
 * @note Thread safety: Allocator implementations must be synchronized by the caller.
 */

/**
 * @brief Allocation function type
 * @param user_data User-defined data passed to allocator
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (must be power of 2, 0 uses default)
 * @return Pointer to allocated memory or NULL on failure
 */
typedef void *(*nmo_alloc_fn)(void *user_data, size_t size, size_t alignment);

/**
 * @brief Free function type
 * @param user_data User-defined data passed to allocator
 * @param ptr Pointer to memory to free
 */
typedef void (*nmo_free_fn)(void *user_data, void *ptr);

/**
 * @brief Allocator interface
 *
 * Encapsulates allocation and deallocation functions along with
 * optional user data for context.
 */
typedef struct nmo_allocator {
    nmo_alloc_fn alloc; /**< Allocation function */
    nmo_free_fn free;   /**< Deallocation function */
    void *user_data;    /**< User-defined context */
} nmo_allocator_t;

/**
 * @brief Allocation statistics snapshot
 */
typedef struct nmo_allocator_stats {
    size_t total_allocations; /**< Total successful allocations */
    size_t total_frees;       /**< Total frees */
    size_t total_bytes;       /**< Total bytes allocated over lifetime */
    size_t current_bytes;     /**< Bytes currently allocated */
    size_t peak_bytes;        /**< Peak allocation watermark */
} nmo_allocator_stats_t;

/**
 * @brief Tracking allocator context
 *
 * The context must stay alive as long as the allocator is used.
 */
typedef struct nmo_allocator_tracking {
    nmo_allocator_t base;            /**< Base allocator to wrap */
    nmo_allocator_stats_t *stats;    /**< Stats sink (optional) */
} nmo_allocator_tracking_t;

/**
 * @brief Debug allocator context
 *
 * Records allocator origin for runtime validation in Debug builds.
 */
typedef struct nmo_allocator_debug {
    nmo_allocator_t base;   /**< Base allocator to wrap */
    const char *module;     /**< Allocator origin module */
    const char *tag;        /**< Allocator usage tag */
} nmo_allocator_debug_t;

/**
 * @brief Create default system allocator
 *
 * Returns an allocator that uses malloc/free.
 *
 * @return Default allocator instance
 */
NMO_API nmo_allocator_t nmo_allocator_default(void);

/**
 * @brief Create custom allocator
 *
 * @param alloc Allocation function
 * @param free Deallocation function
 * @param user_data User-defined context
 * @return Custom allocator instance
 */
NMO_API nmo_allocator_t nmo_allocator_custom(nmo_alloc_fn alloc, nmo_free_fn free, void *user_data);

/**
 * @brief Create a tracking allocator wrapper
 *
 * The returned allocator delegates to @p base and updates @p stats if provided.
 * The @p tracking context must remain valid for the allocator's lifetime.
 *
 * @param tracking Tracking context storage (required)
 * @param base Base allocator to wrap
 * @param stats Stats sink to update (NULL to disable)
 * @return Tracking allocator instance
 */
NMO_API nmo_allocator_t nmo_allocator_tracking_init(nmo_allocator_tracking_t *tracking,
                                                     nmo_allocator_t base,
                                                     nmo_allocator_stats_t *stats);

/**
 * @brief Create a debug allocator wrapper
 *
 * Records module/tag metadata per allocation and validates matching frees.
 * In non-Debug builds, this behaves like a normal wrapper without assertions.
 *
 * @param debug Debug context storage (required)
 * @param base Base allocator to wrap
 * @param module Allocator origin module (string literal recommended)
 * @param tag Allocator usage tag (string literal recommended)
 * @return Debug allocator instance
 */
NMO_API nmo_allocator_t nmo_allocator_debug_init(nmo_allocator_debug_t *debug,
                                                  nmo_allocator_t base,
                                                  const char *module,
                                                  const char *tag);

/**
 * @brief Reset allocator statistics to zero.
 */
NMO_API void nmo_allocator_stats_reset(nmo_allocator_stats_t *stats);

/**
 * @brief Allocate memory
 *
 * @param allocator Allocator to use
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (must be power of 2, 0 uses default)
 * @return Pointer to allocated memory or NULL on failure
 * @ownership owned
 */
NMO_API void *nmo_alloc(nmo_allocator_t *allocator, size_t size, size_t alignment);

/**
 * @brief Free memory
 *
 * @param allocator Allocator to use
 * @param ptr Pointer to memory to free
 */
NMO_API void nmo_free(nmo_allocator_t *allocator, void *ptr);

/**
 * @brief Duplicate a string using the allocator
 *
 * @param alloc Allocator to use
 * @param src Null-terminated string to duplicate
 * @return Pointer to duplicated string or NULL on failure
 * @ownership owned
 */
NMO_API char *nmo_strdup(nmo_allocator_t *alloc, const char *src);

#ifdef __cplusplus
}
#endif

#endif // NMO_ALLOCATOR_H
