#ifndef NMO_CHUNK_WRITER_H
#define NMO_CHUNK_WRITER_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "format/nmo_chunk_context.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_math.h"
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
typedef struct nmo_chunk_writer nmo_chunk_writer_t;

/**
 * @brief Create writer with arena allocator
 *
 * @param arena Arena for allocations
 * @return Writer or NULL on allocation failure
 */
NMO_API nmo_chunk_writer_t* nmo_chunk_writer_create(nmo_arena_t* arena);

/**
 * @brief Attach file-context remap tables used for SaveFindObjectIndex semantics.
 *
 * When set, the writer encodes object IDs as file indices and automatically
 * raises the NMO_CHUNK_OPTION_FILE flag. Passing NULL clears the context and
 * reverts to raw ID behavior.
 */
NMO_API void nmo_chunk_writer_set_file_context(nmo_chunk_writer_t* w,
											   const nmo_chunk_file_context_t* ctx);

/**
 * @brief Start new chunk
 *
 * @param w Writer
 * @param class_id Object class ID
 * @param chunk_version Chunk version
 */
NMO_API void nmo_chunk_writer_start(nmo_chunk_writer_t* w, nmo_class_id_t class_id, uint32_t chunk_version);

/**
 * @brief Write uint8_t (padded to DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_byte(nmo_chunk_writer_t* w, uint8_t value);

/**
 * @brief Write uint16_t (padded to DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_word(nmo_chunk_writer_t* w, uint16_t value);

/**
 * @brief Write uint32_t (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_dword(nmo_chunk_writer_t* w, uint32_t value);

/**
 * @brief Write uint32_t as two 16-bit words (Phase 7)
 *
 * Writes a 32-bit value as two consecutive 16-bit values (low word first).
 * Used for compressed animation data and specific file format requirements.
 * Matches CKStateChunk::WriteDwordAsWords behavior.
 *
 * Format: [16-bit low][16-bit high] (both padded to DWORD boundaries)
 *
 * @param w Writer
 * @param value 32-bit value to split and write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_dword_as_words(nmo_chunk_writer_t* w, uint32_t value);

/**
 * @brief Write array of uint32_t values as 16-bit word pairs
 *
 * Convenience helper for bulk writing of `count` DWORDs via the
 * `WriteDwordAsWords` encoding. Performs a single buffer reservation to
 * minimize reallocation churn compared to looping manually.
 *
 * @param w Writer
 * @param values Source array (must contain @p count entries)
 * @param count Number of DWORD values to encode
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_array_dword_as_words(
	nmo_chunk_writer_t* w,
	const uint32_t* values,
	size_t count);

/**
 * @brief Write int32_t (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_int(nmo_chunk_writer_t* w, int32_t value);

/**
 * @brief Write float (exactly one DWORD)
 *
 * @param w Writer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_float(nmo_chunk_writer_t* w, float value);

/**
 * @brief Write GUID (two DWORDs)
 *
 * @param w Writer
 * @param guid GUID to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_guid(nmo_chunk_writer_t* w, nmo_guid_t guid);

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
NMO_API int nmo_chunk_writer_write_bytes(nmo_chunk_writer_t* w, const void* data, size_t bytes);

/**
 * @brief Write null-terminated string
 *
 * Format: [4 bytes length][length bytes data][padding to DWORD]
 *
 * @param w Writer
 * @param str String to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_string(nmo_chunk_writer_t* w, const char* str);

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
NMO_API int nmo_chunk_writer_write_buffer(nmo_chunk_writer_t* w, const void* data, size_t size);

/**
 * @brief Write buffer without size prefix
 *
 * Writes raw buffer data without a size prefix.
 * Matches CKStateChunk::WriteBufferNoSize_LEndian behavior.
 *
 * @param w Writer
 * @param bytes Number of bytes to write
 * @param data Source data
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_buffer_nosize(nmo_chunk_writer_t* w, size_t bytes, const void* data);

/**
 * @brief Write buffer without size prefix with 16-bit endian conversion
 *
 * Writes raw buffer data without a size prefix, applying 16-bit little-endian conversion.
 * Used for specific compressed data formats that require 16-bit field swapping.
 * Matches CKStateChunk::WriteBufferNoSize_LEndian16 behavior.
 *
 * @param w Writer
 * @param value_count Number of 16-bit values to write
 * @param data Source data (must contain value_count * 2 bytes)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_buffer_nosize_lendian16(nmo_chunk_writer_t* w, size_t value_count, const void* data);

/**
 * @brief Lock write buffer for direct writing
 *
 * Returns a pointer to the chunk's data buffer for direct writing.
 * Caller must ensure they write exactly dword_count DWORDs.
 * Matches CKStateChunk::LockWriteBuffer behavior.
 *
 * @param w Writer
 * @param dword_count Number of DWORDs to reserve
 * @return Pointer to write buffer, or NULL on error
 */
NMO_API uint32_t* nmo_chunk_writer_lock_write_buffer(nmo_chunk_writer_t* w, size_t dword_count);

