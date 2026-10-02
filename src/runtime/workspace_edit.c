/**
 * @file workspace_edit.c
 * @brief Workspace edit transactions: begin, snapshots, journal actions.
 */

#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "format/nmo_interface_chunk.h"

#include <stdlib.h>

#include "workspace_edit_internal.h"

typedef struct destroy_objects_action {
    nmo_object_id_t *ids;
    size_t count;
    uint32_t flags;
} destroy_objects_action_t;

typedef struct object_chunk_snapshot {
    nmo_object_id_t id;
    nmo_chunk_t *chunk;
} object_chunk_snapshot_t;

typedef struct behavior_state_snapshot {
    nmo_behavior_state_t *target;
    nmo_behavior_state_t state;
    bool owns_state;
} behavior_state_snapshot_t;

static void workspace_edit_free(nmo_workspace_edit_t *edit)
{
    if (edit == NULL) {
        return;
    }
    if (edit->owns_active_edit) {
        nmo_workspace_internal_release_edit(edit->workspace);
        edit->owns_active_edit = false;
    }
    nmo_workspace_edit_journal_dispose(&edit->journal);
    free(edit->label);
    free(edit);
}

static void workspace_edit_finish(nmo_workspace_edit_t *edit)
{
    if (edit == NULL) {
        return;
    }
    edit->finished = true;
    nmo_workspace_edit_journal_cleanup_all(&edit->journal, edit);
    workspace_edit_free(edit);
}

static nmo_status_t workspace_edit_finish_status(
    nmo_workspace_edit_t *edit,
    nmo_status_t status)
{
    workspace_edit_finish(edit);
    return status;
}

static nmo_status_t workspace_edit_finish_rollback_status(
    nmo_workspace_edit_t *edit,
    nmo_status_t status)
{
    nmo_workspace_edit_journal_rollback_all(&edit->journal, edit);
    return workspace_edit_finish_status(edit, status);
}

static nmo_status_t workspace_edit_run_commit_actions(nmo_workspace_edit_t *edit)
{
    return edit != NULL
        ? nmo_workspace_edit_journal_run_commit(&edit->journal, edit)
        : NMO_ERR_INVALID_ARGUMENT;
}

static void workspace_edit_zero_behavior_arrays(nmo_behavior_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(&state->sub_behaviors, 0, sizeof(state->sub_behaviors));
    memset(&state->sub_behavior_links, 0, sizeof(state->sub_behavior_links));
    memset(&state->operations, 0, sizeof(state->operations));
    memset(&state->in_parameters, 0, sizeof(state->in_parameters));
    memset(&state->out_parameters, 0, sizeof(state->out_parameters));
    memset(&state->local_parameters, 0, sizeof(state->local_parameters));
    memset(&state->inputs, 0, sizeof(state->inputs));
    memset(&state->outputs, 0, sizeof(state->outputs));
}

static void workspace_edit_dispose_behavior_arrays(nmo_behavior_state_t *state)
{
    if (state == NULL) {
        return;
    }
    nmo_array_dispose(&state->sub_behaviors);
    nmo_array_dispose(&state->sub_behavior_links);
    nmo_array_dispose(&state->operations);
    nmo_array_dispose(&state->in_parameters);
    nmo_array_dispose(&state->out_parameters);
    nmo_array_dispose(&state->local_parameters);
    nmo_array_dispose(&state->inputs);
    nmo_array_dispose(&state->outputs);
}

