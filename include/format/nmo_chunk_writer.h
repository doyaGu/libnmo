#ifndef NMO_CHUNK_WRITER_H
#define NMO_CHUNK_WRITER_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "format/nmo_chunk_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"

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
 * - Sub-chunk support with version context stack (Phase 1.3)
 * - Automatic buffer growth
 */

/** Maximum nesting depth for sub-chunks */
#define NMO_CHUNK_WRITER_MAX_DEPTH 16

/**
 * @brief Chunk version context for nested sub-chunk tracking (Phase 1.3).
 *
 * When writing nested sub-chunks, the writer maintains a stack of these
 * contexts so that child chunks can access their parent's version number.
 */
typedef struct nmo_chunk_version_context {
    uint32_t chunk_version;      /**< Version of this chunk */
    size_t header_offset;        /**< Position of chunk header in buffer (DWORDs) */
    int expected_ids;            /**< Expected number of object IDs (-1 = not tracked) */
    int written_ids;             /**< Actual number of IDs written */
} nmo_chunk_version_context_t;

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
 * @brief Push a new version context onto the stack (Phase 1.3).
 *
 * Call this when starting a nested sub-chunk to track its version.
 * The version context enables child chunks to access parent version
 * via nmo_chunk_writer_parent_version().
 *
 * @param w Writer
 * @param version Chunk version for the new context
 * @return NMO_OK on success, NMO_ERR_BUFFER_OVERRUN if stack is full
 */
NMO_API int nmo_chunk_writer_push_context(nmo_chunk_writer_t* w, uint32_t version);

/**
 * @brief Pop the current version context from the stack (Phase 1.3).
 *
 * Call this when finishing a nested sub-chunk. Will validate that
 * expected_ids matches written_ids if ID tracking was enabled.
 *
 * @param w Writer
 * @return NMO_OK on success, NMO_ERR_INVALID_STATE if stack is empty
 */
NMO_API int nmo_chunk_writer_pop_context(nmo_chunk_writer_t* w);

/**
 * @brief Get the parent chunk's version number (Phase 1.3).
 *
 * When writing nested sub-chunks, returns the version of the enclosing
 * parent chunk. Returns 0 if there is no parent (i.e., at root level).
 *
 * @param w Writer
 * @return Parent chunk version, or 0 if at root level
 */
NMO_API uint32_t nmo_chunk_writer_parent_version(const nmo_chunk_writer_t* w);

/**
 * @brief Get the current nesting depth (Phase 1.3).
 *
 * @param w Writer
 * @return Current stack depth (0 = no context pushed)
 */
NMO_API int nmo_chunk_writer_depth(const nmo_chunk_writer_t* w);

/**
 * @brief Set expected ID count for the current context (Phase 1.3).
 *
 * Used for IntList auditing - validates that the expected number of
 * object IDs are written before popping the context.
 *
 * @param w Writer
 * @param expected_count Expected number of IDs to write (-1 to disable)
 */
NMO_API void nmo_chunk_writer_set_expected_ids(nmo_chunk_writer_t* w, int expected_count);

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
 * @brief Write a 32-bit value as two 16-bit words
 *
 * Writes a DWORD as two words stored in separate DWORD slots.
 * Matches CKStateChunk::WriteDwordAsWords behavior.
 *
 * @param w Writer context
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_dword_as_words(nmo_chunk_writer_t* w, uint32_t value);

/**
 * @brief Write an array of 32-bit values as 16-bit word pairs
 *
 * @param w Writer context
 * @param values Array of DWORD values
 * @param count Number of values
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_array_dword_as_words(nmo_chunk_writer_t* w,
                                                       const uint32_t* values,
                                                       size_t count);

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

/* ============================================================================
 * Reserve-and-Patch API (Phase 2.2)
 * ============================================================================ */

/**
 * @brief Patch token for deferred writes
 *
 * Represents a position in the chunk buffer that can be patched later.
 * Used for forward references where the value isn't known at write time.
 */
typedef struct nmo_patch_token {
    size_t offset;   /**< Offset in DWORDs from start of chunk data */
    uint8_t size;    /**< Size of reserved space: 1, 2, or 4 DWORDs */
    uint8_t valid;   /**< Non-zero if token is valid */
} nmo_patch_token_t;

/**
 * @brief Invalid patch token constant
 */
#define NMO_PATCH_TOKEN_INVALID ((nmo_patch_token_t){0, 0, 0})

/**
 * @brief Check if patch token is valid
 *
 * @param token Token to check
 * @return Non-zero if valid
 */
static inline int nmo_patch_token_valid(nmo_patch_token_t token) {
    return token.valid != 0;
}

/**
 * @brief Reserve space for a uint32_t and return patch token
 *
 * Reserves one DWORD (4 bytes) at the current write position.
 * Use nmo_chunk_writer_patch_u32() to fill the value later.
 *
 * @param w Writer
 * @return Patch token for later patching, or invalid token on error
 */
NMO_API nmo_patch_token_t nmo_chunk_writer_reserve_u32(nmo_chunk_writer_t* w);

/**
 * @brief Reserve space for a uint64_t and return patch token
 *
 * Reserves two DWORDs (8 bytes) at the current write position.
 * Use nmo_chunk_writer_patch_u64() to fill the value later.
 *
 * @param w Writer
 * @return Patch token for later patching, or invalid token on error
 */
NMO_API nmo_patch_token_t nmo_chunk_writer_reserve_u64(nmo_chunk_writer_t* w);

/**
 * @brief Reserve space for multiple DWORDs and return patch token
 *
 * @param w Writer
 * @param dword_count Number of DWORDs to reserve (1-4)
 * @return Patch token for later patching, or invalid token on error
 */
