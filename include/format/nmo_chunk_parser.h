/**
 * @file nmo_chunk_parser.h
 * @brief Chunk parser for reading chunk data
 */

#ifndef NMO_CHUNK_PARSER_H
#define NMO_CHUNK_PARSER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chunk parser context
 */
typedef struct nmo_chunk_parser nmo_chunk_parser_t;

/**
 * Create chunk parser
 * @param data Data buffer containing chunks
 * @param size Data buffer size
 * @return Parser or NULL on error
 */
NMO_API nmo_chunk_parser_t* nmo_chunk_parser_create(const void* data, size_t size);

/**
 * Destroy chunk parser
 * @param parser Parser to destroy
 */
NMO_API void nmo_chunk_parser_destroy(nmo_chunk_parser_t* parser);

/**
 * Reset parser to beginning
 * @param parser Parser
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_parser_reset(nmo_chunk_parser_t* parser);

/**
 * Parse next chunk
 * @param parser Parser
 * @param out_chunk Output chunk (caller must free with nmo_chunk_destroy)
 * @return NMO_OK on success, NMO_ERROR_EOF when no more chunks
 */
NMO_API nmo_result_t nmo_chunk_parser_next(nmo_chunk_parser_t* parser, void** out_chunk);

/**
 * Get current parse position
 * @param parser Parser
 * @return Current position in bytes
 */
NMO_API size_t nmo_chunk_parser_get_position(const nmo_chunk_parser_t* parser);

/**
 * Get total data size
 * @param parser Parser
 * @return Total size in bytes
 */
NMO_API size_t nmo_chunk_parser_get_size(const nmo_chunk_parser_t* parser);

/**
 * Check if parser has more chunks
 * @param parser Parser
 * @return 1 if more chunks available, 0 otherwise
 */
NMO_API int nmo_chunk_parser_has_next(const nmo_chunk_parser_t* parser);

/**
 * Seek to position in parser
 * @param parser Parser
 * @param position Position to seek to
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_parser_seek(nmo_chunk_parser_t* parser, size_t position);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_PARSER_H */
