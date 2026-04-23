#ifndef NMO_RUNTIME_WORKSPACE_H
#define NMO_RUNTIME_WORKSPACE_H

#include "document/nmo_document.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>

#define NMO_WORKSPACE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_WORKSPACE_LIFECYCLE_API_TIER NMO_API_TIER_STABLE_CONSUMER
#define NMO_WORKSPACE_EDIT_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_workspace_edit nmo_workspace_edit_t;
typedef struct nmo_workspace nmo_workspace_t;

typedef enum nmo_workspace_edit_flags {
    NMO_WORKSPACE_EDIT_OBJECT_STATE   = 1u << 0,
    NMO_WORKSPACE_EDIT_REFERENCES     = 1u << 1,
    NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH = 1u << 2,
    NMO_WORKSPACE_EDIT_NAMES          = 1u << 3,
    NMO_WORKSPACE_EDIT_RESOURCES      = 1u << 4
} nmo_workspace_edit_flags_t;

NMO_API nmo_status_t nmo_workspace_create(
    nmo_context_t *ctx,
    nmo_document_t *document,
    nmo_workspace_t **out_workspace);

NMO_API void nmo_workspace_destroy(nmo_workspace_t *workspace);
NMO_API nmo_document_t *nmo_workspace_get_document(nmo_workspace_t *workspace);

NMO_API nmo_status_t nmo_workspace_edit_begin(
    nmo_workspace_t *workspace,
    const char *label,
    nmo_workspace_edit_t **out_edit);

NMO_API nmo_status_t nmo_workspace_edit_commit(nmo_workspace_edit_t *edit);
NMO_API void nmo_workspace_edit_rollback(nmo_workspace_edit_t *edit);
NMO_API void *nmo_workspace_edit_alloc(
    nmo_workspace_edit_t *edit,
    size_t size,
    size_t align);
NMO_API nmo_status_t nmo_workspace_edit_snapshot_bytes(
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size);
NMO_API nmo_status_t nmo_workspace_edit_track_created_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id);
NMO_API nmo_status_t nmo_workspace_edit_snapshot_object_chunk(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id);
NMO_API void nmo_workspace_edit_mark(
    nmo_workspace_edit_t *edit,
    uint32_t flags);

NMO_API nmo_status_t nmo_workspace_apply_edit_flags(
    nmo_workspace_t *workspace,
    uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RUNTIME_WORKSPACE_H */
