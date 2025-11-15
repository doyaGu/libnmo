/**
 * @file nmo_arena_array.h
 * @brief Generic dynamic array with arena-based memory management
 * 
 * Arena-backed arrays provide automatic memory management through arena allocators.
 * Memory is managed by the arena's reset/destroy operations.
 * 
 * For allocator-backed arrays with explicit memory management, use nmo_array.h
 */

#ifndef NMO_ARENA_ARRAY_H
#define NMO_ARENA_ARRAY_H

#include "nmo_types.h"
#include "nmo_error.h"
#include "nmo_arena.h"
#include "nmo_container_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic dynamic array structure (arena-backed)
 *
 * Arena-backed version: Fast allocation, no individual frees.
 * Memory is managed by arena reset/destroy.
 */
typedef struct nmo_arena_array {
    void *data;                /**< Pointer to array data */
    size_t count;              /**< Number of elements currently used */
    size_t capacity;           /**< Maximum number of elements allocated */
    size_t element_size;       /**< Size of each element in bytes */
    nmo_arena_t *arena;        /**< Arena for allocations */
    nmo_container_lifecycle_t lifecycle; /**< Optional lifecycle hooks */
} nmo_arena_array_t;

/**
 * @brief Initialize an arena-backed array
 *
 * @param array array to initialize (required)
 * @param element_size Size of each element in bytes (required)
 * @param initial_capacity Initial capacity (0 for lazy allocation)
 * @param arena Arena for allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_init(nmo_arena_array_t *array,
                                           size_t element_size,
                                           size_t initial_capacity,
                                           nmo_arena_t *arena);

/**
 * @brief Configure lifecycle callbacks for stored elements.
 *
 * When a dispose callback is provided it is invoked for every element that
 * leaves the array (overwrite, remove, pop, clear, dispose, set_data).
 * Passing NULL resets the lifecycle to a no-op configuration.
 *
 * @param array  Array to configure (required)
 * @param lifecycle Lifecycle hooks (NULL to reset)
 */
NMO_API void nmo_arena_array_set_lifecycle(nmo_arena_array_t *array,
                                            const nmo_container_lifecycle_t *lifecycle);

/**
 * @brief Reserve capacity in array
 *
 * Ensures the array has at least the specified capacity.
 * Does not reduce capacity if current capacity is larger.
 *
 * @param array array to reserve (required)
 * @param capacity Minimum capacity required
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_reserve(nmo_arena_array_t *array, size_t capacity);

/**
 * @brief Ensure space for additional elements
 *
 * Grows array capacity if needed to accommodate additional elements.
 * Uses exponential growth strategy (doubles capacity).
 *
 * @param array array to grow (required)
 * @param additional Number of additional elements needed
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_ensure_space(nmo_arena_array_t *array, size_t additional);

/**
 * @brief Append an element to array
 *
 * @param array array to append to (required)
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_append(nmo_arena_array_t *array, const void *element);

/**
 * @brief Append multiple elements to array
 *
 * @param array array to append to (required)
 * @param elements Pointer to element array (required)
 * @param count Number of elements to append
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_append_array(nmo_arena_array_t *array,
                                                   const void *elements,
                                                   size_t count);

/**
 * @brief Extend array with uninitialized space for additional elements
 *
 * Provides a pointer to the first newly reserved element so callers can
 * write data directly without an intermediate copy.
 *
 * @param array array to extend (required)
 * @param additional Number of elements to add to the logical size
 * @param out_begin Optional pointer that receives the first new element
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_extend(nmo_arena_array_t *array,
                                             size_t additional,
                                             void **out_begin);

/**
 * @brief Get element at index
 *
 * @param array array to access (required)
 * @param index Element index
 * @return Pointer to element or NULL if out of bounds
 */
NMO_API void *nmo_arena_array_get(const nmo_arena_array_t *array, size_t index);

