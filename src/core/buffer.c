/**
 * @file nmo_buffer.c
 * @brief Generic dynamic buffer implementation
 */

#include "core/nmo_buffer.h"
#include "core/nmo_error.h"
#include <string.h>

nmo_result_t nmo_buffer_init(nmo_buffer_t *buffer,
                             size_t element_size,
                             size_t initial_capacity,
                             nmo_arena_t *arena) {
    if (!buffer || !arena || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer init arguments"));
    }

    buffer->data = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->element_size = element_size;
    buffer->arena = arena;

    if (initial_capacity > 0) {
        return nmo_buffer_reserve(buffer, initial_capacity);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_buffer_reserve(nmo_buffer_t *buffer, size_t capacity) {
    if (!buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer argument"));
    }

    if (capacity <= buffer->capacity) {
        return nmo_result_ok(); // Already have enough capacity
    }

    size_t new_size = capacity * buffer->element_size;
    /* Use standard alignment (sizeof(void*)) instead of element_size to avoid 
     * non-power-of-2 alignments (e.g., 12 bytes for struct { int x, y, z; }) */
    size_t alignment = (buffer->element_size < sizeof(void*)) ? buffer->element_size : sizeof(void*);
    /* Ensure alignment is power of 2 */
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    void *new_data = nmo_arena_alloc(buffer->arena, new_size, alignment);
    if (!new_data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate buffer memory"));
    }

    // Copy existing data if any
    if (buffer->data && buffer->count > 0) {
        memcpy(new_data, buffer->data, buffer->count * buffer->element_size);
    }

    buffer->data = new_data;
    buffer->capacity = capacity;

    return nmo_result_ok();
}

nmo_result_t nmo_buffer_ensure_space(nmo_buffer_t *buffer, size_t additional) {
    if (!buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer argument"));
    }

    size_t required = buffer->count + additional;
    if (required <= buffer->capacity) {
        return nmo_result_ok(); // Already have enough space
    }

    // Exponential growth: start with 4, then double
    size_t new_capacity = buffer->capacity == 0 ? 4 : buffer->capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    return nmo_buffer_reserve(buffer, new_capacity);
}

nmo_result_t nmo_buffer_append(nmo_buffer_t *buffer, const void *element) {
    if (!buffer || !element) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer append arguments"));
    }

    nmo_result_t result = nmo_buffer_ensure_space(buffer, 1);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)buffer->data + (buffer->count * buffer->element_size);
    memcpy(dest, element, buffer->element_size);
    buffer->count++;

    return nmo_result_ok();
}

nmo_result_t nmo_buffer_append_array(nmo_buffer_t *buffer,
                                     const void *elements,
                                     size_t count) {
    if (!buffer || (!elements && count > 0)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer append array arguments"));
    }

    if (count == 0) {
        return nmo_result_ok();
    }

    nmo_result_t result = nmo_buffer_ensure_space(buffer, count);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)buffer->data + (buffer->count * buffer->element_size);
    memcpy(dest, elements, count * buffer->element_size);
    buffer->count += count;

    return nmo_result_ok();
}

void *nmo_buffer_get(const nmo_buffer_t *buffer, size_t index) {
    if (!buffer || index >= buffer->count) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)buffer->data;
    return data + (index * buffer->element_size);
}

nmo_result_t nmo_buffer_set(nmo_buffer_t *buffer, size_t index, const void *element) {
    if (!buffer || !element || index >= buffer->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer set arguments"));
    }

    uint8_t *dest = (uint8_t *)buffer->data + (index * buffer->element_size);
    memcpy(dest, element, buffer->element_size);

    return nmo_result_ok();
}

void nmo_buffer_clear(nmo_buffer_t *buffer) {
    if (buffer) {
        buffer->count = 0;
    }
}

nmo_result_t nmo_buffer_set_data(nmo_buffer_t *buffer,
                                 void *data,
                                 size_t count) {
    if (!buffer || (!data && count > 0)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer set_data arguments"));
    }

    buffer->data = data;
    buffer->count = count;
    buffer->capacity = count; // Set capacity to count since data is pre-allocated

    return nmo_result_ok();
}

nmo_result_t nmo_buffer_alloc(nmo_buffer_t *buffer,
                              size_t element_size,
                              size_t count,
                              nmo_arena_t *arena) {
    if (!buffer || !arena || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer alloc arguments"));
    }

    // Initialize buffer structure
    buffer->element_size = element_size;
    buffer->arena = arena;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->data = NULL;

    if (count == 0) {
        return nmo_result_ok();
    }

    // Allocate memory
    size_t size = count * element_size;
    void *data = nmo_arena_alloc(arena, size, element_size);
    if (!data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate buffer memory"));
    }

    buffer->data = data;
    buffer->count = count;
    buffer->capacity = count;

    return nmo_result_ok();
}

nmo_result_t nmo_buffer_clone(const nmo_buffer_t *src,
                              nmo_buffer_t *dest,
                              nmo_arena_t *arena) {
    if (!src || !dest || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid buffer clone arguments"));
    }

    // Initialize destination buffer
    nmo_result_t result = nmo_buffer_init(dest, src->element_size, src->count, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    // Copy data if any
    if (src->count > 0 && src->data) {
        result = nmo_buffer_append_array(dest, src->data, src->count);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}
