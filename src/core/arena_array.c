/**
 * @file arena_array.c
 * @brief Generic dynamic array implementation with arena-based memory management
 * 
 * This file implements arena-backed arrays (nmo_arena_array_*).
 */

#include "core/nmo_arena_array.h"
#include "core/nmo_error.h"
#include <string.h>
#include <limits.h>

static size_t nmo_array_alignment(size_t element_size) {
    size_t alignment = element_size < sizeof(void*) ? sizeof(void*) : element_size;
    /* Ensure power of two alignment */
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    return alignment;
}

static void nmo_arena_array_dispose_range(nmo_arena_array_t *array, size_t start, size_t count) {
    if (array == NULL || array->data == NULL || array->lifecycle.dispose == NULL || count == 0) {
        return;
    }

    if (start >= array->count) {
        return;
    }

    if (start + count > array->count) {
        count = array->count - start;
    }

    uint8_t *base = (uint8_t *)array->data + (start * array->element_size);
    for (size_t i = 0; i < count; ++i) {
        array->lifecycle.dispose(base + (i * array->element_size), array->lifecycle.user_data);
    }
}

void nmo_arena_array_set_lifecycle(nmo_arena_array_t *array,
                                    const nmo_container_lifecycle_t *lifecycle) {
    if (!array) {
        return;
    }
    if (lifecycle) {
        array->lifecycle = *lifecycle;
    } else {
        array->lifecycle.dispose = NULL;
        array->lifecycle.user_data = NULL;
    }
}

nmo_result_t nmo_arena_array_init(nmo_arena_array_t *array,
                                   size_t element_size,
                                   size_t initial_capacity,
                                   nmo_arena_t *arena) {
    if (!array || !arena || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array init arguments"));
    }

    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->element_size = element_size;
    array->arena = arena;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (initial_capacity > 0) {
        return nmo_arena_array_reserve(array, initial_capacity);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_reserve(nmo_arena_array_t *array, size_t capacity) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array argument"));
    }

    if (array->arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "array has no arena"));
    }

    if (capacity <= array->capacity) {
        return nmo_result_ok(); // Already have enough capacity
    }

    size_t new_size = capacity * array->element_size;
    size_t alignment = nmo_array_alignment(array->element_size);
    void *new_data = nmo_arena_alloc(array->arena, new_size, alignment);
    if (!new_data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate array memory"));
    }

    // Copy existing data if any
    if (array->data && array->count > 0) {
        memcpy(new_data, array->data, array->count * array->element_size);
    }

    array->data = new_data;
    array->capacity = capacity;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_ensure_space(nmo_arena_array_t *array, size_t additional) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array argument"));
    }

    if (additional > 0 && array->count > SIZE_MAX - additional) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "array size overflow"));
    }

    size_t required = array->count + additional;
    if (required <= array->capacity) {
        return nmo_result_ok(); // Already have enough space
    }

    // Exponential growth: start with 4, then double
    size_t new_capacity = array->capacity == 0 ? 4 : array->capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    return nmo_arena_array_reserve(array, new_capacity);
}

nmo_result_t nmo_arena_array_append(nmo_arena_array_t *array, const void *element) {
    if (!array || !element) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array append arguments"));
    }

    nmo_result_t result = nmo_arena_array_ensure_space(array, 1);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    memcpy(dest, element, array->element_size);
    array->count++;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_append_array(nmo_arena_array_t *array,
                                           const void *elements,
                                           size_t count) {
    if (!array || (!elements && count > 0)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array append array arguments"));
    }

    if (count == 0) {
        return nmo_result_ok();
    }

    nmo_result_t result = nmo_arena_array_ensure_space(array, count);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    memcpy(dest, elements, count * array->element_size);
    array->count += count;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_extend(nmo_arena_array_t *array,
                                     size_t additional,
                                     void **out_begin) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array extend arguments"));
    }

    uint8_t *start = NULL;
    if (additional > 0) {
        nmo_result_t result = nmo_arena_array_ensure_space(array, additional);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (array->data) {
        start = (uint8_t *)array->data + (array->count * array->element_size);
    }

    if (out_begin) {
        *out_begin = start;
    }

    array->count += additional;

    return nmo_result_ok();
}

void *nmo_arena_array_get(const nmo_arena_array_t *array, size_t index) {
    if (!array || index >= array->count) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)array->data;
    return data + (index * array->element_size);
}

nmo_result_t nmo_arena_array_set(nmo_arena_array_t *array, size_t index, const void *element) {
    if (!array || !element || index >= array->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array set arguments"));
    }

    uint8_t *dest = (uint8_t *)array->data + (index * array->element_size);
    nmo_arena_array_dispose_range(array, index, 1);
    memcpy(dest, element, array->element_size);

    return nmo_result_ok();
}

void *nmo_arena_array_front(const nmo_arena_array_t *array) {
    if (!array || array->count == 0) {
        return NULL;
    }
    return array->data;
}

void *nmo_arena_array_back(const nmo_arena_array_t *array) {
    if (!array || array->count == 0) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)array->data;
    return data + ((array->count - 1) * array->element_size);
}

