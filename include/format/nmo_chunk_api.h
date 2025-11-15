/**
 * @file nmo_chunk_api.h
 * @brief High-level CKStateChunk-compatible API
 * 
 * This API provides a unified, type-safe interface for reading and writing
 * chunk data, matching the design of Virtools' CKStateChunk class.
 * 
 * This header consolidates all high-level chunk operations:
 * - Lifecycle and mode management (start_read, start_write, close)
 * - Navigation (goto, skip, seek)
 * - Primitive types (byte, word, int, dword, float, GUID)
 * - Complex types (string, buffer, object ID)
 * - Arrays (primitive and object ID arrays)
 * - Math types (vector2/3/4, quaternion, matrix, color)
 * - Sub-chunks (write, read, sequences)
 * - Manager sequences
 * - Identifiers (write, read, seek)
 * - Compression (pack, unpack)
 * - ID remapping
 * 
 * For low-level chunk structure and core operations (create, destroy, serialize),
 * see nmo_chunk.h.
 */

#ifndef NMO_CHUNK_API_H
#define NMO_CHUNK_API_H

#include "nmo_chunk.h"
#include "nmo_types.h"
#include "nmo_image.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_id_remap nmo_id_remap_t;

/**
 * @brief Chunk parser state (internal)
 *
 * Tracks the current position during read/write operations.
 * Similar to CKStateChunk::ChunkParser.
 */
typedef struct nmo_chunk_parser_state {
    size_t current_pos;         /**< Current position in DWORDs */
    size_t prev_identifier_pos; /**< Previous identifier position */
    size_t data_size;           /**< Total data size in DWORDs */
} nmo_chunk_parser_state_t;

// =============================================================================
// LIFECYCLE & MODE MANAGEMENT
// =============================================================================

/**
 * @brief Start read mode
 *
 * Initializes the chunk for reading. Sets current position to 0.
 * Must be called before any Read* operations.
 *
 * @param chunk Chunk to start reading (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_read(nmo_chunk_t *chunk);

/**
 * @brief Start write mode
 *
 * Initializes the chunk for writing. Sets current position to 0.
 * Must be called before any Write* operations.
 *
 * @param chunk Chunk to start writing (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_write(nmo_chunk_t *chunk);

/**
 * @brief Close chunk and finalize
 *
 * Finalizes the chunk after writing. Updates data_size.
 * Optional - chunk is usable without calling this.
 *
 * @param chunk Chunk to close (required)
 */
NMO_API void nmo_chunk_close(nmo_chunk_t *chunk);

/**
 * @brief Clear chunk data
 *
 * Resets chunk to empty state. Keeps allocated memory.
 *
 * @param chunk Chunk to clear (required)
 */
NMO_API void nmo_chunk_clear(nmo_chunk_t *chunk);

/**
 * @brief Update data size
 *
 * Updates data_size based on current write position.
 * Called automatically by Close().
 *
 * @param chunk Chunk to update (required)
 */
NMO_API void nmo_chunk_update_data_size(nmo_chunk_t *chunk);

// =============================================================================
// METADATA ACCESS
// =============================================================================

/**
 * @brief Get data version
 *
 * @param chunk Chunk (required)
 * @return Data version (class-specific)
 */
NMO_API uint32_t nmo_chunk_get_data_version(const nmo_chunk_t *chunk);

/**
 * @brief Set data version
 *
 * @param chunk Chunk (required)
 * @param version New data version
 */
NMO_API void nmo_chunk_set_data_version(nmo_chunk_t *chunk, uint32_t version);

/**
 * @brief Get chunk version
 *
 * @param chunk Chunk (required)
 * @return Chunk format version (usually 7 for VERSION4)
 */
NMO_API uint32_t nmo_chunk_get_chunk_version(const nmo_chunk_t *chunk);

/**
 * @brief Get data size in bytes
 *
 * @param chunk Chunk (required)
 * @return Data size in bytes (data_size * 4)
 */
NMO_API size_t nmo_chunk_get_data_size(const nmo_chunk_t *chunk);

/**
 * @brief Get chunk class ID
 *
 * Returns the class identifier of the chunk.
 *
 * @param chunk Chunk (required)
 * @return Class ID (chunk->class_id), or 0 if chunk is NULL
 */
