/**
 * @file nmo_parser.h
 * @brief Parser pipeline for loading NMO files
 */

#ifndef NMO_PARSER_H
#define NMO_PARSER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parser context
 */
typedef struct nmo_parser nmo_parser_t;

/**
 * @brief Parse stage
 */
typedef enum {
    NMO_PARSE_STAGE_INIT = 0,
    NMO_PARSE_STAGE_HEADER,
    NMO_PARSE_STAGE_HEADER1,
    NMO_PARSE_STAGE_OBJECTS,
    NMO_PARSE_STAGE_COMPLETED,
} nmo_parse_stage_t;

/**
 * Create parser
 * @param io_context IO context to parse from
 * @return Parser or NULL on error
 */
NMO_API nmo_parser_t* nmo_parser_create(void* io_context);

/**
 * Destroy parser
 * @param parser Parser to destroy
 */
NMO_API void nmo_parser_destroy(nmo_parser_t* parser);

/**
 * Parse next stage
 * @param parser Parser
 * @return Current parse stage or NMO_PARSE_STAGE_COMPLETED
 */
NMO_API nmo_parse_stage_t nmo_parser_parse_next_stage(nmo_parser_t* parser);

/**
 * Get current parse stage
 * @param parser Parser
 * @return Current parse stage
 */
NMO_API nmo_parse_stage_t nmo_parser_get_current_stage(const nmo_parser_t* parser);

/**
 * Parse file header
 * @param parser Parser
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_parser_parse_file_header(nmo_parser_t* parser);

/**
 * Parse Header1
 * @param parser Parser
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_parser_parse_header1(nmo_parser_t* parser);

/**
 * Parse all objects
 * @param parser Parser
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_parser_parse_objects(nmo_parser_t* parser);

/**
 * Get parsed object repository
 * @param parser Parser
 * @return Object repository
 */
NMO_API void* nmo_parser_get_repository(const nmo_parser_t* parser);

/**
 * Get parse error message
 * @param parser Parser
 * @return Error message or NULL if no error
 */
NMO_API const char* nmo_parser_get_error(const nmo_parser_t* parser);

/**
 * Check if parsing is complete
 * @param parser Parser
 * @return 1 if complete, 0 otherwise
 */
NMO_API int nmo_parser_is_complete(const nmo_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARSER_H */
