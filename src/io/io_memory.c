/**
 * @file io_memory.c
 * @brief Memory IO operations implementation
 */

#include "io/nmo_io_memory.h"
#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Memory IO context structure for legacy API
 */
struct nmo_io_memory {
    uint8_t* buffer;
    size_t size;
    size_t capacity;
    size_t position;
    int owns_buffer;
};

/**
 * @brief Memory IO handle for nmo_io_interface (read-only)
 */
typedef struct {
    const uint8_t* data;
    size_t size;
    size_t position;
} nmo_memory_read_handle;

/**
 * @brief Memory IO handle for nmo_io_interface (write with dynamic growth)
 */
typedef struct {
    uint8_t* data;
    size_t size;        // Current size of written data
    size_t capacity;    // Total allocated capacity
    size_t position;    // Current read/write position
} nmo_memory_write_handle;

/**
 * @brief Read function for read-only memory IO
 */
static int memory_read_io_read(void* handle, void* buffer, size_t size, size_t* bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_read_handle* mh = (nmo_memory_read_handle*)handle;

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
static int memory_read_io_write(void* handle, const void* buffer, size_t size) {
    (void)handle;
    (void)buffer;
    (void)size;
    return NMO_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief Seek function for read-only memory IO
 */
static int memory_read_io_seek(void* handle, int64_t offset, nmo_seek_origin origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_read_handle* mh = (nmo_memory_read_handle*)handle;

    int64_t new_pos = 0;
    switch (origin) {
        case NMO_SEEK_SET:
            new_pos = offset;
            break;
        case NMO_SEEK_CUR:
            new_pos = (int64_t)mh->position + offset;
            break;
        case NMO_SEEK_END:
            new_pos = (int64_t)mh->size + offset;
            break;
        default:
            return NMO_ERR_INVALID_ARGUMENT;
    }

    if (new_pos < 0 || (size_t)new_pos > mh->size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    mh->position = (size_t)new_pos;
    return NMO_OK;
}

/**
 * @brief Tell function for read-only memory IO
 */
static int64_t memory_read_io_tell(void* handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_memory_read_handle* mh = (nmo_memory_read_handle*)handle;
    return (int64_t)mh->position;
}

/**
 * @brief Close function for read-only memory IO
 */
static int memory_read_io_close(void* handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_allocator alloc = nmo_allocator_default();
    nmo_free(&alloc, handle);

    return NMO_OK;
}

/**
 * @brief Read function for write memory IO
 */
static int memory_write_io_read(void* handle, void* buffer, size_t size, size_t* bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)handle;

    // Calculate how much can be read from written data
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
 * @brief Write function for write memory IO (with dynamic growth)
 */
static int memory_write_io_write(void* handle, const void* buffer, size_t size) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)handle;

    // Check if we need to grow the buffer
    size_t required = mh->position + size;
    if (required > mh->capacity) {
        // Calculate new capacity (double until sufficient)
        size_t new_capacity = mh->capacity;
        if (new_capacity == 0) {
            new_capacity = 64;  // Minimum initial capacity
        }
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        // Reallocate buffer
        nmo_allocator alloc = nmo_allocator_default();
        uint8_t* new_data = (uint8_t*)nmo_alloc(&alloc, new_capacity, 1);
        if (new_data == NULL) {
            return NMO_ERR_NOMEM;
        }

        // Copy existing data
        if (mh->data != NULL && mh->size > 0) {
            memcpy(new_data, mh->data, mh->size);
            nmo_free(&alloc, mh->data);
        }

        mh->data = new_data;
        mh->capacity = new_capacity;
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
static int memory_write_io_seek(void* handle, int64_t offset, nmo_seek_origin origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)handle;

    int64_t new_pos = 0;
    switch (origin) {
        case NMO_SEEK_SET:
            new_pos = offset;
            break;
        case NMO_SEEK_CUR:
            new_pos = (int64_t)mh->position + offset;
            break;
        case NMO_SEEK_END:
            new_pos = (int64_t)mh->size + offset;
            break;
        default:
            return NMO_ERR_INVALID_ARGUMENT;
    }

    if (new_pos < 0) {
        return NMO_ERR_INVALID_OFFSET;
    }

    // Allow seeking beyond current size (will be filled with zeros on write)
    mh->position = (size_t)new_pos;
    return NMO_OK;
}

/**
 * @brief Tell function for write memory IO
 */
static int64_t memory_write_io_tell(void* handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)handle;
    return (int64_t)mh->position;
}

/**
 * @brief Close function for write memory IO
 */
static int memory_write_io_close(void* handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)handle;
    nmo_allocator alloc = nmo_allocator_default();

    if (mh->data != NULL) {
        nmo_free(&alloc, mh->data);
    }

    nmo_free(&alloc, mh);

    return NMO_OK;
}