NMO_API nmo_class_id_t nmo_chunk_get_class_id(const nmo_chunk_t *chunk);

/**
 * @brief Get chunk data size in bytes (alias for nmo_chunk_get_data_size)
 *
 * Returns the size of the data buffer in bytes.
 *
 * @param chunk Chunk (required)
 * @return Data size in bytes (data_size * 4), or 0 if chunk is NULL
 */
NMO_API uint32_t nmo_chunk_get_size(const nmo_chunk_t *chunk);

/**
 * @brief Get pointer to chunk data buffer
 *
 * Returns a direct pointer to the internal data buffer.
 *
 * @param chunk Chunk (required)
 * @param out_size Output size in bytes (optional, can be NULL)
 * @return Pointer to data buffer or NULL if chunk is NULL
 */
NMO_API const void *nmo_chunk_get_data(const nmo_chunk_t *chunk, size_t *out_size);

/**
 * @brief Check if chunk data is compressed
 *
 * Checks if PACKED option flag is set.
 *
 * @param chunk Chunk (required)
 * @return 1 if compressed (OPTION_PACKED set), 0 otherwise
 */
NMO_API int nmo_chunk_is_compressed(const nmo_chunk_t *chunk);

// =============================================================================
// NAVIGATION
// =============================================================================

/**
 * @brief Get current position
 *
 * @param chunk Chunk (required)
 * @return Current position in DWORDs
 */
NMO_API size_t nmo_chunk_get_position(const nmo_chunk_t *chunk);

/**
 * @brief Seek to absolute position
 *
 * @param chunk Chunk (required)
 * @param pos Position in DWORDs
 * @return NMO_OK on success, NMO_ERR_OUT_OF_BOUNDS if pos > data_size
 */
NMO_API nmo_result_t nmo_chunk_goto(nmo_chunk_t *chunk, size_t pos);

/**
 * @brief Skip forward
 *
 * @param chunk Chunk (required)
 * @param dwords Number of DWORDs to skip
 * @return NMO_OK on success, NMO_ERR_OUT_OF_BOUNDS if would exceed data_size
 */
NMO_API nmo_result_t nmo_chunk_skip(nmo_chunk_t *chunk, size_t dwords);

// =============================================================================
// MEMORY MANAGEMENT
// =============================================================================

/**
 * @brief Ensure capacity for writing
 *
 * Automatically expands the data buffer if needed.
 * Similar to CKStateChunk::CheckSize().
 *
 * @param chunk Chunk (required)
 * @param needed_dwords Number of DWORDs needed from current position
 * @return NMO_OK on success, NMO_ERR_NOMEM on allocation failure
 */
NMO_API nmo_result_t nmo_chunk_check_size(nmo_chunk_t *chunk, size_t needed_dwords);

// =============================================================================
// PRIMITIVE TYPES - WRITE
// =============================================================================

