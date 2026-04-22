#ifndef NMO_RUNTIME_WORKSPACE_H
#define NMO_RUNTIME_WORKSPACE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_workspace_t;
typedef struct nmo_session_edit nmo_workspace_edit_t;

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
