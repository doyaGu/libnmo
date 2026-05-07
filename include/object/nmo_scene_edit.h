#ifndef NMO_SCENE_EDIT_H
#define NMO_SCENE_EDIT_H

#include "runtime/nmo_workspace.h"
#include "object/nmo_object_enum_defs.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_scene_membership_flags {
    NMO_SCENE_MEMBERSHIP_ACTIVE = 1u << 0,
    NMO_SCENE_MEMBERSHIP_START_ACTIVE = 1u << 1
} nmo_scene_membership_flags_t;

typedef struct nmo_scene_environment_settings {
    bool has_background_color;
    float background_color[4];
    bool has_ambient_light;
    float ambient_light[4];
    bool has_fog;
    VXFOG_MODE fog_mode;
    float fog_color[4];
    float fog_start;
    float fog_end;
    float fog_density;
} nmo_scene_environment_settings_t;

NMO_API nmo_status_t nmo_scene_edit_add_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    nmo_object_id_t object_id,
    uint32_t flags);

NMO_API nmo_status_t nmo_scene_edit_set_environment(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    const nmo_scene_environment_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCENE_EDIT_H */