/**
 * @brief Write byte (stored as DWORD with padding)
 *
 * @param chunk Chunk (required)
 * @param value Byte value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_byte(nmo_chunk_t *chunk, uint8_t value);

/**
 * @brief Write word (stored as DWORD with padding)
 *
 * @param chunk Chunk (required)
 * @param value Word value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_word(nmo_chunk_t *chunk, uint16_t value);

/**
 * @brief Write 32-bit integer
 *
 * @param chunk Chunk (required)
 * @param value Integer value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_int(nmo_chunk_t *chunk, int32_t value);

/**
 * @brief Write 32-bit unsigned integer (DWORD)
 *
 * @param chunk Chunk (required)
 * @param value DWORD value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_dword(nmo_chunk_t *chunk, uint32_t value);

/**
 * @brief Write float
 *
 * @param chunk Chunk (required)
 * @param value Float value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_float(nmo_chunk_t *chunk, float value);

/**
 * @brief Write GUID (8 bytes = 2 DWORDs)
 *
 * @param chunk Chunk (required)
 * @param value GUID value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_guid(nmo_chunk_t *chunk, nmo_guid_t value);

/**
 * @brief Write null-terminated string
 *
 * Format: [4-byte length][string data, DWORD-aligned]
 * Length includes null terminator.
 *
 * @param chunk Chunk (required)
 * @param str String to write (NULL = empty string)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_string(nmo_chunk_t *chunk, const char *str);

/**
 * @brief Write buffer with size prefix
 *
 * Format: [4-byte size][data, DWORD-aligned]
 *
 * @param chunk Chunk (required)
 * @param data Buffer data (NULL = write zero size)
 * @param size Buffer size in bytes
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_buffer(nmo_chunk_t *chunk,
                                            const void *data,
                                            size_t size);

/**
 * @brief Write buffer without size prefix
 *
 * Format: [data, DWORD-aligned] (no size stored)
 * Caller must know the size when reading.
 *
 * @param chunk Chunk (required)
 * @param data Buffer data (required if size > 0)
 * @param size Buffer size in bytes
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_buffer_no_size(nmo_chunk_t *chunk,
                                                    const void *data,
                                                    size_t size);

/**
 * @brief Write object ID
 *
 * Tracks position in ids[] list for later remapping.
 *
 * @param chunk Chunk (required)
 * @param id Object ID (0 = null reference)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_object_id(nmo_chunk_t *chunk, nmo_object_id_t id);

// =============================================================================
// PRIMITIVE TYPES - READ
// =============================================================================

/**
 * @brief Read byte
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_byte(nmo_chunk_t *chunk, uint8_t *out_value);

/**
 * @brief Read word
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_word(nmo_chunk_t *chunk, uint16_t *out_value);

/**
 * @brief Read 32-bit integer
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_int(nmo_chunk_t *chunk, int32_t *out_value);

/**
 * @brief Read 32-bit unsigned integer (DWORD)
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_dword(nmo_chunk_t *chunk, uint32_t *out_value);

/**
 * @brief Read float
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_float(nmo_chunk_t *chunk, float *out_value);

/**
 * @brief Read GUID
 *
 * @param chunk Chunk (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_guid(nmo_chunk_t *chunk, nmo_guid_t *out_value);

/**
 * @brief Read string (arena-allocated)
 *
 * Returns arena-allocated string. Caller does not need to free.
 *
 * @param chunk Chunk (required)
 * @param out_str Output string pointer (required)
 * @return String length (excluding null terminator), 0 if empty or error
 */
NMO_API size_t nmo_chunk_read_string(nmo_chunk_t *chunk, char **out_str);

/**
 * @brief Read buffer (allocates from arena)
 *
 * @param chunk Chunk (required)
 * @param out_data Output data pointer (required)
 * @param out_size Output size in bytes (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_buffer(nmo_chunk_t *chunk,
                                           void **out_data,
                                           size_t *out_size);

/**
 * @brief Read buffer into existing buffer
 *
 * Reads size from chunk, then copies data into buffer.
 * Buffer must be large enough.
 *
 * @param chunk Chunk (required)
 * @param buffer Pre-allocated buffer (required)
 * @param buffer_size Buffer size in bytes
 * @return Number of bytes read, 0 on error
 */
NMO_API size_t nmo_chunk_read_and_fill_buffer(nmo_chunk_t *chunk,
                                              void *buffer,
                                              size_t buffer_size);

