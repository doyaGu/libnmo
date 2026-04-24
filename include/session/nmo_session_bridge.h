#ifndef NMO_SESSION_BRIDGE_H
#define NMO_SESSION_BRIDGE_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_document nmo_document_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_workspace nmo_workspace_t;

#define NMO_SESSION_BRIDGE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SESSION_BRIDGE_API_TIER NMO_API_TIER_ADVANCED_C

NMO_API nmo_status_t nmo_session_borrow_document(
    nmo_session_t *session,
    nmo_document_t **out_document);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_BRIDGE_H */
