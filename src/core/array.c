/**
 * @file nmo_array.c
 * @brief Generic dynamic array implementation
 */

#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include <string.h>
#include <limits.h>

static size_t nmo_array_alignment(const nmo_array_t *array) {
    size_t alignment = array->element_size < sizeof(void*)
        ? sizeof(void*)
        : array->element_size;
    /* Ensure power of two alignment */
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    return alignment;
}

static void *nmo_array_allocate(nmo_array_t *array, size_t bytes) {
    size_t alignment = nmo_array_alignment(array);
    if (array->use_allocator) {
        return nmo_alloc(&array->allocator, bytes, alignment);
    }
    if (array->arena == NULL) {
        return NULL;
    }
    return nmo_arena_alloc(array->arena, bytes, alignment);
}

static void nmo_array_release_owned_data(nmo_array_t *array) {
    if (array && array->use_allocator && array->owns_data && array->data != NULL) {
        nmo_free(&array->allocator, array->data);
    }
    if (array) {
        array->owns_data = 0;
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

void nmo_array_set_lifecycle(nmo_array_t *array,
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

nmo_result_t nmo_array_init(nmo_array_t *array,
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
    array->allocator = nmo_allocator_default();
    array->use_allocator = 0;
    array->owns_data = 0;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (initial_capacity > 0) {
        return nmo_array_reserve(array, initial_capacity);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_array_init_with_allocator(nmo_array_t *array,
                                            size_t element_size,
                                            size_t initial_capacity,
                                            const nmo_allocator_t *allocator) {
    if (!array || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array init arguments"));
    }

    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->element_size = element_size;
    array->arena = NULL;
    array->allocator = allocator ? *allocator : nmo_allocator_default();
    array->use_allocator = 1;
    array->owns_data = 0;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (initial_capacity > 0) {
        return nmo_array_reserve(array, initial_capacity);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_array_reserve(nmo_array_t *array, size_t capacity) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array argument"));
    }

    if (!array->use_allocator && array->arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "array has no allocator or arena"));
    }

    if (capacity <= array->capacity) {
        return nmo_result_ok(); // Already have enough capacity
    }

    size_t new_size = capacity * array->element_size;
    void *new_data = nmo_array_allocate(array, new_size);
    if (!new_data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate array memory"));
    }

    // Copy existing data if any
    if (array->data && array->count > 0) {
        memcpy(new_data, array->data, array->count * array->element_size);
    }

    void *old_data = array->data;
    int old_owned = array->owns_data;
    array->data = new_data;
    array->capacity = capacity;
    array->owns_data = array->use_allocator ? 1 : 0;

    if (array->use_allocator && old_data != NULL && old_owned) {
        nmo_free(&array->allocator, old_data);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_array_ensure_space(nmo_array_t *array, size_t additional) {
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

    return nmo_array_reserve(array, new_capacity);
}

nmo_result_t nmo_array_append(nmo_array_t *array, const void *element) {
    if (!array || !element) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array append arguments"));
    }

    nmo_result_t result = nmo_array_ensure_space(array, 1);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    memcpy(dest, element, array->element_size);
    array->count++;

    return nmo_result_ok();
}

nmo_result_t nmo_array_append_array(nmo_array_t *array,
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

    nmo_result_t result = nmo_array_ensure_space(array, count);
    if (result.code != NMO_OK) {
        return result;
    }

    uint8_t *dest = (uint8_t *)array->data + (array->count * array->element_size);
    memcpy(dest, elements, count * array->element_size);
    array->count += count;

    return nmo_result_ok();
}

nmo_result_t nmo_array_extend(nmo_array_t *array,
                               size_t additional,
                               void **out_begin) {
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array extend arguments"));
    }

    uint8_t *start = NULL;
    if (additional > 0) {
        nmo_result_t result = nmo_array_ensure_space(array, additional);
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

void *nmo_array_get(const nmo_array_t *array, size_t index) {
    if (!array || index >= array->count) {
        return NULL;
    }

    uint8_t *data = (uint8_t *)array->data;
    return data + (index * array->element_size);
}

nmo_result_t nmo_array_set(nmo_array_t *array, size_t index, const void *element) {
    if (!array || !element || index >= array->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array set arguments"));
    }

    uint8_t *dest = (uint8_t *)array->data + (index * array->element_size);
    nmo_array_dispose_range(array, index, 1);
    memcpy(dest, element, array->element_size);

    return nmo_result_ok();
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

nmo_result_t nmo_array_insert(nmo_array_t *array,
                               size_t index,
                               const void *element) {
    if (!array || !element || index > array->count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array insert arguments"));
    }

    nmo_result_t result = nmo_array_ensure_space(array, 1);
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

nmo_result_t nmo_array_remove(nmo_array_t *array,
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

    nmo_array_dispose_range(array, index, 1);

    if (index < array->count - 1) {
        size_t move_bytes = (array->count - index - 1) * array->element_size;
        memmove(target, target + array->element_size, move_bytes);
    }

    array->count--;

    return nmo_result_ok();
}

nmo_result_t nmo_array_pop(nmo_array_t *array, void *out_element) {
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

    nmo_array_dispose_range(array, array->count - 1, 1);
    array->count--;

    return nmo_result_ok();
}

void nmo_array_clear(nmo_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_array_dispose_range(array, 0, array->count);
    array->count = 0;
}

nmo_result_t nmo_array_set_data(nmo_array_t *array,
                                 void *data,
                                 size_t count) {
    if (!array || (!data && count > 0)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array set_data arguments"));
    }

    nmo_array_dispose_range(array, 0, array->count);
    nmo_array_release_owned_data(array);
    array->data = data;
    array->count = count;
    array->capacity = count; // Set capacity to count since data is pre-allocated
    array->owns_data = 0;

    return nmo_result_ok();
}

nmo_result_t nmo_array_alloc(nmo_array_t *array,
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
    array->use_allocator = 0;
    array->owns_data = 0;
    array->lifecycle.dispose = NULL;
    array->lifecycle.user_data = NULL;

    if (count == 0) {
        return nmo_result_ok();
    }

    // Allocate memory
    size_t size = count * element_size;
    void *data = nmo_arena_alloc(arena, size, element_size);
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

nmo_result_t nmo_array_clone(const nmo_array_t *src,
                              nmo_array_t *dest,
                              nmo_arena_t *arena) {
    if (!src || !dest || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid array clone arguments"));
    }

    // Initialize destination array
    nmo_result_t result = nmo_array_init(dest, src->element_size, src->count, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    // Copy data if any
    if (src->count > 0 && src->data) {
        result = nmo_array_append_array(dest, src->data, src->count);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

void nmo_array_dispose(nmo_array_t *array) {
    if (array == NULL) {
        return;
    }

    nmo_array_clear(array);
    nmo_array_release_owned_data(array);
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