/**
 * @brief Read object ID
 *
 * @param chunk Chunk (required)
 * @param out_id Output ID (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_object_id(nmo_chunk_t *chunk, nmo_object_id_t *out_id);

// =============================================================================
// PRIMITIVE ARRAYS
// =============================================================================

/**
 * @brief Write int32 array
 *
 * Format: [4-byte count][element1][element2]...
 *
 * @param chunk Chunk (required)
 * @param array Array data (required)
 * @param count Number of elements
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_int_array(nmo_chunk_t *chunk,
                                                const int32_t *array,
                                                size_t count);

/**
 * @brief Read int32 array (arena-allocated)
 *
 * @param chunk Chunk (required)
 * @param out_array Output array pointer (required)
 * @param out_count Output element count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_int_array(nmo_chunk_t *chunk,
                                               int32_t **out_array,
                                               size_t *out_count,
                                               nmo_arena_t *arena);

/**
 * @brief Write float array
 *
 * @param chunk Chunk (required)
 * @param array Array data (required)
 * @param count Number of elements
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_float_array(nmo_chunk_t *chunk,
                                                  const float *array,
                                                  size_t count);

/**
 * @brief Read float array (arena-allocated)
 *
 * @param chunk Chunk (required)
 * @param out_array Output array pointer (required)
 * @param out_count Output element count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_float_array(nmo_chunk_t *chunk,
                                                 float **out_array,
                                                 size_t *out_count,
                                                 nmo_arena_t *arena);

/**
 * @brief Write uint32 (DWORD) array
 *
 * @param chunk Chunk (required)
 * @param array Array data (required)
 * @param count Number of elements
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_dword_array(nmo_chunk_t *chunk,
                                                  const uint32_t *array,
                                                  size_t count);

/**
 * @brief Read uint32 (DWORD) array (arena-allocated)
 *
 * @param chunk Chunk (required)
 * @param out_array Output array pointer (required)
 * @param out_count Output element count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_dword_array(nmo_chunk_t *chunk,
                                                 uint32_t **out_array,
                                                 size_t *out_count,
                                                 nmo_arena_t *arena);

/**
 * @brief Write byte array
 *
 * @param chunk Chunk (required)
 * @param array Array data (required)
 * @param count Number of elements
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_byte_array(nmo_chunk_t *chunk,
                                                 const uint8_t *array,
                                                 size_t count);

/**
 * @brief Read byte array (arena-allocated)
 *
 * @param chunk Chunk (required)
 * @param out_array Output array pointer (required)
 * @param out_count Output element count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_byte_array(nmo_chunk_t *chunk,
                                                uint8_t **out_array,
                                                size_t *out_count,
                                                nmo_arena_t *arena);

/**
 * @brief Write string array
 *
 * @param chunk Chunk (required)
 * @param strings Array of string pointers (required)
 * @param count Number of strings
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_string_array(nmo_chunk_t *chunk,
                                                   const char * const *strings,
                                                   size_t count);

/**
 * @brief Read string array (arena-allocated)
 *
 * @param chunk Chunk (required)
 * @param out_strings Output array of string pointers (required)
 * @param out_count Output string count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_string_array(nmo_chunk_t *chunk,
                                                  char ***out_strings,
                                                  size_t *out_count,
                                                  nmo_arena_t *arena);

/**
 * @brief Write object ID array
 *
 * Equivalent to XObjectPointerArray::Save() in Virtools.
 * Pattern: [4-byte count][id1][id2]...
 *
 * @param chunk Chunk (required)
 * @param ids Array of object IDs (required)
 * @param count Number of IDs
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_object_id_array(nmo_chunk_t *chunk,
                                                      const nmo_object_id_t *ids,
                                                      size_t count);

/**
 * @brief Read object ID array (arena-allocated)
 *
 * Equivalent to XObjectPointerArray::Load() in Virtools.
 * Pattern: [4-byte count][id1][id2]...
 *
 * @param chunk Chunk (required)
 * @param out_ids Output array pointer (required)
 * @param out_count Output ID count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_object_id_array(nmo_chunk_t *chunk,
                                                     nmo_object_id_t **out_ids,
                                                     size_t *out_count,
                                                     nmo_arena_t *arena);

// =============================================================================
// MATH TYPES - VECTORS
// =============================================================================

/**
 * @brief Write 2D vector (8 bytes = 2 floats)
 *
 * @param chunk Chunk (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector2(nmo_chunk_t *chunk, const nmo_vector2_t *vec);

/**
 * @brief Read 2D vector
 *
 * @param chunk Chunk (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector2(nmo_chunk_t *chunk, nmo_vector2_t *out_vec);

/**
 * @brief Write 3D vector (12 bytes = 3 floats)
 *
 * @param chunk Chunk (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector3(nmo_chunk_t *chunk, const nmo_vector_t *vec);

/**
 * @brief Read 3D vector
 *
 * @param chunk Chunk (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector3(nmo_chunk_t *chunk, nmo_vector_t *out_vec);

/**
 * @brief Write 4D vector (16 bytes = 4 floats)
 *
 * @param chunk Chunk (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector4(nmo_chunk_t *chunk, const nmo_vector4_t *vec);

/**
 * @brief Read 4D vector
 *
 * @param chunk Chunk (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector4(nmo_chunk_t *chunk, nmo_vector4_t *out_vec);

// =============================================================================
// MATH TYPES - QUATERNION
// =============================================================================

/**
 * @brief Write quaternion (16 bytes = 4 floats: x, y, z, w)
 *
 * @param chunk Chunk (required)
 * @param quat Quaternion to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_quaternion(nmo_chunk_t *chunk, const nmo_quaternion_t *quat);

/**
 * @brief Read quaternion
 *
 * @param chunk Chunk (required)
 * @param out_quat Output quaternion (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_quaternion(nmo_chunk_t *chunk, nmo_quaternion_t *out_quat);

// =============================================================================
// MATH TYPES - MATRIX
// =============================================================================

/**
 * @brief Write 4x4 matrix (64 bytes = 16 floats, row-major order)
 *
 * @param chunk Chunk (required)
 * @param mat Matrix to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_matrix(nmo_chunk_t *chunk, const nmo_matrix_t *mat);

/**
 * @brief Read 4x4 matrix
 *
 * @param chunk Chunk (required)
 * @param out_mat Output matrix (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_matrix(nmo_chunk_t *chunk, nmo_matrix_t *out_mat);

// =============================================================================
// MATH TYPES - COLOR
// =============================================================================

/**
 * @brief Write RGBA color (16 bytes = 4 floats: r, g, b, a)
 *
 * @param chunk Chunk (required)
 * @param color Color to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_color(nmo_chunk_t *chunk, const nmo_color_t *color);

/**
 * @brief Read RGBA color
 *
 * @param chunk Chunk (required)
 * @param out_color Output color (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_color(nmo_chunk_t *chunk, nmo_color_t *out_color);

// =============================================================================
// OBJECT ID SEQUENCES
// =============================================================================

/**
 * @brief Get number of object IDs tracked
 *
 * Returns the count of entries in the IDs tracking list.
 * The IDs list stores DWORD positions where IDs are located in the data buffer.
 *
 * @param chunk Chunk (required)
 * @return Number of ID entries (chunk->id_count), or 0 if chunk is NULL
 */
