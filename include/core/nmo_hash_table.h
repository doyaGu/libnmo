/**
 * @file nmo_hash_table.h
 * @brief Generic hash table implementation with linear probing
 */

#ifndef NMO_HASH_TABLE_H
#define NMO_HASH_TABLE_H

#include "nmo_types.h"
#include "core/nmo_hash.h"
#include "core/nmo_hash_common.h"
#include "core/nmo_allocator.h"
#include "core/nmo_container_lifecycle.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash table opaque type
 */
typedef struct nmo_hash_table nmo_hash_table_t;

/**
 * @brief Create a hash table
 * @param allocator Allocator for allocations (NULL for default system allocator)
 * @param key_size Size of key in bytes
 * @param value_size Size of value in bytes
 * @param initial_capacity Initial capacity (0 for default)
 * @param hash_func Hash function (NULL for default)
 * @param compare_func Key comparison function (NULL for memcmp)
 * @return New hash table or NULL on error
 */
nmo_hash_table_t *nmo_hash_table_create(
    const nmo_allocator_t *allocator,
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_hash_func_t hash_func,
    nmo_key_compare_func_t compare_func
);

/**
 * @brief Destroy hash table
 * @param table Hash table to destroy
 */
void nmo_hash_table_destroy(nmo_hash_table_t *table);

/**
 * @brief Configure lifecycle hooks for keys and values.
 *
 * The provided callbacks execute whenever an element leaves the table
 * (remove, clear, destroy) or a value is overwritten. Passing NULL resets
 * the lifecycle for that side to the default no-op.
 *
 * @param table Hash table
 * @param key_lifecycle Lifecycle hooks for key storage (optional)
 * @param value_lifecycle Lifecycle hooks for value storage (optional)
 */
void nmo_hash_table_set_lifecycle(nmo_hash_table_t *table,
                                  const nmo_container_lifecycle_t *key_lifecycle,
                                  const nmo_container_lifecycle_t *value_lifecycle);

/**
 * @brief Insert or update entry
 * @param table Hash table
 * @param key Key pointer
 * @param value Value pointer
 * @return NMO_OK on success, error code on failure
 */
int nmo_hash_table_insert(nmo_hash_table_t *table, const void *key, const void *value);

/**
 * @brief Get value by key
 * @param table Hash table
 * @param key Key pointer
 * @param value_out Output value pointer (can be NULL to just check existence)
 * @return 1 if found, 0 if not found
 */
int nmo_hash_table_get(const nmo_hash_table_t *table, const void *key, void *value_out);

/**
 * @brief Remove entry by key
 * @param table Hash table
 * @param key Key pointer
 * @return 1 if removed, 0 if not found
 */
int nmo_hash_table_remove(nmo_hash_table_t *table, const void *key);

/**
 * @brief Check if key exists
 * @param table Hash table
 * @param key Key pointer
 * @return 1 if exists, 0 otherwise
 */
int nmo_hash_table_contains(const nmo_hash_table_t *table, const void *key);

/**
 * @brief Get entry count
 * @param table Hash table
 * @return Number of entries
 */
size_t nmo_hash_table_get_count(const nmo_hash_table_t *table);

/**
 * @brief Get table capacity
 * @param table Hash table
 * @return Current capacity
 */
size_t nmo_hash_table_get_capacity(const nmo_hash_table_t *table);

/**
 * @brief Reserve capacity (Phase 5 optimization)
 * 
 * Pre-allocates space for at least 'capacity' entries to avoid
 * multiple reallocations during bulk inserts.
 * 
 * @param table Hash table
 * @param capacity Minimum desired capacity
 * @return NMO_OK on success
 */
int nmo_hash_table_reserve(nmo_hash_table_t *table, size_t capacity);

/**
 * @brief Get table size (alias for get_count, for compatibility)
 * @param table Hash table
 * @return Number of entries
 */
static inline size_t nmo_hash_table_size(const nmo_hash_table_t *table) {
    return nmo_hash_table_get_count(table);
}

/**
 * @brief Clear all entries
 * @param table Hash table
 */
void nmo_hash_table_clear(nmo_hash_table_t *table);

/**
 * @brief Iterator callback function
 * @param key Key pointer
 * @param value Value pointer
 * @param user_data User data passed to iterator
 * @return 1 to continue iteration, 0 to stop
 */
typedef int (*nmo_hash_table_iterator_func_t)(const void *key, void *value, void *user_data);

/**
 * @brief Iterate over all entries
 * @param table Hash table
 * @param func Iterator function
 * @param user_data User data to pass to iterator
 */
void nmo_hash_table_iterate(const nmo_hash_table_t *table, nmo_hash_table_iterator_func_t func, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HASH_TABLE_H */