NMO_API nmo_patch_token_t nmo_chunk_writer_reserve_dwords(nmo_chunk_writer_t* w, size_t dword_count);

/**
 * @brief Patch a reserved uint32_t position
 *
 * Writes a uint32_t value at the position specified by the patch token.
 * The token must have been created by nmo_chunk_writer_reserve_u32().
 *
 * @param w Writer
 * @param token Patch token from reserve_u32
 * @param value Value to write
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if token is invalid
 */
NMO_API int nmo_chunk_writer_patch_u32(nmo_chunk_writer_t* w, nmo_patch_token_t token, uint32_t value);

/**
 * @brief Patch a reserved uint64_t position
 *
 * Writes a uint64_t value at the position specified by the patch token.
 * The token must have been created by nmo_chunk_writer_reserve_u64().
 *
 * @param w Writer
 * @param token Patch token from reserve_u64
 * @param value Value to write
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if token is invalid
 */
NMO_API int nmo_chunk_writer_patch_u64(nmo_chunk_writer_t* w, nmo_patch_token_t token, uint64_t value);

/**
 * @brief Get current write position in DWORDs
 *
 * Returns the current position in the chunk buffer, useful for calculating
 * sizes after writing a section.
 *
 * @param w Writer
 * @return Current position in DWORDs
 */
NMO_API size_t nmo_chunk_writer_tell(const nmo_chunk_writer_t* w);

/* ============================================================================
 * IntList Auditor API (Phase 2.3) - DEBUG Mode Only
 * ============================================================================ */

/**
 * @defgroup IntListAuditor IntList Auditor (Phase 2.3)
 * @brief Debug-mode validation for object ID list writes
 *
 * The IntList Auditor validates that the declared count of object IDs
 * matches the actual number written. This catches common bugs where the
 * count field is incorrect, which would cause Reference SDK to have
 * undefined behavior (out-of-bounds reads or data truncation).
 *
 * These APIs are only active in DEBUG builds (when NDEBUG is not defined).
 * In release builds, they compile to no-ops with zero overhead.
 *
 * Usage:
 * @code
 * // Write count first
 * nmo_chunk_writer_write_dword(w, 3);
 *
 * // Begin auditing with the expected count
 * nmo_chunk_writer_begin_intlist(w, 3, "CKObject.children");
 *
 * // Write each ID - these calls are tracked
 * nmo_chunk_writer_write_object_id_audited(w, id1);
 * nmo_chunk_writer_write_object_id_audited(w, id2);
 * nmo_chunk_writer_write_object_id_audited(w, id3);
 *
 * // End auditing - assertion if count mismatch
 * nmo_chunk_writer_end_intlist(w);
 * @endcode
 * @{
 */

#ifndef NDEBUG
/**
 * @brief Maximum context string length for IntList auditor
 */
#define NMO_INTLIST_CONTEXT_MAX 64

/**
 * @brief IntList audit state (DEBUG mode only)
 */
typedef struct nmo_intlist_audit {
    int expected_count;                        /**< Declared ID count */
    int written_count;                         /**< Actual IDs written */
    size_t start_offset;                       /**< IntList start position (DWORDs) */
    char context[NMO_INTLIST_CONTEXT_MAX];     /**< Debug context string */
    int active;                                /**< Non-zero if audit in progress */
} nmo_intlist_audit_t;
#endif /* NDEBUG */

/**
 * @brief Begin IntList audit (DEBUG mode only)
 *
 * Starts tracking object ID writes to validate the count matches.
 * Call this after writing the count value but before writing IDs.
 *
 * In release builds, this is a no-op.
 *
 * @param w Writer
 * @param expected_count Number of IDs that should be written
 * @param context Debug context string (e.g., "CKObject.children")
 */
NMO_API void nmo_chunk_writer_begin_intlist(nmo_chunk_writer_t* w,
                                            int expected_count,
                                            const char* context);

/**
 * @brief Write object ID with audit tracking (DEBUG mode only)
 *
 * Writes an object ID and increments the audit counter.
 * If no audit is active, this behaves identically to write_object_id.
 *
 * @param w Writer
 * @param id Object ID to write
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_writer_write_object_id_audited(nmo_chunk_writer_t* w, nmo_object_id_t id);

/**
 * @brief End IntList audit and validate count (DEBUG mode only)
 *
 * Ends the current IntList audit and validates that expected_count
 * equals written_count. In DEBUG builds, triggers an assertion failure
 * if the counts don't match. In release builds, this is a no-op.
 *
 * @param w Writer
 * @return NMO_OK if counts match (or in release builds),
 *         NMO_ERR_INVALID_STATE if no audit was active,
 *         NMO_ERR_DATA_CORRUPT if counts don't match (DEBUG only returns this instead of asserting if NMO_INTLIST_AUDIT_SOFT is defined)
 */
NMO_API int nmo_chunk_writer_end_intlist(nmo_chunk_writer_t* w);

/**
 * @brief Check if IntList audit is currently active
 *
 * @param w Writer
 * @return Non-zero if audit is in progress (DEBUG mode), always 0 in release
 */
NMO_API int nmo_chunk_writer_intlist_audit_active(const nmo_chunk_writer_t* w);

/**
 * @brief Get current IntList audit context string
 *
 * @param w Writer
 * @return Context string if audit active (DEBUG mode), NULL otherwise
 */
NMO_API const char* nmo_chunk_writer_intlist_audit_context(const nmo_chunk_writer_t* w);

/** @} */ /* end of IntListAuditor group */

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
