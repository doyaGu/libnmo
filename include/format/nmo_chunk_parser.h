#ifndef NMO_CHUNK_PARSER_H
#define NMO_CHUNK_PARSER_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include "core/nmo_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_chunk_parser.h
 * @brief Sequential parser for CKStateChunk data
 *
 * Provides DWORD-aligned sequential reading of chunk data:
 * - Primitive types (byte, word, dword, int, float, GUID)
 * - Complex types (strings, buffers)
 * - Object ID tracking
 * - Identifier-based navigation
 * - Bounds checking on all operations
 */

// Forward declaration
typedef struct nmo_chunk_parser nmo_chunk_parser_t;

/**
 * @brief Create parser from chunk
 *
 * @param chunk Chunk to parse (must not be NULL)
 * @return Parser or NULL on allocation failure
 */
NMO_API nmo_chunk_parser_t *nmo_chunk_parser_create(nmo_chunk_t *chunk);

/**
 * @brief Get current cursor position
 *
 * @param p Parser
 * @return Current position in DWORDs
 */
NMO_API size_t nmo_chunk_parser_tell(nmo_chunk_parser_t *p);

/**
 * @brief Seek to absolute position
 *
 * @param p Parser
 * @param pos Position in DWORDs
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_seek(nmo_chunk_parser_t *p, size_t pos);

/**
 * @brief Skip forward by offset
 *
 * @param p Parser
 * @param dwords Number of DWORDs to skip
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_skip(nmo_chunk_parser_t *p, size_t dwords);

/**
 * @brief Get remaining DWORDs
 *
 * @param p Parser
 * @return Number of DWORDs remaining
 */
NMO_API size_t nmo_chunk_parser_remaining(nmo_chunk_parser_t *p);

/**
 * @brief Check if at end of chunk
 *
 * @param p Parser
 * @return 1 if at end, 0 otherwise
 */
NMO_API int nmo_chunk_parser_at_end(nmo_chunk_parser_t *p);

/**
 * @brief Read uint8_t (padded to DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_byte(nmo_chunk_parser_t *p, uint8_t *out);

/**
 * @brief Read uint16_t (padded to DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_word(nmo_chunk_parser_t *p, uint16_t *out);

/**
 * @brief Read uint32_t (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_dword(nmo_chunk_parser_t *p, uint32_t *out);

/**
 * @brief Read 32-bit value stored as two 16-bit words
 *
 * Reads a 32-bit value that was written as two separate 16-bit writes (each padded to DWORD).
 * Used for compressed animation data formats. Inverse of write_dword_as_words.
 * Matches CKStateChunk::ReadDwordAsWords behavior.
 *
 * @param p Parser
 * @param out Output value (reconstructed from low and high words)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_dword_as_words(nmo_chunk_parser_t *p, uint32_t *out);

/**
 * @brief Read array of 32-bit values stored as 16-bit word pairs
 *
 * Helper for bulk decoding of sequences written via
 * `nmo_chunk_writer_write_array_dword_as_words`. Automatically iterates the
 * parser cursor and reconstructs each DWORD.
 *
 * @param p Parser
 * @param out_values Destination buffer (must contain @p count entries)
 * @param count Number of DWORD values to reconstruct
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_dword_array_as_words(
	nmo_chunk_parser_t *p,
	uint32_t *out_values,
	size_t count);

/**
 * @brief Read int32_t (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_int(nmo_chunk_parser_t *p, int32_t *out);

/**
 * @brief Read float (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_float(nmo_chunk_parser_t *p, float *out);

/**
 * @brief Read GUID (two DWORDs)
 *
 * @param p Parser
 * @param out Output GUID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_guid(nmo_chunk_parser_t *p, nmo_guid_t *out);

/**
 * @brief Read raw bytes (DWORD-aligned)
 *
 * Reads exactly `bytes` bytes, padding to next DWORD boundary.
 *
 * @param p Parser
 * @param dest Destination buffer
 * @param bytes Number of bytes to read
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_bytes(nmo_chunk_parser_t *p, void *dest, size_t bytes);

/**
 * @brief Read null-terminated string
 *
 * Format: [4 bytes length][length bytes data][padding to DWORD]
 *
 * @param p Parser
 * @param out Output string (allocated from arena)
 * @param arena Arena for allocation
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_string(nmo_chunk_parser_t *p, char **out, nmo_arena_t *arena);

/**
 * @brief Read binary buffer
 *
 * Format: [4 bytes size][size bytes data][padding to DWORD]
 *
 * @param p Parser
 * @param out Output buffer (allocated from arena)
 * @param size Output size in bytes
 * @param arena Arena for allocation
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_buffer(nmo_chunk_parser_t *p, void **out, size_t *size, nmo_arena_t *arena);

/**
 * @brief Read buffer without size prefix
 *
 * Reads raw buffer data without a size prefix. Caller provides the size.
 * Matches CKStateChunk::ReadAndFillBuffer_LEndian(int size, void *buffer) behavior.
 *
 * @param p Parser
 * @param bytes Number of bytes to read
 * @param buffer Destination buffer (must be pre-allocated)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_buffer_nosize(nmo_chunk_parser_t *p, size_t bytes, void *buffer);

/**
 * @brief Read buffer without size prefix with 16-bit endian conversion (Phase 6)
 *
 * Reads raw buffer data without a size prefix, applying 16-bit little-endian conversion.
 * Used for specific compressed data formats that require 16-bit field swapping.
 * Matches CKStateChunk::ReadAndFillBuffer_LEndian16 behavior.
 *
 * @param p Parser
 * @param value_count Number of 16-bit values to read
 * @param buffer Destination buffer (must be pre-allocated to value_count * 2 bytes)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_buffer_nosize_lendian16(nmo_chunk_parser_t *p, size_t value_count, void *buffer);

/**
 * @brief Lock read buffer for direct reading
 *
 * Returns a pointer to the chunk's data buffer for direct reading.
 * Matches CKStateChunk::LockReadBuffer behavior.
 *
 * @param p Parser
 * @return Pointer to read buffer at current position, or NULL on error
 */
