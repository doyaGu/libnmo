#ifndef NMO_CHUNK_INSPECT_H
#define NMO_CHUNK_INSPECT_H

#include "format/nmo_chunk.h"

#include <stdio.h>

#define NMO_CHUNK_INSPECT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_CHUNK_INSPECT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_dump_level {
    NMO_DUMP_BRIEF = 0,
    NMO_DUMP_NORMAL = 1,
    NMO_DUMP_DETAILED = 2,
    NMO_DUMP_FULL = 3
} nmo_dump_level_t;

typedef struct nmo_chunk_validation {
    bool is_valid;
    size_t error_count;
    size_t warning_count;
    bool header_valid;
    bool identifiers_valid;
    bool data_valid;
    bool sub_chunks_valid;
    char error_message[256];
} nmo_chunk_validation_t;

typedef struct nmo_inspector_options {
    nmo_dump_level_t level;
    bool show_hex;
    bool show_sub_chunks;
    bool validate;
    bool colorize;
    size_t max_depth;
    size_t hex_bytes;
} nmo_inspector_options_t;

void nmo_inspector_init_options(nmo_inspector_options_t *options);

nmo_status_t nmo_inspector_dump_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options);

nmo_status_t nmo_inspector_validate_chunk(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result);

int nmo_inspector_hex_dump(
    const nmo_chunk_t *chunk,
    FILE *stream,
    size_t max_bytes,
    size_t bytes_per_line);

nmo_status_t nmo_inspector_print_summary(
    const nmo_chunk_t *chunk,
    FILE *stream);

int nmo_inspector_compare_chunks(
    const nmo_chunk_t *chunk1,
    const nmo_chunk_t *chunk2,
    FILE *stream);

nmo_status_t nmo_inspector_export_json(
    const nmo_chunk_t *chunk,
    FILE *stream,
    bool include_data);

NMO_API nmo_status_t nmo_chunk_inspect_validate(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_INSPECT_H */
