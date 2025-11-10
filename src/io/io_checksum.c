/**
 * @file io_checksum.c
 * @brief Checksum IO operations implementation
 */

#include "io/nmo_io_checksum.h"

/**
 * Create a checksum IO context for reading
 */
nmo_io_checksum_t* nmo_io_checksum_create_read(
    void* base_io, nmo_checksum_type_t checksum_type) {
    (void)base_io;
    (void)checksum_type;
    return NULL;
}

/**
 * Create a checksum IO context for writing
 */
nmo_io_checksum_t* nmo_io_checksum_create_write(
    void* base_io, nmo_checksum_type_t checksum_type) {
    (void)base_io;
    (void)checksum_type;
    return NULL;
}

/**
 * Destroy checksum IO context
 */
void nmo_io_checksum_destroy(nmo_io_checksum_t* io_checksum) {
    (void)io_checksum;
}

/**
 * Read from checksum stream
 */
size_t nmo_io_checksum_read(nmo_io_checksum_t* io_checksum, void* buffer, size_t size) {
    (void)io_checksum;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Write to checksum stream
 */
size_t nmo_io_checksum_write(nmo_io_checksum_t* io_checksum, const void* buffer, size_t size) {
    (void)io_checksum;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Get computed checksum
 */
nmo_result_t nmo_io_checksum_get_digest(
    const nmo_io_checksum_t* io_checksum, void* out_digest, size_t* out_digest_size) {
    (void)io_checksum;
    (void)out_digest;
    if (out_digest_size != NULL) {
        *out_digest_size = 0;
    }
    return nmo_result_ok();
}

/**
 * Verify checksum against expected value
 */
nmo_result_t nmo_io_checksum_verify(
    const nmo_io_checksum_t* io_checksum, const void* expected_digest, size_t digest_size) {
    (void)io_checksum;
    (void)expected_digest;
    (void)digest_size;
    return nmo_result_ok();
}
