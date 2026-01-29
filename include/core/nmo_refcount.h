#ifndef NMO_REFCOUNT_H
#define NMO_REFCOUNT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_refcount.h
 * @brief Lightweight reference counter utilities.
 *
 * @note Thread safety: This module is not thread-safe. Synchronize access.
 */

typedef void (*nmo_refcount_dispose_fn)(void *user_data);

/**
 * @brief Reference counter wrapper.
 */
typedef struct nmo_refcount {
    uint32_t count;
} nmo_refcount_t;

/**
 * @brief Initialize reference counter to 1.
 */
NMO_API void nmo_refcount_init(nmo_refcount_t *refcount);

/**
 * @brief Get current reference count (0 if refcount is NULL).
 */
NMO_API uint32_t nmo_refcount_get(const nmo_refcount_t *refcount);

/**
 * @brief Increment reference count and return new value.
 */
NMO_API uint32_t nmo_refcount_retain(nmo_refcount_t *refcount);

/**
 * @brief Decrement reference count and return new value.
 */
NMO_API uint32_t nmo_refcount_release(nmo_refcount_t *refcount);

/**
 * @brief Decrement reference count and call disposer when it reaches zero.
 *
 * @param refcount Reference counter.
 * @param disposer Callback invoked when count reaches zero (optional).
 * @param user_data Payload passed to disposer.
 * @return New reference count after release.
 */
NMO_API uint32_t nmo_refcount_release_with(nmo_refcount_t *refcount,
                                           nmo_refcount_dispose_fn disposer,
                                           void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REFCOUNT_H */
