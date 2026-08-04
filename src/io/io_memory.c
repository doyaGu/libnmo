/**
 * @file io_memory.c
 * @brief Memory IO operations implementation
 */

#include "io/nmo_io_memory.h"
#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/**
 * @brief Memory IO handle for nmo_io_interface (read-only)
 */
typedef struct nmo_memory_read_handle {
    const uint8_t *data;
    size_t size;
    size_t position;
} nmo_memory_read_handle_t;

/**
 * @brief Memory IO handle for nmo_io_interface (write with dynamic growth)
 */
typedef struct nmo_memory_write_handle {
    uint8_t *data;
    size_t size;     // Current size of written data
    size_t capacity; // Total allocated capacity
    size_t position; // Current read/write position
    int failed;      // Set on write failure; poisons subsequent operations
} nmo_memory_write_handle_t;

/**
 * @brief Read function for read-only memory IO
 */
static nmo_status_t memory_read_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_read_handle_t *mh = (nmo_memory_read_handle_t *) handle;

    // Calculate how much can be read
    size_t available = mh->size - mh->position;
    size_t to_read = (size < available) ? size : available;

    if (to_read > 0) {
        memcpy(buffer, mh->data + mh->position, to_read);
        mh->position += to_read;
    }

    if (bytes_read != NULL) {
        *bytes_read = to_read;
    }

    return NMO_OK;
}

/**
 * @brief Write function for read-only memory IO (not supported)
 */
static nmo_status_t memory_read_io_write(void *handle, const void *buffer, size_t size) {
    (void) handle;
    (void) buffer;
    (void) size;
    return NMO_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief Seek function for read-only memory IO
 */
static nmo_status_t memory_read_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_read_handle_t *mh = (nmo_memory_read_handle_t *) handle;

    int64_t new_pos = 0;
    switch (origin) {
    case NMO_SEEK_SET:
        new_pos = offset;
        break;
    case NMO_SEEK_CUR:
        new_pos = (int64_t) mh->position + offset;
        break;
    case NMO_SEEK_END:
        new_pos = (int64_t) mh->size + offset;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (new_pos < 0 || (size_t) new_pos > mh->size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    mh->position = (size_t) new_pos;
    return NMO_OK;
}

/**
 * @brief Tell function for read-only memory IO
 */
static int64_t memory_read_io_tell(void *handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_memory_read_handle_t *mh = (nmo_memory_read_handle_t *) handle;
    return (int64_t) mh->position;
}

/**
 * @brief Close function for read-only memory IO
 */
static nmo_status_t memory_read_io_close(void *handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, handle);

    return NMO_OK;
}

/**
 * @brief Read function for write memory IO
 */
static nmo_status_t memory_write_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) handle;

    // Guard: position may exceed size after seek-beyond-size
    size_t available = (mh->position < mh->size) ? (mh->size - mh->position) : 0;
    size_t to_read = (size < available) ? size : available;

    if (to_read > 0) {
        memcpy(buffer, mh->data + mh->position, to_read);
        mh->position += to_read;
    }

    if (bytes_read != NULL) {
        *bytes_read = to_read;
    }

    return NMO_OK;
}

/**
 * @brief Write function for write memory IO (with dynamic growth)
 */
static nmo_status_t memory_write_io_write(void *handle, const void *buffer, size_t size) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) handle;

    if (mh->failed) {
        return NMO_ERR_INVALID_STATE;
    }

    // Overflow guard: position + size
    if (size > SIZE_MAX - mh->position) {
        mh->failed = 1;
        return NMO_ERR_NOMEM;
    }

    // Check if we need to grow the buffer
    size_t required = mh->position + size;
    if (required > mh->capacity) {
        // Calculate new capacity (double until sufficient)
        size_t new_capacity = mh->capacity;
        if (new_capacity == 0) {
            new_capacity = 64; // Minimum initial capacity
        }
        while (new_capacity < required) {
            size_t doubled = new_capacity * 2;
            if (doubled <= new_capacity) {
                // Overflow in doubling; clamp to required
                new_capacity = required;
                break;
            }
            new_capacity = doubled;
        }

        // Reallocate buffer
        nmo_allocator_t alloc = nmo_allocator_default();
        uint8_t *new_data = (uint8_t *) nmo_alloc(&alloc, new_capacity, 1);
        if (new_data == NULL) {
            mh->failed = 1;
            return NMO_ERR_NOMEM;
        }

        // Copy existing data
        if (mh->data != NULL && mh->size > 0) {
            memcpy(new_data, mh->data, mh->size);
        }
        if (mh->data != NULL) {
            nmo_free(&alloc, mh->data);
        }

        mh->data = new_data;
        mh->capacity = new_capacity;
    }

    // If writing after a sparse seek, zero-fill the gap.
    if (mh->position > mh->size) {
        memset(mh->data + mh->size, 0, mh->position - mh->size);
    }

    // Write data
    memcpy(mh->data + mh->position, buffer, size);
    mh->position += size;

    // Update size if we wrote past the end
    if (mh->position > mh->size) {
        mh->size = mh->position;
    }

    return NMO_OK;
}