static nmo_status_t workspace_edit_clone_behavior_ref_array(
    nmo_workspace_edit_t *edit,
    const nmo_array_t *source,
    nmo_array_t *destination)
{
    if (edit == NULL || source == NULL || destination == NULL ||
        source->element_size != sizeof(nmo_behavior_ref_t) ||
        source->count > source->capacity ||
        (source->count > 0u && source->data == NULL)) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t status = nmo_array_init(
        destination,
        sizeof(nmo_behavior_ref_t),
        source->count,
        &source->allocator);
    if (status != NMO_OK) {
        return status;
    }
    nmo_array_set_lifecycle(destination, &source->lifecycle);

    nmo_behavior_ref_t *destination_refs = NULL;
    status = nmo_array_extend(
        destination, source->count, (void **)&destination_refs);
    if (status != NMO_OK) {
        nmo_array_dispose(destination);
        return status;
    }

    const nmo_behavior_ref_t *source_refs =
        NMO_ARRAY_DATA(nmo_behavior_ref_t, source);
    nmo_arena_t *arena =
        nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        nmo_array_dispose(destination);
        return NMO_ERR_INVALID_STATE;
    }
    for (size_t i = 0u; i < source->count; ++i) {
        destination_refs[i].ref = source_refs[i].ref;
        if (source_refs[i].chunk != NULL) {
            destination_refs[i].chunk =
                nmo_chunk_clone(source_refs[i].chunk, arena);
            if (destination_refs[i].chunk == NULL) {
                nmo_array_dispose(destination);
                return NMO_ERR_NOMEM;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t workspace_edit_clone_behavior_state(
    nmo_workspace_edit_t *edit,
    const nmo_behavior_state_t *source,
    nmo_behavior_state_t *destination)
{
    if (edit == NULL || source == NULL || destination == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *destination = *source;
    workspace_edit_zero_behavior_arrays(destination);

    const nmo_array_t *source_arrays[] = {
        &source->sub_behaviors,
        &source->sub_behavior_links,
        &source->operations,
        &source->in_parameters,
        &source->out_parameters,
        &source->local_parameters,
        &source->inputs,
        &source->outputs,
    };
    nmo_array_t *destination_arrays[] = {
        &destination->sub_behaviors,
        &destination->sub_behavior_links,
        &destination->operations,
        &destination->in_parameters,
        &destination->out_parameters,
        &destination->local_parameters,
        &destination->inputs,
        &destination->outputs,
    };

    for (size_t i = 0u;
         i < sizeof(source_arrays) / sizeof(source_arrays[0]);
         ++i) {
        nmo_status_t status = workspace_edit_clone_behavior_ref_array(
            edit, source_arrays[i], destination_arrays[i]);
        if (status != NMO_OK) {
            workspace_edit_dispose_behavior_arrays(destination);
            return status;
        }
    }

    /* The editor layout is edited in place by the interface commands and by the
       canonicalization policy, so the struct copy above (which shares the pointer)
       would restore the edited data. The copy lives in the journal arena and is
       copied back to the document arena only if the edit is rolled back. */
    destination->interface_data = NULL;
    if (source->interface_data != NULL) {
        nmo_status_t status = nmo_interface_data_copy(
            edit->journal.arena, &destination->interface_data, source->interface_data);
        if (status != NMO_OK) {
            workspace_edit_dispose_behavior_arrays(destination);
            return status;
        }
    }
    return NMO_OK;
}

static nmo_status_t rollback_behavior_state(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    behavior_state_snapshot_t *snapshot =
        (behavior_state_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->target == NULL ||
        !snapshot->owns_state) {
        return NMO_ERR_INVALID_STATE;
    }

    /* The snapshot's interface data dies with the journal; the restored state needs a
       copy that lives as long as the document. Without one the rest is still restored
       and the interface keeps its edited form. */
    nmo_status_t status = NMO_OK;
    nmo_interface_data_t *restored_interface = NULL;
    nmo_arena_t *document_arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (snapshot->state.interface_data != NULL) {
        status = document_arena != NULL
            ? nmo_interface_data_copy(document_arena, &restored_interface,
                                      snapshot->state.interface_data)
            : NMO_ERR_INVALID_STATE;
        if (status != NMO_OK) {
            restored_interface = snapshot->target->interface_data;
        }
    }

    workspace_edit_dispose_behavior_arrays(snapshot->target);
    *snapshot->target = snapshot->state;
    snapshot->target->interface_data = snapshot->state.interface_data != NULL
        ? restored_interface : NULL;
    memset(&snapshot->state, 0, sizeof(snapshot->state));
    snapshot->owns_state = false;
    return status;
}

static nmo_status_t cleanup_behavior_state_snapshot(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    (void)edit;
    behavior_state_snapshot_t *snapshot =
        (behavior_state_snapshot_t *)payload;
    if (snapshot == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (snapshot->owns_state) {
        workspace_edit_dispose_behavior_arrays(&snapshot->state);
        snapshot->owns_state = false;
    }
    return NMO_OK;
}

static nmo_status_t commit_destroy_objects(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    destroy_objects_action_t *action = (destroy_objects_action_t *)payload;
    if (edit == NULL || action == NULL || action->ids == NULL ||
        action->count == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_workspace_internal_destroy_objects(
        edit->workspace, action->ids, action->count, action->flags);
}

static nmo_status_t rollback_object_chunk(nmo_workspace_edit_t *edit, void *payload)
{
    object_chunk_snapshot_t *snapshot = (object_chunk_snapshot_t *)payload;
    if (edit == NULL || snapshot == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, snapshot->id);
    if (object == NULL) {
        return NMO_OK;
    }
    return nmo_object_set_chunk(object, snapshot->chunk);
}

static nmo_status_t workspace_edit_begin_for_workspace(
    nmo_workspace_t *workspace,
    const char *label,
    nmo_workspace_edit_t **out_edit)
{
    if (workspace == NULL || out_edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_edit = NULL;
    if (nmo_document_internal_is_partial_load(nmo_workspace_get_document(workspace))) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_workspace_edit_t *edit = (nmo_workspace_edit_t *)calloc(1, sizeof(*edit));
    if (edit == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_status_t journal_result =
        nmo_workspace_edit_journal_init(&edit->journal);
    if (journal_result != NMO_OK) {
        free(edit);
        return journal_result;
    }
    edit->workspace = workspace;

    if (label != NULL) {
        size_t len = strlen(label);
        edit->label = (char *)malloc(len + 1u);
        if (edit->label == NULL) {
            workspace_edit_free(edit);
            return NMO_ERR_NOMEM;
        }
        memcpy(edit->label, label, len + 1u);
    }

    nmo_status_t acquire_result =
        nmo_workspace_internal_acquire_edit(workspace);
    if (acquire_result != NMO_OK) {
        workspace_edit_free(edit);
        return acquire_result;
    }
    edit->owns_active_edit = true;

    *out_edit = edit;
    return NMO_OK;
}

nmo_status_t nmo_workspace_edit_begin(
    nmo_workspace_t *workspace,
    const char *label,
    nmo_workspace_edit_t **out_edit)
{
    return workspace_edit_begin_for_workspace(
        workspace,
        label,
        out_edit);
}

nmo_status_t nmo_workspace_edit_commit(nmo_workspace_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if ((edit->flags & ~NMO_WORKSPACE_EDIT_KNOWN_FLAGS) != 0u) {
        return workspace_edit_finish_rollback_status(
            edit, NMO_ERR_INVALID_ARGUMENT);
    }

    nmo_status_t action_result = workspace_edit_run_commit_actions(edit);
    if (action_result != NMO_OK) {
        return workspace_edit_finish_rollback_status(edit, action_result);
    }

    nmo_status_t apply_result =
        nmo_workspace_apply_edit_flags(edit->workspace, edit->flags);
    return workspace_edit_finish_status(edit, apply_result);
}

void nmo_workspace_edit_rollback(nmo_workspace_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return;
    }
    (void)workspace_edit_finish_rollback_status(edit, NMO_OK);
}

void *nmo_workspace_edit_alloc(
    nmo_workspace_edit_t *edit,
    size_t size,
    size_t align)
{
    if (edit == NULL || edit->finished || size == 0u) {
        return NULL;
    }
    return nmo_workspace_edit_journal_alloc(&edit->journal, size, align);
}

nmo_status_t nmo_workspace_edit_snapshot_bytes(
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size)
{
    if (edit == NULL || edit->finished || target == NULL || size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return workspace_edit_push_bytes_snapshot(edit, target, size);
}

nmo_status_t nmo_workspace_edit_snapshot_behavior_state(
    nmo_workspace_edit_t *edit,
    nmo_behavior_state_t *state)
{
    if (edit == NULL || edit->finished || state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior_state_snapshot_t *snapshot =
        (behavior_state_snapshot_t *)nmo_workspace_edit_alloc(
            edit,
            sizeof(*snapshot),
            _Alignof(behavior_state_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->target = state;

    nmo_status_t status = workspace_edit_clone_behavior_state(
        edit, state, &snapshot->state);
    if (status != NMO_OK) {
        return status;
    }
    snapshot->owns_state = true;

    return nmo_workspace_edit_journal_push_rollback_cleanup(
        &edit->journal,
        edit,
        rollback_behavior_state,
        cleanup_behavior_state_snapshot,
        snapshot);
}

nmo_status_t nmo_workspace_edit_track_created_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id)
{
    if (edit == NULL || edit->finished || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_object_repository_find_by_id(repo, object_id) == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    object_id_action_t *rollback = NULL;
    nmo_status_t action_result =
        workspace_edit_make_object_id_action(edit, object_id, &rollback);
    if (action_result != NMO_OK) {
        return action_result;
    }

    nmo_status_t push_result =
        workspace_edit_push_rollback(edit, workspace_edit_action_remove_object, rollback);
    if (push_result != NMO_OK) {
        return push_result;
    }
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
        NMO_WORKSPACE_EDIT_REFERENCES |
        NMO_WORKSPACE_EDIT_NAMES);
    return NMO_OK;
}

nmo_status_t nmo_workspace_edit_defer_destroy_objects(
    nmo_workspace_edit_t *edit,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags)
{
    if (edit == NULL || edit->finished || object_ids == NULL ||
        object_count == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    destroy_objects_action_t *action =
        (destroy_objects_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*action), _Alignof(destroy_objects_action_t));
    if (action == NULL) {
        return NMO_ERR_NOMEM;
    }
    action->ids = (nmo_object_id_t *)nmo_workspace_edit_alloc(
        edit,
        object_count * sizeof(*action->ids),
        _Alignof(nmo_object_id_t));
    if (action->ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(action->ids, object_ids, object_count * sizeof(*action->ids));
    action->count = object_count;
    action->flags = flags;
    return workspace_edit_push_commit(edit, commit_destroy_objects, action);
}

nmo_status_t nmo_workspace_edit_snapshot_object_chunk(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id)
{
    if (edit == NULL || edit->finished || object_id == 0) {
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

    nmo_chunk_t *current = nmo_object_get_chunk(object);
    nmo_chunk_t *snapshot_chunk = NULL;
    if (current != NULL) {
        snapshot_chunk = nmo_chunk_clone(
            current, nmo_workspace_internal_document_arena(edit->workspace));
        if (snapshot_chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    object_chunk_snapshot_t *snapshot =
        (object_chunk_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(object_chunk_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->id = object_id;
    snapshot->chunk = snapshot_chunk;

    nmo_status_t push_result =
        workspace_edit_push_rollback(edit, rollback_object_chunk, snapshot);
    if (push_result != NMO_OK) {
        return push_result;
    }
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
        NMO_WORKSPACE_EDIT_REFERENCES |
        NMO_WORKSPACE_EDIT_RESOURCES);
    return NMO_OK;
}

void nmo_workspace_edit_mark(nmo_workspace_edit_t *edit, uint32_t flags)
{
    if (edit != NULL) {
        edit->flags |= flags;
    }
}

nmo_status_t nmo_workspace_apply_edit_flags(nmo_workspace_t *workspace, uint32_t flags)
{
    if (workspace == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_document_t *document = nmo_workspace_get_document(workspace);
    return document != NULL
        ? nmo_document_internal_apply_edit_flags(document, flags)
        : NMO_ERR_INVALID_STATE;
}
