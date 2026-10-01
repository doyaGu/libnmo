/**
 * @file workspace_edit_entity.c
 * @brief Entity edits: camera, light, parent, matrix, targets.
 */

#include "object/nmo_entity_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"

#include "workspace_edit_internal.h"

static nmo_camera_state_t *workspace_edit_camera_state_for_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (object == NULL) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        void *state = nmo_object_get_state(object);
        switch (nmo_object_get_class_id(object)) {
        case NMO_CID_CAMERA:
            return (nmo_camera_state_t *)state;
        case NMO_CID_TARGETCAMERA:
            return state != NULL
                ? &((nmo_targetcamera_state_t *)state)->base
                : NULL;
        default:
            return NULL;
        }
    }
    return (nmo_camera_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_CAMERA);
}

nmo_status_t nmo_entity_edit_set_camera_settings(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_entity_camera_settings_t *settings)
{
    if (edit == NULL || edit->finished || object_id == 0u || settings == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_camera_state_t *camera =
        workspace_edit_camera_state_for_object(registry, object);
    if (camera == NULL) {
        return workspace_edit_session_object_derives(registry, object, NMO_CID_CAMERA)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, camera, sizeof(*camera));
    if (status != NMO_OK) {
        return status;
    }

    camera->fov = settings->fov;
    camera->near_plane = settings->near_plane;
    camera->far_plane = settings->far_plane;
    /* The sections holding the edited values must be written. A camera that
       was loaded without them (the engine always writes them) gets them now;
       one in the legacy layout gets the legacy sections of these values. */
    if (camera->has_fov_chunk || camera->has_proj_chunk ||
        camera->has_ortho_chunk || camera->has_aspect_chunk ||
        camera->has_planes_chunk) {
        camera->has_fov_chunk = 1;
        camera->has_planes_chunk = 1;
    } else {
        camera->has_cameraonly_chunk = 1;
    }
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_3dentity_state_t *workspace_edit_entity_state_for_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (object == NULL) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        void *state = nmo_object_get_state(object);
        if (state == NULL) {
            return NULL;
        }
        switch (nmo_object_get_class_id(object)) {
        case NMO_CID_3DENTITY:
            return (nmo_3dentity_state_t *)state;
        case NMO_CID_3DOBJECT:
            return &((nmo_3dobject_state_t *)state)->entity;
        case NMO_CID_CAMERA:
            return &((nmo_camera_state_t *)state)->entity;
        case NMO_CID_TARGETCAMERA:
            return &((nmo_targetcamera_state_t *)state)->base.entity;
        case NMO_CID_LIGHT:
            return &((nmo_light_state_t *)state)->entity;
        case NMO_CID_TARGETLIGHT:
            return &((nmo_targetlight_state_t *)state)->base.entity;
        case NMO_CID_CHARACTER:
            return &((nmo_character_state_t *)state)->base;
        case NMO_CID_SPRITE3D:
            return &((nmo_sprite3d_state_t *)state)->base;
        case NMO_CID_CURVE:
            return &((nmo_curve_state_t *)state)->base;
        case NMO_CID_CURVEPOINT:
            return &((nmo_curvepoint_state_t *)state)->base;
        case NMO_CID_BODYPART:
            return &((nmo_bodypart_state_t *)state)->base.entity;
        default:
            return NULL;
        }
    }
    return (nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_3DENTITY);
}

