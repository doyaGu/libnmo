/**
 * @file workspace_edit_object.c
 * @brief Object edits: create, fields, rename, script binding.
 */

#include "object/nmo_object_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "type/nmo_reflection.h"

#include "workspace_edit_internal.h"

typedef struct rename_object_action {
    nmo_object_id_t id;
    const char *name;
} rename_object_action_t;

static nmo_status_t rollback_rename_object(nmo_workspace_edit_t *edit, void *payload)
{
    rename_object_action_t *action = (rename_object_action_t *)payload;
    if (edit == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    return nmo_object_repository_rename(repo, action->id, action->name);
}

nmo_status_t nmo_object_edit_create(
    nmo_workspace_edit_t *edit,
    const nmo_object_create_desc_t *desc,
    nmo_object_id_t *out_object_id)
{
    if (out_object_id != NULL) {
        *out_object_id = 0;
    }
    if (edit == NULL || edit->finished || desc == NULL || out_object_id == NULL ||
        desc->class_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_id_t object_id = 0;
    nmo_status_t create_result =
        nmo_workspace_internal_create_object(
            edit->workspace,
            desc->class_id,
            desc->name,
            desc->type_guid,
            &object_id);
    if (create_result != NMO_OK) {
        return create_result;
    }

    nmo_status_t track_result =
        nmo_workspace_edit_track_created_object(edit, object_id);
    if (track_result != NMO_OK) {
        (void)nmo_workspace_internal_destroy_objects(
            edit->workspace,
            &object_id,
            1,
            NMO_RUNTIME_REQUEST_DEFAULT);
        return track_result;
    }

    *out_object_id = object_id;
    return NMO_OK;
}

nmo_status_t nmo_object_edit_bind_script(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t behavior_id)
{
    if (edit == NULL || edit->finished || object_id == 0u || behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry =
        nmo_workspace_internal_type_registry(edit->workspace);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *owner_object = nmo_object_repository_find_by_id(repo, object_id);
    nmo_object_t *behavior_object =
        nmo_object_repository_find_by_id(repo, behavior_id);
    if (owner_object == NULL || behavior_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, owner_object, NMO_CID_BEOBJECT) ||
        !workspace_edit_session_object_derives(registry, behavior_object, NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, owner_object, CKPGUID_BEOBJECT);
    nmo_behavior_state_t *behavior_state =
        (nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, behavior_object, CKPGUID_BEHAVIOR);
    if (owner_state == NULL || behavior_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    nmo_status_t status =
        nmo_workspace_edit_snapshot_behavior_state(edit, behavior_state);
    if (status != NMO_OK) {
        return workspace_edit_abort_arena_status(edit, checkpoint, status);
    }

    size_t existing_index = 0u;
    if (nmo_beobject_script_array_find(
            &owner_state->scripts, behavior_id, &existing_index) == 0) {
        status = nmo_beobject_script_array_append(
            &owner_state->scripts, behavior_id);
        if (status != NMO_OK) {
            return workspace_edit_abort_arena_status(edit, checkpoint, status);
        }

        array_id_action_t *rollback = NULL;
        status = workspace_edit_make_array_id_action(
            edit,
            &owner_state->scripts,
            behavior_id,
            owner_state->scripts.count - 1u,
            &rollback);
        if (status != NMO_OK) {
            return workspace_edit_abort_arena_status(edit, checkpoint, status);
        }
        status = workspace_edit_push_rollback_or_abort(
            edit, checkpoint, workspace_edit_rollback_remove_array_id, rollback);
        if (status != NMO_OK) {
            return workspace_edit_abort_arena_status(edit, checkpoint, status);
        }
    }

    behavior_state->flags |= CKBEHAVIOR_SCRIPT;
    behavior_state->flags &= ~(uint32_t)CKBEHAVIOR_BUILDINGBLOCK;
    const nmo_type_descriptor_t *owner_type =
        nmo_type_query_find_for_object(registry, owner_object);
    behavior_state->compatible_class_id = owner_type != NULL
        ? (int32_t)owner_type->class_id
        : (int32_t)nmo_object_get_class_id(owner_object);
    nmo_behavior_set_owner_id(behavior_state, object_id);

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}

nmo_status_t nmo_object_edit_set_fields(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_session_field_edit_t *fields,
    size_t field_count,
    nmo_session_field_edit_result_t *out_result)
{
    nmo_session_field_edit_result_t result = {0, 0};
    if (out_result != NULL) {
        *out_result = result;
    }
    if (edit == NULL || edit->finished || fields == NULL || field_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    void *state = nmo_object_get_state(object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_query_find_for_object(registry, object);
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].field_name == NULL || fields[i].value_str == NULL) {
            result.failed++;
            if (out_result != NULL) {
                *out_result = result;
            }
            return workspace_edit_abort_arena_status(
                edit, checkpoint, NMO_ERR_INVALID_ARGUMENT);
        }

        const nmo_type_field_t *field =
            nmo_type_get_field_by_name(type, fields[i].field_name);
        if (field == NULL) {
            result.failed++;
            if (out_result != NULL) {
                *out_result = result;
            }
            return workspace_edit_abort_arena_status(
                edit, checkpoint, NMO_ERR_NOT_FOUND);
        }

        void *field_ptr = (uint8_t *)state + field->offset;
        nmo_status_t snapshot_result =
            workspace_edit_push_bytes_snapshot_or_abort(
                edit, checkpoint, field_ptr, field->size);
        if (snapshot_result != NMO_OK) {
            result.failed++;
            if (out_result != NULL) {
                *out_result = result;
            }
            return workspace_edit_abort_arena_status(
                edit, checkpoint, snapshot_result);
        }

        nmo_status_t set_result =
            nmo_type_set_field(
                state, type, registry, fields[i].field_name, fields[i].value_str);
        if (set_result != NMO_OK) {
            result.failed++;
            if (out_result != NULL) {
                *out_result = result;
            }
            return workspace_edit_abort_arena_status(
                edit, checkpoint, set_result);
        }

        result.applied++;
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        if (nmo_field_is_reference(field)) {
            nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
        }
        if (strcmp(field->name, "name") == 0) {
            nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_NAMES);
        }
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}

nmo_status_t nmo_object_edit_rename(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    const char *old_name = nmo_object_get_name(object);
    const char *old_name_copy = NULL;
    if (old_name != NULL) {
        size_t old_name_len = strlen(old_name) + 1u;
        char *copy = (char *)nmo_workspace_edit_alloc(edit, old_name_len, 1);
        if (copy == NULL) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, NMO_ERR_NOMEM);
        }
        memcpy(copy, old_name, old_name_len);
        old_name_copy = copy;
    }

    rename_object_action_t *rollback =
        (rename_object_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*rollback), _Alignof(rename_object_action_t));
    if (rollback == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_NOMEM);
    }
    rollback->id = object_id;
    rollback->name = old_name_copy;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_rename_object, rollback);
    if (push_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, push_result);
    }

    nmo_status_t rename_result =
        nmo_object_repository_rename(repo, object_id, new_name);
    if (rename_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, rename_result);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_NAMES);
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}