/**
 * @brief Write object ID and track
 *
 * Automatically adds to chunk's ID list if not present.
 *
 * @param w Writer
 * @param id Object ID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_object_id(nmo_chunk_writer_t* w, nmo_object_id_t id);

/**
 * @brief Start object sequence
 *
 * Sets up ID tracking for a sequence of object IDs.
 *
 * @param w Writer
 * @param count Number of objects in sequence
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_object_sequence(nmo_chunk_writer_t* w, size_t count);

/**
 * @brief Start manager sequence
 *
 * Writes the sequence header [count][GUID.d1][GUID.d2] and tracks offsets.
 *
 * @param w Writer
 * @param manager Manager GUID for the sequence
 * @param count Number of entries in the sequence
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer_t* w,
													nmo_guid_t manager,
													size_t count);

/**
 * @brief Write manager int with GUID
 *
 * Writes [GUID.d1][GUID.d2][value] and tracks position in managers list.
 * Matches CKStateChunk::WriteManagerInt behavior.
 *
 * @param w Writer
 * @param manager Manager GUID
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_manager_int(nmo_chunk_writer_t* w, nmo_guid_t manager, int32_t value);

/**
 * @brief Write array with little-endian byte order
 *
 * Writes array in format: [totalBytes][elementCount][data padded to DWORDs].
 * Matches CKStateChunk::WriteArray_LEndian behavior.
 *
 * @param w Writer
 * @param element_count Number of elements in array
 * @param element_size Size of each element in bytes
 * @param src_data Source data pointer (can be NULL for empty array)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_array_lendian(nmo_chunk_writer_t* w, int element_count, int element_size, const void* src_data);

/**
 * @brief Write array with 16-bit little-endian byte order
 *
 * Similar to write_array_lendian but handles 16-bit field-level byte swapping.
 * Used for certain data types that require 16-bit endianness conversion.
 * Matches CKStateChunk::WriteArray_LEndian16 behavior.
 *
 * @param w Writer
 * @param element_count Number of elements in array
 * @param element_size Size of each element in bytes
 * @param src_data Source data pointer (can be NULL for empty array)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_array_lendian16(nmo_chunk_writer_t* w, int element_count, int element_size, const void* src_data);

/**
 * @brief Write buffer with 16-bit little-endian conversion
 *
 * Writes raw buffer data with 16-bit field-level endianness conversion.
 *
 * @param w Writer
 * @param bytes Number of bytes to write
 * @param data Source data pointer
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_buffer_lendian16(nmo_chunk_writer_t* w, size_t bytes, const void* data);

/**
 * @brief Start sub-chunk sequence
 *
 * Writes the count of sub-chunks that will follow and tracks the position.
 * Matches CKStateChunk::StartSubChunkSequence behavior.
 *
 * @param w Writer
 * @param count Number of sub-chunks in sequence
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_start_subchunk_sequence(nmo_chunk_writer_t* w, size_t count);

/**
 * @brief Write sub-chunk to parent chunk
 *
 * Serializes a complete sub-chunk into the parent chunk's data buffer.
 * Matches CKStateChunk::WriteSubChunkSequence behavior.
 *
 * @param w Writer
 * @param sub Sub-chunk to write (can be NULL for empty slot)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_subchunk(nmo_chunk_writer_t* w, const nmo_chunk_t* sub);

/**
 * @brief Write 2D vector (2 floats = 2 DWORDs)
 *
 * @param w Writer
 * @param v Vector to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_vector2(nmo_chunk_writer_t* w, const nmo_vector2_t* v);

/**
 * @brief Write 3D vector (3 floats = 3 DWORDs)
 *
 * @param w Writer
 * @param v Vector to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_vector(nmo_chunk_writer_t* w, const nmo_vector_t* v);

/**
 * @brief Write 4D vector (4 floats = 4 DWORDs)
 *
 * @param w Writer
 * @param v Vector to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_vector4(nmo_chunk_writer_t* w, const nmo_vector4_t* v);

/**
 * @brief Write 4x4 matrix (16 floats = 16 DWORDs)
 *
 * @param w Writer
 * @param m Matrix to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_matrix(nmo_chunk_writer_t* w, const nmo_matrix_t* m);

/**
 * @brief Write quaternion (4 floats = 4 DWORDs)
 *
 * @param w Writer
 * @param q Quaternion to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_quaternion(nmo_chunk_writer_t* w, const nmo_quaternion_t* q);

/**
 * @brief Write RGBA color (4 floats = 4 DWORDs)
 *
 * @param w Writer
 * @param c Color to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_color(nmo_chunk_writer_t* w, const nmo_color_t* c);

/**
 * @brief Write identifier
 *
 * @param w Writer
 * @param identifier Identifier to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_identifier(nmo_chunk_writer_t* w, uint32_t identifier);

/**
 * @brief Finalize and get chunk
 *
 * Returns the completed chunk. Writer should not be used after this.
 *
 * @param w Writer
 * @return Completed chunk or NULL on error
 */
NMO_API nmo_chunk_t* nmo_chunk_writer_finalize(nmo_chunk_writer_t* w);

/**
 * @brief Destroy writer
 *
 * @param w Writer
 */
NMO_API void nmo_chunk_writer_destroy(nmo_chunk_writer_t* w);

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_WRITER_H
