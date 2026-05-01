#ifndef NMO_ENTITY_EDIT_H
#define NMO_ENTITY_EDIT_H

#include "core/nmo_error.h"
#include "object/nmo_object_enum_defs.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace_edit nmo_workspace_edit_t;

typedef struct nmo_entity_camera_settings {
    float fov;
    float near_plane;
    float far_plane;
} nmo_entity_camera_settings_t;

typedef struct nmo_entity_light_settings {
    float diffuse[4];
    float range;
    VXLIGHT_TYPE type;
} nmo_entity_light_settings_t;

NMO_API nmo_status_t nmo_entity_edit_set_camera_settings(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_entity_camera_settings_t *settings);

NMO_API nmo_status_t nmo_entity_edit_set_camera_target(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t target_id);

NMO_API nmo_status_t nmo_entity_edit_set_light_settings(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_entity_light_settings_t *settings);

NMO_API nmo_status_t nmo_entity_edit_set_light_target(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t target_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENTITY_EDIT_H */