/**
 * Open a read-only memory buffer and return an IO interface
 */
nmo_io_interface* nmo_memory_io_open_read(const void* data, size_t size) {
    if (data == NULL || size == 0) {
        return NULL;
    }

    nmo_allocator alloc = nmo_allocator_default();

    // Allocate handle
    nmo_memory_read_handle* mh = (nmo_memory_read_handle*)nmo_alloc(&alloc, sizeof(nmo_memory_read_handle), sizeof(void*));
    if (mh == NULL) {
        return NULL;
    }

    mh->data = (const uint8_t*)data;
    mh->size = size;
    mh->position = 0;

    // Allocate interface
    nmo_io_interface* io = (nmo_io_interface*)nmo_alloc(&alloc, sizeof(nmo_io_interface), sizeof(void*));
    if (io == NULL) {
        nmo_free(&alloc, mh);
        return NULL;
    }

    io->read = memory_read_io_read;
    io->write = memory_read_io_write;
    io->seek = memory_read_io_seek;
    io->tell = memory_read_io_tell;
    io->close = memory_read_io_close;
    io->handle = mh;

    return io;
}

/**
 * Open a write-only memory buffer with dynamic growth and return an IO interface
 */
nmo_io_interface* nmo_memory_io_open_write(size_t initial_capacity) {
    nmo_allocator alloc = nmo_allocator_default();

    // Allocate handle
    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)nmo_alloc(&alloc, sizeof(nmo_memory_write_handle), sizeof(void*));
    if (mh == NULL) {
        return NULL;
    }

    // Initialize with capacity if specified
    if (initial_capacity > 0) {
        mh->data = (uint8_t*)nmo_alloc(&alloc, initial_capacity, 1);
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
    nmo_io_interface* io = (nmo_io_interface*)nmo_alloc(&alloc, sizeof(nmo_io_interface), sizeof(void*));
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
    io->close = memory_write_io_close;
    io->handle = mh;

    return io;
}

/**
 * Get the data from a write memory IO interface
 */
const void* nmo_memory_io_get_data(nmo_io_interface* io, size_t* size) {
    if (io == NULL || io->handle == NULL) {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }

    // Assume handle is a write handle
    nmo_memory_write_handle* mh = (nmo_memory_write_handle*)io->handle;

    if (size != NULL) {
        *size = mh->size;
    }

    return mh->data;
}

/**
 * Create a memory IO context from buffer
 */
nmo_io_memory_t* nmo_io_memory_create(const void* buffer, size_t size, int copy_data) {
    if (buffer == NULL || size == 0) {
        return NULL;
    }

    nmo_allocator alloc = nmo_allocator_default();
    nmo_io_memory_t* io_memory = (nmo_io_memory_t*)nmo_alloc(&alloc, sizeof(nmo_io_memory_t), sizeof(void*));
    if (io_memory == NULL) {
        return NULL;
    }

    if (copy_data) {
        io_memory->buffer = (uint8_t*)nmo_alloc(&alloc, size, 1);
        if (io_memory->buffer == NULL) {
            nmo_free(&alloc, io_memory);
            return NULL;
        }
        memcpy(io_memory->buffer, buffer, size);
        io_memory->owns_buffer = 1;
    } else {
        // Cast away const - caller must ensure buffer remains valid
        io_memory->buffer = (uint8_t*)buffer;
        io_memory->owns_buffer = 0;
    }

    io_memory->size = size;
    io_memory->capacity = size;
    io_memory->position = 0;

    return io_memory;
}

/**
 * Create an empty memory IO context for writing
 */
nmo_io_memory_t* nmo_io_memory_create_empty(size_t initial_capacity) {
    nmo_allocator alloc = nmo_allocator_default();
    nmo_io_memory_t* io_memory = (nmo_io_memory_t*)nmo_alloc(&alloc, sizeof(nmo_io_memory_t), sizeof(void*));
    if (io_memory == NULL) {
        return NULL;
    }

    if (initial_capacity > 0) {
        io_memory->buffer = (uint8_t*)nmo_alloc(&alloc, initial_capacity, 1);
        if (io_memory->buffer == NULL) {
            nmo_free(&alloc, io_memory);
            return NULL;
        }
        io_memory->capacity = initial_capacity;
    } else {
        io_memory->buffer = NULL;
        io_memory->capacity = 0;
    }

    io_memory->size = 0;
    io_memory->position = 0;
    io_memory->owns_buffer = 1;

    return io_memory;
}