/**
 * @brief Set element at index
 *
 * @param array array to modify (required)
 * @param index Element index
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_set(nmo_arena_array_t *array, size_t index, const void *element);

/**
 * @brief Insert element at the specified position (with shifting)
 *
 * @param array array to modify (required)
 * @param index Insertion index (0..count)
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_insert(nmo_arena_array_t *array,
                                             size_t index,
                                             const void *element);

/**
 * @brief Remove element at index (shifts remaining elements)
 *
 * Optionally copies the removed element into @p out_element.
 *
 * @param array array to modify (required)
 * @param index Element index
 * @param out_element Optional pointer receiving removed element data
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_remove(nmo_arena_array_t *array,
                                             size_t index,
                                             void *out_element);

/**
 * @brief Pop element from the end of the array
 *
 * @param array array to modify (required)
 * @param out_element Optional pointer receiving removed element
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_pop(nmo_arena_array_t *array, void *out_element);

/**
 * @brief Get pointer to first element (or NULL if empty)
 */
NMO_API void *nmo_arena_array_front(const nmo_arena_array_t *array);

/**
 * @brief Get pointer to last element (or NULL if empty)
 */
NMO_API void *nmo_arena_array_back(const nmo_arena_array_t *array);

/**
 * @brief Clear array (reset count to 0)
 *
 * Does not deallocate memory, just resets the count. If a lifecycle dispose
 * callback is configured it will be executed for each element before the
 * logical contents are dropped.
 *
 * @param array array to clear (required)
 */
NMO_API void nmo_arena_array_clear(nmo_arena_array_t *array);

/**
 * @brief Set array data directly (takes ownership of pre-allocated data)
 *
 * This is useful when you already have allocated data and want to
 * avoid an extra copy. The array will NOT free this data (arena manages it).
 *
 * @param array array to set (required, should be initialized)
 * @param data Pre-allocated data pointer (required)
 * @param count Number of elements in data
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_set_data(nmo_arena_array_t *array,
                                               void *data,
                                               size_t count);

/**
 * @brief Allocate and initialize array with given count
 *
 * Combines init + reserve + allocation in one step. Allocates space
 * for 'count' elements and sets count to that value.
 *
 * @param array array to initialize (required)
 * @param element_size Size of each element in bytes (required)
 * @param count Number of elements to allocate
 * @param arena Arena for allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_alloc(nmo_arena_array_t *array,
                                            size_t element_size,
                                            size_t count,
                                            nmo_arena_t *arena);

/**
 * @brief Clone array
 *
 * Creates a deep copy of the array with all its elements.
 *
 * @param src Source array (required)
 * @param dest Destination array (required, will be initialized)
 * @param arena Arena for destination allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_clone(const nmo_arena_array_t *src,
                                            nmo_arena_array_t *dest,
                                            nmo_arena_t *arena);

/**
 * @brief Release bookkeeping and reset the array
 *
 * For arena-backed arrays this only clears bookkeeping fields.
 * The actual memory is managed by the arena.
 */
NMO_API void nmo_arena_array_dispose(nmo_arena_array_t *array);

/* Accessor functions to avoid direct struct access */

/**
 * @brief Get the number of elements in the array
 * @param array Array to query (required)
 * @return Number of elements or 0 if array is NULL
 */
NMO_API size_t nmo_arena_array_size(const nmo_arena_array_t *array);

/**
 * @brief Get array capacity
 * @param array Array to query (required)
 * @return Capacity or 0 if array is NULL
 */
NMO_API size_t nmo_arena_array_capacity(const nmo_arena_array_t *array);

/**
 * @brief Get element size
 * @param array Array to query (required)
 * @return Element size or 0 if array is NULL
 */
NMO_API size_t nmo_arena_array_element_size(const nmo_arena_array_t *array);

/**
 * @brief Check if array is empty
 * @param array Array to query (required)
 * @return 1 if empty, 0 otherwise
 */
NMO_API int nmo_arena_array_is_empty(const nmo_arena_array_t *array);

/**
 * @brief Get direct pointer to array data
 * 
 * Use with caution. Pointer may be invalidated by operations that grow the array.
 * 
 * @param array Array to query (required)
 * @return Data pointer or NULL if empty
 */
NMO_API void *nmo_arena_array_data(const nmo_arena_array_t *array);

