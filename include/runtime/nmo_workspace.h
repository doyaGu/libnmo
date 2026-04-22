#ifndef NMO_RUNTIME_WORKSPACE_H
#define NMO_RUNTIME_WORKSPACE_H

#include "document/nmo_document.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_session_edit nmo_workspace_edit_t;
typedef struct nmo_workspace nmo_workspace_t;

NMO_API nmo_status_t nmo_workspace_create(
    nmo_context_t *ctx,
    nmo_document_t *document,
    nmo_workspace_t **out_workspace);

NMO_API void nmo_workspace_destroy(nmo_workspace_t *workspace);
NMO_API nmo_document_t *nmo_workspace_get_document(nmo_workspace_t *workspace);

/* Transitional access for legacy APIs during the session split. */
NMO_API nmo_session_t *nmo_workspace_session(nmo_workspace_t *workspace);
NMO_API const nmo_session_t *nmo_workspace_session_const(
    const nmo_workspace_t *workspace);

NMO_API nmo_status_t nmo_workspace_edit_begin(
    nmo_workspace_t *workspace,
    const char *label,
    nmo_workspace_edit_t **out_edit);

NMO_API nmo_status_t nmo_workspace_edit_commit(nmo_workspace_edit_t *edit);
NMO_API void nmo_workspace_edit_rollback(nmo_workspace_edit_t *edit);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RUNTIME_WORKSPACE_H */
