/**
 * @file nmo_array.h
 * @brief Generic dynamic array with allocator-based memory management
 * 
 * Allocator-backed arrays provide explicit memory management.
 * Requires explicit dispose() call to free memory.
 * 
 * For arena-backed arrays with automatic management, use nmo_arena_array.h
 */

#ifndef NMO_ARRAY_H
#define NMO_ARRAY_H

#include "nmo_types.h"
#include "nmo_error.h"
#include "nmo_allocator.h"
#include "nmo_container_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic dynamic array structure (allocator-backed)
 *
 * Allocator-backed version: Explicit memory management with individual ownership.
 * Requires explicit dispose() call to free memory.
 */
typedef struct nmo_array {
    void *data;                /**< Pointer to array data */
    size_t count;              /**< Number of elements currently used */
    size_t capacity;           /**< Maximum number of elements allocated */
    size_t element_size;       /**< Size of each element in bytes */
    nmo_allocator_t allocator; /**< Allocator for allocations */
    nmo_container_lifecycle_t lifecycle; /**< Optional lifecycle hooks */
} nmo_array_t;

/**
 * @brief Initialize an allocator-backed array
 *
 * @param array array to initialize (required)
 * @param element_size Size of each element in bytes (required)
 * @param initial_capacity Initial capacity (0 for lazy allocation)
 * @param allocator Allocator to use (NULL for default)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_array_init(nmo_array_t *array,
                                     size_t element_size,
                                     size_t initial_capacity,
                                     const nmo_allocator_t *allocator);

/**
 * @brief Configure lifecycle callbacks for stored elements.
 */
NMO_API void nmo_array_set_lifecycle(nmo_array_t *array,
                                      const nmo_container_lifecycle_t *lifecycle);

/**
 * @brief Reserve capacity in array
 */
NMO_API nmo_result_t nmo_array_reserve(nmo_array_t *array, size_t capacity);

/**
 * @brief Ensure space for additional elements
 */
NMO_API nmo_result_t nmo_array_ensure_space(nmo_array_t *array, size_t additional);

/**
 * @brief Append an element to array
 */
NMO_API nmo_result_t nmo_array_append(nmo_array_t *array, const void *element);

/**
 * @brief Append multiple elements to array
 */
NMO_API nmo_result_t nmo_array_append_array(nmo_array_t *array,
                                             const void *elements,
                                             size_t count);

/**
 * @brief Extend array with uninitialized space
 */
NMO_API nmo_result_t nmo_array_extend(nmo_array_t *array,
                                       size_t additional,
                                       void **out_begin);

/**
 * @brief Get element at index
 */
NMO_API void *nmo_array_get(const nmo_array_t *array, size_t index);

/**
 * @brief Set element at index
 */
NMO_API nmo_result_t nmo_array_set(nmo_array_t *array, size_t index, const void *element);

/**
 * @brief Insert element at position
 */
NMO_API nmo_result_t nmo_array_insert(nmo_array_t *array,
                                       size_t index,
                                       const void *element);

/**
 * @brief Remove element at index
 */
NMO_API nmo_result_t nmo_array_remove(nmo_array_t *array,
                                       size_t index,
                                       void *out_element);

/**
 * @brief Pop element from end
 */
NMO_API nmo_result_t nmo_array_pop(nmo_array_t *array, void *out_element);

/**
 * @brief Get pointer to first element
 */
NMO_API void *nmo_array_front(const nmo_array_t *array);

/**
 * @brief Get pointer to last element
 */
NMO_API void *nmo_array_back(const nmo_array_t *array);

/**
 * @brief Clear array
 */
NMO_API void nmo_array_clear(nmo_array_t *array);

/**
 * @brief Set array data directly
 */
NMO_API nmo_result_t nmo_array_set_data(nmo_array_t *array,
                                         void *data,
                                         size_t count);

/**
 * @brief Allocate and initialize array
 */
NMO_API nmo_result_t nmo_array_alloc(nmo_array_t *array,
                                      size_t element_size,
                                      size_t count,
                                      const nmo_allocator_t *allocator);

/**
 * @brief Clone array
 */
NMO_API nmo_result_t nmo_array_clone(const nmo_array_t *src,
                                      nmo_array_t *dest,
                                      const nmo_allocator_t *allocator);

/**
 * @brief Release storage and reset array
 */
NMO_API void nmo_array_dispose(nmo_array_t *array);

/* Accessor functions */
NMO_API size_t nmo_array_size(const nmo_array_t *array);
NMO_API size_t nmo_array_capacity(const nmo_array_t *array);
NMO_API size_t nmo_array_element_size(const nmo_array_t *array);
NMO_API int nmo_array_is_empty(const nmo_array_t *array);
NMO_API void *nmo_array_data(const nmo_array_t *array);

/**
 * @brief Swap contents of two arrays
 */
NMO_API nmo_result_t nmo_array_swap(nmo_array_t *a, nmo_array_t *b);

/**
 * @brief Find first occurrence of element
 */
NMO_API int nmo_array_find(const nmo_array_t *array,
                            const void *element,
                            size_t *out_index);

/**
 * @brief Check if array contains element
 */
NMO_API int nmo_array_contains(const nmo_array_t *array, const void *element);

/**
 * @brief Shrink capacity to match count
 */
NMO_API nmo_result_t nmo_array_shrink_to_fit(nmo_array_t *array);

/**
 * @brief Resize array to specified count
 */
NMO_API nmo_result_t nmo_array_resize(nmo_array_t *array, size_t new_count);

/* Convenience macros */
#define NMO_ARRAY_GET(type, array_ptr, index) \
    ((type *)nmo_array_get(array_ptr, index))

#define NMO_ARRAY_APPEND(type, array_ptr, value_expr)            \
    do {                                                         \
        type nmo_array_temp_value = (value_expr);                \
        nmo_array_append((array_ptr), &nmo_array_temp_value);    \
    } while (0)

#define NMO_ARRAY_DATA(type, array_ptr) \
    ((type *)((array_ptr)->data))

#define NMO_ARRAY_FRONT(type, array_ptr) \
    ((type *)nmo_array_front(array_ptr))

#define NMO_ARRAY_BACK(type, array_ptr) \
    ((type *)nmo_array_back(array_ptr))

#define NMO_ARRAY_EXTEND(array_ptr, count, out_ptr_var) \
    nmo_array_extend(array_ptr, count, (void **)&(out_ptr_var))

#define NMO_ARRAY_POP(type, array_ptr, out_var)                  \
    do {                                                         \
        type nmo_array_temp_value;                               \
        nmo_array_pop((array_ptr), &nmo_array_temp_value);       \
        (out_var) = nmo_array_temp_value;                        \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* NMO_ARRAY_H */
