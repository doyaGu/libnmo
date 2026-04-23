#ifndef NMO_EXPORT_TEXT_H
#define NMO_EXPORT_TEXT_H

#include "chunk/nmo_chunk_inspect.h"
#include "document/nmo_document_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef nmo_inspector_options_t nmo_export_text_chunk_options_t;

NMO_API nmo_status_t nmo_export_text_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_export_text_chunk_options_t *options);

NMO_API void nmo_export_text_document_stats(
    const nmo_file_stats_t *stats,
    FILE *stream);

NMO_API void nmo_export_text_document_stats_summary(
    const nmo_file_stats_t *stats,
    FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_TEXT_H */
