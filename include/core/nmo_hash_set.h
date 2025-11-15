/**
 * @file nmo_hash_set.h
 * @brief Generic hash set implementation with allocator-based storage.
 */

#ifndef NMO_HASH_SET_H
#define NMO_HASH_SET_H

#include "nmo_types.h"
#include "core/nmo_allocator.h"
#include "core/nmo_hash_common.h"
#include "core/nmo_container_lifecycle.h"
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
 * @return NMO_OK on success, NMO_ERR_ALREADY_EXISTS if the key is present.
 */
int nmo_hash_set_insert(nmo_hash_set_t *set, const void *key);

/**
 * @brief Remove a key from the set.
 *
 * @param set Hash set.
 * @param key Pointer to key data.
 * @return 1 if removed, 0 if key was not present.
 */
int nmo_hash_set_remove(nmo_hash_set_t *set, const void *key);

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
int nmo_hash_set_reserve(nmo_hash_set_t *set, size_t capacity);

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
