/**
 * @file nmo_io_compressed.h
 * @brief Compressed IO operations
 */

#ifndef NMO_IO_COMPRESSED_H
#define NMO_IO_COMPRESSED_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compressed IO context
 */
typedef struct nmo_io_compressed nmo_io_compressed_t;

/**
 * Compression algorithm type
 */
typedef enum {
    NMO_COMPRESSION_NONE = 0,
    NMO_COMPRESSION_ZLIB,
    NMO_COMPRESSION_ZSTD,
} nmo_compression_type_t;

/**
 * Create a compressed IO context for reading
 * @param base_io Base IO to read compressed data from
 * @param compression_type Type of compression
 * @return Compressed IO context or NULL on error
 */
NMO_API nmo_io_compressed_t* nmo_io_compressed_create_read(
    void* base_io, nmo_compression_type_t compression_type);

/**
 * Create a compressed IO context for writing
 * @param base_io Base IO to write compressed data to
 * @param compression_type Type of compression
 * @param level Compression level (0-9)
 * @return Compressed IO context or NULL on error
 */
NMO_API nmo_io_compressed_t* nmo_io_compressed_create_write(
    void* base_io, nmo_compression_type_t compression_type, int level);

/**
 * Destroy compressed IO context
 * @param io_compressed Compressed IO context
 */
NMO_API void nmo_io_compressed_destroy(nmo_io_compressed_t* io_compressed);

/**
 * Read from compressed stream
 * @param io_compressed Compressed IO context
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
NMO_API size_t nmo_io_compressed_read(nmo_io_compressed_t* io_compressed, void* buffer, size_t size);

/**
 * Write to compressed stream
 * @param io_compressed Compressed IO context
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written
 */
NMO_API size_t nmo_io_compressed_write(nmo_io_compressed_t* io_compressed, const void* buffer, size_t size);

/**
 * Flush compressed stream
 * @param io_compressed Compressed IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_compressed_flush(nmo_io_compressed_t* io_compressed);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_COMPRESSED_H */