NMO_API size_t nmo_chunk_get_id_count(const nmo_chunk_t *chunk);

/**
 * @brief Get object ID by index from tracking list
 *
 * Returns the object ID at the specified position in the IDs list.
 * The IDs list stores DWORD positions where IDs are located in the data buffer,
 * so this function looks up the position and reads the ID from there.
 *
 * @param chunk Chunk (required)
 * @param index ID list index (0-based)
 * @return Object ID at that position, or 0 if index out of bounds or chunk is NULL
 */
NMO_API uint32_t nmo_chunk_get_object_id(const nmo_chunk_t *chunk, size_t index);

/**
 * @brief Start writing object ID sequence
 *
 * Writes count and sets IDS option flag.
 *
 * @param chunk Chunk (required)
 * @param count Number of objects in sequence
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_object_sequence_start(nmo_chunk_t *chunk, size_t count);

/**
 * @brief Write object ID sequence item
 *
 * Must be called after nmo_chunk_write_object_sequence_start().
 *
 * @param chunk Chunk (required)
 * @param id Object ID
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t id);

/**
 * @brief Start reading object ID sequence
 *
 * Reads count of items in sequence.
 *
 * @param chunk Chunk (required)
 * @param out_count Output count (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_object_sequence_start(nmo_chunk_t *chunk, size_t *out_count);

/**
 * @brief Read object ID sequence item
 *
 * Must be called after nmo_chunk_read_object_sequence_start().
 *
 * @param chunk Chunk (required)
 * @param out_id Output ID (required)
 * @return NMO_OK on success, NMO_ERR_EOF if no data available
 */
NMO_API nmo_result_t nmo_chunk_read_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t *out_id);

// =============================================================================
// MANAGER SEQUENCES
// =============================================================================

