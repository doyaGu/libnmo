/**
 * @file workspace_edit_common.c
 * @brief Helpers shared by the workspace edit families.
 */

#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"

#include "workspace_edit_internal.h"

nmo_status_t workspace_edit_push_action(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_phase_t phase,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_edit_journal_push(&edit->journal, phase, fn, payload);
}

nmo_status_t workspace_edit_push_rollback(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return workspace_edit_push_action(
        edit,
        NMO_WORKSPACE_EDIT_ACTION_ROLLBACK,
        fn,
        payload);
}

nmo_status_t workspace_edit_push_commit(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return workspace_edit_push_action(
        edit,
        NMO_WORKSPACE_EDIT_ACTION_COMMIT,
        fn,
        payload);
}

workspace_edit_checkpoint_t workspace_edit_checkpoint(
    const nmo_workspace_edit_t *edit)
{
    return (workspace_edit_checkpoint_t){
        .journal = nmo_workspace_edit_journal_checkpoint(
            edit != NULL ? &edit->journal : NULL),
        .flags = edit != NULL ? edit->flags : 0u,
    };
}

nmo_status_t workspace_edit_checkpoint_mark_arena(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t *checkpoint)
{
    if (edit == NULL || checkpoint == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_edit_journal_mark_arena(
        &edit->journal, &checkpoint->journal);
}

nmo_status_t workspace_edit_checkpoint_release_arena(
    nmo_workspace_edit_t *edit,
    const workspace_edit_checkpoint_t *checkpoint)
{
    if (edit == NULL || checkpoint == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_edit_journal_release_arena(
        &edit->journal, &checkpoint->journal);
}

void workspace_edit_abort_to(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint)
{
    if (edit == NULL) {
        return;
    }
    nmo_workspace_edit_journal_abort(
        &edit->journal, edit, checkpoint.journal);
    edit->flags = checkpoint.flags;
}

nmo_status_t workspace_edit_abort_status(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_status_t status)
{
    workspace_edit_abort_to(edit, checkpoint);
    return status;
}

nmo_status_t workspace_edit_abort_arena_status(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_status_t status)
{
    workspace_edit_abort_to(edit, checkpoint);
    if (!checkpoint.journal.arena_mark_active) {
        return status;
    }
    nmo_status_t rewind_status =
        nmo_workspace_edit_journal_rewind_arena(
            &edit->journal, &checkpoint.journal);
    return rewind_status == NMO_OK ? status : rewind_status;
}

nmo_status_t workspace_edit_push_rollback_or_abort(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    nmo_status_t status = workspace_edit_push_rollback(edit, fn, payload);
    if (status != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, status);
    }
    return NMO_OK;
}

nmo_status_t workspace_edit_push_bytes_snapshot(
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size)
{
    if (edit == NULL || edit->finished || target == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_edit_journal_snapshot_bytes(
        &edit->journal, edit, target, size);
}

nmo_status_t workspace_edit_push_bytes_snapshot_or_abort(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    void *target,
    size_t size)
{
    nmo_status_t status = workspace_edit_push_bytes_snapshot(edit, target, size);
    if (status != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, status);
    }
    return NMO_OK;
}

nmo_status_t workspace_edit_rollback_remove_array_id(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    array_id_action_t *action = (array_id_action_t *)payload;
    if (action == NULL || action->array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t index = 0;
    int found = 0;
    if (action->array->element_size == sizeof(nmo_behavior_ref_t)) {
        found = nmo_behavior_ref_array_find(
            action->array, action->id, &index);
    } else if (action->array->element_size == sizeof(nmo_ref_t)) {
        found = nmo_beobject_script_array_find(
            action->array, action->id, &index);
    } else {
        found = nmo_array_find(action->array, &action->id, &index);
    }
    if (found != 0) {
        return nmo_array_remove(action->array, index, NULL);
    }
    return NMO_OK;
}

nmo_status_t workspace_edit_action_remove_object(nmo_workspace_edit_t *edit, void *payload)
{
    object_id_action_t *action = (object_id_action_t *)payload;
    if (edit == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_object_repository_find_by_id(repo, action->id) == NULL) {
        return NMO_OK;
    }

    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_object_repository_take(repo, action->id, &object);
    if (status != NMO_OK) {
        return status;
    }
    nmo_runtime_destroy_object_state(
        nmo_workspace_internal_session(edit->workspace), object);
    nmo_object_destroy(object);
    return NMO_OK;
}

nmo_status_t workspace_edit_make_object_id_action(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t id,
    object_id_action_t **out_action)
{
    if (out_action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_action = NULL;
    if (edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_id_action_t *action =
        (object_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*action), _Alignof(object_id_action_t));
    if (action == NULL) {
        return NMO_ERR_NOMEM;
    }
    action->id = id;
    *out_action = action;
    return NMO_OK;
}

nmo_status_t workspace_edit_make_array_id_action(
    nmo_workspace_edit_t *edit,
    nmo_array_t *array,
    nmo_object_id_t id,
    size_t index,
    array_id_action_t **out_action)
{
    if (out_action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_action = NULL;
    if (edit == NULL || array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    array_id_action_t *action =
        (array_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*action), _Alignof(array_id_action_t));
    if (action == NULL) {
        return NMO_ERR_NOMEM;
    }
    action->array = array;
    action->id = id;
    action->index = index;
    *out_action = action;
    return NMO_OK;
}

const nmo_type_registry_t *workspace_edit_type_registry(
    const nmo_workspace_edit_t *edit)
{
    return edit != NULL
        ? nmo_workspace_internal_type_registry(edit->workspace)
        : NULL;
}

bool workspace_edit_session_object_derives(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object,
    nmo_class_id_t base_class_id)
{
    if (object == NULL) {
        return false;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == base_class_id) {
        return true;
    }
    return nmo_type_query_object_is_derived_from_class(
        registry, object, base_class_id);
}

void *workspace_edit_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid)
{
    if (object == NULL) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == class_id) {
        return nmo_object_get_state(object);
    }
    return nmo_type_query_object_get_ancestor_state_by_guid(
        registry, object, type_guid);
}

nmo_status_t workspace_edit_seek_message_manager_identifier(
    nmo_chunk_t *chunk,
    size_t *out_section_end)
{
    if (chunk == NULL || out_section_end == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_read(chunk));
    size_t section_dwords = 0u;
    NMO_RETURN_IF_ERROR(nmo_chunk_seek_identifier_with_size(
        chunk, 0x53u, &section_dwords));
    *out_section_end = nmo_chunk_get_position(chunk) + section_dwords;
    return NMO_OK;
}

uint8_t workspace_edit_float_color_channel(float value)
{
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (uint8_t)(value * 255.0f + 0.5f);
}

uint32_t workspace_edit_pack_argb(float r, float g, float b, float a)
{
    uint32_t alpha = workspace_edit_float_color_channel(a);
    uint32_t red = workspace_edit_float_color_channel(r);
    uint32_t green = workspace_edit_float_color_channel(g);
    uint32_t blue = workspace_edit_float_color_channel(b);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

bool workspace_edit_class_is_entity_target(nmo_class_id_t class_id)
{
    switch (class_id) {
    case NMO_CID_3DENTITY:
    case NMO_CID_3DOBJECT:
    case NMO_CID_CAMERA:
    case NMO_CID_TARGETCAMERA:
    case NMO_CID_LIGHT:
    case NMO_CID_TARGETLIGHT:
    case NMO_CID_CHARACTER:
    case NMO_CID_SPRITE3D:
    case NMO_CID_CURVE:
    case NMO_CID_CURVEPOINT:
    case NMO_CID_BODYPART:
        return true;
    default:
        return false;
    }
}

bool workspace_edit_object_is_entity_target(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object)
{
    if (object == NULL) {
        return false;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        return workspace_edit_class_is_entity_target(
            nmo_object_get_class_id(object));
    }
    return workspace_edit_session_object_derives(registry, object, NMO_CID_3DENTITY);
}

nmo_status_t workspace_edit_read_message_manager_names(
    nmo_session_t *session,
    const nmo_manager_data_t *manager,
    const char ***out_names,
    uint32_t *out_count)
{
    if (session == NULL || manager == NULL || out_names == NULL ||
        out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_names = NULL;
    *out_count = 0u;
    if (manager->chunk == NULL) {
        return NMO_OK;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_chunk_t *chunk = nmo_chunk_clone(manager->chunk, arena);
    if (chunk == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t section_end = 0u;
    NMO_RETURN_IF_ERROR(workspace_edit_seek_message_manager_identifier(
        chunk, &section_end));

    int32_t count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    if (count < 0) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (count == 0) {
        return NMO_OK;
    }
    if ((size_t)count > section_end - nmo_chunk_get_position(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    if ((size_t)count > SIZE_MAX / sizeof(const char *)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    const char **names = (const char **)nmo_arena_alloc(
        arena, (size_t)count * sizeof(*names), _Alignof(const char *));
    if (names == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(names, 0, (size_t)count * sizeof(*names));
    for (int32_t i = 0; i < count; ++i) {
        char *entry_name = NULL;
        size_t entry_length = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
            chunk, &entry_name, &entry_length));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        (void)entry_length;
        names[i] = nmo_arena_strdup(arena, entry_name ? entry_name : "");
        if (names[i] == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    *out_names = names;
    *out_count = (uint32_t)count;
    return NMO_OK;
}
