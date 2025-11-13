#ifndef NMO_IO_H
#define NMO_IO_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_io.h
 * @brief Base IO interface for composable IO operations
 *
 * Provides a unified interface for different IO backends:
 * - File IO (POSIX/Windows)
 * - Memory IO (in-memory buffers)
 * - Compressed IO (zlib wrapper)
 * - Checksummed IO (Adler-32 wrapper)
 * - Transactional IO (atomic writes)
 *
 * IO interfaces can be composed in layers:
 * File → Checksum → Compression → Parser
 */

/**
 * @brief IO mode flags
 */
typedef enum nmo_io_mode {
    NMO_IO_READ   = 0x01, /**< Read mode */
    NMO_IO_WRITE  = 0x02, /**< Write mode */
    NMO_IO_CREATE = 0x04, /**< Create if doesn't exist */
} nmo_io_mode_t;

/**
 * @brief Seek origins
 */
typedef enum nmo_seek_origin {
    NMO_SEEK_SET = 0, /**< Seek from beginning */
    NMO_SEEK_CUR = 1, /**< Seek from current position */
    NMO_SEEK_END = 2, /**< Seek from end */
} nmo_seek_origin_t;

// Forward declarations
typedef struct nmo_io_interface nmo_io_interface_t;
typedef struct nmo_allocator nmo_allocator_t;

/**
 * @brief Read function type
 *
 * @param handle IO handle
 * @param buffer Output buffer
 * @param size Number of bytes to read
 * @param bytes_read Number of bytes actually read (output)
 * @return NMO_OK on success, error code otherwise
 */
typedef int (*nmo_io_read_fn)(void *handle, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Write function type
 *
 * @param handle IO handle
 * @param buffer Input buffer
 * @param size Number of bytes to write
 * @return NMO_OK on success, error code otherwise
 */
typedef int (*nmo_io_write_fn)(void *handle, const void *buffer, size_t size);

/**
 * @brief Seek function type
 *
 * @param handle IO handle
 * @param offset Offset to seek to
 * @param origin Seek origin (SET/CUR/END)
 * @return NMO_OK on success, error code otherwise
 */
typedef int (*nmo_io_seek_fn)(void *handle, int64_t offset, nmo_seek_origin_t origin);

/**
 * @brief Tell function type
 *
 * @param handle IO handle
 * @return Current position or -1 on error
 */
typedef int64_t (*nmo_io_tell_fn)(void *handle);

/**
 * @brief Flush function type
 *
 * Flush any buffered data to the underlying stream.
 * For compression: finalizes compression without closing the stream.
 * For checksums: writes checksum footer without closing.
 * For transactions: commits pending writes without closing.
 *
 * @param handle IO handle
 * @return NMO_OK on success, error code otherwise
 */
typedef int (*nmo_io_flush_fn)(void *handle);

/**
 * @brief Close function type
 *
 * @param handle IO handle
 * @return NMO_OK on success, error code otherwise
 */
typedef int (*nmo_io_close_fn)(void *handle);

/**
 * @brief IO interface structure
 *
 * Implements the Strategy pattern for IO operations.
 */
typedef struct nmo_io_interface {
    nmo_io_read_fn read;   /**< Read function */
    nmo_io_write_fn write; /**< Write function */
    nmo_io_seek_fn seek;   /**< Seek function */
    nmo_io_tell_fn tell;   /**< Tell function */
    nmo_io_flush_fn flush; /**< Flush function (optional, can be NULL) */
    nmo_io_close_fn close; /**< Close function */
    void *handle;          /**< Implementation-specific handle */
} nmo_io_interface_t;

/**
 * @brief Read from IO interface
 *
 * @param io IO interface
 * @param buffer Output buffer
 * @param size Number of bytes to read
 * @param bytes_read Number of bytes actually read (output, can be NULL)
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_io_read(nmo_io_interface_t *io, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Write to IO interface
 *
 * @param io IO interface
 * @param buffer Input buffer
 * @param size Number of bytes to write
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_io_write(nmo_io_interface_t *io, const void *buffer, size_t size);

/**
 * @brief Seek in IO interface
 *
 * @param io IO interface
 * @param offset Offset to seek to
 * @param origin Seek origin (SET/CUR/END)
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_io_seek(nmo_io_interface_t *io, int64_t offset, nmo_seek_origin_t origin);

/**
 * @brief Get current position in IO interface
 *
 * @param io IO interface
 * @return Current position or -1 on error
 */
NMO_API int64_t nmo_io_tell(nmo_io_interface_t *io);

/**
 * @brief Flush buffered data in IO interface
 *
 * Flushes any buffered data to the underlying stream without closing.
 * For compression wrappers, this finalizes the compression stream.
 * For checksum wrappers, this writes the checksum footer.
 * For transactional wrappers, this commits the transaction.
 *
 * After flush, the underlying stream remains open and accessible.
 * This is useful when you need to get data from a memory stream
 * before closing the compression wrapper.
 *
 * @param io IO interface
 * @return NMO_OK on success, NMO_ERR_NOT_SUPPORTED if flush not supported, error code otherwise
 */
NMO_API int nmo_io_flush(nmo_io_interface_t *io);

/**
 * @brief Close IO interface
 *
 * @param io IO interface
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_io_close(nmo_io_interface_t *io);

/**
 * @brief Read exact number of bytes (fail if can't read all)
 *
 * @param io IO interface
 * @param buffer Output buffer
 * @param size Number of bytes to read
 * @return NMO_OK on success, NMO_ERR_EOF if can't read all bytes
 */
NMO_API int nmo_io_read_exact(nmo_io_interface_t *io, void *buffer, size_t size);

/**
 * @brief Read uint8_t
 *
 * @param io IO interface
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_io_read_u8(nmo_io_interface_t *io, uint8_t *out);

/**
 * @brief Read uint16_t (little-endian)
 *
 * @param io IO interface
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_io_read_u16(nmo_io_interface_t *io, uint16_t *out);

/**
 * @brief Read uint32_t (little-endian)
 *
 * @param io IO interface
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_io_read_u32(nmo_io_interface_t *io, uint32_t *out);

/**
 * @brief Read uint64_t (little-endian)
 *
 * @param io IO interface
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_io_read_u64(nmo_io_interface_t *io, uint64_t *out);

/**
 * @brief Write uint8_t
 *
 * @param io IO interface
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_io_write_u8(nmo_io_interface_t *io, uint8_t value);

/**
 * @brief Write uint16_t (little-endian)
 *
 * @param io IO interface
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_io_write_u16(nmo_io_interface_t *io, uint16_t value);

/**
 * @brief Write uint32_t (little-endian)
 *
 * @param io IO interface
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_io_write_u32(nmo_io_interface_t *io, uint32_t value);

/**
 * @brief Write uint64_t (little-endian)
 *
 * @param io IO interface
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_io_write_u64(nmo_io_interface_t *io, uint64_t value);

#ifdef __cplusplus
}
#endif

#endif // NMO_IO_H
