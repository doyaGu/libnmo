#ifndef NMO_EXPORT_JSON_H
#define NMO_EXPORT_JSON_H

#include "app/nmo_inspector.h"
#include "app/nmo_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_export_json_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    bool include_data);

NMO_API nmo_status_t nmo_export_json_document_stats(
    const nmo_file_stats_t *stats,
    const char *output_path);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_JSON_H */
