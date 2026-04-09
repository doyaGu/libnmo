/**
 * @file nmo_io_mmap.h
 * @brief Memory-mapped file IO operations (Phase 2.1)
 *
 * Provides memory-mapped file access for efficient reading of
 * uncompressed NMO files. This allows direct memory access to
 * file contents without copying, reducing memory usage for
 * large uncompressed files.
 *
 * Platform support:
 * - Windows: CreateFileMapping/MapViewOfFile
 * - POSIX: mmap(2)
 */

#ifndef NMO_IO_MMAP_H
#define NMO_IO_MMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory-mapped file context
 */
typedef struct nmo_io_mmap nmo_io_mmap_t;

/**
 * @brief Create a memory-mapped file context for reading
 *
 * Opens a file and maps it into memory for read-only access.
 * This is more efficient than regular file IO for sequential
 * and random access patterns on uncompressed files.
 *
 * @param path File path to map
 * @return Memory-mapped context or NULL on error
 * @ownership owned
 */
NMO_API nmo_io_mmap_t *nmo_io_mmap_open(const char *path);

/**
 * @brief Close and unmap a memory-mapped file
 *
 * @param mmap Memory-mapped context
 */
NMO_API void nmo_io_mmap_close(nmo_io_mmap_t *mmap);

/**
 * @brief Get pointer to mapped memory region
 *
 * Returns a pointer to the beginning of the mapped file data.
 * The pointer is valid until nmo_io_mmap_close() is called.
 *
 * @param mmap Memory-mapped context
 * @return Pointer to mapped data, or NULL on error
 * @ownership borrowed
 */
NMO_API const void *nmo_io_mmap_data(const nmo_io_mmap_t *mmap);

/**
 * @brief Get size of mapped file
 *
 * @param mmap Memory-mapped context
 * @return File size in bytes
 */
NMO_API size_t nmo_io_mmap_size(const nmo_io_mmap_t *mmap);

/**
 * @brief Read from memory-mapped file at current position
 *
 * Copies data from the mapped region to the buffer.
 * This is provided for API compatibility with nmo_io_interface.
 *
 * @param mmap Memory-mapped context
 * @param buffer Output buffer
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
NMO_API size_t nmo_io_mmap_read(nmo_io_mmap_t *mmap, void *buffer, size_t size);

/**
 * @brief Seek to position in memory-mapped file
 *
 * @param mmap Memory-mapped context
 * @param offset Offset to seek to
 * @param whence Seek origin (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New position or -1 on error
 */
NMO_API int64_t nmo_io_mmap_seek(nmo_io_mmap_t *mmap, int64_t offset, int whence);

/**
 * @brief Get current position in memory-mapped file
 *
 * @param mmap Memory-mapped context
 * @return Current position
 */
NMO_API int64_t nmo_io_mmap_tell(const nmo_io_mmap_t *mmap);

/**
 * @brief Get pointer to data at specific offset
 *
 * Returns a direct pointer to the mapped data at the specified
 * offset without copying. Useful for zero-copy parsing.
 *
 * @param mmap Memory-mapped context
 * @param offset Offset from start of file
 * @param size Required size (for bounds checking)
 * @return Pointer to data, or NULL if out of bounds
 * @ownership borrowed
 */
NMO_API const void *nmo_io_mmap_ptr_at(const nmo_io_mmap_t *mmap, size_t offset, size_t size);

/**
 * @brief Open a memory-mapped file and return an IO interface
 *
 * Creates an nmo_io_interface wrapper around a memory-mapped file.
 * This allows using mmap IO with the standard IO layer.
 *
 * @param path File path to map
 * @return IO interface or NULL on error
 * @ownership owned
 */
NMO_API nmo_io_interface_t *nmo_mmap_io_open(const char *path);

/**
 * @brief Check if memory mapping is supported on this platform
 *
 * @return 1 if supported, 0 if not
 */
NMO_API int nmo_io_mmap_supported(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_MMAP_H */
