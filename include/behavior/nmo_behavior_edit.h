#ifndef NMO_BEHAVIOR_EDIT_H
#define NMO_BEHAVIOR_EDIT_H

#include "runtime/nmo_workspace.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "behavior/nmo_behavior_rewrite.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_behavior_edit_add_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);

NMO_API nmo_status_t nmo_behavior_edit_mark_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EDIT_H */
