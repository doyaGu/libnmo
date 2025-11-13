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
 */

/**
 * @brief Allocation function type
 * @param user_data User-defined data passed to allocator
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
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
 * @brief Allocate memory
 *
 * @param allocator Allocator to use
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory or NULL on failure
 */
NMO_API void *nmo_alloc(nmo_allocator_t *allocator, size_t size, size_t alignment);

/**
 * @brief Free memory
 *
 * @param allocator Allocator to use
 * @param ptr Pointer to memory to free
 */
NMO_API void nmo_free(nmo_allocator_t *allocator, void *ptr);

#ifdef __cplusplus
}
#endif

#endif // NMO_ALLOCATOR_H
