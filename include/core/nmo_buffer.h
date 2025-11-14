/**
 * @file nmo_buffer.h
 * @brief Generic dynamic buffer for arrays with capacity management
 */

#ifndef NMO_BUFFER_H
#define NMO_BUFFER_H

#include "nmo_types.h"
#include "nmo_error.h"
#include "nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic dynamic buffer structure
 *
 * This structure provides a reusable pattern for managing dynamic arrays
 * with automatic capacity growth. Used for data buffers, ID lists,
 * sub-chunk lists, manager lists, etc.
 *
 * The buffer stores elements of a fixed size (element_size) and grows
 * automatically when capacity is exceeded.
 */
typedef struct nmo_buffer {
    void *data;           /**< Pointer to buffer data */
    size_t count;         /**< Number of elements currently used */
    size_t capacity;      /**< Maximum number of elements allocated */
    size_t element_size;  /**< Size of each element in bytes */
    nmo_arena_t *arena;   /**< Arena for allocations */
} nmo_buffer_t;

/**
 * @brief Initialize a buffer
 *
 * @param buffer Buffer to initialize (required)
 * @param element_size Size of each element in bytes (required)
 * @param initial_capacity Initial capacity (0 for lazy allocation)
 * @param arena Arena for allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_init(nmo_buffer_t *buffer,
                                     size_t element_size,
                                     size_t initial_capacity,
                                     nmo_arena_t *arena);

/**
 * @brief Reserve capacity in buffer
 *
 * Ensures the buffer has at least the specified capacity.
 * Does not reduce capacity if current capacity is larger.
 *
 * @param buffer Buffer to reserve (required)
 * @param capacity Minimum capacity required
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_reserve(nmo_buffer_t *buffer, size_t capacity);

/**
 * @brief Ensure space for additional elements
 *
 * Grows buffer capacity if needed to accommodate additional elements.
 * Uses exponential growth strategy (doubles capacity).
 *
 * @param buffer Buffer to grow (required)
 * @param additional Number of additional elements needed
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_ensure_space(nmo_buffer_t *buffer, size_t additional);

/**
 * @brief Append an element to buffer
 *
 * @param buffer Buffer to append to (required)
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_append(nmo_buffer_t *buffer, const void *element);

/**
 * @brief Append multiple elements to buffer
 *
 * @param buffer Buffer to append to (required)
 * @param elements Pointer to element array (required)
 * @param count Number of elements to append
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_append_array(nmo_buffer_t *buffer,
                                             const void *elements,
                                             size_t count);

/**
 * @brief Extend buffer with uninitialized space for additional elements
 *
 * Provides a pointer to the first newly reserved element so callers can
 * write data directly without an intermediate copy.
 *
 * @param buffer Buffer to extend (required)
 * @param additional Number of elements to add to the logical size
 * @param out_begin Optional pointer that receives the first new element
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_extend(nmo_buffer_t *buffer,
                                       size_t additional,
                                       void **out_begin);

/**
 * @brief Get element at index
 *
 * @param buffer Buffer to access (required)
 * @param index Element index
 * @return Pointer to element or NULL if out of bounds
 */
NMO_API void *nmo_buffer_get(const nmo_buffer_t *buffer, size_t index);

/**
 * @brief Set element at index
 *
 * @param buffer Buffer to modify (required)
 * @param index Element index
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_set(nmo_buffer_t *buffer, size_t index, const void *element);

/**
 * @brief Insert element at the specified position (with shifting)
 *
 * @param buffer Buffer to modify (required)
 * @param index Insertion index (0..count)
 * @param element Pointer to element data (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_insert(nmo_buffer_t *buffer,
                                       size_t index,
                                       const void *element);

/**
 * @brief Remove element at index (shifts remaining elements)
 *
 * Optionally copies the removed element into @p out_element.
 *
 * @param buffer Buffer to modify (required)
 * @param index Element index
 * @param out_element Optional pointer receiving removed element data
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_remove(nmo_buffer_t *buffer,
                                       size_t index,
                                       void *out_element);

/**
 * @brief Pop element from the end of the buffer
 *
 * @param buffer Buffer to modify (required)
 * @param out_element Optional pointer receiving removed element
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_pop(nmo_buffer_t *buffer, void *out_element);

/**
 * @brief Get pointer to first element (or NULL if empty)
 */