/**
 * @brief Start manager sequence
 *
 * Writes manager GUID and count, sets MAN option flag.
 * Used for storing manager-specific data.
 *
 * @param chunk Chunk (required)
 * @param manager_guid Manager identifier
 * @param count Number of entries in sequence
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_manager_sequence(nmo_chunk_t *chunk,
                                                      nmo_guid_t manager_guid,
                                                      size_t count);

/**
 * @brief Write manager int
 *
 * Writes manager ID and value pair.
 * Tracks position in managers[] list.
 *
 * @param chunk Chunk (required)
 * @param mgr_id Manager ID
 * @param value Integer value
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                                 nmo_guid_t manager_guid,
                                                 uint32_t value);

/**
 * @brief Read manager int
 *
 * Reads manager ID and value pair.
 *
 * @param chunk Chunk (required)
 * @param out_mgr_id Output manager ID (required)
 * @param out_value Output value (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                                nmo_guid_t *out_manager_guid,
                                                uint32_t *out_value);

/**
 * @brief Start reading manager sequence
 *
 * Reads manager GUID and count from chunk.
 * Corresponds to StartManagerReadSequence in Virtools.
 *
 * @param chunk Chunk (required)
 * @param out_manager_guid Output manager GUID (required)
 * @param out_count Output count (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                           nmo_guid_t *out_manager_guid,
                                                           size_t *out_count);

// =============================================================================
// SUB-CHUNKS
// =============================================================================

/**
 * @brief Get number of sub-chunks
 *
 * Returns the count of sub-chunks attached to this chunk.
 *
 * @param chunk Chunk (required)
 * @return Number of sub-chunks (chunk->chunk_count), or 0 if chunk is NULL
 */
NMO_API uint32_t nmo_chunk_get_sub_chunk_count(const nmo_chunk_t *chunk);

/**
 * @brief Get sub-chunk by index
 *
 * Returns a pointer to the sub-chunk at the specified index.
 *
 * @param chunk Chunk (required)
 * @param index Sub-chunk index (0-based)
 * @return Sub-chunk pointer or NULL if index out of bounds or chunk is NULL
 */
NMO_API nmo_chunk_t *nmo_chunk_get_sub_chunk(const nmo_chunk_t *chunk, uint32_t index);

/**
 * @brief Add sub-chunk to parent chunk
 *
 * Automatically grows the sub-chunks array if needed.
 * Sets CHN option flag.
 *
 * @param chunk Parent chunk (required)
 * @param sub_chunk Sub-chunk to add (required)
 * @return NMO_OK on success, NMO_ERR_NOMEM on allocation failure
 */
NMO_API nmo_result_t nmo_chunk_add_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub_chunk);

/**
 * @brief Write sub-chunk
 *
 * Serializes sub-chunk and writes as buffer.
 * Sets CHN option flag.
 *
 * @param chunk Parent chunk (required)
 * @param sub Sub-chunk to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub);

/**
 * @brief Read sub-chunk
 *
 * Reads serialized sub-chunk data and parses it.
 *
 * @param chunk Parent chunk (required)
 * @param out_sub Output sub-chunk (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t **out_sub);

/**
 * @brief Start reading sub-chunk sequence
 *
 * Reads count of sub-chunks in sequence and prepares for reading them.
 * Corresponds to StartReadSequence in Virtools SDK.
 *
 * @param chunk Chunk (required)
 * @param out_count Output count of sub-chunks (optional, can be NULL)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_read_sub_chunk_sequence(nmo_chunk_t *chunk, size_t *out_count);

/**
 * @brief Start sub-chunk sequence
 *
 * Writes count of sub-chunks to follow.
 *
 * @param chunk Chunk (required)
 * @param count Number of sub-chunks
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_start_sub_chunk_sequence(nmo_chunk_t *chunk, size_t count);

// =============================================================================
// IDENTIFIERS
// =============================================================================

/**
 * @brief Write identifier marker
 *
 * Writes identifier and sets up prev_identifier_pos for linked list.
 *
 * @param chunk Chunk (required)
 * @param id Identifier value (should be unique within chunk)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_identifier(nmo_chunk_t *chunk, uint32_t id);

/**
 * @brief Read identifier
 *
 * @param chunk Chunk (required)
 * @param out_id Output identifier (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_identifier(nmo_chunk_t *chunk, uint32_t *out_id);

/**
 * @brief Seek to identifier
 *
 * Searches for identifier starting from current position or prev_identifier_pos.
 * Positions cursor after the identifier if found.
 *
 * @param chunk Chunk (required)
 * @param id Identifier to find
 * @return NMO_OK if found, NMO_ERR_NOT_FOUND if not found
 */
NMO_API nmo_result_t nmo_chunk_seek_identifier(nmo_chunk_t *chunk, uint32_t id);

// =============================================================================
// COMPRESSION
// =============================================================================

