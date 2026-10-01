/**
 * @file workspace_edit_scene.c
 * @brief Scene edits.
 */

#include "object/nmo_scene_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_scene_schemas.h"

#include "workspace_edit_internal.h"

typedef struct scene_object_descs_snapshot {
    nmo_array_t *object_descs;
    size_t previous_count;
    nmo_object_id_t object_id;
} scene_object_descs_snapshot_t;

static nmo_status_t rollback_scene_object_desc_append(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    (void)edit;
    scene_object_descs_snapshot_t *snapshot =
        (scene_object_descs_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->object_descs == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = nmo_array_size(snapshot->object_descs);
    if (count == snapshot->previous_count) {
        return NMO_OK;
    }
    if (count != snapshot->previous_count + 1u) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_scene_object_desc_t *desc =
        NMO_ARRAY_GET(
            nmo_scene_object_desc_t,
            snapshot->object_descs,
            snapshot->previous_count);
    if (desc == NULL ||
        nmo_ref_runtime_id(&desc->ref) != snapshot->object_id) {
        return NMO_ERR_INVALID_STATE;
    }

    return nmo_array_resize(snapshot->object_descs, snapshot->previous_count);
}

nmo_status_t nmo_scene_edit_add_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    nmo_object_id_t object_id,
    uint32_t flags)
{
    const uint32_t allowed_flags =
        NMO_SCENE_MEMBERSHIP_ACTIVE | NMO_SCENE_MEMBERSHIP_START_ACTIVE;
    if (edit == NULL || edit->finished || scene_id == 0 || object_id == 0 ||
        (flags & ~allowed_flags) != 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *scene_object = nmo_object_repository_find_by_id(repo, scene_id);
    if (scene_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)workspace_edit_object_state(
            registry, scene_object, NMO_CID_SCENE, CKPGUID_SCENE);
    if (scene_state == NULL) {
        return workspace_edit_session_object_derives(registry, scene_object, NMO_CID_SCENE)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_t *target_object = nmo_object_repository_find_by_id(repo, object_id);
    if (target_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene_state->object_descs);
    size_t existing_count = nmo_array_size(&scene_state->object_descs);
    for (size_t i = 0; i < existing_count; ++i) {
        if (nmo_ref_runtime_id(&descs[i].ref) == object_id) {
            return NMO_ERR_ALREADY_EXISTS;
        }
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    scene_object_descs_snapshot_t *snapshot =
        (scene_object_descs_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(scene_object_descs_snapshot_t));
    if (snapshot == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_NOMEM);
    }
    snapshot->object_descs = &scene_state->object_descs;
    snapshot->previous_count = existing_count;
    snapshot->object_id = object_id;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_scene_object_desc_append, snapshot);
    if (push_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, push_result);
    }

    uint32_t scene_flags = 0;
    if ((flags & NMO_SCENE_MEMBERSHIP_ACTIVE) != 0u) {
        scene_flags |= CK_SCENEOBJECT_ACTIVE;
    }
    if ((flags & NMO_SCENE_MEMBERSHIP_START_ACTIVE) != 0u) {
        scene_flags |= CK_SCENEOBJECT_START_ACTIVATE;
    }

    nmo_scene_object_desc_t scene_desc = {0};
    scene_desc.ref = nmo_ref_from_id(object_id);
    scene_desc.flags = scene_flags;
    nmo_status_t append_result =
        nmo_array_append(&scene_state->object_descs, &scene_desc);
    if (append_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, append_result);
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}

nmo_status_t nmo_scene_edit_set_environment(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    const nmo_scene_environment_settings_t *settings)
{
    if (edit == NULL || edit->finished || scene_id == 0u ||
        settings == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *scene_object = nmo_object_repository_find_by_id(repo, scene_id);
    if (scene_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)workspace_edit_object_state(
            registry, scene_object, NMO_CID_SCENE, CKPGUID_SCENE);
    if (scene_state == NULL) {
        return workspace_edit_session_object_derives(registry, scene_object, NMO_CID_SCENE)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, scene_state, sizeof(*scene_state));
    if (status != NMO_OK) {
        return status;
    }

    if (settings->has_background_color) {
        scene_state->background_color = workspace_edit_pack_argb(
            settings->background_color[0],
            settings->background_color[1],
            settings->background_color[2],
            settings->background_color[3]);
    }
    if (settings->has_ambient_light) {
        scene_state->ambient_light_color = workspace_edit_pack_argb(
            settings->ambient_light[0],
            settings->ambient_light[1],
            settings->ambient_light[2],
            settings->ambient_light[3]);
    }
    if (settings->has_fog) {
        scene_state->fog_mode = settings->fog_mode;
        scene_state->fog_color = workspace_edit_pack_argb(
            settings->fog_color[0],
            settings->fog_color[1],
            settings->fog_color[2],
            settings->fog_color[3]);
        scene_state->fog_start = settings->fog_start;
        scene_state->fog_end = settings->fog_end;
        scene_state->fog_density = settings->fog_density;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_scene_edit_set_active_camera(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    nmo_object_id_t camera_id)
{
    if (edit == NULL || edit->finished || scene_id == 0u ||
        camera_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *scene_object = nmo_object_repository_find_by_id(repo, scene_id);
    if (scene_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)workspace_edit_object_state(
            registry, scene_object, NMO_CID_SCENE, CKPGUID_SCENE);
    if (scene_state == NULL) {
        return workspace_edit_session_object_derives(registry, scene_object, NMO_CID_SCENE)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_t *camera_object =
        nmo_object_repository_find_by_id(repo, camera_id);
    if (camera_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, camera_object, NMO_CID_CAMERA)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, scene_state, sizeof(*scene_state));
    if (status != NMO_OK) {
        return status;
    }

    scene_state->starting_camera = nmo_ref_from_id(camera_id);
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
