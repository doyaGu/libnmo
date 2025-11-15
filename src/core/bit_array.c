/**
 * @file bit_array.c
 * @brief Packed bitset utilities mirroring Virtools XBitArray behaviour.
 */

#include "core/nmo_bit_array.h"

#include <string.h>

#define NMO_BITS_PER_WORD 32U

static size_t nmo_bit_array_words_for_bits(size_t bits) {
    if (bits == 0) {
        return 0;
    }
    return (bits + (NMO_BITS_PER_WORD - 1)) / NMO_BITS_PER_WORD;
}

static nmo_result_t nmo_bit_array_grow(nmo_bit_array_t *array, size_t new_word_count) {
    if (new_word_count <= array->word_capacity) {
        return nmo_result_ok();
    }

    size_t target = array->word_capacity ? array->word_capacity : 1;
    while (target < new_word_count) {
        if (target > SIZE_MAX / 2) {
            target = new_word_count;
            break;
        }
        target *= 2;
    }

    size_t bytes = target * sizeof(uint32_t);
    uint32_t *words = (uint32_t *) nmo_alloc(&array->alloc, bytes, sizeof(uint32_t));
    if (words == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate bit array storage"));
    }

    size_t existing_bytes = array->word_capacity * sizeof(uint32_t);
    if (existing_bytes > 0 && array->words != NULL) {
        memcpy(words, array->words, existing_bytes);
    }

    if (bytes > existing_bytes) {
        memset((uint8_t *)words + existing_bytes, 0, bytes - existing_bytes);
    }

    if (array->words != NULL) {
        nmo_free(&array->alloc, array->words);
    }

    array->words = words;
    array->word_capacity = target;
    array->bit_capacity = target * NMO_BITS_PER_WORD;
    return nmo_result_ok();
}

static nmo_result_t nmo_bit_array_ensure_index(nmo_bit_array_t *array, size_t index) {
    size_t required_bits = index + 1;
    size_t required_words = nmo_bit_array_words_for_bits(required_bits);
    return nmo_bit_array_grow(array, required_words);
}

nmo_result_t nmo_bit_array_init(nmo_bit_array_t *array,
                                size_t initial_bits,
                                const nmo_allocator_t *allocator) {
    if (array == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "array must not be NULL"));
    }

    memset(array, 0, sizeof(*array));
    array->alloc = allocator ? *allocator : nmo_allocator_default();

    size_t initial_words = nmo_bit_array_words_for_bits(initial_bits);
    if (initial_words == 0) {
        return nmo_result_ok();
    }

    nmo_result_t grow = nmo_bit_array_grow(array, initial_words);
    if (grow.code != NMO_OK) {
        return grow;
    }

    return nmo_result_ok();
}

void nmo_bit_array_dispose(nmo_bit_array_t *array) {
    if (array == NULL) {
        return;
    }

    if (array->words != NULL) {
        nmo_free(&array->alloc, array->words);
    }

    memset(array, 0, sizeof(*array));
}

size_t nmo_bit_array_capacity(const nmo_bit_array_t *array) {
    return array ? array->bit_capacity : 0;
}

nmo_result_t nmo_bit_array_reserve(nmo_bit_array_t *array, size_t bit_count) {
    if (array == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "array must not be NULL"));
    }
    size_t required_words = nmo_bit_array_words_for_bits(bit_count);
    return nmo_bit_array_grow(array, required_words);
}

nmo_result_t nmo_bit_array_set(nmo_bit_array_t *array, size_t index) {
    if (array == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "array must not be NULL"));
    }

    nmo_result_t ensure = nmo_bit_array_ensure_index(array, index);
    if (ensure.code != NMO_OK) {
        return ensure;
    }

    size_t word = index / NMO_BITS_PER_WORD;
    size_t bit = index % NMO_BITS_PER_WORD;
    array->words[word] |= (1U << bit);
    return nmo_result_ok();
}

nmo_result_t nmo_bit_array_clear(nmo_bit_array_t *array, size_t index) {
    if (array == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "array must not be NULL"));
    }

    if (index >= array->bit_capacity || array->words == NULL) {
        return nmo_result_ok();
    }

    size_t word = index / NMO_BITS_PER_WORD;
    size_t bit = index % NMO_BITS_PER_WORD;
    array->words[word] &= ~(1U << bit);
    return nmo_result_ok();
}

nmo_result_t nmo_bit_array_toggle(nmo_bit_array_t *array, size_t index) {
    if (array == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "array must not be NULL"));
    }

    nmo_result_t ensure = nmo_bit_array_ensure_index(array, index);
    if (ensure.code != NMO_OK) {
        return ensure;
    }

    size_t word = index / NMO_BITS_PER_WORD;
    size_t bit = index % NMO_BITS_PER_WORD;
    array->words[word] ^= (1U << bit);
    return nmo_result_ok();
}

int nmo_bit_array_test(const nmo_bit_array_t *array, size_t index) {
    if (array == NULL || array->words == NULL || index >= array->bit_capacity) {
        return 0;
    }

    size_t word = index / NMO_BITS_PER_WORD;
    size_t bit = index % NMO_BITS_PER_WORD;
    return (array->words[word] & (1U << bit)) != 0;
}