/**
 * @brief Compress chunk payload unconditionally.
 *
 * Always compresses the chunk buffer using miniz/zlib with the requested
 * compression level. The compressed payload replaces the current buffer and
 * the original DWORD count is stored in unpack_size so it can be restored
 * later via nmo_chunk_decompress().
 *
 * @param chunk Chunk to compress (required)
 * @param compression_level Compression level (0-9, values <0 fall back to 6)
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_chunk_compress(nmo_chunk_t *chunk, int compression_level);

/**
 * @brief Compress chunk payload only if compression helps.
 *
 * Invokes nmo_chunk_compress() and keeps the compressed buffer only when the
 * resulting size satisfies the provided ratio threshold.
 *
 * @param chunk Chunk to compress (required)
 * @param compression_level Compression level (0-9)
 * @param min_ratio Minimum compression ratio to accept (0 < ratio <= 1). The
 *        compressed size must be <= original_size * min_ratio.
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT on bad ratio
 */
NMO_API nmo_result_t nmo_chunk_compress_if_beneficial(nmo_chunk_t *chunk,
                                                      int compression_level,
                                                      float min_ratio);

/**
 * @brief Decompress chunk payload produced by nmo_chunk_compress().
 *
 * Restores the uncompressed buffer using the stored unpack_size metadata and
 * clears the PACKED option flag.
 *
 * @param chunk Chunk to decompress (required)
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_chunk_decompress(nmo_chunk_t *chunk);

/**
 * @brief Legacy helper that forwards to nmo_chunk_compress().
 */
NMO_API nmo_result_t nmo_chunk_pack(nmo_chunk_t *chunk, int compression_level);

/**
 * @brief Legacy helper that forwards to nmo_chunk_decompress().
 */
NMO_API nmo_result_t nmo_chunk_unpack(nmo_chunk_t *chunk);

// =============================================================================
// ID REMAPPING
// =============================================================================

/**
 * @brief Remap object IDs in chunk (modern API)
 *
 * Remaps all object IDs in the chunk data according to the provided mapping table.
 * This function:
 * - Processes single object IDs
 * - Processes object ID sequences
 * - Recursively processes sub-chunks
 *
 * Based on CKStateChunk::RemapObjects and ObjectRemapper implementation.
 *
 * Algorithm:
 * 1. For modern chunks (version >= 4), uses the m_Ids list which stores offsets
 *    to all object IDs in the data buffer
 * 2. For each entry in m_Ids:
 *    - If offset >= 0: single object ID at that position
 *    - If offset < 0: marks a sequence, next entry has the sequence header offset
 * 3. For each object ID found:
 *    - Look up in remap table
 *    - If found and different, replace with new ID
 * 4. Recursively process all sub-chunks
 *
 * @param chunk Chunk to remap (required)
 * @param remap ID remapping table (required)
 * @return NMO_OK on success, error code on failure
 *
 * @note Only works for chunks with version >= CHUNK_VERSION1 (4)
 * @note Legacy format (version < 4) with magic markers is not supported
 */
NMO_API nmo_result_t nmo_chunk_remap_object_ids(nmo_chunk_t *chunk,
                                                const nmo_id_remap_t *remap);

// =============================================================================
// BITMAP OPERATIONS
// =============================================================================

/**
 * @brief Write planar raw bitmap payload (CKStateChunk::WriteRawBitmap)
 *
 * Writes bits-per-pixel, dimensions, channel masks, compression flag (0)
 * followed by R/G/B/(A) planes with bottom-up scanlines.
 */
NMO_API nmo_result_t nmo_chunk_write_raw_bitmap(nmo_chunk_t *chunk,
                                                const nmo_image_desc_t *desc);

/**
 * @brief Read planar raw bitmap payload (CKStateChunk::ReadRawBitmap)
 *
 * Reconstructs a 32-bit ARGB image descriptor allocated from the chunk arena.
 */
NMO_API nmo_result_t nmo_chunk_read_raw_bitmap(nmo_chunk_t *chunk,
                                               nmo_image_desc_t *out_desc,
                                               uint8_t **out_pixels);

/**
 * @brief Write encoded bitmap payload (CKStateChunk::WriteReaderBitmap)
 *
 * Uses registered codecs (PNG/JPG/etc.) to store compressed bitmap data.
 */
NMO_API nmo_result_t nmo_chunk_write_encoded_bitmap(nmo_chunk_t *chunk,
                                                    const nmo_image_desc_t *desc,
                                                    const nmo_bitmap_properties_t *props);

/**
 * @brief Read encoded bitmap payload (CKStateChunk::ReadBitmap2)
 */
