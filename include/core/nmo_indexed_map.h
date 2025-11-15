/**
 * @file nmo_indexed_map.h
 * @brief Generic indexed map (hash table + dense array for iteration)
 */

#ifndef NMO_INDEXED_MAP_H
#define NMO_INDEXED_MAP_H

#include "nmo_types.h"
#include "core/nmo_arena.h"
#include "core/nmo_container_lifecycle.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Indexed map opaque type
 *
 * Combines a hash table for fast lookup with a dense array for efficient iteration.
 * Useful when you need both O(1) lookup by key and sequential iteration.
 */
typedef struct nmo_indexed_map nmo_indexed_map_t;

/**
 * @brief Hash function type
 */
typedef size_t (*nmo_map_hash_func_t)(const void *key, size_t key_size);

/**
 * @brief Key comparison function type
 */
typedef int (*nmo_map_compare_func_t)(const void *key1, const void *key2, size_t key_size);

/**
 * @brief Create an indexed map
 * @param arena Arena for allocations (NULL to use a private arena)
 * @param key_size Size of key in bytes
 * @param value_size Size of value in bytes
 * @param initial_capacity Initial capacity (0 for default)
 * @param hash_func Hash function (NULL for default)
 * @param compare_func Key comparison function (NULL for memcmp)
 * @return New indexed map or NULL on error
 */
nmo_indexed_map_t *nmo_indexed_map_create(
    nmo_arena_t *arena,
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_map_hash_func_t hash_func,
    nmo_map_compare_func_t compare_func
);

/**
 * @brief Configure lifecycle hooks for stored keys and values.
 *
 * Dispose callbacks run whenever an element leaves the map (remove, clear,
 * destroy) or a value is overwritten. Passing NULL resets the lifecycle.
 *
 * @param map Indexed map
 * @param key_lifecycle Lifecycle hooks for keys (optional)
 * @param value_lifecycle Lifecycle hooks for values (optional)
 */
void nmo_indexed_map_set_lifecycle(nmo_indexed_map_t *map,
                                   const nmo_container_lifecycle_t *key_lifecycle,
                                   const nmo_container_lifecycle_t *value_lifecycle);

/**
 * @brief Destroy indexed map
 * @param map Indexed map to destroy
 */
void nmo_indexed_map_destroy(nmo_indexed_map_t *map);

/**
 * @brief Insert or update entry
 * @param map Indexed map
 * @param key Key pointer
 * @param value Value pointer
 * @return NMO_OK on success, error code on failure
 */
int nmo_indexed_map_insert(nmo_indexed_map_t *map, const void *key, const void *value);

/**
 * @brief Get value by key
 * @param map Indexed map
 * @param key Key pointer
 * @param value_out Output value pointer (can be NULL to just check existence)
 * @return 1 if found, 0 if not found
 */
int nmo_indexed_map_get(const nmo_indexed_map_t *map, const void *key, void *value_out);

/**
 * @brief Remove entry by key
 * @param map Indexed map
 * @param key Key pointer
 * @return 1 if removed, 0 if not found
 */
int nmo_indexed_map_remove(nmo_indexed_map_t *map, const void *key);

/**
 * @brief Check if key exists
 * @param map Indexed map
 * @param key Key pointer
 * @return 1 if exists, 0 otherwise
 */
int nmo_indexed_map_contains(const nmo_indexed_map_t *map, const void *key);

/**
 * @brief Get entry count
 * @param map Indexed map
 * @return Number of entries
 */
size_t nmo_indexed_map_get_count(const nmo_indexed_map_t *map);

/**
 * @brief Get key at index (for iteration)
 * @param map Indexed map
 * @param index Index (0 to count-1)
 * @param key_out Output key pointer
 * @return 1 if valid index, 0 otherwise
 */
int nmo_indexed_map_get_key_at(const nmo_indexed_map_t *map, size_t index, void *key_out);

/**
 * @brief Get value at index (for iteration)
 * @param map Indexed map
 * @param index Index (0 to count-1)
 * @param value_out Output value pointer
 * @return 1 if valid index, 0 otherwise
 */
int nmo_indexed_map_get_value_at(const nmo_indexed_map_t *map, size_t index, void *value_out);

/**
 * @brief Get both key and value at index (for iteration)
 * @param map Indexed map
 * @param index Index (0 to count-1)
 * @param key_out Output key pointer (can be NULL)
 * @param value_out Output value pointer (can be NULL)
 * @return 1 if valid index, 0 otherwise
 */
int nmo_indexed_map_get_at(const nmo_indexed_map_t *map, size_t index, void *key_out, void *value_out);

/**
 * @brief Clear all entries
 * @param map Indexed map
 */
void nmo_indexed_map_clear(nmo_indexed_map_t *map);

/**
 * @brief Iterator callback function
 * @param key Key pointer
 * @param value Value pointer
 * @param user_data User data passed to iterator
 * @return 1 to continue iteration, 0 to stop
 */
typedef int (*nmo_indexed_map_iterator_func_t)(const void *key, void *value, void *user_data);

/**
 * @brief Iterate over all entries
 * @param map Indexed map
 * @param func Iterator function
 * @param user_data User data to pass to iterator
 */
void nmo_indexed_map_iterate(const nmo_indexed_map_t *map, nmo_indexed_map_iterator_func_t func, void *user_data);

/* Utility hash functions - reuse from hash_table */

/**
 * @brief Hash function for uint32_t keys
 */
size_t nmo_map_hash_uint32(const void *key, size_t key_size);

/**
 * @brief Hash function for string keys
 */
size_t nmo_map_hash_string(const void *key, size_t key_size);

/**
 * @brief Comparison function for string keys
 */
int nmo_map_compare_string(const void *key1, const void *key2, size_t key_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INDEXED_MAP_H */

