#ifndef NMO_BEHAVIOR_INTERNAL_H
#define NMO_BEHAVIOR_INTERNAL_H

/*
 * The behavior acceleration of a session (behavior_acceleration.c): the cached
 * behavior index, the parsed interfaces and the interface views of a document
 * or a workspace. Implemented in the behavior layer; the layers above use it.
 */

#include "../runtime/runtime_internal.h"
#include "format/nmo_interface_view.h"

nmo_behavior_index_t *nmo_session_get_behavior_index(
    nmo_session_t *session);
nmo_status_t nmo_session_ensure_behavior_acceleration(
    nmo_session_t *session);
nmo_behavior_index_t *nmo_document_internal_behavior_index(
    nmo_document_t *document);
nmo_status_t nmo_document_internal_ensure_behavior_acceleration(
    nmo_document_t *document);
nmo_status_t nmo_document_internal_interface_view_from_behavior(
    nmo_document_t *document,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view);
nmo_behavior_index_t *nmo_workspace_internal_behavior_index(
    nmo_workspace_t *workspace);
nmo_status_t nmo_workspace_internal_ensure_behavior_acceleration(
    nmo_workspace_t *workspace);
nmo_status_t nmo_workspace_internal_interface_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view);

#endif /* NMO_BEHAVIOR_INTERNAL_H */
