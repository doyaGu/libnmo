#ifndef NMO_DOCUMENT_SAVE_H
#define NMO_DOCUMENT_SAVE_H

#include "document/nmo_document.h"
#include "core/nmo_error.h"
#include "session/nmo_serializer.h"

#define NMO_SAVE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SAVE_WORKFLOW_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_save_file(nmo_session_t *session,
                                   const char *path,
                                   const nmo_save_options_t *opts);

NMO_API nmo_status_t nmo_document_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_SAVE_H */
