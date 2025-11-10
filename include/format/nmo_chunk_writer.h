/**
 * @file nmo_chunk_writer.h
 * @brief Chunk writer for writing chunk data
 */

#ifndef NMO_CHUNK_WRITER_H
#define NMO_CHUNK_WRITER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chunk writer context
 */
typedef struct nmo_chunk_writer nmo_chunk_writer_t;

/**
 * Create chunk writer
 * @param initial_capacity Initial buffer capacity
 * @return Writer or NULL on error
 */
NMO_API nmo_chunk_writer_t* nmo_chunk_writer_create(size_t initial_capacity);

/**
 * Destroy chunk writer
 * @param writer Writer to destroy
 */
NMO_API void nmo_chunk_writer_destroy(nmo_chunk_writer_t* writer);

/**
 * Reset writer
 * @param writer Writer
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_writer_reset(nmo_chunk_writer_t* writer);

/**
 * Write chunk
 * @param writer Writer
 * @param chunk Chunk to write
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_writer_write_chunk(nmo_chunk_writer_t* writer, const void* chunk);

/**
 * Write raw data
 * @param writer Writer
 * @param data Data to write
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_writer_write_data(nmo_chunk_writer_t* writer, const void* data, size_t size);

/**
 * Get written data
 * @param writer Writer
 * @param out_size Output data size
 * @return Pointer to buffer (valid until next write or destroy)
 */
NMO_API const void* nmo_chunk_writer_get_buffer(const nmo_chunk_writer_t* writer, size_t* out_size);

/**
 * Get current write position
 * @param writer Writer
 * @return Current position in bytes
 */
NMO_API size_t nmo_chunk_writer_get_position(const nmo_chunk_writer_t* writer);

/**
 * Copy buffer to external location
 * @param writer Writer
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes copied or negative on error
 */
NMO_API ssize_t nmo_chunk_writer_copy_buffer(const nmo_chunk_writer_t* writer, void* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_WRITER_H */
