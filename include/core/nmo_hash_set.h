/**
 * @file nmo_hash_set.h
 * @brief Generic hash set implementation with allocator-based storage.
 *
 * @note Thread safety: This module is not thread-safe. Synchronize access.
 */

#ifndef NMO_HASH_SET_H
#define NMO_HASH_SET_H

#include "nmo_types.h"
#include "core/nmo_allocator.h"
#include "core/nmo_hash_common.h"
#include "core/nmo_container_lifecycle.h"
#include "core/nmo_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash set opaque type.
 */
typedef struct nmo_hash_set nmo_hash_set_t;

/**
 * @brief Iterator callback signature for hash set traversal.
 * @param key Pointer to the key stored inside the set.
 * @param user_data User supplied context.
 * @return 1 to continue iterating, 0 to stop.
 */
typedef int (*nmo_hash_set_iterator_func_t)(const void *key, void *user_data);

/**
 * @brief Create a hash set.
 *
 * @param allocator Allocator for internal storage (NULL for default system allocator).
 * @param key_size Size of each key in bytes.
 * @param initial_capacity Desired starting capacity (0 for default).
 * @param hash_func Hash function (NULL to use FNV-1a).
 * @param compare_func Key comparison function (NULL for memcmp).
 * @return New hash set or NULL on error.
 */
nmo_hash_set_t *nmo_hash_set_create(const nmo_allocator_t *allocator,
                                    size_t key_size,
                                    size_t initial_capacity,
                                    nmo_hash_func_t hash_func,
                                    nmo_key_compare_func_t compare_func);

/**
 * @brief Destroy a hash set and release its backing storage.
 */
void nmo_hash_set_destroy(nmo_hash_set_t *set);

/**
 * @brief Configure lifecycle hooks for stored keys.
 *
 * Copy callbacks are invoked when keys are written into the set. Move
 * callbacks are invoked when keys are relocated during rehash.
 *
 * @param set Hash set to configure.
 * @param key_lifecycle Lifecycle callbacks (NULL to clear).
 */
void nmo_hash_set_set_lifecycle(nmo_hash_set_t *set,
                                const nmo_container_lifecycle_t *key_lifecycle);

/**
 * @brief Insert a key into the set.
 *
 * @param set Hash set.
 * @param key Pointer to key data.
 * @return Result with NMO_OK on success or NMO_ERR_ALREADY_EXISTS if present.
 */
nmo_status_t nmo_hash_set_insert(nmo_hash_set_t *set, const void *key);

/**
 * @brief Remove a key from the set.
 *
 * @param set Hash set.
 * @param key Pointer to key data.
 * @return Result with NMO_OK if removed or NMO_ERR_NOT_FOUND if absent.
 */
nmo_status_t nmo_hash_set_remove(nmo_hash_set_t *set, const void *key);

/**
 * @brief Check if a key exists inside the set.
 */
int nmo_hash_set_contains(const nmo_hash_set_t *set, const void *key);

/**
 * @brief Get the number of elements inside the set.
 */
size_t nmo_hash_set_get_count(const nmo_hash_set_t *set);

/**
 * @brief Get the current capacity of the set.
 */
size_t nmo_hash_set_get_capacity(const nmo_hash_set_t *set);

/**
 * @brief Reserve storage for at least the specified number of keys.
 */
nmo_status_t nmo_hash_set_reserve(nmo_hash_set_t *set, size_t capacity);

/**
 * @brief Rehash to a new capacity (rounded to power of two).
 *
 * @param set Hash set
 * @param capacity Desired capacity (must be >= current count)
 * @return Result with NMO_OK on success or error code on failure
 */
nmo_status_t nmo_hash_set_rehash(nmo_hash_set_t *set, size_t capacity);

/**
 * @brief Resize set capacity (alias of rehash).
 *
 * @param set Hash set
 * @param capacity Desired capacity (must be >= current count)
 * @return Result with NMO_OK on success or error code on failure
 */
nmo_status_t nmo_hash_set_resize(nmo_hash_set_t *set, size_t capacity);

/**
 * @brief Get current load factor (count / capacity).
 *
 * @param set Hash set
 * @return Load factor (0.0f if set is NULL or capacity is 0)
 */
float nmo_hash_set_load_factor(const nmo_hash_set_t *set);

/**
 * @brief Clear all keys from the set.
 */
void nmo_hash_set_clear(nmo_hash_set_t *set);

/**
 * @brief Iterate over every key stored in the set.
 *
 * @param set Hash set to iterate.
 * @param func Callback invoked for each key.
 * @param user_data Optional user data pointer.
 */
void nmo_hash_set_iterate(const nmo_hash_set_t *set,
                          nmo_hash_set_iterator_func_t func,
                          void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HASH_SET_H */
