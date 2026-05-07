#ifndef NMO_SOUND_EDIT_H
#define NMO_SOUND_EDIT_H

#include "core/nmo_error.h"
#include "object/nmo_object_enum_defs.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace_edit nmo_workspace_edit_t;

typedef struct nmo_sound_edit_settings {
    const char *file_path;
    bool has_gain;
    float gain;
    bool has_pan;
    float pan;
    bool has_pitch;
    float pitch;
    bool has_attached_object;
    nmo_object_id_t attached_object_id;
    bool has_position;
    float position[3];
    bool has_direction;
    float direction[3];
} nmo_sound_edit_settings_t;

NMO_API nmo_status_t nmo_sound_edit_set_sound(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t sound_id,
    const nmo_sound_edit_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SOUND_EDIT_H */
