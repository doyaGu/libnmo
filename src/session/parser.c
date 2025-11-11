/**
 * @file parser.c
 * @brief Parser pipeline for loading NMO files implementation
 */

#include "session/nmo_parser.h"

/**
 * Create parser
 */
nmo_parser_t* nmo_parser_create(void* io_context) {
    (void)io_context;
    return NULL;
}

/**
 * Destroy parser
 */
void nmo_parser_destroy(nmo_parser_t* parser) {
    (void)parser;
}

/**
 * Parse next stage
 */
nmo_parse_stage_t nmo_parser_parse_next_stage(nmo_parser_t* parser) {
    (void)parser;
    return NMO_PARSE_STAGE_COMPLETED;
}

/**
 * Get current parse stage
 */
nmo_parse_stage_t nmo_parser_get_current_stage(const nmo_parser_t* parser) {
    (void)parser;
    return NMO_PARSE_STAGE_INIT;
}

/**
 * Parse file header
 */
nmo_result_t nmo_parser_parse_file_header(nmo_parser_t* parser) {
    (void)parser;
    return nmo_result_ok();
}

/**
 * Parse Header1
 */
nmo_result_t nmo_parser_parse_header1(nmo_parser_t* parser) {
    (void)parser;
    return nmo_result_ok();
}

/**
 * Parse all objects
 */
nmo_result_t nmo_parser_parse_objects(nmo_parser_t* parser) {
    (void)parser;
    return nmo_result_ok();
}

/**
 * Get parsed object repository
 */
void* nmo_parser_get_repository(const nmo_parser_t* parser) {
    (void)parser;
    return NULL;
}

/**
 * Get parse error message
 */
const char* nmo_parser_get_error(const nmo_parser_t* parser) {
    (void)parser;
    return NULL;
}

/**
 * Check if parsing is complete
 */
int nmo_parser_is_complete(const nmo_parser_t* parser) {
    (void)parser;
    return 0;
}
