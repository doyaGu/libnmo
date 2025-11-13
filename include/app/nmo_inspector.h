/**
 * @file nmo_inspector.h
 * @brief Chunk inspection and debugging utilities
 *
 * Provides tools for detailed chunk analysis, validation, and visualization.
 */

#ifndef NMO_INSPECTOR_H
#define NMO_INSPECTOR_H

#include "nmo_types.h"
#include "format/nmo_chunk.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chunk dump detail levels
 */
typedef enum {
    NMO_DUMP_BRIEF = 0,      /**< Brief summary only */
    NMO_DUMP_NORMAL = 1,     /**< Normal detail with identifiers */
    NMO_DUMP_DETAILED = 2,   /**< Full detail with data preview */
    NMO_DUMP_FULL = 3        /**< Everything including hex dumps */
} nmo_dump_level_t;

/**
 * @brief Chunk validation result
 */
typedef struct {
    bool is_valid;              /**< Overall validity */
    size_t error_count;         /**< Number of errors found */
    size_t warning_count;       /**< Number of warnings found */
    
    /* Specific checks */
    bool header_valid;          /**< Header structure valid */
    bool identifiers_valid;     /**< All identifiers valid */
    bool data_valid;            /**< Data region accessible */
    bool sub_chunks_valid;      /**< Sub-chunks valid */
    
    char error_message[256];    /**< Detailed error message */
} nmo_chunk_validation_t;

/**
 * @brief Chunk inspector options
 */
typedef struct {
    nmo_dump_level_t level;     /**< Dump detail level */
    bool show_hex;              /**< Show hex dumps */
    bool show_sub_chunks;       /**< Recursively show sub-chunks */
    bool validate;              /**< Perform validation */
    bool colorize;              /**< Use ANSI colors (terminal output) */
    size_t max_depth;           /**< Maximum recursion depth (0=unlimited) */
    size_t hex_bytes;           /**< Bytes per hex dump line (default 16) */
} nmo_inspector_options_t;

/**
 * @brief Initialize inspector options with defaults
 * 
 * @param options Options structure to initialize
 */
void nmo_inspector_init_options(nmo_inspector_options_t *options);

/**
 * @brief Dump chunk information to stream
 * 
 * Outputs human-readable chunk structure with configurable detail level.
 * Supports recursive sub-chunk dumping.
 * 
 * @param chunk Chunk to dump
 * @param stream Output stream (stdout, stderr, or file)
 * @param options Dump options
 * @return 0 on success, negative on error
 */
int nmo_inspector_dump_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options
);

/**
 * @brief Validate chunk structure
 * 
 * Performs comprehensive validation including:
 * - Header integrity
 * - Identifier validity
 * - Data region bounds
 * - Sub-chunk consistency
 * 
 * @param chunk Chunk to validate
 * @param result Validation result (output)
 * @return 0 on success (check result->is_valid for validity)
 */
int nmo_inspector_validate_chunk(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result
);

/**
 * @brief Dump chunk data as hexadecimal
 * 
 * Outputs hex dump in standard format:
 * Offset: HEX_BYTES ASCII_REPR
 * 
 * @param chunk Chunk to dump
 * @param stream Output stream
 * @param max_bytes Maximum bytes to dump (0=all)
 * @param bytes_per_line Bytes per line (typically 16)
 * @return Number of bytes dumped, negative on error
 */
int nmo_inspector_hex_dump(
    const nmo_chunk_t *chunk,
    FILE *stream,
    size_t max_bytes,
    size_t bytes_per_line
);

/**
 * @brief Print chunk summary (one line)
 * 
 * Compact format: Class=XXX IDs=[N] Data=XXX bytes Sub=[N]
 * 
 * @param chunk Chunk to summarize
 * @param stream Output stream
 * @return 0 on success, negative on error
 */
int nmo_inspector_print_summary(
    const nmo_chunk_t *chunk,
    FILE *stream
);

/**
 * @brief Compare two chunks and report differences
 * 
 * Useful for debugging chunk modifications or file format changes.
 * 
 * @param chunk1 First chunk
 * @param chunk2 Second chunk
 * @param stream Output stream
 * @return 0 if identical, 1 if different, negative on error
 */
int nmo_inspector_compare_chunks(
    const nmo_chunk_t *chunk1,
    const nmo_chunk_t *chunk2,
    FILE *stream
);

/**
 * @brief Export chunk structure to JSON
 * 
 * Creates machine-readable representation of chunk structure.
 * 
 * @param chunk Chunk to export
 * @param stream Output stream
 * @param include_data Include base64-encoded data
 * @return 0 on success, negative on error
 */
int nmo_inspector_export_json(
    const nmo_chunk_t *chunk,
    FILE *stream,
    bool include_data
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECTOR_H */