nmo_status_t nmo_entity_edit_set_parent(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t parent_id)
{
    if (edit == NULL || edit->finished || object_id == 0u ||
        parent_id == object_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    nmo_object_t *parent = parent_id != 0u
        ? nmo_object_repository_find_by_id(repo, parent_id) : NULL;
    if (object == NULL || (parent_id != 0u && parent == NULL)) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!workspace_edit_object_is_entity_target(registry, object) ||
        (parent != NULL && !workspace_edit_object_is_entity_target(registry, parent))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_3dentity_state_t *state =
        workspace_edit_entity_state_for_object(registry, object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    state->parent = parent_id != 0u
        ? nmo_ref_from_id(parent_id) : nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t nmo_entity_edit_set_world_matrix(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const float matrix[16])
{
    if (edit == NULL || edit->finished || object_id == 0u ||
        matrix == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!workspace_edit_object_is_entity_target(registry, object)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_3dentity_state_t *state =
        workspace_edit_entity_state_for_object(registry, object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    memcpy(state->world_matrix, matrix, sizeof(state->world_matrix));
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

/* RCKTargetCamera::SetTarget and RCKTargetLight::SetTarget keep a flag on the
 * entities they aim at: the new target gets target_flag and loses
 * CK_3DENTITY_FRAME, the previous target gets the opposite. */
static nmo_status_t workspace_edit_update_target_flags(
    nmo_workspace_edit_t *edit,
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t entity_id,
    uint32_t target_flag,
    bool is_target)
{
    nmo_object_t *entity_object = nmo_object_repository_find_by_id(repo, entity_id);
    nmo_3dentity_state_t *entity =
        workspace_edit_entity_state_for_object(registry, entity_object);
    if (entity == NULL) {
        return NMO_OK;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, entity, sizeof(*entity));
    if (status != NMO_OK) {
        return status;
    }
    if (is_target) {
        entity->entity_flags =
            (entity->entity_flags | target_flag) & ~(uint32_t)CK_3DENTITY_FRAME;
    } else {
        entity->entity_flags =
            (entity->entity_flags & ~target_flag) | (uint32_t)CK_3DENTITY_FRAME;
    }
    return NMO_OK;
}

nmo_status_t nmo_entity_edit_set_camera_target(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t target_id)
{
    if (edit == NULL || edit->finished || object_id == 0u || target_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    nmo_object_t *target = nmo_object_repository_find_by_id(repo, target_id);
    if (object == NULL || target == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!workspace_edit_object_is_entity_target(registry, target)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_targetcamera_state_t *state =
        (nmo_targetcamera_state_t *)workspace_edit_object_state(
            registry,
            object,
            NMO_CID_TARGETCAMERA,
            CKPGUID_TARGETCAMERA);
    if (state == NULL) {
        return workspace_edit_session_object_derives(registry, object, NMO_CID_TARGETCAMERA)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }
    const nmo_object_id_t previous_target_id =
        state->has_target ? nmo_ref_runtime_id(&state->target) : 0u;
    if (previous_target_id != target_id) {
        if (previous_target_id != 0u) {
            status = workspace_edit_update_target_flags(
                edit, registry, repo, previous_target_id, CK_3DENTITY_TARGETCAMERA, false);
            if (status != NMO_OK) {
                return status;
            }
        }
        status = workspace_edit_update_target_flags(
            edit, registry, repo, target_id, CK_3DENTITY_TARGETCAMERA, true);
        if (status != NMO_OK) {
            return status;
        }
    }
    state->has_target = 1u;
    state->target = nmo_ref_from_id(target_id);
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static nmo_light_state_t *workspace_edit_light_state_for_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (object == NULL) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        void *state = nmo_object_get_state(object);
        switch (nmo_object_get_class_id(object)) {
        case NMO_CID_LIGHT:
            return (nmo_light_state_t *)state;
        case NMO_CID_TARGETLIGHT:
            return state != NULL
                ? &((nmo_targetlight_state_t *)state)->base
                : NULL;
        default:
            return NULL;
        }
    }
    return (nmo_light_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_LIGHT);
}

nmo_status_t nmo_entity_edit_set_light_settings(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_entity_light_settings_t *settings)
{
    if (edit == NULL || edit->finished || object_id == 0u || settings == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    /* CKLight stores point, spot and directional lights; a file with any other
       type loads as a point light, so VX_LIGHTPARA cannot be written. */
    if (settings->type < VX_LIGHTPOINT || settings->type > VX_LIGHTDIREC) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_light_state_t *light =
        workspace_edit_light_state_for_object(registry, object);
    if (light == NULL) {
        return workspace_edit_session_object_derives(registry, object, NMO_CID_LIGHT)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, light, sizeof(*light));
    if (status != NMO_OK) {
        return status;
    }

    light->light_data.diffuse.r = settings->diffuse[0];
    light->light_data.diffuse.g = settings->diffuse[1];
    light->light_data.diffuse.b = settings->diffuse[2];
    light->light_data.diffuse.a = settings->diffuse[3];
    light->light_data.range = settings->range;
    light->light_data.type = settings->type;
    if (settings->type != VX_LIGHTSPOT) {
        nmo_light_apply_nonspot_defaults(light);
    }
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_entity_edit_set_light_target(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t target_id)
{
    if (edit == NULL || edit->finished || object_id == 0u || target_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    nmo_object_t *target = nmo_object_repository_find_by_id(repo, target_id);
    if (object == NULL || target == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!workspace_edit_object_is_entity_target(registry, target)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_targetlight_state_t *state =
        (nmo_targetlight_state_t *)workspace_edit_object_state(
            registry,
            object,
            NMO_CID_TARGETLIGHT,
            CKPGUID_TARGETLIGHT);
    if (state == NULL) {
        return workspace_edit_session_object_derives(registry, object, NMO_CID_TARGETLIGHT)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }
    const nmo_object_id_t previous_target_id =
        state->has_target ? nmo_ref_runtime_id(&state->target) : 0u;
    if (previous_target_id != target_id) {
        if (previous_target_id != 0u) {
            status = workspace_edit_update_target_flags(
                edit, registry, repo, previous_target_id, CK_3DENTITY_TARGETLIGHT, false);
            if (status != NMO_OK) {
                return status;
            }
        }
        status = workspace_edit_update_target_flags(
            edit, registry, repo, target_id, CK_3DENTITY_TARGETLIGHT, true);
        if (status != NMO_OK) {
            return status;
        }
    }
    state->has_target = 1u;
    state->target = nmo_ref_from_id(target_id);
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
