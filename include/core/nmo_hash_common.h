/**
 * @file nmo_hash_common.h
 * @brief Shared hash type definitions for hash-based containers.
 */

#ifndef NMO_HASH_COMMON_H
#define NMO_HASH_COMMON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash function type used by hash-based containers.
 * @param key Key to hash.
 * @param key_size Size of the key in bytes.
 * @return Hash value.
 */
typedef size_t (*nmo_hash_func_t)(const void *key, size_t key_size);

/**
 * @brief Key comparison function type for hash-based containers.
 * @param key1 First key.
 * @param key2 Second key.
 * @param key_size Size of keys in bytes.
 * @return 0 if equal, non-zero otherwise.
 */
typedef int (*nmo_key_compare_func_t)(const void *key1, const void *key2, size_t key_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HASH_COMMON_H */
