#ifndef NMO_DOCUMENT_STATS_H
#define NMO_DOCUMENT_STATS_H

#include "document/nmo_document.h"

#include <stdio.h>

#define NMO_STATS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_STATS_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

typedef struct nmo_file_stats {
    struct {
        size_t total_count;
        size_t by_class[256];
        size_t max_class_id;
        size_t unique_classes;
    } objects;
    struct {
        size_t total_size;
        size_t header_size;
        size_t data_size;
        size_t chunk_data_size;
        size_t chunk_overhead;
        size_t compression_ratio;
    } memory;
    struct {
        double load_time_ms;
        double parse_time_ms;
        double remap_time_ms;
    } performance;
    struct {
        size_t total_references;
        size_t resolved;
        size_t unresolved;
    } references;
    struct {
        size_t total_chunks;
        size_t compressed_chunks;
        size_t max_chunk_size;
        size_t avg_chunk_size;
    } chunks;
} nmo_file_stats_t;

NMO_API nmo_status_t nmo_stats_collect(
    nmo_session_t *session,
    nmo_file_stats_t *out_stats);

NMO_API void nmo_stats_print(
    const nmo_file_stats_t *stats,
    FILE *output);

NMO_API nmo_status_t nmo_stats_export_json(
    const nmo_file_stats_t *stats,
    const char *output_path);

NMO_API void nmo_stats_print_summary(
    const nmo_file_stats_t *stats,
    FILE *output);

NMO_API nmo_status_t nmo_document_stats_collect(
    nmo_document_t *document,
    nmo_file_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_STATS_H */
