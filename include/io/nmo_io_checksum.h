/**
 * @file nmo_io_checksum.h
 * @brief Checksum IO operations
 */

#ifndef NMO_IO_CHECKSUM_H
#define NMO_IO_CHECKSUM_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checksum IO context
 */
typedef struct nmo_io_checksum nmo_io_checksum_t;

/**
 * Checksum algorithm type
 */
typedef enum {
    NMO_CHECKSUM_NONE = 0,
    NMO_CHECKSUM_CRC32,
    NMO_CHECKSUM_MD5,
    NMO_CHECKSUM_SHA1,
    NMO_CHECKSUM_SHA256,
} nmo_checksum_type_t;

/**
 * Create a checksum IO context for reading
 * @param base_io Base IO to read from
 * @param checksum_type Type of checksum
 * @return Checksum IO context or NULL on error
 */
NMO_API nmo_io_checksum_t* nmo_io_checksum_create_read(
    void* base_io, nmo_checksum_type_t checksum_type);

/**
 * Create a checksum IO context for writing
 * @param base_io Base IO to write to
 * @param checksum_type Type of checksum
 * @return Checksum IO context or NULL on error
 */
NMO_API nmo_io_checksum_t* nmo_io_checksum_create_write(
    void* base_io, nmo_checksum_type_t checksum_type);

/**
 * Destroy checksum IO context
 * @param io_checksum Checksum IO context
 */
NMO_API void nmo_io_checksum_destroy(nmo_io_checksum_t* io_checksum);

/**
 * Read from checksum stream
 * @param io_checksum Checksum IO context
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
NMO_API size_t nmo_io_checksum_read(nmo_io_checksum_t* io_checksum, void* buffer, size_t size);

/**
 * Write to checksum stream
 * @param io_checksum Checksum IO context
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written
 */
NMO_API size_t nmo_io_checksum_write(nmo_io_checksum_t* io_checksum, const void* buffer, size_t size);

/**
 * Get computed checksum
 * @param io_checksum Checksum IO context
 * @param out_digest Output digest buffer
 * @param out_digest_size Output digest size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_checksum_get_digest(
    const nmo_io_checksum_t* io_checksum, void* out_digest, size_t* out_digest_size);

/**
 * Verify checksum against expected value
 * @param io_checksum Checksum IO context
 * @param expected_digest Expected digest
 * @param digest_size Digest size
 * @return NMO_OK if checksums match
 */
NMO_API nmo_result_t nmo_io_checksum_verify(
    const nmo_io_checksum_t* io_checksum, const void* expected_digest, size_t digest_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_CHECKSUM_H */
