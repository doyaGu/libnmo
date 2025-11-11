#ifndef NMO_CHUNK_WRITER_H
#define NMO_CHUNK_WRITER_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_chunk_writer.h
 * @brief Sequential writer for CKStateChunk data
 *
 * Provides DWORD-aligned sequential writing of chunk data:
 * - Primitive types (byte, word, dword, int, float, GUID)
 * - Complex types (strings, buffers)
 * - Object ID tracking
 * - Sub-chunk support
 * - Automatic buffer growth
 */

// Forward declaration
typedef struct nmo_chunk_writer nmo_chunk_writer;

/**
 * @brief Create writer with arena allocator
 *
 * @param arena Arena for allocations
 * @return Writer or NULL on allocation failure
 */
NMO_API nmo_chunk_writer* nmo_chunk_writer_create(nmo_arena* arena);

/**
 * @brief Start new chunk
 *
 * @param w Writer
 * @param class_id Object class ID
 * @param chunk_version Chunk version
 */
NMO_API void nmo_chunk_writer_start(nmo_chunk_writer* w, nmo_class_id class_id, uint32_t chunk_version);

/**
 * @brief Write uint8_t (padded to DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_byte(nmo_chunk_writer* w, uint8_t value);

/**
 * @brief Write uint16_t (padded to DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_word(nmo_chunk_writer* w, uint16_t value);

/**
 * @brief Write uint32_t (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_dword(nmo_chunk_writer* w, uint32_t value);

/**
 * @brief Write int32_t (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_int(nmo_chunk_writer* w, int32_t value);

/**
 * @brief Write float (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_float(nmo_chunk_writer* w, float value);

/**
 * @brief Write GUID (two DWORDs)
 *
 * @param w Writer
 * @param guid GUID to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_guid(nmo_chunk_writer* w, nmo_guid guid);

/**
 * @brief Write raw bytes (DWORD-aligned)
 *
 * Writes exactly `bytes` bytes, padding to next DWORD boundary.
 *
 * @param w Writer
 * @param data Source data
 * @param bytes Number of bytes to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_bytes(nmo_chunk_writer* w, const void* data, size_t bytes);

/**
 * @brief Write null-terminated string
 *
 * Format: [4 bytes length][length bytes data][padding to DWORD]
 *
 * @param w Writer
 * @param str String to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_string(nmo_chunk_writer* w, const char* str);

/**
 * @brief Write binary buffer
 *
 * Format: [4 bytes size][size bytes data][padding to DWORD]
 *
 * @param w Writer
 * @param data Buffer data
 * @param size Buffer size in bytes
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_buffer(nmo_chunk_writer* w, const void* data, size_t size);

/**
 * @brief Write object ID and track
 *
 * Automatically adds to chunk's ID list if not present.
 *
 * @param w Writer
 * @param id Object ID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_object_id(nmo_chunk_writer* w, nmo_object_id id);

/**
 * @brief Start object sequence
 *
 * Sets up ID tracking for a sequence of object IDs.
 *
 * @param w Writer
 * @param count Number of objects in sequence
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_object_sequence(nmo_chunk_writer* w, size_t count);

/**
 * @brief Start manager sequence
 *
 * Sets up manager tracking for a sequence of manager ints.
 *
 * @param w Writer
 * @param count Number of managers in sequence
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer* w, size_t count);

/**
 * @brief Start sub-chunk
 *
 * @param w Writer
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_sub_chunk(nmo_chunk_writer* w);

/**
 * @brief End sub-chunk
 *
 * @param w Writer
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_end_sub_chunk(nmo_chunk_writer* w);

/**
 * @brief Write identifier
 *
 * @param w Writer
 * @param identifier Identifier to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_identifier(nmo_chunk_writer* w, uint32_t identifier);

/**
 * @brief Finalize and get chunk
 *
 * Returns the completed chunk. Writer should not be used after this.
 *
 * @param w Writer
 * @return Completed chunk or NULL on error
 */
NMO_API nmo_chunk* nmo_chunk_writer_finalize(nmo_chunk_writer* w);

/**
 * @brief Destroy writer
 *
 * @param w Writer
 */
NMO_API void nmo_chunk_writer_destroy(nmo_chunk_writer* w);

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_WRITER_H
