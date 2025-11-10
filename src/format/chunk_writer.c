/**
 * @file chunk_writer.c
 * @brief Chunk writer for writing chunk data implementation
 */

#include "format/nmo_chunk_writer.h"

/**
 * Create chunk writer
 */
nmo_chunk_writer_t* nmo_chunk_writer_create(size_t initial_capacity) {
    (void)initial_capacity;
    return NULL;
}

/**
 * Destroy chunk writer
 */
void nmo_chunk_writer_destroy(nmo_chunk_writer_t* writer) {
    (void)writer;
}

/**
 * Reset writer
 */
nmo_result_t nmo_chunk_writer_reset(nmo_chunk_writer_t* writer) {
    (void)writer;
    return nmo_result_ok();
}

/**
 * Write chunk
 */
nmo_result_t nmo_chunk_writer_write_chunk(nmo_chunk_writer_t* writer, const void* chunk) {
    (void)writer;
    (void)chunk;
    return nmo_result_ok();
}

/**
 * Write raw data
 */
nmo_result_t nmo_chunk_writer_write_data(nmo_chunk_writer_t* writer, const void* data, size_t size) {
    (void)writer;
    (void)data;
    (void)size;
    return nmo_result_ok();
}

/**
 * Get written data
 */
const void* nmo_chunk_writer_get_buffer(const nmo_chunk_writer_t* writer, size_t* out_size) {
    (void)writer;
    if (out_size != NULL) {
        *out_size = 0;
    }
    return NULL;
}

/**
 * Get current write position
 */
size_t nmo_chunk_writer_get_position(const nmo_chunk_writer_t* writer) {
    (void)writer;
    return 0;
}

/**
 * Copy buffer to external location
 */
ssize_t nmo_chunk_writer_copy_buffer(const nmo_chunk_writer_t* writer, void* buffer, size_t size) {
    (void)writer;
    (void)buffer;
    (void)size;
    return -1;
}
