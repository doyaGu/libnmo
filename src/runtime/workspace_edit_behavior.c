/**
 * @file workspace_edit_behavior.c
 * @brief Behavior graph edits: links and interface marking.
 */

#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_statesave_ids.h"

#include "workspace_edit_internal.h"

static nmo_status_t workspace_edit_push_commit_or_abort(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    nmo_status_t status = workspace_edit_push_commit(edit, fn, payload);
    if (status != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, status);
    }
    return NMO_OK;
}

static nmo_status_t rollback_insert_array_id(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    array_id_action_t *action = (array_id_action_t *)payload;
    if (action == NULL || action->array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (action->array->element_size == sizeof(nmo_behavior_ref_t)) {
        nmo_behavior_ref_t value = nmo_behavior_ref_from_id(action->id);
        if (action->index <= action->array->count) {
            return nmo_array_insert(action->array, action->index, &value);
        }
        return nmo_array_append(action->array, &value);
    }
    if (action->array->element_size == sizeof(nmo_ref_t)) {
        nmo_ref_t value = nmo_ref_from_id(action->id);
        if (action->index <= action->array->count) {
            return nmo_array_insert(action->array, action->index, &value);
        }
        return nmo_array_append(action->array, &value);
    }
    if (action->index <= action->array->count) {
        return nmo_array_insert(action->array, action->index, &action->id);
    }
    return nmo_array_append(action->array, &action->id);
}

static nmo_status_t commit_destroy_object(nmo_workspace_edit_t *edit, void *payload)
{
    object_id_action_t *action = (object_id_action_t *)payload;
    if (edit == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_internal_destroy_objects(
        edit->workspace,
        &action->id,
        1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION);
}

nmo_status_t nmo_behavior_edit_add_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id)
{
    if (out_link_id != NULL) {
        *out_link_id = 0;
    }
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (parent_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, parent_obj, NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_object_repository_find_by_id(repo, from_io_id) == NULL ||
        nmo_object_repository_find_by_id(repo, to_io_id) == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    nmo_object_id_t link_id = 0;
    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_CREATE;
    request.flags = NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION;
    request.payload.create.class_id = NMO_CID_BEHAVIORLINK;
    request.payload.create.type_guid = (nmo_guid_t){0, 0};
    request.payload.create.out_created_id = &link_id;
    nmo_status_t create_result =
        nmo_workspace_internal_execute_runtime_request(edit->workspace, &request, NULL);
    if (create_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, create_result);
    }

    object_id_action_t *object_action = NULL;
    nmo_status_t object_action_result =
        workspace_edit_make_object_id_action(edit, link_id, &object_action);
    if (object_action_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return workspace_edit_abort_arena_status(
            edit, checkpoint, object_action_result);
    }
    nmo_status_t push_object_result =
        workspace_edit_push_rollback(edit, workspace_edit_action_remove_object, object_action);
    if (push_object_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return workspace_edit_abort_arena_status(
            edit, checkpoint, push_object_result);
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_INTERNAL);
    }
    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (link_state == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_INTERNAL);
    }
    nmo_behaviorlink_set_in_io_id(link_state, to_io_id);
    nmo_behaviorlink_set_out_io_id(link_state, from_io_id);
    link_state->activation_delay = activation_delay;
    link_state->initial_activation_delay = activation_delay;
    link_state->use_new_format = true;
    link_state->has_format = true;

    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, parent_obj, CKPGUID_BEHAVIOR);
    if (parent_state == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_INTERNAL);
    }
    nmo_status_t append_result =
        nmo_behavior_ref_array_append(
            &parent_state->sub_behavior_links, link_id, NULL);
    if (append_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, append_result);
    }

    array_id_action_t *array_action = NULL;
    nmo_status_t array_action_result =
        workspace_edit_make_array_id_action(
            edit,
            &parent_state->sub_behavior_links,
            link_id,
            parent_state->sub_behavior_links.count - 1u,
            &array_action);
    if (array_action_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, array_action_result);
    }
    nmo_status_t push_array_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, workspace_edit_rollback_remove_array_id, array_action);
    if (push_array_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, push_array_result);
    }

    parent_state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    parent_state->has_save_flags = true;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_REFERENCES);
    nmo_status_t release_result =
        workspace_edit_checkpoint_release_arena(edit, &checkpoint);
    if (release_result == NMO_OK && out_link_id != NULL) {
        *out_link_id = link_id;
    }
    return release_result;
}

nmo_status_t nmo_behavior_edit_remove_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, link_obj, NMO_CID_BEHAVIORLINK)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (parent_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, parent_obj, NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, parent_obj, CKPGUID_BEHAVIOR);
    if (parent_state == NULL) {
        return NMO_ERR_INTERNAL;
    }

    size_t index = 0;
    if (!nmo_behavior_ref_array_find(
            &parent_state->sub_behavior_links, link_id, &index)) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    array_id_action_t *array_action = NULL;
    nmo_status_t array_action_result =
        workspace_edit_make_array_id_action(
            edit,
            &parent_state->sub_behavior_links,
            link_id,
            index,
            &array_action);
    if (array_action_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, array_action_result);
    }
    object_id_action_t *object_action = NULL;
    nmo_status_t object_action_result =
        workspace_edit_make_object_id_action(edit, link_id, &object_action);
    if (object_action_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, object_action_result);
    }

    nmo_status_t remove_result =
        nmo_array_remove(&parent_state->sub_behavior_links, index, NULL);
    if (remove_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, remove_result);
    }
    if (parent_state->sub_behavior_links.count == 0) {
        parent_state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
    } else {
        parent_state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    }
    parent_state->has_save_flags = true;

    nmo_status_t rollback_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_insert_array_id, array_action);
    if (rollback_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, rollback_result);
    }
    nmo_status_t commit_result =
        workspace_edit_push_commit_or_abort(
            edit, checkpoint, commit_destroy_object, object_action);
    if (commit_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, commit_result);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_REFERENCES);
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}

nmo_status_t nmo_behavior_edit_mark_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    if (behavior_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!workspace_edit_session_object_derives(registry, behavior_obj, NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry, behavior_obj, CKPGUID_BEHAVIOR);
    if (state == NULL || state->interface_data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
        NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
        NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
