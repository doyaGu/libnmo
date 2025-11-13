#ifndef NMO_CHUNK_HELPERS_H
#define NMO_CHUNK_HELPERS_H

#include "nmo_types.h"
#include "nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_math.h"

/**
 * @file nmo_chunk_helpers.h
 * @brief High-level helper functions for common chunk operations
 * 
 * Provides convenience functions for reading/writing common data structures:
 * - Object ID arrays (XObjectPointerArray equivalent)
 * - Primitive type arrays (int, float, dword, byte, string)
 * - Math types (vector, matrix, quaternion, color)
 * - Convenience macros for common patterns
 * 
 * This module reduces code duplication by encapsulating common patterns
 * similar to Virtools helper classes like XObjectPointerArray, XArray, etc.
 */

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Chunk API Helpers (nmo_chunk_t based)
// =============================================================================

/**
 * @brief Read object ID array using chunk API
 * 
 * Equivalent to XObjectPointerArray::Load() in Virtools.
 * Reads using StartReadSequence() + ReadObjectID() pattern.
 * 
 * Pattern:
 * 1. nmo_chunk_start_read_sequence() -> returns count
 * 2. For each: nmo_chunk_read_object_id()
 * 
 * @param chunk Chunk to read from (required)
 * @param out_ids Pointer to receive allocated array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 * 
 * @note The array is allocated from the arena. If count is 0, *out_ids will be NULL.
 * 
 * Example usage:
 * @code
 * nmo_object_id_t *script_ids = NULL;
 * size_t script_count = 0;
 * result = nmo_chunk_read_object_id_array(chunk, &script_ids, &script_count, arena);
 * if (result.code == NMO_OK) {
 *     // Use script_ids array
 * }
 * @endcode
 */
NMO_API nmo_result_t nmo_chunk_read_object_id_array(nmo_chunk_t *chunk,
                                                     nmo_object_id_t **out_ids,
                                                     size_t *out_count,
                                                     nmo_arena_t *arena);

/**
 * @brief Write object ID array using chunk API
 * 
 * Equivalent to XObjectPointerArray::Save() in Virtools.
 * Writes using WriteDword(count) + WriteObjectID() pattern.
 * 
 * Pattern:
 * 1. nmo_chunk_write_dword(count)
 * 2. For each: nmo_chunk_write_object_id()
 * 
 * @param chunk Chunk to write to (required)
 * @param ids Array of object IDs (can be NULL if count is 0)
 * @param count Number of IDs
 * @return NMO_OK on success, error code on failure
 * 
 * Example usage:
 * @code
 * result = nmo_chunk_write_object_id_array(chunk, script_ids, script_count);
 * @endcode
 */
NMO_API nmo_result_t nmo_chunk_write_object_id_array(nmo_chunk_t *chunk,
                                                      const nmo_object_id_t *ids,
                                                      size_t count);

