#ifndef NMO_DOCUMENT_STATS_H
#define NMO_DOCUMENT_STATS_H

#include "document/nmo_document.h"
#include "app/nmo_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_document_stats_collect(
    nmo_document_t *document,
    nmo_file_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_STATS_H */
