/**
 * @file save_buffer.c
 * @brief Memory buffer for two-phase save pipeline
 */

#include "app/nmo_save_buffer.h"
#include "core/nmo_allocator.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_INITIAL_CAPACITY 4096
#define GROWTH_FACTOR 2

/**
 * @brief Save buffer structure
 */
struct nmo_save_buffer {
    uint8_t *data;       /**< Buffer data (malloc-allocated for resizing) */
    size_t size;         /**< Current write position / data size */
    size_t capacity;     /**< Allocated capacity */
    nmo_arena_t *arena;  /**< Arena for struct allocation (not for data) */
};

static int nmo_checked_add_size(size_t a, size_t b, size_t *out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (a > (SIZE_MAX - b)) {
        return NMO_ERR_NOMEM;
    }
    *out = a + b;
    return NMO_OK;
}

nmo_save_buffer_t *nmo_save_buffer_create(nmo_arena_t *arena, size_t initial_capacity) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_save_buffer_t *buffer = (nmo_save_buffer_t *)nmo_arena_alloc(
        arena, sizeof(nmo_save_buffer_t), _Alignof(nmo_save_buffer_t));
    if (buffer == NULL) {
        return NULL;
    }

    memset(buffer, 0, sizeof(nmo_save_buffer_t));
    buffer->arena = arena;

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_INITIAL_CAPACITY;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    buffer->data = (uint8_t *)nmo_alloc(&alloc, initial_capacity, 1);
    if (buffer->data == NULL) {
        return NULL;
    }

    buffer->capacity = initial_capacity;
    buffer->size = 0;

    return buffer;
}

void nmo_save_buffer_destroy(nmo_save_buffer_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    if (buffer->data != NULL) {
        nmo_allocator_t alloc = nmo_allocator_default();
        nmo_free(&alloc, buffer->data);
        buffer->data = NULL;
    }

    buffer->size = 0;
    buffer->capacity = 0;
    /* buffer struct itself is arena-allocated, no need to free */
}

void nmo_save_buffer_reset(nmo_save_buffer_t *buffer) {
    if (buffer != NULL) {
        buffer->size = 0;
    }
}

size_t nmo_save_buffer_size(const nmo_save_buffer_t *buffer) {
    return (buffer != NULL) ? buffer->size : 0;
}

size_t nmo_save_buffer_capacity(const nmo_save_buffer_t *buffer) {
    return (buffer != NULL) ? buffer->capacity : 0;
}

void *nmo_save_buffer_data(nmo_save_buffer_t *buffer) {
    return (buffer != NULL) ? buffer->data : NULL;
}

const void *nmo_save_buffer_data_const(const nmo_save_buffer_t *buffer) {
    return (buffer != NULL) ? buffer->data : NULL;
}

int nmo_save_buffer_reserve(nmo_save_buffer_t *buffer, size_t capacity) {
    if (buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (buffer->capacity >= capacity) {
        return NMO_OK;
    }

    /* Grow to at least double current capacity or requested size */
    size_t new_capacity = capacity;
    if (buffer->capacity <= (SIZE_MAX / GROWTH_FACTOR)) {
        new_capacity = buffer->capacity * GROWTH_FACTOR;
    }
    if (new_capacity < capacity) {
        new_capacity = capacity;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    uint8_t *new_data = (uint8_t *)nmo_alloc(&alloc, new_capacity, 1);
    if (new_data == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(new_data, buffer->data, buffer->size);
    nmo_free(&alloc, buffer->data);
    buffer->data = new_data;
    buffer->capacity = new_capacity;

    return NMO_OK;
}

int nmo_save_buffer_write(nmo_save_buffer_t *buffer, const void *data, size_t size) {
    if (buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (size == 0) {
        return NMO_OK;
    }

    if (data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t required = 0;
    int add_result = nmo_checked_add_size(buffer->size, size, &required);
    if (add_result != NMO_OK) {
        return add_result;
    }
    if (required > buffer->capacity) {
        int result = nmo_save_buffer_reserve(buffer, required);
        if (result != NMO_OK) {
            return result;
        }
    }

    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;

    return NMO_OK;
}

int nmo_save_buffer_write_u32(nmo_save_buffer_t *buffer, uint32_t value) {
    return nmo_save_buffer_write(buffer, &value, sizeof(uint32_t));
}

int nmo_save_buffer_write_u64(nmo_save_buffer_t *buffer, uint64_t value) {
    return nmo_save_buffer_write(buffer, &value, sizeof(uint64_t));
}

int nmo_save_buffer_reserve_space(nmo_save_buffer_t *buffer, size_t size, size_t *out_offset) {
    if (buffer == NULL || out_offset == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t required = 0;
    int add_result = nmo_checked_add_size(buffer->size, size, &required);
    if (add_result != NMO_OK) {
        return add_result;
    }
    if (required > buffer->capacity) {
        int result = nmo_save_buffer_reserve(buffer, required);
        if (result != NMO_OK) {
            return result;
        }
    }

    *out_offset = buffer->size;

    /* Zero the reserved space */
    memset(buffer->data + buffer->size, 0, size);
    buffer->size += size;

    return NMO_OK;
}

int nmo_save_buffer_patch(nmo_save_buffer_t *buffer, size_t offset, const void *data, size_t size) {
    if (buffer == NULL || data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (offset > (SIZE_MAX - size) || (offset + size) > buffer->size) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memcpy(buffer->data + offset, data, size);

    return NMO_OK;
}

int nmo_save_buffer_patch_u32(nmo_save_buffer_t *buffer, size_t offset, uint32_t value) {
    return nmo_save_buffer_patch(buffer, offset, &value, sizeof(uint32_t));
}

void *nmo_save_buffer_detach(nmo_save_buffer_t *buffer, size_t *out_size) {
    if (buffer == NULL) {
        if (out_size != NULL) {
            *out_size = 0;
        }
        return NULL;
    }

    void *data = buffer->data;
    size_t size = buffer->size;

    /* Reset buffer state (caller now owns the data) */
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;

    if (out_size != NULL) {
        *out_size = size;
    }

    return data;
}