/**
 * @brief Read int32 array using chunk API
 * 
 * Similar to XArray<int>::Load() pattern.
 * 
 * @param chunk Chunk to read from (required)
 * @param out_array Pointer to receive allocated array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_int_array(nmo_chunk_t *chunk,
                                               int32_t **out_array,
                                               size_t *out_count,
                                               nmo_arena_t *arena);

/**
 * @brief Write int32 array using chunk API
 * 
 * Similar to XArray<int>::Save() pattern.
 * 
 * @param chunk Chunk to write to (required)
 * @param array Array of integers (can be NULL if count is 0)
 * @param count Number of integers
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_int_array(nmo_chunk_t *chunk,
                                                const int32_t *array,
                                                size_t count);

/**
 * @brief Read float array using chunk API
 * 
 * Similar to XArray<float>::Load() pattern.
 * 
 * @param chunk Chunk to read from (required)
 * @param out_array Pointer to receive allocated array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_float_array(nmo_chunk_t *chunk,
                                                 float **out_array,
                                                 size_t *out_count,
                                                 nmo_arena_t *arena);

/**
 * @brief Write float array using chunk API
 * 
 * Similar to XArray<float>::Save() pattern.
 * 
 * @param chunk Chunk to write to (required)
 * @param array Array of floats (can be NULL if count is 0)
 * @param count Number of floats
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_float_array(nmo_chunk_t *chunk,
                                                  const float *array,
                                                  size_t count);

// =============================================================================
// Additional Type-Specialized Array Helpers
// =============================================================================

/**
 * @brief Read uint32 (DWORD) array
 * 
 * @param chunk Chunk to read from (required)
 * @param out_array Pointer to receive allocated array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_dword_array(nmo_chunk_t *chunk,
                                                 uint32_t **out_array,
                                                 size_t *out_count,
                                                 nmo_arena_t *arena);

/**
 * @brief Write uint32 (DWORD) array
 * 
 * @param chunk Chunk to write to (required)
 * @param array Array of dwords (can be NULL if count is 0)
 * @param count Number of dwords
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_dword_array(nmo_chunk_t *chunk,
                                                  const uint32_t *array,
                                                  size_t count);

/**
 * @brief Read byte array
 * 
 * @param chunk Chunk to read from (required)
 * @param out_array Pointer to receive allocated array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_byte_array(nmo_chunk_t *chunk,
                                                uint8_t **out_array,
                                                size_t *out_count,
                                                nmo_arena_t *arena);

/**
 * @brief Write byte array
 * 
 * @param chunk Chunk to write to (required)
 * @param array Array of bytes (can be NULL if count is 0)
 * @param count Number of bytes
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_byte_array(nmo_chunk_t *chunk,
                                                 const uint8_t *array,
                                                 size_t count);

/**
 * @brief Read string array
 * 
 * @param chunk Chunk to read from (required)
 * @param out_strings Pointer to receive allocated string array (required)
 * @param out_count Pointer to receive count (required)
 * @param arena Arena for allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_string_array(nmo_chunk_t *chunk,
                                                  char ***out_strings,
                                                  size_t *out_count,
                                                  nmo_arena_t *arena);

/**
 * @brief Write string array
 * 
 * @param chunk Chunk to write to (required)
 * @param strings Array of string pointers (can be NULL if count is 0)
 * @param count Number of strings
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_string_array(nmo_chunk_t *chunk,
                                                   const char * const *strings,
                                                   size_t count);

// =============================================================================
// Math Type Helpers
// =============================================================================

/**
 * @brief Read 2D vector
 * 
 * @param chunk Chunk to read from (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector2(nmo_chunk_t *chunk, nmo_vector2_t *out_vec);

/**
 * @brief Write 2D vector
 * 
 * @param chunk Chunk to write to (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector2(nmo_chunk_t *chunk, const nmo_vector2_t *vec);

/**
 * @brief Read 3D vector
 * 
 * @param chunk Chunk to read from (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector3(nmo_chunk_t *chunk, nmo_vector_t *out_vec);

/**
 * @brief Write 3D vector
 * 
 * @param chunk Chunk to write to (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector3(nmo_chunk_t *chunk, const nmo_vector_t *vec);

/**
 * @brief Read 4D vector
 * 
 * @param chunk Chunk to read from (required)
 * @param out_vec Output vector (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_vector4(nmo_chunk_t *chunk, nmo_vector4_t *out_vec);

/**
 * @brief Write 4D vector
 * 
 * @param chunk Chunk to write to (required)
 * @param vec Vector to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_vector4(nmo_chunk_t *chunk, const nmo_vector4_t *vec);

/**
 * @brief Read quaternion
 * 
 * @param chunk Chunk to read from (required)
 * @param out_quat Output quaternion (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_quaternion(nmo_chunk_t *chunk, nmo_quaternion_t *out_quat);

/**
 * @brief Write quaternion
 * 
 * @param chunk Chunk to write to (required)
 * @param quat Quaternion to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_quaternion(nmo_chunk_t *chunk, const nmo_quaternion_t *quat);

/**
 * @brief Read 4x4 matrix
 * 
 * @param chunk Chunk to read from (required)
 * @param out_mat Output matrix (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_matrix(nmo_chunk_t *chunk, nmo_matrix_t *out_mat);

/**
 * @brief Write 4x4 matrix
 * 
 * @param chunk Chunk to write to (required)
 * @param mat Matrix to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_matrix(nmo_chunk_t *chunk, const nmo_matrix_t *mat);

/**
 * @brief Read RGBA color
 * 
 * @param chunk Chunk to read from (required)
 * @param out_color Output color (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_read_color(nmo_chunk_t *chunk, nmo_color_t *out_color);

/**
 * @brief Write RGBA color
 * 
 * @param chunk Chunk to write to (required)
 * @param color Color to write (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_chunk_write_color(nmo_chunk_t *chunk, const nmo_color_t *color);

// =============================================================================
// Convenience Macros
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

/**
 * @brief Convenience macro for reading multiple values in sequence
 * 
 * Checks result after each read and returns on error.
 * 
 * Usage:
 * @code
 * int32_t a, b, c;
 * NMO_CHUNK_READ_SEQUENCE_BEGIN(chunk);
 * NMO_CHUNK_READ_INT(&a);
 * NMO_CHUNK_READ_INT(&b);
 * NMO_CHUNK_READ_INT(&c);
 * NMO_CHUNK_READ_SEQUENCE_END();
 * @endcode
 */
#define NMO_CHUNK_READ_SEQUENCE_BEGIN(chunk_var) \
    do { \
        nmo_chunk_t *_chunk = (chunk_var); \
        nmo_result_t _result;

#define NMO_CHUNK_READ_INT(out_ptr) \
        _result = nmo_chunk_read_int(_chunk, (out_ptr)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_READ_FLOAT(out_ptr) \
        _result = nmo_chunk_read_float(_chunk, (out_ptr)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_READ_DWORD(out_ptr) \
        _result = nmo_chunk_read_dword(_chunk, (out_ptr)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_READ_STRING(out_ptr) \
        if (nmo_chunk_read_string(_chunk, (out_ptr)) == 0) \
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT, \
                                              NMO_SEVERITY_ERROR, "Failed to read string"));

#define NMO_CHUNK_READ_SEQUENCE_END() \
    } while(0)

/**
 * @brief Convenience macro for writing multiple values in sequence
 * 
 * Checks result after each write and returns on error.
 * 
 * Usage:
 * @code
 * NMO_CHUNK_WRITE_SEQUENCE_BEGIN(chunk);
 * NMO_CHUNK_WRITE_INT(42);
 * NMO_CHUNK_WRITE_FLOAT(3.14f);
 * NMO_CHUNK_WRITE_STRING("hello");
 * NMO_CHUNK_WRITE_SEQUENCE_END();
 * @endcode
 */
#define NMO_CHUNK_WRITE_SEQUENCE_BEGIN(chunk_var) \
    do { \
        nmo_chunk_t *_chunk = (chunk_var); \
        nmo_result_t _result;

#define NMO_CHUNK_WRITE_INT(value) \
        _result = nmo_chunk_write_int(_chunk, (value)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_WRITE_FLOAT(value) \
        _result = nmo_chunk_write_float(_chunk, (value)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_WRITE_DWORD(value) \
        _result = nmo_chunk_write_dword(_chunk, (value)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_WRITE_STRING(value) \
        _result = nmo_chunk_write_string(_chunk, (value)); \
        if (_result.code != NMO_OK) return _result;

#define NMO_CHUNK_WRITE_SEQUENCE_END() \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_HELPERS_H
