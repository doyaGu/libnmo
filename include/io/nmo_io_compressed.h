/**
 * @file nmo_io_compressed.h
 * @brief Compressed IO wrapper for composable IO operations
 */

#ifndef NMO_IO_COMPRESSED_H
#define NMO_IO_COMPRESSED_H

#include "nmo_types.h"
#include "io/nmo_io.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compression codec types
 */
typedef enum {
    NMO_CODEC_ZLIB = 0,  /**< zlib compression (DEFLATE) */
} nmo_compression_codec;

/**
 * @brief Compression mode
 */
typedef enum {
    NMO_COMPRESS_MODE_DEFLATE = 0,  /**< Compress data (for writing) */
    NMO_COMPRESS_MODE_INFLATE = 1,  /**< Decompress data (for reading) */
} nmo_compression_mode;

/**
 * @brief Compressed IO descriptor
 *
 * Configures the compression parameters for wrapping an IO interface.
 */
typedef struct {
    nmo_compression_codec codec;  /**< Compression codec to use */
    nmo_compression_mode  mode;   /**< Compression mode (deflate/inflate) */
    int level;                     /**< Compression level (1-9, where 1=fastest, 9=best compression, ignored for inflate) */
} nmo_compressed_io_desc;

/**
 * @brief Wrap an IO interface with compression
 *
 * Creates a new IO interface that compresses writes and decompresses reads,
 * wrapping the provided inner IO interface. The wrapper handles streaming
 * compression/decompression with internal buffering (64KB default).
 *
 * The wrapper passes through seek/tell operations to the underlying IO.
 * Compression is properly flushed on close.
 *
 * @param inner The inner IO interface to wrap (must not be NULL)
 * @param desc Compression descriptor (must not be NULL)
 * @return New wrapped IO interface, or NULL on error
 *
 * @note The caller is responsible for closing the returned interface.
 *       Closing will also close the inner interface.
 * @note The inner interface should not be used directly after wrapping.
 */
NMO_API nmo_io_interface* nmo_compressed_io_wrap(nmo_io_interface* inner,
                                                   const nmo_compressed_io_desc* desc);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_COMPRESSED_H */
