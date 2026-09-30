#ifndef NMO_BEHAVIOR_LINK_EDIT_H
#define NMO_BEHAVIOR_LINK_EDIT_H

#include "runtime/nmo_workspace.h"

#include <stdint.h>

#define NMO_BEHAVIOR_LINK_EDIT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_LINK_EDIT_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Link and interface primitives that journal their changes in a workspace
 * edit. They live with the workspace edit machinery in the core library;
 * script edits and behavior rewrites are built on them.
 */
NMO_API nmo_status_t nmo_behavior_edit_add_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);

NMO_API nmo_status_t nmo_behavior_edit_remove_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

NMO_API nmo_status_t nmo_behavior_edit_mark_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_LINK_EDIT_H */