/**
 * Destroy memory IO context
 */
void nmo_io_memory_destroy(nmo_io_memory_t* io_memory) {
    if (io_memory == NULL) {
        return;
    }

    nmo_allocator alloc = nmo_allocator_default();

    if (io_memory->owns_buffer && io_memory->buffer != NULL) {
        nmo_free(&alloc, io_memory->buffer);
    }

    nmo_free(&alloc, io_memory);
}

/**
 * Read from memory
 */
size_t nmo_io_memory_read(nmo_io_memory_t* io_memory, void* buffer, size_t size) {
    if (io_memory == NULL || buffer == NULL) {
        return 0;
    }

    size_t available = io_memory->size - io_memory->position;
    size_t to_read = (size < available) ? size : available;

    if (to_read > 0) {
        memcpy(buffer, io_memory->buffer + io_memory->position, to_read);
        io_memory->position += to_read;
    }

    return to_read;
}

/**
 * Write to memory
 */
size_t nmo_io_memory_write(nmo_io_memory_t* io_memory, const void* buffer, size_t size) {
    if (io_memory == NULL || buffer == NULL) {
        return 0;
    }

    // Check if we need to grow the buffer
    size_t required = io_memory->position + size;
    if (required > io_memory->capacity) {
        if (!io_memory->owns_buffer) {
            // Cannot grow a buffer we don't own
            return 0;
        }

        // Calculate new capacity (double until sufficient)
        size_t new_capacity = io_memory->capacity;
        if (new_capacity == 0) {
            new_capacity = 64;
        }
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        // Reallocate buffer
        nmo_allocator alloc = nmo_allocator_default();
        uint8_t* new_buffer = (uint8_t*)nmo_alloc(&alloc, new_capacity, 1);
        if (new_buffer == NULL) {
            return 0;
        }

        // Copy existing data
        if (io_memory->buffer != NULL && io_memory->size > 0) {
            memcpy(new_buffer, io_memory->buffer, io_memory->size);
            nmo_free(&alloc, io_memory->buffer);
        }

        io_memory->buffer = new_buffer;
        io_memory->capacity = new_capacity;
    }

    // Write data
    memcpy(io_memory->buffer + io_memory->position, buffer, size);
    io_memory->position += size;

    // Update size if we wrote past the end
    if (io_memory->position > io_memory->size) {
        io_memory->size = io_memory->position;
    }

    return size;
}

/**
 * Seek in memory
 */
int64_t nmo_io_memory_seek(nmo_io_memory_t* io_memory, int64_t offset, int whence) {
    if (io_memory == NULL) {
        return -1;
    }

    int64_t new_pos = 0;
    switch (whence) {
        case 0:  // SEEK_SET
            new_pos = offset;
            break;
        case 1:  // SEEK_CUR
            new_pos = (int64_t)io_memory->position + offset;
            break;
        case 2:  // SEEK_END
            new_pos = (int64_t)io_memory->size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0 || (size_t)new_pos > io_memory->size) {
        return -1;
    }

    io_memory->position = (size_t)new_pos;
    return new_pos;
}

/**
 * Get current position in memory
 */
int64_t nmo_io_memory_tell(nmo_io_memory_t* io_memory) {
    if (io_memory == NULL) {
        return 0;
    }

    return (int64_t)io_memory->position;
}

/**
 * Get memory buffer
 */
const void* nmo_io_memory_get_buffer(const nmo_io_memory_t* io_memory, size_t* out_size) {
    if (io_memory == NULL) {
        if (out_size != NULL) {
            *out_size = 0;
        }
        return NULL;
    }

    if (out_size != NULL) {
        *out_size = io_memory->size;
    }

    return io_memory->buffer;
}

/**
 * Reset memory position to beginning
 */
nmo_result_t nmo_io_memory_reset(nmo_io_memory_t* io_memory) {
    if (io_memory == NULL) {
        return nmo_result_ok();
    }

    io_memory->position = 0;
    return nmo_result_ok();
}
