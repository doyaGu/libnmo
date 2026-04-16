/**
 * @file array.c
 * @brief Generic dynamic array implementation with allocator-based memory management
 * 
 * This file implements allocator-backed arrays (nmo_array_*).
 */

#include "core/nmo_array.h"
#include "core/nmo_utils.h"
#include "core/nmo_error.h"
#include <string.h>
#include <limits.h>

static void nmo_array_copy_range(nmo_array_t *array,
                                 uint8_t *dest,
                                 const uint8_t *src,
                                 size_t count) {
    if (array == NULL || dest == NULL || src == NULL || count == 0) {
        return;
    }

    if (array->lifecycle.copy) {
        size_t stride = array->element_size;
        for (size_t i = 0; i < count; ++i) {
            nmo_container_copy_element(&array->lifecycle,
                                       dest + (i * stride),
                                       src + (i * stride),
                                       stride);
        }
    } else {
        memcpy(dest, src, count * array->element_size);
    }
}

static void nmo_array_init_range(nmo_array_t *array, size_t start, size_t count) {
    if (array == NULL || array->data == NULL || count == 0) {
        return;
    }

    if (start > array->capacity || count > array->capacity - start) {
        return;
    }

    uint8_t *base = (uint8_t *)array->data + (start * array->element_size);
    size_t bytes = 0;
    if (!nmo_safe_mul_size(count, array->element_size, &bytes)) {
        return;
    }
    if (array->lifecycle.init == NULL) {
        memset(base, 0, bytes);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_container_init_element(&array->lifecycle,
                                   base + (i * array->element_size),
                                   array->element_size);
    }
}

static void nmo_array_move_range(nmo_array_t *array,
                                 uint8_t *dest,
                                 uint8_t *src,
                                 size_t count) {
    if (array == NULL || dest == NULL || src == NULL || count == 0 || dest == src) {
        return;
    }

    if (array->lifecycle.move) {
        size_t stride = array->element_size;
        if (dest < src) {
            for (size_t i = 0; i < count; ++i) {
                nmo_container_move_element(&array->lifecycle,
                                           dest + (i * stride),
                                           src + (i * stride),
                                           stride);
            }
        } else {
            for (size_t i = count; i > 0; --i) {
                nmo_container_move_element(&array->lifecycle,
                                           dest + ((i - 1) * stride),
                                           src + ((i - 1) * stride),
                                           stride);
            }
        }
    } else {
        memmove(dest, src, count * array->element_size);
    }
}

static void nmo_array_dispose_range(nmo_array_t *array, size_t start, size_t count) {
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

static void nmo_array_reset_range(nmo_array_t *array, size_t start, size_t count) {
    if (array == NULL || array->data == NULL || count == 0) {
        return;
    }

    if (start >= array->count) {
        return;
    }

    if (start + count > array->count) {
        count = array->count - start;
    }

    uint8_t *base = (uint8_t *)array->data + (start * array->element_size);
    if (array->lifecycle.reset == NULL && array->lifecycle.dispose == NULL) {
        size_t bytes = 0;
        if (!nmo_safe_mul_size(count, array->element_size, &bytes)) {
            return;
        }
        memset(base, 0, bytes);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_container_reset_element(&array->lifecycle,
                                    base + (i * array->element_size),
                                    array->element_size);
    }
}

void nmo_array_set_lifecycle(nmo_array_t *array,
                                    const nmo_container_lifecycle_t *lifecycle) {
    if (!array) {
        return;
    }
    if (lifecycle) {
        array->lifecycle = *lifecycle;
    } else {
        array->lifecycle.init = NULL;
        array->lifecycle.reset = NULL;
        array->lifecycle.copy = NULL;
        array->lifecycle.move = NULL;
        array->lifecycle.dispose = NULL;
        array->lifecycle.user_data = NULL;
    }
}

nmo_status_t nmo_array_init(nmo_array_t *array,
                             size_t element_size,
                             size_t initial_capacity,
                             const nmo_allocator_t *allocator) {
    if (!array || element_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array init arguments");
    }

    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->element_size = element_size;
    array->allocator = allocator ? *allocator : nmo_allocator_default();
    array->lifecycle.init = NULL;
    array->lifecycle.reset = NULL;
    array->lifecycle.copy = NULL;
    array->lifecycle.move = NULL;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (initial_capacity > 0) {
        return nmo_array_reserve(array, initial_capacity);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_reserve(nmo_array_t *array, size_t capacity) {
    if (!array) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array argument");
    }

    /* Allocator is always available (default if not set) */

    if (capacity <= array->capacity) {
        NMO_RETURN_OK(); // Already have enough capacity
    }

    size_t new_size = 0;
    if (!nmo_safe_mul_size(capacity, array->element_size, &new_size)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "array byte size overflow");
    }
    size_t alignment = nmo_elem_alignment(array->element_size);
    void *new_data = nmo_alloc(&array->allocator, new_size, alignment);
    if (!new_data) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate array memory");
    }

    // Move existing data if any
    if (array->data && array->count > 0) {
        nmo_array_move_range(array, (uint8_t *)new_data, (uint8_t *)array->data, array->count);
    }

    // Free old data
    if (array->data) {
        nmo_free(&array->allocator, array->data);
    }

    array->data = new_data;
    array->capacity = capacity;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_ensure_space(nmo_array_t *array, size_t additional) {
    if (!array) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array argument");
    }

    if (additional > 0 && array->count > SIZE_MAX - additional) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "array size overflow");
    }

    size_t required = array->count + additional;
    if (required <= array->capacity) {
        NMO_RETURN_OK(); // Already have enough space
    }

    // Exponential growth: start with 4, then double
    size_t new_capacity = array->capacity == 0 ? 4 : array->capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2;
    }

    return nmo_array_reserve(array, new_capacity);
}