void nmo_bit_array_clear_all(nmo_bit_array_t *array) {
    if (array == NULL || array->words == NULL) {
        return;
    }
    memset(array->words, 0, array->word_capacity * sizeof(uint32_t));
}

void nmo_bit_array_fill(nmo_bit_array_t *array, int value) {
    if (array == NULL || array->words == NULL) {
        return;
    }
    memset(array->words, value ? 0xFF : 0x00, array->word_capacity * sizeof(uint32_t));
}

static size_t nmo_bit_array_popcount32(uint32_t value) {
#if defined(__GNUC__)
    return (size_t)__builtin_popcount(value);
#else
    size_t count = 0;
    while (value != 0) {
        value &= value - 1U;
        ++count;
    }
    return count;
#endif
}

size_t nmo_bit_array_count_set(const nmo_bit_array_t *array) {
    if (array == NULL || array->words == NULL) {
        return 0;
    }

    size_t total = 0;
    for (size_t i = 0; i < array->word_capacity; ++i) {
        total += nmo_bit_array_popcount32(array->words[i]);
    }
    return total;
}

size_t nmo_bit_array_find_nth_set(const nmo_bit_array_t *array, size_t ordinal) {
    if (array == NULL || array->words == NULL) {
        return SIZE_MAX;
    }

    size_t seen = 0;
    size_t bits = array->bit_capacity;
    for (size_t bit = 0; bit < bits; ++bit) {
        if (nmo_bit_array_test(array, bit)) {
            if (seen == ordinal) {
                return bit;
            }
            ++seen;
        }
    }
    return SIZE_MAX;
}

size_t nmo_bit_array_find_nth_unset(nmo_bit_array_t *array, size_t ordinal) {
    if (array == NULL) {
        return SIZE_MAX;
    }

    size_t seen = 0;
    size_t bits = array->bit_capacity;
    for (size_t bit = 0; bit < bits; ++bit) {
        if (!nmo_bit_array_test(array, bit)) {
            if (seen == ordinal) {
                return bit;
            }
            ++seen;
        }
    }

    size_t target = bits + (ordinal - seen);
    nmo_bit_array_ensure_index(array, target);
    return target;
}

static size_t nmo_bit_array_min_words(const nmo_bit_array_t *array,
                                      const nmo_bit_array_t *other) {
    size_t lhs = array ? array->word_capacity : 0;
    size_t rhs = other ? other->word_capacity : 0;
    return lhs < rhs ? lhs : rhs;
}

static nmo_result_t nmo_bit_array_match_size(nmo_bit_array_t *array,
                                             const nmo_bit_array_t *other) {
    if (array == NULL || other == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid bit array operands"));
    }
    return nmo_bit_array_grow(array, other->word_capacity);
}

nmo_result_t nmo_bit_array_and(nmo_bit_array_t *array,
                               const nmo_bit_array_t *other) {
    nmo_result_t ensure = nmo_bit_array_match_size(array, other);
    if (ensure.code != NMO_OK) {
        return ensure;
    }

    size_t count = nmo_bit_array_min_words(array, other);
    for (size_t i = 0; i < count; ++i) {
        array->words[i] &= other->words ? other->words[i] : 0;
    }
    if (count < array->word_capacity) {
        memset(array->words + count, 0, (array->word_capacity - count) * sizeof(uint32_t));
    }
    return nmo_result_ok();
}

nmo_result_t nmo_bit_array_or(nmo_bit_array_t *array,
                              const nmo_bit_array_t *other) {
    nmo_result_t ensure = nmo_bit_array_match_size(array, other);
    if (ensure.code != NMO_OK) {
        return ensure;
    }

    size_t count = nmo_bit_array_min_words(array, other);
    for (size_t i = 0; i < count; ++i) {
        array->words[i] |= other->words ? other->words[i] : 0;
    }
    return nmo_result_ok();
}

nmo_result_t nmo_bit_array_xor(nmo_bit_array_t *array,
                               const nmo_bit_array_t *other) {
    nmo_result_t ensure = nmo_bit_array_match_size(array, other);
    if (ensure.code != NMO_OK) {
        return ensure;
    }

    size_t count = nmo_bit_array_min_words(array, other);
    for (size_t i = 0; i < count; ++i) {
        array->words[i] ^= other->words ? other->words[i] : 0;
    }
    return nmo_result_ok();
}

void nmo_bit_array_not(nmo_bit_array_t *array) {
    if (array == NULL || array->words == NULL) {
        return;
    }
    for (size_t i = 0; i < array->word_capacity; ++i) {
        array->words[i] = ~array->words[i];
    }
}

char *nmo_bit_array_to_string(const nmo_bit_array_t *array,
                              char *buffer,
                              size_t buffer_size) {
    if (array == NULL || buffer == NULL) {
        return NULL;
    }
    if (buffer_size <= array->bit_capacity) {
        return NULL;
    }

    for (size_t bit = 0; bit < array->bit_capacity; ++bit) {
        buffer[bit] = nmo_bit_array_test(array, bit) ? '1' : '0';
    }
    buffer[array->bit_capacity] = '\0';
    return buffer;
}

size_t nmo_bit_array_memory_usage(const nmo_bit_array_t *array, int include_struct) {
    size_t bytes = 0;
    if (array != NULL) {
        bytes = array->word_capacity * sizeof(uint32_t);
        if (include_struct) {
            bytes += sizeof(*array);
        }
    }
    return bytes;
}
