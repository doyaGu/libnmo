#ifndef NMO_ANIMATION_EDIT_H
#define NMO_ANIMATION_EDIT_H

#include "core/nmo_error.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "nmo_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace_edit nmo_workspace_edit_t;

typedef struct nmo_object_animation_settings {
    CK_OBJECTANIMATION_FORMAT format;
    nmo_object_id_t entity_id;
    bool has_root_position;
    float root_position[3];
    bool has_flags;
    uint32_t flags;
    bool has_length;
    float length;
    size_t controller_count;
    const nmo_objanim_controller_t *controllers;
    size_t morph_key_count;
    const nmo_objanim_morph_key_t *morph_keys;
} nmo_object_animation_settings_t;

NMO_API nmo_status_t nmo_animation_edit_set_object_animation(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t animation_id,
    const nmo_object_animation_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ANIMATION_EDIT_H */
