/**
 * @file io_memory.c
 * @brief Memory IO operations implementation
 */

#include "io/nmo_io_memory.h"

/**
 * Create a memory IO context from buffer
 */
nmo_io_memory_t* nmo_io_memory_create(const void* buffer, size_t size, int copy_data) {
    (void)buffer;
    (void)size;
    (void)copy_data;
    return NULL;
}

/**
 * Create an empty memory IO context for writing
 */
nmo_io_memory_t* nmo_io_memory_create_empty(size_t initial_capacity) {
    (void)initial_capacity;
    return NULL;
}

/**
 * Destroy memory IO context
 */
void nmo_io_memory_destroy(nmo_io_memory_t* io_memory) {
    (void)io_memory;
}

/**
 * Read from memory
 */
size_t nmo_io_memory_read(nmo_io_memory_t* io_memory, void* buffer, size_t size) {
    (void)io_memory;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Write to memory
 */
size_t nmo_io_memory_write(nmo_io_memory_t* io_memory, const void* buffer, size_t size) {
    (void)io_memory;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Seek in memory
 */
int64_t nmo_io_memory_seek(nmo_io_memory_t* io_memory, int64_t offset, int whence) {
    (void)io_memory;
    (void)offset;
    (void)whence;
    return -1;
}

/**
 * Get current position in memory
 */
int64_t nmo_io_memory_tell(nmo_io_memory_t* io_memory) {
    (void)io_memory;
    return 0;
}

/**
 * Get memory buffer
 */
const void* nmo_io_memory_get_buffer(const nmo_io_memory_t* io_memory, size_t* out_size) {
    (void)io_memory;
    if (out_size != NULL) {
        *out_size = 0;
    }
    return NULL;
}

/**
 * Reset memory position to beginning
 */
nmo_result_t nmo_io_memory_reset(nmo_io_memory_t* io_memory) {
    (void)io_memory;
    return nmo_result_ok();
}
