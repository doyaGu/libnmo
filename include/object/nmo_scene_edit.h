#ifndef NMO_SCENE_EDIT_H
#define NMO_SCENE_EDIT_H

#include "runtime/nmo_workspace.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_scene_membership_flags {
    NMO_SCENE_MEMBERSHIP_ACTIVE = 1u << 0,
    NMO_SCENE_MEMBERSHIP_START_ACTIVE = 1u << 1
} nmo_scene_membership_flags_t;

NMO_API nmo_status_t nmo_scene_edit_add_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    nmo_object_id_t object_id,
    uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCENE_EDIT_H */
