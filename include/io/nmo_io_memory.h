/**
 * @file nmo_io_memory.h
 * @brief Memory IO operations
 */

#ifndef NMO_IO_MEMORY_H
#define NMO_IO_MEMORY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory IO context
 */
typedef struct nmo_io_memory nmo_io_memory_t;

/**
 * Create a memory IO context from buffer
 * @param buffer Data buffer
 * @param size Buffer size
 * @param copy_data Whether to copy buffer data
 * @return Memory IO context or NULL on error
 */
NMO_API nmo_io_memory_t* nmo_io_memory_create(const void* buffer, size_t size, int copy_data);

/**
 * Create an empty memory IO context for writing
 * @param initial_capacity Initial buffer capacity
 * @return Memory IO context or NULL on error
 */
NMO_API nmo_io_memory_t* nmo_io_memory_create_empty(size_t initial_capacity);

/**
 * Destroy memory IO context
 * @param io_memory Memory IO context
 */
NMO_API void nmo_io_memory_destroy(nmo_io_memory_t* io_memory);

/**
 * Read from memory
 * @param io_memory Memory IO context
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
NMO_API size_t nmo_io_memory_read(nmo_io_memory_t* io_memory, void* buffer, size_t size);

/**
 * Write to memory
 * @param io_memory Memory IO context
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written
 */
NMO_API size_t nmo_io_memory_write(nmo_io_memory_t* io_memory, const void* buffer, size_t size);

/**
 * Seek in memory
 * @param io_memory Memory IO context
 * @param offset Offset to seek to
 * @param whence Seek origin (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New position or negative on error
 */
NMO_API int64_t nmo_io_memory_seek(nmo_io_memory_t* io_memory, int64_t offset, int whence);

/**
 * Get current position in memory
 * @param io_memory Memory IO context
 * @return Current position
 */
NMO_API int64_t nmo_io_memory_tell(nmo_io_memory_t* io_memory);

/**
 * Get memory buffer
 * @param io_memory Memory IO context
 * @param out_size Output buffer size
 * @return Pointer to buffer
 */
NMO_API const void* nmo_io_memory_get_buffer(const nmo_io_memory_t* io_memory, size_t* out_size);

/**
 * Reset memory position to beginning
 * @param io_memory Memory IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_memory_reset(nmo_io_memory_t* io_memory);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_MEMORY_H */
