/**
 * @file nmo_chunk.h
 * @brief CKStateChunk handling
 */

#ifndef NMO_CHUNK_H
#define NMO_CHUNK_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chunk context (CKStateChunk)
 */
typedef struct nmo_chunk nmo_chunk_t;

/**
 * @brief Chunk header info
 */
typedef struct {
    uint32_t chunk_id;       /* Chunk identifier */
    uint32_t chunk_size;     /* Chunk size */
    uint32_t sub_chunk_count;/* Number of sub-chunks */
    uint32_t flags;          /* Chunk flags */
} nmo_chunk_header_t;

/**
 * Create chunk
 * @param chunk_id Chunk ID
 * @return Chunk or NULL on error
 */
NMO_API nmo_chunk_t* nmo_chunk_create(uint32_t chunk_id);

/**
 * Destroy chunk
 * @param chunk Chunk to destroy
 */
NMO_API void nmo_chunk_destroy(nmo_chunk_t* chunk);

/**
 * Parse chunk from data
 * @param chunk Chunk
 * @param data Data buffer
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_parse(nmo_chunk_t* chunk, const void* data, size_t size);

/**
 * Write chunk to data
 * @param chunk Chunk
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written or negative on error
 */
NMO_API ssize_t nmo_chunk_write(const nmo_chunk_t* chunk, void* buffer, size_t size);

/**
 * Get chunk header
 * @param chunk Chunk
 * @param out_header Output header
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_get_header(const nmo_chunk_t* chunk, nmo_chunk_header_t* out_header);

/**
 * Get chunk ID
 * @param chunk Chunk
 * @return Chunk ID
 */
NMO_API uint32_t nmo_chunk_get_id(const nmo_chunk_t* chunk);

/**
 * Get chunk size
 * @param chunk Chunk
 * @return Chunk size in bytes
 */
NMO_API uint32_t nmo_chunk_get_size(const nmo_chunk_t* chunk);

/**
 * Get chunk data
 * @param chunk Chunk
 * @param out_size Output data size
 * @return Pointer to chunk data
 */
NMO_API const void* nmo_chunk_get_data(const nmo_chunk_t* chunk, size_t* out_size);

/**
 * Add sub-chunk
 * @param chunk Parent chunk
 * @param sub_chunk Sub-chunk to add
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_add_sub_chunk(nmo_chunk_t* chunk, nmo_chunk_t* sub_chunk);

/**
 * Get sub-chunk count
 * @param chunk Chunk
 * @return Number of sub-chunks
 */
NMO_API uint32_t nmo_chunk_get_sub_chunk_count(const nmo_chunk_t* chunk);

/**
 * Get sub-chunk by index
 * @param chunk Chunk
 * @param index Sub-chunk index
 * @return Sub-chunk or NULL
 */
NMO_API nmo_chunk_t* nmo_chunk_get_sub_chunk(const nmo_chunk_t* chunk, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_H */
