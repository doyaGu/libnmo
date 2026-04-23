#ifndef NMO_DOCUMENT_LOAD_H
#define NMO_DOCUMENT_LOAD_H

#include "document/nmo_document.h"
#include "core/nmo_error.h"
#include "session/nmo_deserializer.h"

#define NMO_LOAD_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LOAD_WORKFLOW_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_load_file(nmo_session_t *session,
                                   const char *path,
                                   const nmo_load_options_t *opts);

NMO_API nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    nmo_document_t **out_document);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_LOAD_H */
