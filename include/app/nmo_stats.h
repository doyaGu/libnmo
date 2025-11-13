#ifndef NMO_STATS_H
#define NMO_STATS_H

#include "nmo_types.h"
#include "app/nmo_session.h"
#include <stdio.h>

/**
 * @file nmo_stats.h
 * @brief File statistics and diagnostics
 *
 * Provides comprehensive statistics about loaded Virtools files:
 * - Object counts and distribution
 * - Memory usage breakdown
 * - Performance metrics
 * - Reference statistics
 *
 * Reference: CKFile::WriteStats
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File statistics structure
 */
typedef struct nmo_file_stats {
    /* Object statistics */
    struct {
        size_t total_count;         /**< Total number of objects */
        size_t by_class[256];       /**< Objects per class ID */
        size_t max_class_id;        /**< Highest class ID seen */
        size_t unique_classes;      /**< Number of different classes */
    } objects;
    
    /* Memory statistics */
    struct {
        size_t total_size;          /**< Total file size in bytes */
        size_t header_size;         /**< Header sections size */
        size_t data_size;           /**< Data section size */
        size_t chunk_data_size;     /**< Total chunk data */
        size_t chunk_overhead;      /**< Chunk metadata overhead */
        size_t compression_ratio;   /**< Compression ratio (0-100%) */
    } memory;
    
    /* Performance statistics */
    struct {
        double load_time_ms;        /**< Total load time */
        double parse_time_ms;       /**< Parse time */
        double remap_time_ms;       /**< ID remap time */
    } performance;
    
    /* Reference statistics */
    struct {
        size_t total_references;    /**< Total references found */
        size_t resolved;            /**< Successfully resolved */
        size_t unresolved;          /**< Failed to resolve */
    } references;
    
    /* Chunk statistics */
    struct {
        size_t total_chunks;        /**< Total chunks */
        size_t compressed_chunks;   /**< Compressed chunks */
        size_t max_chunk_size;      /**< Largest chunk (bytes) */
        size_t avg_chunk_size;      /**< Average chunk size */
    } chunks;
} nmo_file_stats_t;

/**
 * @brief Collect statistics from session
 *
 * Gathers comprehensive statistics about the loaded file.
 * Session must have successfully loaded a file.
 *
 * @param session Session to analyze
 * @param out_stats Output statistics structure
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_stats_collect(
    nmo_session_t *session,
    nmo_file_stats_t *out_stats
);

/**
 * @brief Print statistics to file
 *
 * Prints a human-readable statistics report.
 *
 * @param stats Statistics to print
 * @param output Output file (use stdout for console)
 */
NMO_API void nmo_stats_print(
    const nmo_file_stats_t *stats,
    FILE *output
);

/**
 * @brief Export statistics as JSON
 *
 * Exports statistics in JSON format for programmatic access.
 *
 * @param stats Statistics to export
 * @param output_path Output file path
 * @return NMO_OK on success
 */
NMO_API int nmo_stats_export_json(
    const nmo_file_stats_t *stats,
    const char *output_path
);

/**
 * @brief Print summary (one-line)
 *
 * Prints a brief one-line summary.
 *
 * @param stats Statistics to summarize
 * @param output Output file
 */
NMO_API void nmo_stats_print_summary(
    const nmo_file_stats_t *stats,
    FILE *output
);

#ifdef __cplusplus
}
#endif

#endif // NMO_STATS_H
