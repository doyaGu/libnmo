/**
 * @file workspace_edit_sound.c
 * @brief Sound edits.
 */

#include "object/nmo_sound_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_sound_schemas.h"

#include "workspace_edit_internal.h"

static nmo_status_t workspace_edit_copy_document_string(
    nmo_workspace_edit_t *edit,
    const char *src,
    char **out_copy)
{
    if (edit == NULL || src == NULL || out_copy == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    const char *copy = nmo_arena_strdup(arena, src);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    *out_copy = (char *)copy;
    return NMO_OK;
}

nmo_status_t nmo_sound_edit_set_sound(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t sound_id,
    const nmo_sound_edit_settings_t *settings)
{
    if (edit == NULL || edit->finished || sound_id == 0u ||
        settings == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *sound = nmo_object_repository_find_by_id(repo, sound_id);
    if (sound == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_sound_state_t *sound_state = NULL;
    nmo_wavesound_state_t *wave_state = NULL;
    if (nmo_guid_is_null(nmo_object_get_type_guid(sound))) {
        void *state = nmo_object_get_state(sound);
        switch (nmo_object_get_class_id(sound)) {
        case NMO_CID_SOUND:
            sound_state = (nmo_sound_state_t *)state;
            break;
        case NMO_CID_WAVESOUND:
            wave_state = (nmo_wavesound_state_t *)state;
            sound_state = wave_state != NULL ? &wave_state->base : NULL;
            break;
        default:
            return NMO_ERR_INVALID_ARGUMENT;
        }
    } else if (workspace_edit_session_object_derives(registry, sound, NMO_CID_WAVESOUND)) {
        wave_state = (nmo_wavesound_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry, sound, CKPGUID_WAVESOUND);
        sound_state = wave_state != NULL ? &wave_state->base : NULL;
    } else if (workspace_edit_session_object_derives(registry, sound, NMO_CID_SOUND)) {
        sound_state = (nmo_sound_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry, sound, CKPGUID_SOUND);
    } else {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (sound_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (settings->has_attached_object) {
        if (settings->attached_object_id == 0u) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_object_t *attached =
            nmo_object_repository_find_by_id(repo, settings->attached_object_id);
        if (attached == NULL) {
            return NMO_ERR_NOT_FOUND;
        }
        if (!workspace_edit_object_is_entity_target(registry, attached)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    nmo_status_t status;
    if (wave_state == NULL) {
        if (settings->has_gain || settings->has_pan || settings->has_pitch ||
            settings->has_attached_object || settings->has_position ||
            settings->has_direction) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        status = nmo_workspace_edit_snapshot_bytes(
            edit,
            sound_state,
            sizeof(*sound_state));
        if (status != NMO_OK) {
            return status;
        }
        if (settings->file_path != NULL) {
            status = workspace_edit_copy_document_string(
                edit,
                settings->file_path,
                &sound_state->file_name);
            if (status != NMO_OK) {
                return status;
            }
            sound_state->save_options = CKSOUND_INCLUDEORIGINALFILE;
        }
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        return NMO_OK;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, wave_state, sizeof(*wave_state));
    if (status != NMO_OK) {
        return status;
    }
    if (settings->file_path != NULL) {
        status = workspace_edit_copy_document_string(
            edit,
            settings->file_path,
            &wave_state->wave_file_name);
        if (status != NMO_OK) {
            return status;
        }
        wave_state->has_wave_file_name = 1u;
    }

    if (settings->has_gain || settings->has_pan || settings->has_pitch ||
        settings->has_attached_object || settings->has_position ||
        settings->has_direction) {
        wave_state->has_data2 = 1u;
        wave_state->gain = settings->has_gain ? settings->gain : 1.0f;
        wave_state->pan = settings->has_pan ? settings->pan : 0.0f;
        wave_state->pitch = settings->has_pitch ? settings->pitch : 1.0f;
    }
    if (settings->has_attached_object) {
        wave_state->attached_object = nmo_ref_from_id(settings->attached_object_id);
    }
    if (settings->has_position) {
        wave_state->position.x = settings->position[0];
        wave_state->position.y = settings->position[1];
        wave_state->position.z = settings->position[2];
    }
    if (settings->has_direction) {
        wave_state->direction.x = settings->direction[0];
        wave_state->direction.y = settings->direction[1];
        wave_state->direction.z = settings->direction[2];
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