NMO_API const uint32_t *nmo_chunk_parser_lock_read_buffer(nmo_chunk_parser_t *p);

/**
 * @brief Read object ID and track in chunk
 *
 * Automatically adds to chunk's ID list if not present.
 *
 * @param p Parser
 * @param out Output object ID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_object_id(nmo_chunk_parser_t *p, nmo_object_id_t *out);

/**
 * @brief Read manager int with GUID
 *
 * Reads [GUID.d1][GUID.d2][value] and advances cursor.
 * Matches CKStateChunk::ReadManagerInt behavior.
 *
 * @param p Parser
 * @param manager Output manager GUID (can be NULL)
 * @return Manager int value, or 0 on error
 */
NMO_API int32_t nmo_chunk_parser_read_manager_int(nmo_chunk_parser_t *p, nmo_guid_t *manager);

/**
 * @brief Read manager int sequence value
 *
 * Reads just the value without GUID (used after start_manager_sequence).
 * Matches CKStateChunk::ReadManagerIntSequence behavior.
 *
 * @param p Parser
 * @return Manager int value, or 0 on error
 */
NMO_API int32_t nmo_chunk_parser_read_manager_int_sequence(nmo_chunk_parser_t *p);

/**
 * @brief Read array with little-endian byte order
 *
 * Reads array in format: [totalBytes][elementCount][data padded to DWORDs].
 * Allocates memory from arena and returns element count.
 * Matches CKStateChunk::ReadArray_LEndian behavior.
 *
 * @param p Parser
 * @param array Output pointer to allocated array data (NULL on error)
 * @param arena Arena for allocation
 * @return Element count, or 0 on error/empty array
 */
NMO_API int nmo_chunk_parser_read_array_lendian(nmo_chunk_parser_t *p, void **array, nmo_arena_t *arena);

/**
 * @brief Read array with 16-bit little-endian byte order
 *
 * Similar to read_array_lendian but handles 16-bit field-level byte swapping.
 * Used for certain data types that require 16-bit endianness conversion.
 * Matches CKStateChunk::ReadArray_LEndian16 behavior.
 *
 * @param p Parser
 * @param array Output pointer to allocated array data (NULL on error)
 * @param arena Arena for allocation
 * @return Element count, or 0 on error/empty array
 */