NMO_API nmo_result_t nmo_chunk_read_encoded_bitmap(nmo_chunk_t *chunk,
                                                   nmo_image_desc_t *out_desc,
                                                   uint8_t **out_pixels);

/**
 * @brief Write legacy bitmap payload (CKStateChunk::WriteBitmap)
 *
 * Stores "CKxxx" signature followed by codec output for backward compatibility.
 */
NMO_API nmo_result_t nmo_chunk_write_bitmap_legacy(nmo_chunk_t *chunk,
                                                   const nmo_image_desc_t *desc,
                                                   const nmo_bitmap_properties_t *props);

/**
 * @brief Read legacy bitmap payload (CKStateChunk::ReadBitmap)
 */
NMO_API nmo_result_t nmo_chunk_read_bitmap_legacy(nmo_chunk_t *chunk,
                                                  nmo_image_desc_t *out_desc,
                                                  uint8_t **out_pixels);

// =============================================================================
// ADVANCED OPERATIONS
// =============================================================================

/**
 * @brief Write generic array
 *
 * Writes count, element size, then array data.
 * Lower-level API than typed array functions.
 *
 * @param chunk Chunk (required)
 * @param array Array data (required)
 * @param count Number of elements
 * @param elem_size Size of each element in bytes
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_array(nmo_chunk_t *chunk,
                                           const void *array,
                                           size_t count,
                                           size_t elem_size);

/**
 * @brief Read generic array
 *
 * Reads count, element size, then array data.
 * Lower-level API than typed array functions.
 *
 * @param chunk Chunk (required)
 * @param out_array Output array pointer (allocated from arena) (required)
 * @param out_count Output element count (required)
 * @param out_elem_size Output element size (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_array(nmo_chunk_t *chunk,
                                          void **out_array,
                                          size_t *out_count,
                                          size_t *out_elem_size);

/**
 * @brief Compute CRC/Adler32 checksum
 *
 * Computes CRC over chunk data using Adler32 algorithm.
 *
 * @param chunk Chunk (required)
 * @param initial_crc Initial CRC value (usually 1)
 * @param out_crc Output CRC value (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_compute_crc(nmo_chunk_t *chunk,
                                           uint32_t initial_crc,
                                           uint32_t *out_crc);

// =============================================================================
// CONVENIENCE MACROS
// =============================================================================

/**
 * @brief Macro to read typed arrays with simplified syntax
 *
 * Usage:
 * @code
 * int32_t *values = NULL;
 * size_t count = 0;
 * result = NMO_CHUNK_READ_ARRAY(chunk, int, &values, &count, arena);
 * @endcode
 *
 * Supported types: object_id, int, float, dword, byte, string
 */
#define NMO_CHUNK_READ_ARRAY(chunk, type, out_ptr, out_count, arena) \
    nmo_chunk_read_##type##_array((chunk), (out_ptr), (out_count), (arena))

/**
 * @brief Macro to write typed arrays with simplified syntax
 *
 * Usage:
 * @code
 * result = NMO_CHUNK_WRITE_ARRAY(chunk, int, values, count);
 * @endcode
 *
 * Supported types: object_id, int, float, dword, byte, string
 */
#define NMO_CHUNK_WRITE_ARRAY(chunk, type, array, count) \
    nmo_chunk_write_##type##_array((chunk), (array), (count))

/**
 * @brief Macro to read math types with simplified syntax
 *
 * Usage:
 * @code
 * nmo_vector_t pos;
 * result = NMO_CHUNK_READ_MATH(chunk, vector3, &pos);
 * @endcode
 *
 * Supported types: vector2, vector3, vector4, quaternion, matrix, color
 */
#define NMO_CHUNK_READ_MATH(chunk, type, out_ptr) \
    nmo_chunk_read_##type((chunk), (out_ptr))

/**
 * @brief Macro to write math types with simplified syntax
 *
 * Usage:
 * @code
 * result = NMO_CHUNK_WRITE_MATH(chunk, vector3, &pos);
 * @endcode
 *
 * Supported types: vector2, vector3, vector4, quaternion, matrix, color
 */
#define NMO_CHUNK_WRITE_MATH(chunk, type, ptr) \
    nmo_chunk_write_##type((chunk), (ptr))

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_API_H */