/**
 * @brief Swap contents of two arrays (O(1) operation)
 * 
 * Arrays must have the same element size.
 * 
 * @param a First array (required)
 * @param b Second array (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_swap(nmo_arena_array_t *a, nmo_arena_array_t *b);

/**
 * @brief Find first occurrence of element
 * 
 * Uses memcmp for comparison.
 * 
 * @param array Array to search (required)
 * @param element Element to find (required)
 * @param out_index Output index (optional)
 * @return 1 if found, 0 otherwise
 */
NMO_API int nmo_arena_array_find(const nmo_arena_array_t *array,
                                  const void *element,
                                  size_t *out_index);

/**
 * @brief Check if array contains element
 * 
 * @param array Array to search (required)
 * @param element Element to find (required)
 * @return 1 if found, 0 otherwise
 */
NMO_API int nmo_arena_array_contains(const nmo_arena_array_t *array, const void *element);

/**
 * @brief Resize array to specified count
 * 
 * If new count > current count, new elements are zero-initialized.
 * If new count < current count, trailing elements are disposed.
 * 
 * @param array Array to resize (required)
 * @param new_count New element count
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_arena_array_resize(nmo_arena_array_t *array, size_t new_count);

/* Convenience macros for typed array operations */

/**
 * @brief Create typed array initializer
 *
 * Usage: NMO_ARENA_ARRAY_INIT(uint32_t, arena_ptr)
 */
#define NMO_ARENA_ARRAY_INIT(type, arena_ptr) \
    { NULL, 0, 0, sizeof(type), (arena_ptr), NMO_CONTAINER_LIFECYCLE_INIT }

/**
 * @brief Get typed element from array
 *
 * Usage: uint32_t *val = NMO_ARENA_ARRAY_GET(uint32_t, &array, i);
 */
#define NMO_ARENA_ARRAY_GET(type, array_ptr, index) \
    ((type *)nmo_arena_array_get(array_ptr, index))

/**
 * @brief Append typed element to array
 *
 * Usage: NMO_ARENA_ARRAY_APPEND(uint32_t, &array, value);
 */
#define NMO_ARENA_ARRAY_APPEND(type, array_ptr, value_expr)            \
    do {                                                               \
        type nmo_array_temp_value = (value_expr);                      \
        nmo_arena_array_append((array_ptr), &nmo_array_temp_value);   \
    } while (0)

/**
 * @brief Get array data as typed pointer
 *
 * Usage: uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &array);
 */
#define NMO_ARENA_ARRAY_DATA(type, array_ptr) \
    ((type *)((array_ptr)->data))

/**
 * @brief Access first element with type safety
 *
 * Usage: uint32_t *front = NMO_ARENA_ARRAY_FRONT(uint32_t, &array);
 */
#define NMO_ARENA_ARRAY_FRONT(type, array_ptr) \
    ((type *)nmo_arena_array_front(array_ptr))

/**
 * @brief Access last element with type safety
 *
 * Usage: uint32_t *back = NMO_ARENA_ARRAY_BACK(uint32_t, &array);
 */
#define NMO_ARENA_ARRAY_BACK(type, array_ptr) \
    ((type *)nmo_arena_array_back(array_ptr))

/**
 * @brief Extend array with typed pointer output
 *
 * Usage:
 * uint32_t *values = NULL;
 * NMO_ARENA_ARRAY_EXTEND(&array, 4, values);
 */
#define NMO_ARENA_ARRAY_EXTEND(array_ptr, count, out_ptr_var) \
    nmo_arena_array_extend(array_ptr, count, (void **)&(out_ptr_var))

/**
 * @brief Pop element into a typed destination
 *
 * Usage: uint32_t value; NMO_ARENA_ARRAY_POP(&array, value);
 */
#define NMO_ARENA_ARRAY_POP(type, array_ptr, out_var)                  \
    do {                                                               \
        type nmo_array_temp_value;                                     \
        nmo_arena_array_pop((array_ptr), &nmo_array_temp_value);      \
        (out_var) = nmo_array_temp_value;                              \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* NMO_ARENA_ARRAY_H */
