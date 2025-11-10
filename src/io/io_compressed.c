/**
 * @file io_compressed.c
 * @brief Compressed IO operations implementation
 */

#include "io/nmo_io_compressed.h"

/**
 * Create a compressed IO context for reading
 */
nmo_io_compressed_t* nmo_io_compressed_create_read(
    void* base_io, nmo_compression_type_t compression_type) {
    (void)base_io;
    (void)compression_type;
    return NULL;
}

/**
 * Create a compressed IO context for writing
 */
nmo_io_compressed_t* nmo_io_compressed_create_write(
    void* base_io, nmo_compression_type_t compression_type, int level) {
    (void)base_io;
    (void)compression_type;
    (void)level;
    return NULL;
}

/**
 * Destroy compressed IO context
 */
void nmo_io_compressed_destroy(nmo_io_compressed_t* io_compressed) {
    (void)io_compressed;
}

/**
 * Read from compressed stream
 */
size_t nmo_io_compressed_read(nmo_io_compressed_t* io_compressed, void* buffer, size_t size) {
    (void)io_compressed;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Write to compressed stream
 */
size_t nmo_io_compressed_write(nmo_io_compressed_t* io_compressed, const void* buffer, size_t size) {
    (void)io_compressed;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Flush compressed stream
 */
nmo_result_t nmo_io_compressed_flush(nmo_io_compressed_t* io_compressed) {
    (void)io_compressed;
    return nmo_result_ok();
}