NMO_API void *nmo_buffer_front(const nmo_buffer_t *buffer);

/**
 * @brief Get pointer to last element (or NULL if empty)
 */
NMO_API void *nmo_buffer_back(const nmo_buffer_t *buffer);

/**
 * @brief Clear buffer (reset count to 0)
 *
 * Does not deallocate memory, just resets the count.
 *
 * @param buffer Buffer to clear (required)
 */
NMO_API void nmo_buffer_clear(nmo_buffer_t *buffer);

/**
 * @brief Set buffer data directly (takes ownership of pre-allocated data)
 *
 * This is useful when you already have allocated data and want to
 * avoid an extra copy. The buffer will NOT free this data (arena manages it).
 *
 * @param buffer Buffer to set (required, should be initialized)
 * @param data Pre-allocated data pointer (required)
 * @param count Number of elements in data
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_set_data(nmo_buffer_t *buffer,
                                         void *data,
                                         size_t count);

/**
 * @brief Allocate and initialize buffer with given count
 *
 * Combines init + reserve + allocation in one step. Allocates space
 * for 'count' elements and sets count to that value.
 *
 * @param buffer Buffer to initialize (required)
 * @param element_size Size of each element in bytes (required)
 * @param count Number of elements to allocate
 * @param arena Arena for allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_alloc(nmo_buffer_t *buffer,
                                      size_t element_size,
                                      size_t count,
                                      nmo_arena_t *arena);

/**
 * @brief Clone buffer
 *
 * Creates a deep copy of the buffer with all its elements.
 *
 * @param src Source buffer (required)
 * @param dest Destination buffer (required, will be initialized)
 * @param arena Arena for destination allocations (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_buffer_clone(const nmo_buffer_t *src,
                                     nmo_buffer_t *dest,
                                     nmo_arena_t *arena);

/* Convenience macros for typed buffer operations */

/**
 * @brief Create typed buffer initializer
 *
 * Usage: NMO_BUFFER_INIT(uint32_t, 16, arena)
 */
#define NMO_BUFFER_INIT(type, initial_capacity, arena_ptr) \
    { NULL, 0, 0, sizeof(type), arena_ptr }

/**
 * @brief Get typed element from buffer
 *
 * Usage: uint32_t *val = NMO_BUFFER_GET(uint32_t, &buffer, i);
 */
#define NMO_BUFFER_GET(type, buffer_ptr, index) \
    ((type *)nmo_buffer_get(buffer_ptr, index))

/**
 * @brief Append typed element to buffer
 *
 * Usage: NMO_BUFFER_APPEND(uint32_t, &buffer, &value);
 */
#define NMO_BUFFER_APPEND(type, buffer_ptr, element_ptr) \
    nmo_buffer_append(buffer_ptr, element_ptr)

/**
 * @brief Get buffer data as typed pointer
 *
 * Usage: uint32_t *data = NMO_BUFFER_DATA(uint32_t, &buffer);
 */
#define NMO_BUFFER_DATA(type, buffer_ptr) \
    ((type *)((buffer_ptr)->data))

/**
 * @brief Access first element with type safety
 *
 * Usage: uint32_t *front = NMO_BUFFER_FRONT(uint32_t, &buffer);
 */
#define NMO_BUFFER_FRONT(type, buffer_ptr) \
    ((type *)nmo_buffer_front(buffer_ptr))

/**
 * @brief Access last element with type safety
 *
 * Usage: uint32_t *back = NMO_BUFFER_BACK(uint32_t, &buffer);
 */
#define NMO_BUFFER_BACK(type, buffer_ptr) \
    ((type *)nmo_buffer_back(buffer_ptr))

/**
 * @brief Extend buffer with typed pointer output
 *
 * Usage:
 * uint32_t *values = NULL;
 * NMO_BUFFER_EXTEND(&buffer, 4, values);
 */
#define NMO_BUFFER_EXTEND(buffer_ptr, count, out_ptr_var) \
    nmo_buffer_extend(buffer_ptr, count, (void **)&(out_ptr_var))

/**
 * @brief Pop element into a typed destination
 *
 * Usage: uint32_t value; NMO_BUFFER_POP(&buffer, &value);
 */
#define NMO_BUFFER_POP(buffer_ptr, out_ptr) \
    nmo_buffer_pop(buffer_ptr, out_ptr)

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUFFER_H */
