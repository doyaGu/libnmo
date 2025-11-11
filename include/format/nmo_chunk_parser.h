#ifndef NMO_CHUNK_PARSER_H
#define NMO_CHUNK_PARSER_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

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
typedef struct nmo_chunk_parser nmo_chunk_parser;

/**
 * @brief Create parser from chunk
 *
 * @param chunk Chunk to parse (must not be NULL)
 * @return Parser or NULL on allocation failure
 */
NMO_API nmo_chunk_parser* nmo_chunk_parser_create(nmo_chunk* chunk);

/**
 * @brief Get current cursor position
 *
 * @param p Parser
 * @return Current position in DWORDs
 */
NMO_API size_t nmo_chunk_parser_tell(nmo_chunk_parser* p);

/**
 * @brief Seek to absolute position
 *
 * @param p Parser
 * @param pos Position in DWORDs
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_seek(nmo_chunk_parser* p, size_t pos);

/**
 * @brief Skip forward by offset
 *
 * @param p Parser
 * @param dwords Number of DWORDs to skip
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_skip(nmo_chunk_parser* p, size_t dwords);

/**
 * @brief Get remaining DWORDs
 *
 * @param p Parser
 * @return Number of DWORDs remaining
 */
NMO_API size_t nmo_chunk_parser_remaining(nmo_chunk_parser* p);

/**
 * @brief Check if at end of chunk
 *
 * @param p Parser
 * @return 1 if at end, 0 otherwise
 */
NMO_API int nmo_chunk_parser_at_end(nmo_chunk_parser* p);

/**
 * @brief Read uint8_t (padded to DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_byte(nmo_chunk_parser* p, uint8_t* out);

/**
 * @brief Read uint16_t (padded to DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_word(nmo_chunk_parser* p, uint16_t* out);

/**
 * @brief Read uint32_t (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_dword(nmo_chunk_parser* p, uint32_t* out);

/**
 * @brief Read int32_t (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_int(nmo_chunk_parser* p, int32_t* out);

/**
 * @brief Read float (exactly one DWORD)
 *
 * @param p Parser
 * @param out Output value
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_float(nmo_chunk_parser* p, float* out);

/**
 * @brief Read GUID (two DWORDs)
 *
 * @param p Parser
 * @param out Output GUID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_guid(nmo_chunk_parser* p, nmo_guid* out);

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
NMO_API int nmo_chunk_parser_read_bytes(nmo_chunk_parser* p, void* dest, size_t bytes);

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
NMO_API int nmo_chunk_parser_read_string(nmo_chunk_parser* p, char** out, nmo_arena* arena);

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
NMO_API int nmo_chunk_parser_read_buffer(nmo_chunk_parser* p, void** out, size_t* size, nmo_arena* arena);

/**
 * @brief Read object ID and track in chunk
 *
 * Automatically adds to chunk's ID list if not present.
 *
 * @param p Parser
 * @param out Output object ID
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_object_id(nmo_chunk_parser* p, nmo_object_id* out);

/**
 * @brief Seek to identifier
 *
 * Scans forward for identifier DWORD and positions cursor after it.
 *
 * @param p Parser
 * @param identifier Identifier to find
 * @return NMO_OK if found, NMO_ERR_EOF if not found
 */
NMO_API int nmo_chunk_parser_seek_identifier(nmo_chunk_parser* p, uint32_t identifier);

/**
 * @brief Read identifier
 *
 * @param p Parser
 * @param identifier Output identifier
 * @return NMO_OK on success
 */
NMO_API int nmo_chunk_parser_read_identifier(nmo_chunk_parser* p, uint32_t* identifier);

/**
 * @brief Destroy parser
 *
 * @param p Parser
 */
NMO_API void nmo_chunk_parser_destroy(nmo_chunk_parser* p);

#ifdef __cplusplus
}
#endif

#endif // NMO_CHUNK_PARSER_H