/**
 * @brief Seek function for write memory IO
 */
static nmo_status_t memory_write_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) handle;
    if (mh->failed) {
        return NMO_ERR_INVALID_STATE;
    }

    int64_t new_pos = 0;
    switch (origin) {
    case NMO_SEEK_SET:
        new_pos = offset;
        break;
    case NMO_SEEK_CUR:
        new_pos = (int64_t) mh->position + offset;
        break;
    case NMO_SEEK_END:
        new_pos = (int64_t) mh->size + offset;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (new_pos < 0) {
        return NMO_ERR_INVALID_OFFSET;
    }

    // Allow seeking beyond current size (will be filled with zeros on write)
    mh->position = (size_t) new_pos;
    return NMO_OK;
}

/**
 * @brief Tell function for write memory IO
 */
static int64_t memory_write_io_tell(void *handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) handle;
    return (int64_t) mh->position;
}

/**
 * @brief Close function for write memory IO
 */
static nmo_status_t memory_write_io_close(void *handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) handle;
    nmo_allocator_t alloc = nmo_allocator_default();

    if (mh->data != NULL) {
        nmo_free(&alloc, mh->data);
    }

    nmo_free(&alloc, mh);

    return NMO_OK;
}

/**
 * Open a read-only memory buffer and return an IO interface
 */
nmo_io_interface_t *nmo_memory_io_open_read(const void *data, size_t size) {
    if (data == NULL || size == 0) {
        return NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();

    // Allocate handle
    nmo_memory_read_handle_t *mh = (nmo_memory_read_handle_t *) nmo_alloc(
        &alloc, sizeof(nmo_memory_read_handle_t), sizeof(void *));
    if (mh == NULL) {
        return NULL;
    }

    mh->data = (const uint8_t *) data;
    mh->size = size;
    mh->position = 0;

    // Allocate interface
    nmo_io_interface_t *io = (nmo_io_interface_t *) nmo_alloc(&alloc, sizeof(nmo_io_interface_t), sizeof(void *));
    if (io == NULL) {
        nmo_free(&alloc, mh);
        return NULL;
    }

    io->read = memory_read_io_read;
    io->write = memory_read_io_write;
    io->seek = memory_read_io_seek;
    io->tell = memory_read_io_tell;
    io->flush = NULL; /* Memory IO doesn't need flush */
    io->close = memory_read_io_close;
    io->handle = mh;

    return io;
}

/**
 * Open a write-only memory buffer with dynamic growth and return an IO interface
 */
nmo_io_interface_t *nmo_memory_io_open_write(size_t initial_capacity) {
    nmo_allocator_t alloc = nmo_allocator_default();

    // Allocate handle
    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) nmo_alloc(
        &alloc, sizeof(nmo_memory_write_handle_t), sizeof(void *));
    if (mh == NULL) {
        return NULL;
    }
    memset(mh, 0, sizeof(*mh));

    // Initialize with capacity if specified
    if (initial_capacity > 0) {
        mh->data = (uint8_t *) nmo_alloc(&alloc, initial_capacity, 1);
        if (mh->data == NULL) {
            nmo_free(&alloc, mh);
            return NULL;
        }
        mh->capacity = initial_capacity;
    } else {
        mh->data = NULL;
        mh->capacity = 0;
    }

    mh->size = 0;
    mh->position = 0;

    // Allocate interface
    nmo_io_interface_t *io = (nmo_io_interface_t *) nmo_alloc(&alloc, sizeof(nmo_io_interface_t), sizeof(void *));
    if (io == NULL) {
        if (mh->data != NULL) {
            nmo_free(&alloc, mh->data);
        }
        nmo_free(&alloc, mh);
        return NULL;
    }

    io->read = memory_write_io_read;
    io->write = memory_write_io_write;
    io->seek = memory_write_io_seek;
    io->tell = memory_write_io_tell;
    io->flush = NULL; /* Memory IO doesn't need flush */
    io->close = memory_write_io_close;
    io->handle = mh;

    return io;
}

/**
 * Get the data from a write memory IO interface
 */
const void *nmo_memory_io_get_data(nmo_io_interface_t *io, size_t *size) {
    if (io == NULL || io->handle == NULL) {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }

    // Assume handle is a write handle
    nmo_memory_write_handle_t *mh = (nmo_memory_write_handle_t *) io->handle;

    if (size != NULL) {
        *size = mh->size;
    }

    return mh->data;
}