NMO_API int nmo_chunk_parser_read_array_lendian16(nmo_chunk_parser_t *p, void **array, nmo_arena_t *arena);

/**
 * @brief Read buffer with 16-bit little-endian conversion
 *
 * Reads raw buffer data and performs 16-bit field-level endianness conversion.
 * Caller provides the size and pre-allocated buffer.
 *
 * @param p Parser
 * @param bytes Number of bytes to read
 * @param buffer Destination buffer (must be pre-allocated)
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_buffer_lendian16(nmo_chunk_parser_t *p, size_t bytes, void *buffer);

/**
 * @brief Seek to identifier
 *
 * Scans forward for identifier DWORD and positions cursor after it.
 *
 * @param p Parser
 * @param identifier Identifier to find
 * @return NMO_OK if found, NMO_ERR_EOF if not found
 */
NMO_API int nmo_chunk_parser_seek_identifier(nmo_chunk_parser_t *p, uint32_t identifier);

/**
 * @brief Seek to identifier and return size until next
 *
 * Similar to seek_identifier but also returns the size (in DWORDs) 
 * between this identifier and the next one (or end of chunk).
 * Matches CKStateChunk::SeekIdentifierAndReturnSize behavior.
 *
 * @param p Parser
 * @param identifier Identifier to find
 * @param out_size Output size in DWORDs until next identifier (can be NULL)
 * @return NMO_OK if found, NMO_ERR_EOF if not found
 */
NMO_API int nmo_chunk_parser_seek_identifier_with_size(nmo_chunk_parser_t *p, uint32_t identifier, size_t *out_size);

/**
 * @brief Read identifier
 *
 * @param p Parser
 * @param identifier Output identifier
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_identifier(nmo_chunk_parser_t *p, uint32_t *identifier);

/**
 * @brief Start reading sub-chunk sequence
 *
 * Reads and returns the count of sub-chunks in the sequence.
 * Matches CKStateChunk::StartReadSequence behavior.
 *
 * @param p Parser
 * @return Number of sub-chunks, or negative error code on failure
 */
NMO_API int nmo_chunk_parser_start_read_sequence(nmo_chunk_parser_t *p);

/**
 * @brief Read sub-chunk from parent chunk
 *
 * Reconstructs a sub-chunk from the parent chunk's data buffer.
 * Matches CKStateChunk::ReadSubChunk behavior.
 *
 * @param p Parser
 * @param arena Arena for allocations
 * @param out_chunk Output sub-chunk pointer
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_chunk_parser_read_subchunk(nmo_chunk_parser_t *p, nmo_arena_t *arena, nmo_chunk_t **out_chunk);

/**
 * @brief Read 2D vector (2 floats = 2 DWORDs)
 *
 * @param p Parser
 * @param out Output vector
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_vector2(nmo_chunk_parser_t *p, nmo_vector2_t *out);

/**
 * @brief Read 3D vector (3 floats = 3 DWORDs)
 *
 * @param p Parser
 * @param out Output vector
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_vector(nmo_chunk_parser_t *p, nmo_vector_t *out);

/**
 * @brief Read 4D vector (4 floats = 4 DWORDs)
 *
 * @param p Parser
 * @param out Output vector
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_vector4(nmo_chunk_parser_t *p, nmo_vector4_t *out);

/**
 * @brief Read 4x4 matrix (16 floats = 16 DWORDs)
 *
 * @param p Parser
 * @param out Output matrix
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_matrix(nmo_chunk_parser_t *p, nmo_matrix_t *out);

/**
 * @brief Read quaternion (4 floats = 4 DWORDs)
 *
 * @param p Parser
 * @param out Output quaternion
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_quaternion(nmo_chunk_parser_t *p, nmo_quaternion_t *out);

/**
 * @brief Read RGBA color (4 floats = 4 DWORDs)
 *
 * @param p Parser
 * @param out Output color
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_color(nmo_chunk_parser_t *p, nmo_color_t *out);

/**
 * @brief Destroy parser
 *
 * @param p Parser
 */
NMO_API void nmo_chunk_parser_destroy(nmo_chunk_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_PARSER_H