nmo_result_t nmo_arena_array_insert(nmo_arena_array_t *array,
                                     size_t index,
                                     const void *element) {
    if (!array || !element || index > array->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array insert arguments"));
    }

    nmo_result_t result = nmo_arena_array_ensure_space(array, 1);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *dest = base + (index * array->element_size);

    if (index < array->count) {
        size_t move_bytes = (array->count - index) * array->element_size;
        memmove(dest + array->element_size, dest, move_bytes);
    }

    memcpy(dest, element, array->element_size);
    array->count++;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_remove(nmo_arena_array_t *array,
                                     size_t index,
                                     void *out_element) {
    if (!array || index >= array->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array remove arguments"));
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *target = base + (index * array->element_size);

    if (out_element) {
        memcpy(out_element, target, array->element_size);
    }

    nmo_arena_array_dispose_range(array, index, 1);

    if (index < array->count - 1) {
        size_t move_bytes = (array->count - index - 1) * array->element_size;
        memmove(target, target + array->element_size, move_bytes);
    }

    array->count--;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_pop(nmo_arena_array_t *array, void *out_element) {
    if (!array || array->count == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array pop arguments"));
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *target = base + ((array->count - 1) * array->element_size);

    if (out_element) {
        memcpy(out_element, target, array->element_size);
    }

    nmo_arena_array_dispose_range(array, array->count - 1, 1);
    array->count--;

    return nmo_result_ok();
}

void nmo_arena_array_clear(nmo_arena_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_arena_array_dispose_range(array, 0, array->count);
    array->count = 0;
}

nmo_result_t nmo_arena_array_set_data(nmo_arena_array_t *array,
                                       void *data,
                                       size_t count) {
    if (!array || (!data && count > 0)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array set_data arguments"));
    }

    nmo_arena_array_dispose_range(array, 0, array->count);
    array->data = data;
    array->count = count;
    array->capacity = count; // Set capacity to count since data is pre-allocated

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_alloc(nmo_arena_array_t *array,
                                    size_t element_size,
                                    size_t count,
                                    nmo_arena_t *arena) {
    if (!array || !arena || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array alloc arguments"));
    }

    // Initialize array structure
    array->element_size = element_size;
    array->arena = arena;
    array->count = 0;
    array->capacity = 0;
    array->data = NULL;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (count == 0) {
        return nmo_result_ok();
    }

    // Allocate memory
    size_t size = count * element_size;
    size_t alignment = nmo_array_alignment(element_size);
    void *data = nmo_arena_alloc(arena, size, alignment);
    if (!data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate array memory"));
    }

    array->data = data;
    array->count = count;
    array->capacity = count;

    return nmo_result_ok();
}

nmo_result_t nmo_arena_array_clone(const nmo_arena_array_t *src,
                                    nmo_arena_array_t *dest,
                                    nmo_arena_t *arena) {
    if (!src || !dest || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array clone arguments"));
    }

    // Initialize destination array
    nmo_result_t result = nmo_arena_array_init(dest, src->element_size, src->count, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    // Copy data if any
    if (src->count > 0 && src->data) {
        result = nmo_arena_array_append_array(dest, src->data, src->count);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

void nmo_arena_array_dispose(nmo_arena_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_arena_array_clear(array);
    // Arena-backed: no need to free individual data
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

/* Accessor functions */

size_t nmo_arena_array_size(const nmo_arena_array_t *array) {
    return array ? array->count : 0;
}

size_t nmo_arena_array_capacity(const nmo_arena_array_t *array) {
    return array ? array->capacity : 0;
}

size_t nmo_arena_array_element_size(const nmo_arena_array_t *array) {
    return array ? array->element_size : 0;
}

int nmo_arena_array_is_empty(const nmo_arena_array_t *array) {
    return !array || array->count == 0;
}

void *nmo_arena_array_data(const nmo_arena_array_t *array) {
    return array ? array->data : NULL;
}

nmo_result_t nmo_arena_array_swap(nmo_arena_array_t *a, nmo_arena_array_t *b) {
    if (!a || !b) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array swap arguments"));
    }
    
    if (a->element_size != b->element_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Cannot swap arrays with different element sizes"));
    }

    nmo_arena_array_t temp = *a;
    *a = *b;
    *b = temp;
    
    return nmo_result_ok();
}

int nmo_arena_array_find(const nmo_arena_array_t *array,
                          const void *element,
                          size_t *out_index) {
    if (!array || !element || !array->data) {
        return 0;
    }

    uint8_t *data = (uint8_t *)array->data;
    for (size_t i = 0; i < array->count; i++) {
        if (memcmp(data + (i * array->element_size), element, array->element_size) == 0) {
            if (out_index) {
                *out_index = i;
            }
            return 1;
        }
    }
    
    return 0;
}

int nmo_arena_array_contains(const nmo_arena_array_t *array, const void *element) {
    return nmo_arena_array_find(array, element, NULL);
}

nmo_result_t nmo_arena_array_resize(nmo_arena_array_t *array, size_t new_count) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array argument"));
    }

    if (new_count == array->count) {
        return nmo_result_ok();
    }

    if (new_count < array->count) {
        /* Shrink: dispose trailing elements */
        nmo_arena_array_dispose_range(array, new_count, array->count - new_count);
        array->count = new_count;
        return nmo_result_ok();
    }

    /* Grow: ensure capacity and zero-initialize new elements */
    nmo_result_t result = nmo_arena_array_ensure_space(array, new_count - array->count);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *new_start = (uint8_t *)array->data + (array->count * array->element_size);
    size_t new_bytes = (new_count - array->count) * array->element_size;
    memset(new_start, 0, new_bytes);
    array->count = new_count;

    return nmo_result_ok();
}
