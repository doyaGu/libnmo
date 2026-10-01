/**
 * @file workspace_edit_animation.c
 * @brief Animation edits.
 */

#include "object/nmo_animation_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"

#include "workspace_edit_internal.h"

nmo_status_t nmo_animation_edit_set_object_animation(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t animation_id,
    const nmo_object_animation_settings_t *settings)
{
    if (edit == NULL || edit->finished || animation_id == 0u ||
        settings == NULL || settings->entity_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *animation =
        nmo_object_repository_find_by_id(repo, animation_id);
    nmo_object_t *entity =
        nmo_object_repository_find_by_id(repo, settings->entity_id);
    if (animation == NULL || entity == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_objectanimation_state_t *state =
        (nmo_objectanimation_state_t *)workspace_edit_object_state(
            registry,
            animation,
            NMO_CID_OBJECTANIMATION,
            CKPGUID_OBJECTANIMATION);
    if (state == NULL) {
        return workspace_edit_session_object_derives(
                   registry, animation, NMO_CID_OBJECTANIMATION)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    if (!workspace_edit_object_is_entity_target(registry, entity)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (settings->controller_count > 0u) {
        if (settings->controllers == NULL ||
            settings->controller_count > UINT32_MAX ||
            (settings->format != CKOBJANIM_FORMAT_CONTROLLERS &&
             settings->format != CKOBJANIM_FORMAT_NEWDATA)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        for (size_t i = 0u; i < settings->controller_count; ++i) {
            const nmo_objanim_controller_t *controller = &settings->controllers[i];
            uint32_t key_size = nmo_objanim_controller_format_key_size(
                controller->type, settings->format);
            if (key_size == 0u || controller->key_count == 0u ||
                controller->data_size == 0u || controller->data == NULL) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            if (controller->key_count > UINT32_MAX / key_size ||
                controller->data_size != controller->key_count * key_size) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            if (settings->format == CKOBJANIM_FORMAT_NEWDATA &&
                controller->type != 0x637c4301u &&
                controller->type != 0x654a3a04u &&
                controller->type != 0x49ed4002u &&
                controller->type != 0x2f200b08u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
        }
    } else if (settings->controllers != NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (settings->morph_key_count > 0u) {
        if (settings->morph_keys == NULL ||
            settings->morph_key_count > INT32_MAX ||
            settings->format != CKOBJANIM_FORMAT_NEWDATA) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        uint32_t morph_vertex_count = 0u;
        for (size_t i = 0u; i < settings->morph_key_count; ++i) {
            const nmo_objanim_morph_key_t *key = &settings->morph_keys[i];
            if (key->data_size == 0u || key->data == NULL ||
                key->data_size % (3u * sizeof(float)) != 0u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            uint32_t key_vertex_count =
                key->data_size / (uint32_t)(3u * sizeof(float));
            if (key_vertex_count > INT32_MAX ||
                (i > 0u && key_vertex_count != morph_vertex_count)) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            morph_vertex_count = key_vertex_count;
        }
    } else if (settings->morph_keys != NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    state->format = settings->format;
    state->entity = nmo_ref_from_id(settings->entity_id);
    if (settings->has_root_position) {
        state->has_root_pos = 1u;
        state->root_pos.x = settings->root_position[0];
        state->root_pos.y = settings->root_position[1];
        state->root_pos.z = settings->root_position[2];
    }
    if (settings->has_flags) {
        state->flags = settings->flags;
    }
    if (settings->has_length) {
        state->has_length = 1u;
        state->length = settings->length;
    }
    if (settings->controller_count > 0u) {
        nmo_arena_t *arena =
            nmo_workspace_internal_document_arena(edit->workspace);
        if (arena == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        nmo_objanim_controller_t *controllers =
            (nmo_objanim_controller_t *)nmo_arena_alloc(
                arena,
                sizeof(*controllers) * settings->controller_count,
                _Alignof(nmo_objanim_controller_t));
        if (controllers == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(controllers, 0, sizeof(*controllers) * settings->controller_count);
        for (size_t i = 0u; i < settings->controller_count; ++i) {
            const nmo_objanim_controller_t *src = &settings->controllers[i];
            void *data = nmo_arena_alloc(arena, src->data_size, 1u);
            if (data == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(data, src->data, src->data_size);
            controllers[i].type = src->type;
            controllers[i].key_count = src->key_count;
            controllers[i].data_size = src->data_size;
            controllers[i].data = data;
        }
        state->controller_count = (uint32_t)settings->controller_count;
        state->controllers = controllers;
    } else {
        state->controller_count = 0u;
        state->controllers = NULL;
    }
    if (settings->morph_key_count > 0u) {
        nmo_arena_t *arena =
            nmo_workspace_internal_document_arena(edit->workspace);
        if (arena == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        nmo_objanim_morph_key_t *morph_keys =
            (nmo_objanim_morph_key_t *)nmo_arena_alloc(
                arena,
                sizeof(*morph_keys) * settings->morph_key_count,
                _Alignof(nmo_objanim_morph_key_t));
        if (morph_keys == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(morph_keys, 0, sizeof(*morph_keys) * settings->morph_key_count);
        for (size_t i = 0u; i < settings->morph_key_count; ++i) {
            const nmo_objanim_morph_key_t *src = &settings->morph_keys[i];
            void *data = nmo_arena_alloc(arena, src->data_size, 1u);
            if (data == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(data, src->data, src->data_size);
            morph_keys[i].time_step = src->time_step;
            morph_keys[i].data_size = src->data_size;
            morph_keys[i].data = data;
        }
        state->has_morph_counts = 1u;
        state->morph_key_count = (int32_t)settings->morph_key_count;
        state->morph_vertex_count =
            (int32_t)(settings->morph_keys[0].data_size / (3u * sizeof(float)));
        state->morph_key_parsed_count = (uint32_t)settings->morph_key_count;
        state->morph_keys = morph_keys;
    } else {
        state->has_morph_counts = 0u;
        state->morph_key_count = 0;
        state->morph_vertex_count = 0;
        state->morph_key_parsed_count = 0u;
        state->morph_keys = NULL;
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
