/**
 * @file nmo_io_file.h
 * @brief File IO operations
 */

#ifndef NMO_IO_FILE_H
#define NMO_IO_FILE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File IO context
 */
typedef struct nmo_io_file nmo_io_file_t;

/**
 * Create a file IO context
 * @param path File path
 * @param mode Open mode ("r", "w", "rb", "wb")
 * @return File IO context or NULL on error
 */
NMO_API nmo_io_file_t* nmo_io_file_create(const char* path, const char* mode);

/**
 * Destroy file IO context
 * @param io_file File IO context
 */
NMO_API void nmo_io_file_destroy(nmo_io_file_t* io_file);

/**
 * Read from file
 * @param io_file File IO context
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read or negative on error
 */
NMO_API size_t nmo_io_file_read(nmo_io_file_t* io_file, void* buffer, size_t size);

/**
 * Write to file
 * @param io_file File IO context
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written or negative on error
 */
NMO_API size_t nmo_io_file_write(nmo_io_file_t* io_file, const void* buffer, size_t size);

/**
 * Seek in file
 * @param io_file File IO context
 * @param offset Offset to seek to
 * @param whence Seek origin (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New position or negative on error
 */
NMO_API int64_t nmo_io_file_seek(nmo_io_file_t* io_file, int64_t offset, int whence);

/**
 * Get current position in file
 * @param io_file File IO context
 * @return Current position or negative on error
 */
NMO_API int64_t nmo_io_file_tell(nmo_io_file_t* io_file);

/**
 * Close file
 * @param io_file File IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_file_close(nmo_io_file_t* io_file);

/**
 * Open a file and return an IO interface
 * @param path File path
 * @param mode IO mode (NMO_IO_READ, NMO_IO_WRITE, NMO_IO_CREATE)
 * @return IO interface or NULL on error
 */
NMO_API nmo_io_interface* nmo_file_io_open(const char* path, nmo_io_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_FILE_H */
