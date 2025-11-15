/**
 * @file nmo_bit_array.h
 * @brief Dynamic bitset primitive mirroring Virtools XBitArray.
 */

#ifndef NMO_BIT_ARRAY_H
#define NMO_BIT_ARRAY_H

#include "nmo_types.h"
#include "nmo_error.h"
#include "nmo_allocator.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Packed bit vector used to track boolean flags efficiently.
 */
typedef struct nmo_bit_array {
    uint32_t *words;       /**< Packed 32-bit words storing the bits. */
    size_t bit_capacity;   /**< Capacity reported in bits (always multiple of 32). */
    size_t word_capacity;  /**< Allocated word count. */
    nmo_allocator_t alloc; /**< Allocator used for the backing storage. */
} nmo_bit_array_t;

/**
 * @brief Initialize a bit array with an optional starting size.
 *
 * @param array Bit array to initialize (required).
 * @param initial_bits Initial number of addressable bits (rounded up to a multiple of 32).
 * @param allocator Allocator to use (NULL for default).
 */
NMO_API nmo_result_t nmo_bit_array_init(nmo_bit_array_t *array,
                                        size_t initial_bits,
                                        const nmo_allocator_t *allocator);

/**
 * @brief Release storage held by the bit array.
 */
NMO_API void nmo_bit_array_dispose(nmo_bit_array_t *array);

/**
 * @brief Get the current capacity expressed in bits.
 */
NMO_API size_t nmo_bit_array_capacity(const nmo_bit_array_t *array);

/**
 * @brief Ensure at least \p bit_count bits are addressable.
 */
NMO_API nmo_result_t nmo_bit_array_reserve(nmo_bit_array_t *array, size_t bit_count);

/**
 * @brief Set a bit to 1, expanding the array if needed.
 */
NMO_API nmo_result_t nmo_bit_array_set(nmo_bit_array_t *array, size_t index);

/**
 * @brief Clear a bit to 0. Out-of-range indices are ignored.
 */
NMO_API nmo_result_t nmo_bit_array_clear(nmo_bit_array_t *array, size_t index);

/**
 * @brief Toggle the value of a bit, expanding if needed.
 */
NMO_API nmo_result_t nmo_bit_array_toggle(nmo_bit_array_t *array, size_t index);

/**
 * @brief Test whether a bit is set.
 *
 * @return Non-zero when set, zero otherwise.
 */
NMO_API int nmo_bit_array_test(const nmo_bit_array_t *array, size_t index);

/**
 * @brief Set all bits to 0.
 */
NMO_API void nmo_bit_array_clear_all(nmo_bit_array_t *array);

/**
 * @brief Fill all bits with the specified value (0 or 1).
 */
NMO_API void nmo_bit_array_fill(nmo_bit_array_t *array, int value);

/**
 * @brief Count the number of bits set to 1.
 */
NMO_API size_t nmo_bit_array_count_set(const nmo_bit_array_t *array);

/**
 * @brief Find the index of the nth set bit.
 *
 * @param array Bit array to inspect.
 * @param ordinal Zero-based ordinal (0 returns the first set bit).
 * @return Bit index or SIZE_MAX if not found.
 */
NMO_API size_t nmo_bit_array_find_nth_set(const nmo_bit_array_t *array, size_t ordinal);

/**
 * @brief Find the index of the nth unset bit.
 *
 * Expands the bit array if the requested ordinal lies beyond the current capacity.
 *
 * @param array Bit array to inspect.
 * @param ordinal Zero-based ordinal (0 returns the first unset bit).
 * @return Bit index (guaranteed valid).
 */
NMO_API size_t nmo_bit_array_find_nth_unset(nmo_bit_array_t *array, size_t ordinal);

/**
 * @brief In-place AND with another bit array.
 */
NMO_API nmo_result_t nmo_bit_array_and(nmo_bit_array_t *array,
                                       const nmo_bit_array_t *other);

/**
 * @brief In-place OR with another bit array.
 */
NMO_API nmo_result_t nmo_bit_array_or(nmo_bit_array_t *array,
                                      const nmo_bit_array_t *other);

/**
 * @brief In-place XOR with another bit array.
 */
NMO_API nmo_result_t nmo_bit_array_xor(nmo_bit_array_t *array,
                                       const nmo_bit_array_t *other);

/**
 * @brief Invert all bits in the array.
 */
NMO_API void nmo_bit_array_not(nmo_bit_array_t *array);

/**
 * @brief Convert bits to a string of '0'/'1'.
 *
 * @param array Bit array to stringify.
 * @param buffer Destination buffer.
 * @param buffer_size Destination buffer size (must be >= capacity + 1).
 * @return buffer on success or NULL on failure.
 */
NMO_API char *nmo_bit_array_to_string(const nmo_bit_array_t *array,
                                      char *buffer,
                                      size_t buffer_size);

/**
 * @brief Return the allocated memory footprint.
 *
 * @param array Bit array to inspect.
 * @param include_struct Non-zero to include sizeof(nmo_bit_array_t).
 */
NMO_API size_t nmo_bit_array_memory_usage(const nmo_bit_array_t *array,
                                          int include_struct);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BIT_ARRAY_H */
