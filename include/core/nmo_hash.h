/**
 * @file nmo_hash.h
 * @brief Optimized hash functions (Phase 5)
 * 
 * Includes MurmurHash3 and other high-performance hash algorithms.
 */

#ifndef NMO_HASH_H
#define NMO_HASH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MurmurHash3 32-bit variant
 * 
 * Fast, high-quality hash function with good distribution.
 * Suitable for hash tables, bloom filters, etc.
 * 
 * @param data Input data
 * @param len Data length in bytes
 * @param seed Hash seed (use 0 for default)
 * @return 32-bit hash value
 * 
 * Reference: https://github.com/aappleby/smhasher
 * 
 * Performance: ~2-3 cycles/byte on modern CPUs
 * Quality: Excellent avalanche, no known collisions
 */
uint32_t nmo_murmur3_32(const void *data, size_t len, uint32_t seed);

/**
 * MurmurHash3 128-bit variant (x64 version)
 * 
 * Higher quality than 32-bit version, suitable for cryptographic-lite uses.
 * 
 * @param data Input data
 * @param len Data length in bytes
 * @param seed Hash seed
 * @param out Output array (16 bytes / 128 bits)
 */
void nmo_murmur3_128(const void *data, size_t len, uint32_t seed, void *out);

/**
 * Fast hash for small integers (based on MurmurHash3 finalizer)
 * 
 * @param value Input value
 * @return Hashed value
 */
static inline uint32_t nmo_hash_int32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x85ebca6b;
    value ^= value >> 13;
    value *= 0xc2b2ae35;
    value ^= value >> 16;
    return value;
}

/**
 * Fast hash for 64-bit integers
 * 
 * @param value Input value
 * @return Hashed value
 */
static inline uint64_t nmo_hash_int64(uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return value;
}

/**
 * XXHash32 - extremely fast hash function
 * 
 * Faster than MurmurHash3 on modern CPUs, good quality.
 * 
 * @param data Input data
 * @param len Data length
 * @param seed Hash seed
 * @return 32-bit hash value
 * 
 * Reference: https://github.com/Cyan4973/xxHash
 */
uint32_t nmo_xxhash32(const void *data, size_t len, uint32_t seed);

/**
 * @brief Default hash function (FNV-1a)
 * @param data Data to hash
 * @param size Size of data in bytes
 * @return Hash value
 */
size_t nmo_hash_fnv1a(const void *data, size_t size);

/**
 * @brief Hash function for uint32_t keys (optimized with MurmurHash3 finalizer)
 * @param key Key to hash
 * @param key_size Size of key in bytes (should be 4 for uint32_t)
 * @return Hash value
 */
size_t nmo_hash_uint32(const void *key, size_t key_size);

/**
 * @brief Hash function for string keys (djb2 algorithm)
 * @param key Key to hash (pointer to const char*)
 * @param key_size Size of key in bytes (should be sizeof(const char*))
 * @return Hash value
 */
size_t nmo_hash_string(const void *key, size_t key_size);

/**
 * @brief Comparison function for string keys
 * @param key1 First key (pointer to const char*)
 * @param key2 Second key (pointer to const char*)
 * @param key_size Size of keys in bytes (should be sizeof(const char*))
 * @return 0 if equal, non-zero otherwise
 */
int nmo_compare_string(const void *key1, const void *key2, size_t key_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HASH_H */