nmo_status_t nmo_array_append(nmo_array_t *array, const void *element) {
    if (!array || !element) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array append arguments");
    }

    nmo_status_t result = nmo_array_ensure_space(array, 1);
    if (result != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    if (array->lifecycle.copy) {
        nmo_container_copy_element(&array->lifecycle, dest, element, array->element_size);
    } else {
        memcpy(dest, element, array->element_size);
    }
    array->count++;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_append_array(nmo_array_t *array,
                                           const void *elements,
                                           size_t count) {
    if (!array || (!elements && count > 0)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array append array arguments");
    }

    if (count == 0) {
        NMO_RETURN_OK();
    }

    nmo_status_t result = nmo_array_ensure_space(array, count);
    if (result != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    nmo_array_copy_range(array, dest, (const uint8_t *)elements, count);
    array->count += count;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_extend(nmo_array_t *array,
                                     size_t additional,
                                     void **out_begin) {
    if (!array) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array extend arguments");
    }

    uint8_t *start = NULL;
    if (additional > 0) {
        nmo_status_t result = nmo_array_ensure_space(array, additional);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (array->data) {
        start = (uint8_t *)array->data + (array->count * array->element_size);
    }

    if (out_begin) {
        *out_begin = start;
    }
    if (additional > 0) {
        nmo_array_init_range(array, array->count, additional);
    }
    array->count += additional;

    NMO_RETURN_OK();
}

void *nmo_array_get(const nmo_array_t *array, size_t index) {
    if (!array || index >= array->count) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)array->data;
    return data + (index * array->element_size);
}

nmo_status_t nmo_array_set(nmo_array_t *array, size_t index, const void *element) {
    if (!array || !element || index >= array->count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array set arguments");
    }

    uint8_t *dest = (uint8_t *)array->data + (index * array->element_size);
    if (array->lifecycle.reset == NULL &&
        array->lifecycle.dispose == NULL &&
        array->lifecycle.copy == NULL) {
        memcpy(dest, element, array->element_size);
        NMO_RETURN_OK();
    }

    nmo_array_reset_range(array, index, 1);
    if (array->lifecycle.copy) {
        nmo_container_copy_element(&array->lifecycle, dest, element, array->element_size);
    } else {
        memcpy(dest, element, array->element_size);
    }

    NMO_RETURN_OK();
}

void *nmo_array_front(const nmo_array_t *array) {
    if (!array || array->count == 0) {
        return NULL;
    }
    return array->data;
}

void *nmo_array_back(const nmo_array_t *array) {
    if (!array || array->count == 0) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)array->data;
    return data + ((array->count - 1) * array->element_size);
}

nmo_status_t nmo_array_insert(nmo_array_t *array,
                                     size_t index,
                                     const void *element) {
    if (!array || !element || index > array->count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array insert arguments");
    }

    nmo_status_t result = nmo_array_ensure_space(array, 1);
    if (result != NMO_OK) {
        return result;
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *dest = base + (index * array->element_size);

    if (index < array->count) {
        size_t move_count = array->count - index;
        nmo_array_move_range(array,
                             dest + array->element_size,
                             dest,
                             move_count);
    }

    if (array->lifecycle.copy) {
        nmo_container_copy_element(&array->lifecycle, dest, element, array->element_size);
    } else {
        memcpy(dest, element, array->element_size);
    }
    array->count++;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_remove(nmo_array_t *array,
                                     size_t index,
                                     void *out_element) {
    if (!array || index >= array->count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array remove arguments");
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *target = base + (index * array->element_size);

    if (out_element) {
        memcpy(out_element, target, array->element_size);
    }

    nmo_array_dispose_range(array, index, 1);

    if (index < array->count - 1) {
        size_t move_count = array->count - index - 1;
        nmo_array_move_range(array,
                             target,
                             target + array->element_size,
                             move_count);
    }

    array->count--;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_pop(nmo_array_t *array, void *out_element) {
    if (!array || array->count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array pop arguments");
    }

    uint8_t *base = (uint8_t *)array->data;
    uint8_t *target = base + ((array->count - 1) * array->element_size);

    if (out_element) {
        memcpy(out_element, target, array->element_size);
    }

    nmo_array_dispose_range(array, array->count - 1, 1);
    array->count--;

    NMO_RETURN_OK();
}

void nmo_array_clear(nmo_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_array_dispose_range(array, 0, array->count);
    array->count = 0;
}

nmo_status_t nmo_array_set_data(nmo_array_t *array,
                                       void *data,
                                       size_t count) {
    if (!array || (!data && count > 0)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array set_data arguments");
    }

    nmo_array_dispose_range(array, 0, array->count);
    if (array->data && array->data != data) {
        nmo_free(&array->allocator, array->data);
    }
    array->data = data;
    array->count = count;
    array->capacity = count; // Set capacity to count since data is pre-allocated

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_alloc(nmo_array_t *array,
                              size_t element_size,
                              size_t count,
                              const nmo_allocator_t *allocator) {
    if (!array || element_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array alloc arguments");
    }

    // Initialize array structure
    array->element_size = element_size;
    array->allocator = allocator ? *allocator : nmo_allocator_default();
    array->count = 0;
    array->capacity = 0;
    array->data = NULL;
    array->lifecycle.init = NULL;
    array->lifecycle.reset = NULL;
    array->lifecycle.copy = NULL;
    array->lifecycle.move = NULL;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (count == 0) {
        NMO_RETURN_OK();
    }

    // Allocate memory
    size_t size = 0;
    if (!nmo_safe_mul_size(count, element_size, &size)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "array byte size overflow");
    }
    size_t alignment = nmo_elem_alignment(element_size);
    void *data = nmo_alloc(&array->allocator, size, alignment);
    if (!data) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate array memory");
    }

    array->data = data;
    array->count = count;
    array->capacity = count;
    nmo_array_init_range(array, 0, count);

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_clone(const nmo_array_t *src,
                              nmo_array_t *dest,
                              const nmo_allocator_t *allocator) {
    if (!src || !dest) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array clone arguments");
    }

    // Initialize destination array
    nmo_status_t result = nmo_array_init(dest, src->element_size, src->count, allocator);
    if (result != NMO_OK) {
        return result;
    }
    nmo_array_set_lifecycle(dest, &src->lifecycle);

    // Copy data if any
    if (src->count > 0 && src->data) {
        result = nmo_array_append_array(dest, src->data, src->count);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
}

void nmo_array_dispose(nmo_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_array_clear(array);
    // Allocator-backed: must free data
    if (array->data) {
        nmo_free(&array->allocator, array->data);
    }
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

/* Accessor functions */

size_t nmo_array_size(const nmo_array_t *array) {
    return array ? array->count : 0;
}

size_t nmo_array_capacity(const nmo_array_t *array) {
    return array ? array->capacity : 0;
}

size_t nmo_array_element_size(const nmo_array_t *array) {
    return array ? array->element_size : 0;
}

int nmo_array_is_empty(const nmo_array_t *array) {
    return !array || array->count == 0;
}

void *nmo_array_data(const nmo_array_t *array) {
    return array ? array->data : NULL;
}

nmo_status_t nmo_array_swap(nmo_array_t *a, nmo_array_t *b) {
    if (!a || !b) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array swap arguments");
    }
    
    if (a->element_size != b->element_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Cannot swap arrays with different element sizes");
    }

    nmo_array_t temp = *a;
    *a = *b;
    *b = temp;
    
    NMO_RETURN_OK();
}

int nmo_array_find(const nmo_array_t *array,
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

int nmo_array_contains(const nmo_array_t *array, const void *element) {
    return nmo_array_find(array, element, NULL);
}

nmo_status_t nmo_array_shrink_to_fit(nmo_array_t *array) {
    if (!array) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array argument");
    }

    if (array->count >= array->capacity || array->capacity == 0) {
        NMO_RETURN_OK(); // Already tight or empty
    }

    if (array->count == 0) {
        // Free all data
        if (array->data) {
            nmo_free(&array->allocator, array->data);
            array->data = NULL;
        }
        array->capacity = 0;
        NMO_RETURN_OK();
    }

    // Reallocate to exact size
    size_t new_size = array->count * array->element_size;
    size_t alignment = nmo_elem_alignment(array->element_size);
    void *new_data = nmo_alloc(&array->allocator, new_size, alignment);
    if (!new_data) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to shrink array");
    }

    nmo_array_move_range(array, (uint8_t *)new_data, (uint8_t *)array->data, array->count);
    nmo_free(&array->allocator, array->data);
    array->data = new_data;
    array->capacity = array->count;

    NMO_RETURN_OK();
}

nmo_status_t nmo_array_resize(nmo_array_t *array, size_t new_count) {
    if (!array) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid array argument");
    }

    if (new_count == array->count) {
        NMO_RETURN_OK();
    }

    if (new_count < array->count) {
        /* Shrink: dispose trailing elements */
        nmo_array_dispose_range(array, new_count, array->count - new_count);
        array->count = new_count;
        NMO_RETURN_OK();
    }

    /* Grow: ensure capacity and zero-initialize new elements */
    nmo_status_t result = nmo_array_ensure_space(array, new_count - array->count);
    if (result != NMO_OK) {
        return result;
    }

    nmo_array_init_range(array, array->count, new_count - array->count);
    array->count = new_count;

    NMO_RETURN_OK();
}
