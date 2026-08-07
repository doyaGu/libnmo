/**
 * @file workspace_edit.c
 * @brief Workspace-scoped edit transaction implementation.
 */

#include "runtime/nmo_workspace.h"
#include "object/nmo_animation_edit.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_entity_edit.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_scene_edit.h"
#include "object/nmo_sound_edit.h"
#include "behavior/nmo_behavior_edit.h"

#include "runtime_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_3dobject_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_object.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_serialize_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_status_t (*nmo_workspace_edit_action_fn)(nmo_workspace_edit_t *edit, void *payload);

typedef struct nmo_workspace_edit_action {
    nmo_workspace_edit_action_fn fn;
    void *payload;
} nmo_workspace_edit_action_t;

typedef struct workspace_edit_checkpoint {
    size_t rollback_count;
    size_t commit_count;
} workspace_edit_checkpoint_t;

struct nmo_workspace_edit {
    nmo_workspace_t *workspace;
    nmo_arena_t *arena;
    char *label;
    nmo_workspace_edit_action_t *rollback_actions;
    size_t rollback_count;
    size_t rollback_capacity;
    nmo_workspace_edit_action_t *commit_actions;
    size_t commit_count;
    size_t commit_capacity;
    nmo_workspace_edit_action_t *cleanup_actions;
    size_t cleanup_count;
    size_t cleanup_capacity;
    uint32_t flags;
    bool finished;
};

typedef struct field_bytes_snapshot {
    void *field_ptr;
    size_t size;
    uint8_t bytes[];
} field_bytes_snapshot_t;

typedef struct parameter_buffer_snapshot {
    nmo_parameter_state_t *state;
    size_t count;
    uint8_t bytes[];
} parameter_buffer_snapshot_t;

typedef struct parameter_manager_snapshot {
    nmo_parameter_state_t *state;
    nmo_guid_t manager_guid;
    uint32_t manager_value;
} parameter_manager_snapshot_t;

typedef struct dataarray_cell_snapshot {
    nmo_dataarray_cell_t *cell;
    nmo_dataarray_cell_t old_cell;
} dataarray_cell_snapshot_t;

typedef struct scene_object_descs_snapshot {
    nmo_array_t *object_descs;
    size_t previous_count;
    nmo_object_id_t object_id;
} scene_object_descs_snapshot_t;

typedef struct array_id_action {
    nmo_array_t *array;
    nmo_object_id_t id;
    size_t index;
} array_id_action_t;

typedef struct object_id_action {
    nmo_object_id_t id;
} object_id_action_t;

typedef struct object_chunk_snapshot {
    nmo_object_id_t id;
    nmo_chunk_t *chunk;
} object_chunk_snapshot_t;

typedef struct rename_object_action {
    nmo_object_id_t id;
    const char *name;
} rename_object_action_t;

typedef struct manager_data_snapshot {
    nmo_session_t *session;
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;
} manager_data_snapshot_t;

typedef struct behavior_state_snapshot {
    nmo_behavior_state_t *target;
    nmo_behavior_state_t state;
    bool owns_state;
} behavior_state_snapshot_t;

static uint32_t workspace_edit_pack_argb(float r, float g, float b, float a);
static nmo_status_t parse_object_id_text(
    const char *value_str,
    nmo_object_id_t *out_id);
static nmo_status_t workspace_edit_prepare_manager_parameter_value(
    nmo_workspace_edit_t *edit,
    const nmo_parameter_state_t *state,
    const char *value_str,
    const nmo_parameter_write_options_t *options,
    nmo_guid_t *out_guid,
    uint32_t *out_value);
static nmo_status_t workspace_edit_read_message_manager_names(
    nmo_session_t *session,
    const nmo_manager_data_t *manager,
    const char ***out_names,
    uint32_t *out_count);
static nmo_status_t workspace_edit_seek_message_manager_identifier(
    nmo_chunk_t *chunk);
static void workspace_edit_free(nmo_workspace_edit_t *edit)
{
    if (edit == NULL) {
        return;
    }
    free(edit->rollback_actions);
    free(edit->commit_actions);
    free(edit->cleanup_actions);
    free(edit->label);
    if (edit->arena != NULL) {
        nmo_arena_destroy(edit->arena);
    }
    free(edit);
}

static nmo_status_t workspace_edit_push_action(
    nmo_workspace_edit_action_t **actions,
    size_t *count,
    size_t *capacity,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    if (actions == NULL || count == NULL || capacity == NULL || fn == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 8u : (*capacity * 2u);
        nmo_workspace_edit_action_t *new_actions =
            (nmo_workspace_edit_action_t *)realloc(
                *actions, new_capacity * sizeof(nmo_workspace_edit_action_t));
        if (new_actions == NULL) {
            return NMO_ERR_NOMEM;
        }
        *actions = new_actions;
        *capacity = new_capacity;
    }

    (*actions)[*count].fn = fn;
    (*actions)[*count].payload = payload;
    (*count)++;
    return NMO_OK;
}

static nmo_status_t workspace_edit_push_rollback(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return workspace_edit_push_action(
        &edit->rollback_actions,
        &edit->rollback_count,
        &edit->rollback_capacity,
        fn,
        payload);
}

static nmo_status_t workspace_edit_push_commit(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return workspace_edit_push_action(
        &edit->commit_actions,
        &edit->commit_count,
        &edit->commit_capacity,
        fn,
        payload);
}

static nmo_status_t workspace_edit_push_cleanup(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return workspace_edit_push_action(
        &edit->cleanup_actions,
        &edit->cleanup_count,
        &edit->cleanup_capacity,
        fn,
        payload);
}

static void workspace_edit_rollback_to(nmo_workspace_edit_t *edit, size_t checkpoint)
{
    if (edit == NULL) {
        return;
    }
    while (edit->rollback_count > checkpoint) {
        edit->rollback_count--;
        nmo_workspace_edit_action_t action = edit->rollback_actions[edit->rollback_count];
        (void)action.fn(edit, action.payload);
    }
}

static workspace_edit_checkpoint_t workspace_edit_checkpoint(
    const nmo_workspace_edit_t *edit)
{
    return (workspace_edit_checkpoint_t){
        edit != NULL ? edit->rollback_count : 0u,
        edit != NULL ? edit->commit_count : 0u,
    };
}

static void workspace_edit_abort_to(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint)
{
    if (edit == NULL) {
        return;
    }
    workspace_edit_rollback_to(edit, checkpoint.rollback_count);
    if (edit->commit_count > checkpoint.commit_count) {
        edit->commit_count = checkpoint.commit_count;
    }
}

static nmo_status_t workspace_edit_abort_status(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_status_t status)
{
    workspace_edit_abort_to(edit, checkpoint);
    return status;
}

static nmo_status_t workspace_edit_push_rollback_or_abort(
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

static void workspace_edit_finish(nmo_workspace_edit_t *edit)
{
    if (edit == NULL) {
        return;
    }
    edit->finished = true;
    while (edit->cleanup_count > 0u) {
        edit->cleanup_count--;
        nmo_workspace_edit_action_t action =
            edit->cleanup_actions[edit->cleanup_count];
        (void)action.fn(edit, action.payload);
    }
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
    workspace_edit_rollback_to(edit, 0);
    return workspace_edit_finish_status(edit, status);
}

static nmo_status_t workspace_edit_run_commit_actions(nmo_workspace_edit_t *edit)
{
    if (edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < edit->commit_count; i++) {
        nmo_status_t status =
            edit->commit_actions[i].fn(edit, edit->commit_actions[i].payload);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}

static nmo_status_t rollback_field_bytes(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    field_bytes_snapshot_t *snapshot = (field_bytes_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->field_ptr == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(snapshot->field_ptr, snapshot->bytes, snapshot->size);
    return NMO_OK;
}

static nmo_status_t workspace_edit_push_bytes_snapshot(
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size)
{
    if (edit == NULL || target == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    field_bytes_snapshot_t *snapshot =
        (field_bytes_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot) + size, _Alignof(field_bytes_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->field_ptr = target;
    snapshot->size = size;
    if (size > 0) {
        memcpy(snapshot->bytes, target, size);
    }

    return workspace_edit_push_rollback(edit, rollback_field_bytes, snapshot);
}

static nmo_status_t workspace_edit_push_bytes_snapshot_or_abort(
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
    return NMO_OK;
}

static nmo_status_t rollback_behavior_state(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    (void)edit;
    behavior_state_snapshot_t *snapshot =
        (behavior_state_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->target == NULL ||
        !snapshot->owns_state) {
        return NMO_ERR_INVALID_STATE;
    }

    workspace_edit_dispose_behavior_arrays(snapshot->target);
    *snapshot->target = snapshot->state;
    memset(&snapshot->state, 0, sizeof(snapshot->state));
    snapshot->owns_state = false;
    return NMO_OK;
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

static nmo_status_t rollback_parameter_buffer(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    parameter_buffer_snapshot_t *snapshot = (parameter_buffer_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_array_t *buffer = &snapshot->state->buffer_data;
    if (buffer->element_size != sizeof(uint8_t) || buffer->count != snapshot->count) {
        nmo_array_dispose(buffer);
        nmo_status_t alloc_result =
            nmo_array_alloc(buffer, sizeof(uint8_t), snapshot->count, NULL);
        if (alloc_result != NMO_OK) {
            return alloc_result;
        }
    }
    if (snapshot->count > 0 && buffer->data != NULL) {
        memcpy(buffer->data, snapshot->bytes, snapshot->count);
    }
    return NMO_OK;
}

static nmo_status_t rollback_parameter_manager(nmo_workspace_edit_t *edit,
                                               void *payload)
{
    (void)edit;
    parameter_manager_snapshot_t *snapshot =
        (parameter_manager_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    snapshot->state->manager_guid = snapshot->manager_guid;
    snapshot->state->manager_value = snapshot->manager_value;
    return NMO_OK;
}

static nmo_status_t rollback_dataarray_cell(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    dataarray_cell_snapshot_t *snapshot = (dataarray_cell_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->cell == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *snapshot->cell = snapshot->old_cell;
    return NMO_OK;
}

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

static nmo_status_t rollback_remove_array_id(nmo_workspace_edit_t *edit, void *payload)
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

static nmo_status_t action_remove_object(nmo_workspace_edit_t *edit, void *payload)
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

static nmo_status_t workspace_edit_make_object_id_action(
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

static nmo_status_t workspace_edit_make_array_id_action(
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

static const nmo_type_registry_t *workspace_edit_type_registry(
    const nmo_workspace_edit_t *edit)
{
    return edit != NULL
        ? nmo_workspace_internal_type_registry(edit->workspace)
        : NULL;
}

static bool session_object_derives(
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

static void *workspace_edit_object_state(
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

static bool session_is_parameter_reference_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object)
{
    const nmo_class_id_t classes[] = {
        NMO_CID_PARAMETER,
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETEROUT,
        NMO_CID_PARAMETERLOCAL,
        NMO_CID_PARAMETEROPERATION,
    };
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) {
        if (session_object_derives(registry, object, classes[i])) return true;
    }
    return false;
}

static nmo_parameter_state_t *workspace_edit_parameter_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        nmo_parameter_state_t *state =
            nmo_parameter_get_mutable_state(object);
        if (state != NULL) {
            return state;
        }
    }
    return (nmo_parameter_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_PARAMETER);
}

static nmo_status_t parse_dataarray_cell(
    nmo_dataarray_state_t *state,
    nmo_arena_t *arena,
    uint32_t row,
    uint32_t col,
    const char *value_str,
    nmo_dataarray_cell_t *out_cell,
    bool *out_is_ref)
{
    if (state == NULL || value_str == NULL || out_cell == NULL || out_is_ref == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (row >= state->row_count || col >= state->column_count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    nmo_dataarray_row_t *target_row = &state->rows[row];
    if (col >= target_row->column_count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    CK_ARRAYTYPE col_type = state->column_formats[col].type;
    nmo_dataarray_cell_t new_cell;
    memset(&new_cell, 0, sizeof(new_cell));
    *out_is_ref = false;

    switch (col_type) {
    case CKARRAYTYPE_INT: {
        int32_t value = 0;
        if (nmo_parse_i32_range_base(value_str, 0, INT32_MIN, INT32_MAX, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        new_cell.int_value = value;
        break;
    }
    case CKARRAYTYPE_FLOAT: {
        float value = 0.0f;
        if (nmo_parse_f32(value_str, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        new_cell.float_value = value;
        break;
    }
    case CKARRAYTYPE_STRING: {
        if (arena == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        size_t len = strlen(value_str);
        char *copy = (char *)nmo_arena_alloc(arena, len + 1u, 1u);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, value_str, len + 1u);
        new_cell.string_value = copy;
        break;
    }
    case CKARRAYTYPE_OBJECT:
    case CKARRAYTYPE_PARAMETER: {
        nmo_object_id_t value = 0;
        if (parse_object_id_text(value_str, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        if (col_type == CKARRAYTYPE_OBJECT) {
            new_cell.object_ref = nmo_ref_from_id(value);
        } else {
            new_cell.parameter.ref = nmo_ref_from_id(value);
        }
        *out_is_ref = true;
        break;
    }
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_cell = new_cell;
    return NMO_OK;
}

static nmo_status_t parse_object_id_text(const char *value_str, nmo_object_id_t *out_id)
{
    if (value_str == NULL || out_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *begin = value_str;
    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }
    if (strncmp(begin, "object:", strlen("object:")) == 0) {
        begin += strlen("object:");
    } else if (*begin == '#') {
        ++begin;
    }
    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }

    const char *end = begin + strlen(begin);
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    if (begin == end) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    char id_buf[64];
    size_t len = (size_t)(end - begin);
    if (len >= sizeof(id_buf)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(id_buf, begin, len);
    id_buf[len] = '\0';
    return nmo_parse_object_id(id_buf, out_id);
}

static nmo_status_t parse_manager_parameter_text(
    const char *value_str,
    nmo_guid_t *out_guid,
    uint32_t *out_value)
{
    if (value_str == NULL || out_guid == NULL || out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *separator = strchr(value_str, ':');
    if (separator == NULL) {
        separator = strchr(value_str, '=');
    }
    if (separator == NULL || separator == value_str ||
        separator[1] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    char guid_buf[32];
    const char *guid_begin = value_str;
    const char *guid_end = separator;
    while (guid_begin < guid_end && isspace((unsigned char)*guid_begin)) {
        ++guid_begin;
    }
    while (guid_end > guid_begin && isspace((unsigned char)guid_end[-1])) {
        --guid_end;
    }
    if ((size_t)(guid_end - guid_begin) > strlen("manager{}") &&
        strncmp(guid_begin, "manager{", strlen("manager{")) == 0 &&
        guid_end[-1] == '}') {
        guid_begin += strlen("manager{");
        --guid_end;
    } else if (guid_begin < guid_end && *guid_begin == '{' &&
               guid_end[-1] == '}') {
        ++guid_begin;
        --guid_end;
    }
    if (guid_begin >= guid_end) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t guid_len = (size_t)(guid_end - guid_begin);
    if (guid_len >= sizeof(guid_buf)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(guid_buf, guid_begin, guid_len);
    guid_buf[guid_len] = '\0';

    nmo_guid_t parsed_guid = nmo_guid_parse(guid_buf);
    if (nmo_guid_is_null(parsed_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *value_begin = separator + 1;
    while (*value_begin != '\0' && isspace((unsigned char)*value_begin)) {
        ++value_begin;
    }
    const char *value_end = value_begin + strlen(value_begin);
    while (value_end > value_begin && isspace((unsigned char)value_end[-1])) {
        --value_end;
    }
    if (value_begin == value_end) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char value_buf[64];
    size_t value_len = (size_t)(value_end - value_begin);
    if (value_len >= sizeof(value_buf)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(value_buf, value_begin, value_len);
    value_buf[value_len] = '\0';
    uint32_t value = 0;
    if (nmo_parse_u32_range_base(value_buf, 0, 0, UINT32_MAX, &value) != NMO_OK) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_guid = parsed_guid;
    *out_value = value;
    return NMO_OK;
}

static bool workspace_edit_has_manager_value_separator(const char *value_str)
{
    return value_str != NULL &&
           (strchr(value_str, ':') != NULL || strchr(value_str, '=') != NULL);
}

static nmo_status_t workspace_edit_find_message_manager_entry(
    nmo_session_t *session,
    const char *name_begin,
    size_t name_len,
    uint32_t *out_value)
{
    if (session == NULL || name_begin == NULL || name_len == 0u ||
        out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_file_state_t *file_state = nmo_session_get_file_state(session);
    if (file_state == NULL || file_state->manager_data == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            manager->chunk == NULL) {
            continue;
        }

        const char **names = NULL;
        uint32_t name_count = 0u;
        if (workspace_edit_read_message_manager_names(
                session, manager, &names, &name_count) != NMO_OK) {
            continue;
        }
        for (uint32_t index = 0; index < name_count; ++index) {
            const char *entry_name = names[index];
            if (entry_name != NULL &&
                strlen(entry_name) == name_len &&
                strncmp(entry_name, name_begin, name_len) == 0) {
                *out_value = index;
                return NMO_OK;
            }
        }
    }

    return NMO_ERR_NOT_FOUND;
}

static nmo_status_t workspace_edit_prepare_manager_parameter_value(
    nmo_workspace_edit_t *edit,
    const nmo_parameter_state_t *state,
    const char *value_str,
    const nmo_parameter_write_options_t *options,
    nmo_guid_t *out_guid,
    uint32_t *out_value)
{
    nmo_manager_entry_options_t manager_entry =
        options != NULL ? options->manager_entry
                        : nmo_manager_entry_options_default();
    const char *entry_text =
        manager_entry.key != NULL && manager_entry.key[0] != '\0'
            ? manager_entry.key
            : value_str;
    nmo_status_t explicit_result = NMO_ERR_INVALID_ARGUMENT;
    if (entry_text == value_str) {
        explicit_result = parse_manager_parameter_text(value_str, out_guid,
                                                      out_value);
    }
    if (explicit_result == NMO_OK ||
        (entry_text == value_str &&
         workspace_edit_has_manager_value_separator(value_str)) ||
        edit == NULL ||
        state == NULL ||
        (!nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_MESSAGE) &&
         !nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_ATTRIBUTE))) {
        return explicit_result;
    }

    const char *name_begin = entry_text;
    while (name_begin != NULL && *name_begin != '\0' &&
           isspace((unsigned char)*name_begin)) {
        ++name_begin;
    }
    const char *name_end = name_begin != NULL ? name_begin + strlen(name_begin) : NULL;
    while (name_end != NULL && name_end > name_begin &&
           isspace((unsigned char)name_end[-1])) {
        --name_end;
    }
    if (name_begin == NULL || name_begin == name_end) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
        if ((manager_entry.schema != NMO_MANAGER_ENTRY_SCHEMA_AUTO &&
             manager_entry.schema != NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE) ||
            (!nmo_guid_is_null(manager_entry.manager_guid) &&
             !nmo_guid_equals(manager_entry.manager_guid,
                              NMO_MANAGER_GUID_ATTRIBUTE))) {
            return NMO_ERR_NOT_SUPPORTED;
        }
        nmo_session_t *session =
            nmo_workspace_internal_session(edit->workspace);
        nmo_status_t status = NMO_ERR_NOT_FOUND;
        if (manager_entry.policy == NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING) {
            size_t name_len = (size_t)(name_end - name_begin);
            char *name_copy =
                (char *)nmo_workspace_edit_alloc(edit, name_len + 1u, 1u);
            if (name_copy == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(name_copy, name_begin, name_len);
            name_copy[name_len] = '\0';
            status = nmo_object_edit_ensure_attribute_manager_entry(
                edit, name_copy, &manager_entry.create, out_value);
        } else {
            nmo_manager_entry_create_options_t no_create = {0};
            char *name_copy = NULL;
            size_t name_len = (size_t)(name_end - name_begin);
            name_copy =
                (char *)nmo_workspace_edit_alloc(edit, name_len + 1u, 1u);
            if (name_copy == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(name_copy, name_begin, name_len);
            name_copy[name_len] = '\0';
            (void)session;
            status = nmo_object_edit_ensure_attribute_manager_entry(
                edit, name_copy, &no_create, out_value);
        }
        if (status != NMO_OK) {
            return status;
        }
        *out_guid = NMO_MANAGER_GUID_ATTRIBUTE;
        return NMO_OK;
    }

    if (manager_entry.schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE ||
        (!nmo_guid_is_null(manager_entry.manager_guid) &&
         !nmo_guid_equals(manager_entry.manager_guid, NMO_MANAGER_GUID_MESSAGE))) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    nmo_session_t *session = nmo_workspace_internal_session(edit->workspace);
    nmo_status_t status = NMO_ERR_NOT_FOUND;
    if (manager_entry.policy == NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING) {
        size_t name_len = (size_t)(name_end - name_begin);
        char *name_copy =
            (char *)nmo_workspace_edit_alloc(edit, name_len + 1u, 1u);
        if (name_copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(name_copy, name_begin, name_len);
        name_copy[name_len] = '\0';
        status = nmo_object_edit_ensure_message_manager_entry(
            edit, name_copy, out_value);
    } else {
        status = workspace_edit_find_message_manager_entry(
            session, name_begin, (size_t)(name_end - name_begin), out_value);
    }
    if (status != NMO_OK) {
        return status;
    }

    *out_guid = NMO_MANAGER_GUID_MESSAGE;
    return NMO_OK;
}

static nmo_status_t rollback_manager_data(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    manager_data_snapshot_t *snapshot = (manager_data_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_session_set_manager_data(
        snapshot->session,
        snapshot->manager_data,
        snapshot->manager_data_count);
    return NMO_OK;
}

static nmo_status_t workspace_edit_replace_manager_data(
    nmo_workspace_edit_t *edit,
    nmo_session_t *session,
    nmo_manager_data_t *old_manager_data,
    uint32_t old_manager_count,
    nmo_manager_data_t *new_manager_data,
    uint32_t new_manager_count)
{
    if (edit == NULL || session == NULL || new_manager_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager_data_snapshot_t *snapshot =
        (manager_data_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(manager_data_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->session = session;
    snapshot->manager_data = old_manager_data;
    snapshot->manager_data_count = old_manager_count;
    NMO_RETURN_IF_ERROR(workspace_edit_push_rollback(
        edit, rollback_manager_data, snapshot));

    nmo_session_set_manager_data(session, new_manager_data, new_manager_count);
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_RESOURCES);
    return NMO_OK;
}

static nmo_status_t workspace_edit_build_manager_data_update(
    nmo_arena_t *arena,
    const nmo_file_state_t *file_state,
    uint32_t *manager_index,
    nmo_guid_t manager_guid,
    nmo_chunk_t *new_chunk,
    nmo_manager_data_t **out_manager_data,
    uint32_t *out_manager_count)
{
    if (arena == NULL || file_state == NULL || manager_index == NULL ||
        new_chunk == NULL || out_manager_data == NULL ||
        out_manager_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t old_manager_count = file_state->manager_data_count;
    nmo_manager_data_t *old_manager_data = file_state->manager_data;
    uint32_t new_manager_count =
        *manager_index == UINT32_MAX ? old_manager_count + 1u : old_manager_count;
    nmo_manager_data_t *new_manager_data =
        (nmo_manager_data_t *)nmo_arena_alloc(
            arena,
            (size_t)new_manager_count * sizeof(*new_manager_data),
            _Alignof(nmo_manager_data_t));
    if (new_manager_data == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (old_manager_count > 0u && old_manager_data != NULL) {
        memcpy(new_manager_data, old_manager_data,
               (size_t)old_manager_count * sizeof(*new_manager_data));
    }
    if (*manager_index == UINT32_MAX) {
        *manager_index = old_manager_count;
        memset(&new_manager_data[*manager_index], 0,
               sizeof(new_manager_data[*manager_index]));
        new_manager_data[*manager_index].guid = manager_guid;
    }
    new_manager_data[*manager_index].chunk = new_chunk;
    new_manager_data[*manager_index].data_size =
        (uint32_t)nmo_chunk_get_size(new_chunk);
    new_manager_data[*manager_index].flags = 0u;

    *out_manager_data = new_manager_data;
    *out_manager_count = new_manager_count;
    return NMO_OK;
}

static nmo_status_t workspace_edit_seek_message_manager_identifier(
    nmo_chunk_t *chunk)
{
    if (chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_chunk_start_read(chunk) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_chunk_seek_identifier(chunk, 0x53u) == NMO_OK) {
        return NMO_OK;
    }

    if (nmo_chunk_start_read(chunk) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }
    uint32_t identifier = 0u;
    if (nmo_chunk_read_identifier(chunk, &identifier) != NMO_OK ||
        identifier != 0x53u) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
}

static nmo_status_t workspace_edit_seek_attribute_manager_identifier(
    nmo_chunk_t *chunk)
{
    if (chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_chunk_start_read(chunk) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_chunk_seek_identifier(chunk, 0x52u) == NMO_OK) {
        return NMO_OK;
    }
    if (nmo_chunk_start_read(chunk) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }
    uint32_t identifier = 0u;
    if (nmo_chunk_read_identifier(chunk, &identifier) != NMO_OK ||
        identifier != 0x52u) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
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
    edit->arena = nmo_arena_create(NULL, 16u * 1024u);
    if (edit->arena == NULL) {
        free(edit);
        return NMO_ERR_NOMEM;
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
        return session_object_derives(registry, scene_object, NMO_CID_SCENE)
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

    scene_object_descs_snapshot_t *snapshot =
        (scene_object_descs_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(scene_object_descs_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->object_descs = &scene_state->object_descs;
    snapshot->previous_count = existing_count;
    snapshot->object_id = object_id;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_scene_object_desc_append, snapshot);
    if (push_result != NMO_OK) {
        return push_result;
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
        return workspace_edit_abort_status(edit, checkpoint, append_result);
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
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
        return session_object_derives(registry, scene_object, NMO_CID_SCENE)
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
        return session_object_derives(registry, scene_object, NMO_CID_SCENE)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_t *camera_object =
        nmo_object_repository_find_by_id(repo, camera_id);
    if (camera_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!session_object_derives(registry, camera_object, NMO_CID_CAMERA)) {
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
    if (!session_object_derives(registry, owner_object, NMO_CID_BEOBJECT) ||
        !session_object_derives(registry, behavior_object, NMO_CID_BEHAVIOR)) {
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

    nmo_status_t status =
        nmo_workspace_edit_snapshot_behavior_state(edit, behavior_state);
    if (status != NMO_OK) {
        return status;
    }

    size_t existing_index = 0u;
    if (nmo_beobject_script_array_find(
            &owner_state->scripts, behavior_id, &existing_index) == 0) {
        status = nmo_beobject_script_array_append(
            &owner_state->scripts, behavior_id);
        if (status != NMO_OK) {
            return workspace_edit_abort_status(edit, checkpoint, status);
        }

        array_id_action_t *rollback = NULL;
        status = workspace_edit_make_array_id_action(
            edit,
            &owner_state->scripts,
            behavior_id,
            owner_state->scripts.count - 1u,
            &rollback);
        if (status != NMO_OK) {
            return workspace_edit_abort_status(edit, checkpoint, status);
        }
        status = workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_remove_array_id, rollback);
        if (status != NMO_OK) {
            return status;
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
    return NMO_OK;
}

static nmo_status_t workspace_edit_find_typed_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_object_t **out_object,
    void **out_state)
{
    if (out_object != NULL) {
        *out_object = NULL;
    }
    if (out_state != NULL) {
        *out_state = NULL;
    }
    if (edit == NULL || edit->finished || object_id == 0 || out_state == NULL) {
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

    void *state = NULL;
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == class_id) {
        state = nmo_object_get_state(object);
    } else {
        const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
        if (!session_object_derives(registry, object, class_id)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        const nmo_type_descriptor_t *base_type =
            nmo_type_query_find_by_class_id(registry, class_id);
        if (base_type == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        state = nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, base_type->guid);
    }
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (out_object != NULL) {
        *out_object = object;
    }
    *out_state = state;
    return NMO_OK;
}

static nmo_status_t workspace_edit_find_typed_state(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    void **out_state)
{
    return workspace_edit_find_typed_object(
        edit,
        object_id,
        class_id,
        NULL,
        out_state);
}

static nmo_status_t workspace_edit_snapshot_typed_state(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    size_t state_size,
    void **out_state)
{
    if (out_state != NULL) {
        *out_state = NULL;
    }
    if (out_state == NULL || state_size == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    void *state = NULL;
    nmo_status_t status =
        workspace_edit_find_typed_state(edit, object_id, class_id, &state);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, state_size);
    if (status != NMO_OK) {
        return status;
    }

    *out_state = state;
    return NMO_OK;
}

static nmo_status_t workspace_edit_require_object_class(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id);

static uint8_t workspace_edit_float_color_channel(float value)
{
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t workspace_edit_pack_argb(float r, float g, float b, float a)
{
    uint32_t alpha = workspace_edit_float_color_channel(a);
    uint32_t red = workspace_edit_float_color_channel(r);
    uint32_t green = workspace_edit_float_color_channel(g);
    uint32_t blue = workspace_edit_float_color_channel(b);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

nmo_status_t nmo_asset_edit_set_material_color(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    float r,
    float g,
    float b,
    float a)
{
    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    uint32_t color = workspace_edit_pack_argb(r, g, b, a);
    state->diffuse_color = color;
    state->ambient_color = color;
    state->specular_color = 0xFFFFFFFFu;
    state->emissive_color = 0xFF000000u;
    state->specular_power = 0.0f;

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_material_channels(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_channels_t *channels)
{
    if (channels == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    if (channels->has_diffuse) {
        state->diffuse_color = workspace_edit_pack_argb(
            channels->diffuse[0],
            channels->diffuse[1],
            channels->diffuse[2],
            channels->diffuse[3]);
    }
    if (channels->has_ambient) {
        state->ambient_color = workspace_edit_pack_argb(
            channels->ambient[0],
            channels->ambient[1],
            channels->ambient[2],
            channels->ambient[3]);
    }
    if (channels->has_specular) {
        state->specular_color = workspace_edit_pack_argb(
            channels->specular[0],
            channels->specular[1],
            channels->specular[2],
            channels->specular[3]);
    }
    if (channels->has_emissive) {
        state->emissive_color = workspace_edit_pack_argb(
            channels->emissive[0],
            channels->emissive[1],
            channels->emissive[2],
            channels->emissive[3]);
    }
    if (channels->has_specular_power) {
        state->specular_power = channels->specular_power;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_material_render_flags(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_render_flags_t *flags)
{
    if (flags == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    uint32_t packed_modes = state->packed_modes;
    uint32_t packed_flags = state->packed_flags;
    if (flags->has_texture_blend) {
        packed_modes = (packed_modes & ~0xFu) |
                       ((uint32_t)flags->texture_blend & 0xFu);
    }
    if (flags->has_min_filter) {
        packed_modes = (packed_modes & ~(0xFu << 4)) |
                       (((uint32_t)flags->min_filter & 0xFu) << 4);
    }
    if (flags->has_mag_filter) {
        packed_modes = (packed_modes & ~(0xFu << 8)) |
                       (((uint32_t)flags->mag_filter & 0xFu) << 8);
    }
    if (flags->has_source_blend) {
        packed_modes = (packed_modes & ~(0xFu << 12)) |
                       (((uint32_t)flags->source_blend & 0xFu) << 12);
    }
    if (flags->has_destination_blend) {
        packed_modes = (packed_modes & ~(0xFu << 16)) |
                       (((uint32_t)flags->destination_blend & 0xFu) << 16);
    }
    if (flags->has_wrap) {
        packed_modes = (packed_modes & ~(0xFu << 28)) |
                       (((uint32_t)flags->wrap & 0xFu) << 28);
    }
    if (flags->has_alpha_func) {
        packed_flags = (packed_flags & ~(0x1Fu << 16)) |
                       (((uint32_t)flags->alpha_func & 0x1Fu) << 16);
    }

    state->packed_modes = packed_modes;
    state->packed_flags = packed_flags;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_texture_rgba(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const void *rgba_pixels,
    uint32_t width,
    uint32_t height)
{
    if (edit == NULL || texture_id == 0u || rgba_pixels == NULL ||
        width == 0u || height == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_texture_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        texture_id,
        NMO_CID_TEXTURE,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_texture_replace_bitmap(state, arena, rgba_pixels, width, height);
    if (status != NMO_OK) {
        return status;
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_RESOURCES);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_bind_material_texture(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    nmo_object_id_t texture_id,
    uint32_t slot)
{
    if (slot >= 4u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    status = workspace_edit_require_object_class(edit, texture_id, NMO_CID_TEXTURE);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    nmo_material_set_texture_id(state, slot, texture_id);
    state->has_additional_textures =
        (nmo_material_texture_id(state, 1) != NMO_OBJECT_ID_NONE ||
         nmo_material_texture_id(state, 2) != NMO_OBJECT_ID_NONE ||
         nmo_material_texture_id(state, 3) != NMO_OBJECT_ID_NONE)
            ? 1u
            : 0u;
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static nmo_status_t workspace_edit_require_object_class(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id)
{
    void *state = NULL;
    return workspace_edit_find_typed_state(edit, object_id, class_id, &state);
}

nmo_status_t nmo_asset_edit_bind_entity_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t entity_id,
    nmo_object_id_t mesh_id)
{
    nmo_3dentity_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        entity_id,
        NMO_CID_3DENTITY,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    status = workspace_edit_require_object_class(edit, mesh_id, NMO_CID_MESH);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_ref_t *mesh_ids = (nmo_ref_t *)nmo_arena_alloc(
        arena,
        sizeof(*mesh_ids),
        _Alignof(nmo_ref_t));
    if (mesh_ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    mesh_ids[0] = nmo_ref_from_id(mesh_id);

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    state->current_mesh = nmo_ref_from_id(mesh_id);
    state->mesh_count = 1u;
    state->mesh_ids = mesh_ids;
    state->has_mesh_chunk = 1u;

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static nmo_status_t workspace_edit_alloc_cube_mesh(
    nmo_arena_t *arena,
    nmo_vertex_t **out_vertices,
    nmo_face_t **out_faces,
    uint16_t **out_indices,
    uint32_t **out_vertex_colors,
    uint32_t **out_vertex_specular,
    nmo_material_group_t **out_groups)
{
    if (arena == NULL || out_vertices == NULL || out_faces == NULL ||
        out_indices == NULL || out_vertex_colors == NULL ||
        out_vertex_specular == NULL || out_groups == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertices) * 8u,
        _Alignof(nmo_vertex_t));
    nmo_face_t *faces = (nmo_face_t *)nmo_arena_alloc(
        arena,
        sizeof(*faces) * 12u,
        _Alignof(nmo_face_t));
    uint16_t *indices = (uint16_t *)nmo_arena_alloc(
        arena,
        sizeof(*indices) * 36u,
        _Alignof(uint16_t));
    uint32_t *vertex_colors = (uint32_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertex_colors) * 8u,
        _Alignof(uint32_t));
    uint32_t *vertex_specular = (uint32_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertex_specular) * 8u,
        _Alignof(uint32_t));
    nmo_material_group_t *groups = (nmo_material_group_t *)nmo_arena_alloc(
        arena,
        sizeof(*groups),
        _Alignof(nmo_material_group_t));
    if (vertices == NULL || faces == NULL || indices == NULL ||
        vertex_colors == NULL || vertex_specular == NULL || groups == NULL) {
        return NMO_ERR_NOMEM;
    }

    static const float coords[8][3] = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    };
    static const uint16_t cube_indices[36] = {
        0u, 2u, 1u, 0u, 3u, 2u,
        4u, 5u, 6u, 4u, 6u, 7u,
        0u, 1u, 5u, 0u, 5u, 4u,
        3u, 6u, 2u, 3u, 7u, 6u,
        1u, 2u, 6u, 1u, 6u, 5u,
        0u, 4u, 7u, 0u, 7u, 3u,
    };

    for (size_t i = 0; i < 8u; ++i) {
        vertices[i].position.x = coords[i][0];
        vertices[i].position.y = coords[i][1];
        vertices[i].position.z = coords[i][2];
        vertices[i].normal.x = coords[i][0] * 1.1547005f;
        vertices[i].normal.y = coords[i][1] * 1.1547005f;
        vertices[i].normal.z = coords[i][2] * 1.1547005f;
        vertices[i].uv.x = 0.0f;
        vertices[i].uv.y = 0.0f;
        vertex_colors[i] = 0xFFFFFFFFu;
        vertex_specular[i] = 0xFF000000u;
    }
    memset(faces, 0, sizeof(*faces) * 12u);
    memcpy(indices, cube_indices, sizeof(cube_indices));
    memset(groups, 0, sizeof(*groups));

    *out_vertices = vertices;
    *out_faces = faces;
    *out_indices = indices;
    *out_vertex_colors = vertex_colors;
    *out_vertex_specular = vertex_specular;
    *out_groups = groups;
    return NMO_OK;
}

typedef struct workspace_obj_mesh_slot {
    int32_t pos_idx;
    int32_t uv_idx;
    int32_t normal_idx;
    uint16_t idx_plus1;
} workspace_obj_mesh_slot_t;

typedef struct workspace_obj_mesh_counts {
    size_t face_vertex_count;
    size_t line_vertex_count;
    size_t max_vertices;
    size_t vertex_bytes;
    size_t vertex_color_bytes;
    size_t vertex_specular_bytes;
    size_t face_bytes;
    size_t face_index_bytes;
    size_t line_index_bytes;
    size_t dedup_capacity;
    size_t dedup_bytes;
} workspace_obj_mesh_counts_t;

static bool workspace_obj_mesh_checked_mul(
    size_t count,
    size_t elem_size,
    size_t *out_size)
{
    if (out_size == NULL || elem_size == 0u) {
        return false;
    }
    if (count > SIZE_MAX / elem_size) {
        return false;
    }
    *out_size = count * elem_size;
    return true;
}

static uint32_t workspace_obj_mesh_rgb_to_argb(const float *rgb)
{
    uint32_t r = workspace_edit_float_color_channel(rgb[0]);
    uint32_t g = workspace_edit_float_color_channel(rgb[1]);
    uint32_t b = workspace_edit_float_color_channel(rgb[2]);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static size_t workspace_obj_mesh_tuple_hash(
    int32_t pos_idx,
    int32_t uv_idx,
    int32_t normal_idx,
    size_t capacity)
{
    uint64_t h = 1469598103934665603ULL;
    h ^= (uint32_t)pos_idx;
    h *= 1099511628211ULL;
    h ^= (uint32_t)uv_idx;
    h *= 1099511628211ULL;
    h ^= (uint32_t)normal_idx;
    h *= 1099511628211ULL;
    return (size_t)(h % capacity);
}

static nmo_status_t workspace_obj_mesh_validate_material_id(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id)
{
    if (material_id == 0 || material_id == NMO_OBJECT_ID_NONE) {
        return NMO_OK;
    }
    return workspace_edit_require_object_class(edit, material_id, NMO_CID_MATERIAL);
}

static nmo_status_t workspace_obj_mesh_validate_materials(
    nmo_workspace_edit_t *edit,
    const nmo_asset_mesh_import_options_t *options)
{
    if (options == NULL) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(workspace_obj_mesh_validate_material_id(
        edit,
        options->default_material_id));
    if (options->material_count > 0 && options->materials == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < options->material_count; ++i) {
        NMO_RETURN_IF_ERROR(workspace_obj_mesh_validate_material_id(
            edit,
            options->materials[i].material_id));
    }
    return NMO_OK;
}

static nmo_object_id_t workspace_obj_mesh_material_for_name(
    const nmo_asset_mesh_import_options_t *options,
    const char *name)
{
    if (options == NULL) {
        return NMO_OBJECT_ID_NONE;
    }
    if (name != NULL && options->materials != NULL) {
        for (size_t i = 0; i < options->material_count; ++i) {
            if (options->materials[i].name != NULL &&
                strcmp(options->materials[i].name, name) == 0) {
                return options->materials[i].material_id;
            }
        }
    }
    return options->default_material_id;
}

static void workspace_obj_mesh_compute_bounds(
    const nmo_obj_data_t *obj_data,
    nmo_vector_t *center,
    nmo_vector_t *box_min,
    nmo_vector_t *box_max,
    float *radius)
{
    if (obj_data == NULL || obj_data->positions == NULL ||
        obj_data->pos_count == 0) {
        *center = (nmo_vector_t){0.0f, 0.0f, 0.0f};
        *box_min = (nmo_vector_t){0.0f, 0.0f, 0.0f};
        *box_max = (nmo_vector_t){0.0f, 0.0f, 0.0f};
        *radius = 0.0f;
        return;
    }

    float minx = obj_data->positions[0];
    float miny = obj_data->positions[1];
    float minz = obj_data->positions[2];
    float maxx = minx;
    float maxy = miny;
    float maxz = minz;

    for (size_t i = 0; i < obj_data->pos_count; ++i) {
        float x = obj_data->positions[i * 3u + 0u];
        float y = obj_data->positions[i * 3u + 1u];
        float z = obj_data->positions[i * 3u + 2u];
        if (x < minx) {
            minx = x;
        }
        if (y < miny) {
            miny = y;
        }
        if (z < minz) {
            minz = z;
        }
        if (x > maxx) {
            maxx = x;
        }
        if (y > maxy) {
            maxy = y;
        }
        if (z > maxz) {
            maxz = z;
        }
    }

    *box_min = (nmo_vector_t){minx, miny, minz};
    *box_max = (nmo_vector_t){maxx, maxy, maxz};
    *center = (nmo_vector_t){
        (minx + maxx) * 0.5f,
        (miny + maxy) * 0.5f,
        (minz + maxz) * 0.5f,
    };

    float max_dist_sq = 0.0f;
    for (size_t i = 0; i < obj_data->pos_count; ++i) {
        float dx = obj_data->positions[i * 3u + 0u] - center->x;
        float dy = obj_data->positions[i * 3u + 1u] - center->y;
        float dz = obj_data->positions[i * 3u + 2u] - center->z;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq) {
            max_dist_sq = dist_sq;
        }
    }
    *radius = sqrtf(max_dist_sq);
}

static nmo_status_t workspace_obj_mesh_get_or_add_vertex(
    const nmo_obj_data_t *obj_data,
    const nmo_obj_face_vertex_t *fv,
    nmo_vertex_t *vertices,
    uint32_t *vertex_colors,
    uint32_t *vertex_specular,
    workspace_obj_mesh_slot_t *dedup_table,
    size_t dedup_capacity,
    uint32_t *unique_count,
    uint16_t *out_index)
{
    int32_t pi = fv->pos_idx;
    int32_t ui = fv->uv_idx;
    int32_t ni = fv->normal_idx;
    size_t slot = workspace_obj_mesh_tuple_hash(pi, ui, ni, dedup_capacity);

    for (;;) {
        workspace_obj_mesh_slot_t *entry = &dedup_table[slot];
        if (entry->idx_plus1 == 0) {
            if (*unique_count >= 65535u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }

            nmo_vertex_t *vertex = &vertices[*unique_count];
            memset(vertex, 0, sizeof(*vertex));

            if (pi >= 0 && (size_t)pi < obj_data->pos_count) {
                vertex->position.x = obj_data->positions[(size_t)pi * 3u + 0u];
                vertex->position.y = obj_data->positions[(size_t)pi * 3u + 1u];
                vertex->position.z = obj_data->positions[(size_t)pi * 3u + 2u];
            }
            if (ui >= 0 && (size_t)ui < obj_data->uv_count) {
                vertex->uv.x = obj_data->uvs[(size_t)ui * 2u + 0u];
                vertex->uv.y = obj_data->uvs[(size_t)ui * 2u + 1u];
            }
            if (ni >= 0 && (size_t)ni < obj_data->normal_count) {
                vertex->normal.x = obj_data->normals[(size_t)ni * 3u + 0u];
                vertex->normal.y = obj_data->normals[(size_t)ni * 3u + 1u];
                vertex->normal.z = obj_data->normals[(size_t)ni * 3u + 2u];
            }

            uint32_t color = 0xFFFFFFFFu;
            if (pi >= 0 && obj_data->position_has_color != NULL &&
                obj_data->colors != NULL &&
                (size_t)pi < obj_data->pos_count &&
                obj_data->position_has_color[pi]) {
                color = workspace_obj_mesh_rgb_to_argb(
                    &obj_data->colors[(size_t)pi * 3u]);
            }
            vertex_colors[*unique_count] = color;
            vertex_specular[*unique_count] = 0xFF000000u;

            entry->pos_idx = pi;
            entry->uv_idx = ui;
            entry->normal_idx = ni;
            entry->idx_plus1 = (uint16_t)(*unique_count + 1u);
            *out_index = (uint16_t)*unique_count;
            (*unique_count)++;
            return NMO_OK;
        }

        if (entry->pos_idx == pi &&
            entry->uv_idx == ui &&
            entry->normal_idx == ni) {
            *out_index = (uint16_t)(entry->idx_plus1 - 1u);
            return NMO_OK;
        }

        slot = (slot + 1u) % dedup_capacity;
    }
}

static uint32_t workspace_obj_mesh_material_group_count(
    const nmo_obj_data_t *obj_data,
    uint32_t *out_material_offset)
{
    bool has_unassigned = false;
    for (size_t i = 0; i < obj_data->face_count; ++i) {
        if (obj_data->faces[i].material_group == NMO_OBJ_NO_MATERIAL) {
            has_unassigned = true;
            break;
        }
    }
    uint32_t offset = has_unassigned ? 1u : 0u;
    uint32_t count = (uint32_t)obj_data->material_name_count + offset;
    if (obj_data->face_count > 0 && count == 0) {
        count = 1u;
        offset = 1u;
    }
    *out_material_offset = offset;
    return count;
}

static nmo_status_t workspace_obj_mesh_compute_counts(
    const nmo_obj_data_t *obj_data,
    workspace_obj_mesh_counts_t *out_counts)
{
    if (obj_data == NULL || out_counts == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->face_count > SIZE_MAX / 3u ||
        obj_data->line_count > SIZE_MAX / 2u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_obj_mesh_counts_t counts = {0};
    counts.face_vertex_count = obj_data->face_count * 3u;
    counts.line_vertex_count = obj_data->line_count * 2u;
    if (counts.face_vertex_count > SIZE_MAX - counts.line_vertex_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    counts.max_vertices = counts.face_vertex_count + counts.line_vertex_count;
    if (counts.max_vertices == 0 || counts.max_vertices > 65535u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(nmo_vertex_t),
            &counts.vertex_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(uint32_t),
            &counts.vertex_color_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(uint32_t),
            &counts.vertex_specular_bytes) ||
        !workspace_obj_mesh_checked_mul(
            obj_data->face_count,
            sizeof(nmo_face_t),
            &counts.face_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.face_vertex_count,
            sizeof(uint16_t),
            &counts.face_index_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.line_vertex_count,
            sizeof(uint16_t),
            &counts.line_index_bytes)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (counts.max_vertices > SIZE_MAX / 2u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    counts.dedup_capacity = counts.max_vertices * 2u;
    if (counts.dedup_capacity < 64u) {
        counts.dedup_capacity = 64u;
    }
    if (!workspace_obj_mesh_checked_mul(
            counts.dedup_capacity,
            sizeof(workspace_obj_mesh_slot_t),
            &counts.dedup_bytes)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_counts = counts;
    return NMO_OK;
}

static nmo_status_t workspace_obj_mesh_build(
    nmo_arena_t *scratch,
    nmo_arena_t *document_arena,
    const nmo_obj_data_t *obj_data,
    const nmo_asset_mesh_import_options_t *options,
    nmo_mesh_state_t *out_state)
{
    if (scratch == NULL || document_arena == NULL ||
        obj_data == NULL || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->face_count == 0 && obj_data->line_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((obj_data->face_count > 0 && obj_data->faces == NULL) ||
        (obj_data->line_count > 0 && obj_data->lines == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->material_name_count > UINT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_obj_mesh_counts_t counts = {0};
    NMO_RETURN_IF_ERROR(workspace_obj_mesh_compute_counts(obj_data, &counts));

    uint32_t material_offset = 0;
    uint32_t material_group_count =
        workspace_obj_mesh_material_group_count(obj_data, &material_offset);
    if (material_group_count > UINT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_bytes,
        _Alignof(nmo_vertex_t));
    uint32_t *vertex_colors = (uint32_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_color_bytes,
        _Alignof(uint32_t));
    uint32_t *vertex_specular = (uint32_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_specular_bytes,
        _Alignof(uint32_t));
    if (vertices == NULL || vertex_colors == NULL || vertex_specular == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_face_t *faces = NULL;
    uint16_t *face_indices = NULL;
    if (obj_data->face_count > 0) {
        faces = (nmo_face_t *)nmo_arena_alloc(
            document_arena,
            counts.face_bytes,
            _Alignof(nmo_face_t));
        face_indices = (uint16_t *)nmo_arena_alloc(
            document_arena,
            counts.face_index_bytes,
            _Alignof(uint16_t));
        if (faces == NULL || face_indices == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    uint16_t *line_indices = NULL;
    if (obj_data->line_count > 0) {
        line_indices = (uint16_t *)nmo_arena_alloc(
            document_arena,
            counts.line_index_bytes,
            _Alignof(uint16_t));
        if (line_indices == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    nmo_material_group_t *material_groups = NULL;
    if (material_group_count > 0) {
        material_groups = (nmo_material_group_t *)nmo_arena_alloc(
            document_arena,
            material_group_count * sizeof(*material_groups),
            _Alignof(nmo_material_group_t));
        if (material_groups == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(material_groups, 0, material_group_count * sizeof(*material_groups));
        for (uint32_t i = 0; i < material_group_count; ++i) {
            material_groups[i].material = nmo_ref_from_id(
                options != NULL ? options->default_material_id :
                                  NMO_OBJECT_ID_NONE);
        }
        for (size_t i = 0; i < obj_data->material_name_count; ++i) {
            uint32_t group_index = (uint32_t)i + material_offset;
            if (group_index < material_group_count) {
                material_groups[group_index].material = nmo_ref_from_id(
                    workspace_obj_mesh_material_for_name(
                        options,
                        obj_data->material_names != NULL
                            ? obj_data->material_names[i]
                            : NULL));
            }
        }
    }

    workspace_obj_mesh_slot_t *dedup_table =
        (workspace_obj_mesh_slot_t *)nmo_arena_alloc(
            scratch,
            counts.dedup_bytes,
            _Alignof(workspace_obj_mesh_slot_t));
    if (dedup_table == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(dedup_table, 0, counts.dedup_bytes);

    uint32_t unique_count = 0;
    for (size_t face_index = 0; face_index < obj_data->face_count; ++face_index) {
        const nmo_obj_face_t *obj_face = &obj_data->faces[face_index];
        for (size_t vertex_index = 0; vertex_index < 3u; ++vertex_index) {
            uint16_t out_index = 0;
            NMO_RETURN_IF_ERROR(workspace_obj_mesh_get_or_add_vertex(
                obj_data,
                &obj_face->verts[vertex_index],
                vertices,
                vertex_colors,
                vertex_specular,
                dedup_table,
                counts.dedup_capacity,
                &unique_count,
                &out_index));
            face_indices[face_index * 3u + vertex_index] = out_index;
        }

        nmo_vertex_t *v0 = &vertices[face_indices[face_index * 3u + 0u]];
        nmo_vertex_t *v1 = &vertices[face_indices[face_index * 3u + 1u]];
        nmo_vertex_t *v2 = &vertices[face_indices[face_index * 3u + 2u]];
        float e1x = v1->position.x - v0->position.x;
        float e1y = v1->position.y - v0->position.y;
        float e1z = v1->position.z - v0->position.z;
        float e2x = v2->position.x - v0->position.x;
        float e2y = v2->position.y - v0->position.y;
        float e2z = v2->position.z - v0->position.z;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }

        faces[face_index].normal = (nmo_vector_t){nx, ny, nz};
        faces[face_index].material_group_idx =
            obj_face->material_group == NMO_OBJ_NO_MATERIAL
                ? 0u
                : (uint16_t)(obj_face->material_group + material_offset);
        faces[face_index].channel_mask = 0u;
    }

    for (size_t line_index = 0; line_index < obj_data->line_count; ++line_index) {
        const nmo_obj_line_t *obj_line = &obj_data->lines[line_index];
        for (size_t vertex_index = 0; vertex_index < 2u; ++vertex_index) {
            uint16_t out_index = 0;
            NMO_RETURN_IF_ERROR(workspace_obj_mesh_get_or_add_vertex(
                obj_data,
                &obj_line->verts[vertex_index],
                vertices,
                vertex_colors,
                vertex_specular,
                dedup_table,
                counts.dedup_capacity,
                &unique_count,
                &out_index));
            line_indices[line_index * 2u + vertex_index] = out_index;
        }
    }

    nmo_vector_t center = {0};
    nmo_vector_t box_min = {0};
    nmo_vector_t box_max = {0};
    float radius = 0.0f;
    workspace_obj_mesh_compute_bounds(
        obj_data,
        &center,
        &box_min,
        &box_max,
        &radius);

    memset(out_state, 0, sizeof(*out_state));
    out_state->flags = 0u;
    out_state->bary_center = center;
    out_state->radius = radius;
    out_state->local_box_min = box_min;
    out_state->local_box_max = box_max;
    out_state->face_count = (uint32_t)obj_data->face_count;
    out_state->faces = faces;
    out_state->face_vertex_indices = face_indices;
    out_state->line_count = (uint32_t)obj_data->line_count;
    out_state->line_indices = line_indices;
    out_state->vertex_count = unique_count;
    out_state->vertices = vertices;
    out_state->vertex_colors = vertex_colors;
    out_state->vertex_specular = vertex_specular;
    out_state->vertex_weights = NULL;
    out_state->vertex_weight_count = 0u;
    out_state->material_group_count = material_group_count;
    out_state->material_groups = material_groups;
    out_state->material_channel_count = 0u;
    out_state->material_channels = NULL;
    out_state->is_valid = true;
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_obj_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const nmo_obj_data_t *obj_data,
    const nmo_asset_mesh_import_options_t *options)
{
    if (edit == NULL || mesh_id == 0 || obj_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *mesh_object = NULL;
    nmo_mesh_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_object(
        edit,
        mesh_id,
        NMO_CID_MESH,
        &mesh_object,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    status = workspace_obj_mesh_validate_materials(edit, options);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *document_arena =
        nmo_workspace_internal_document_arena(edit->workspace);
    if (document_arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_arena_t *scratch = nmo_arena_create(NULL, 0);
    if (scratch == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_mesh_state_t next = {0};
    status = workspace_obj_mesh_build(
        scratch,
        document_arena,
        obj_data,
        options,
        &next);
    if (status == NMO_OK) {
        status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    }
    if (status == NMO_OK) {
        status = nmo_workspace_edit_snapshot_object_chunk(edit, mesh_id);
    }

    nmo_chunk_t *chunk = NULL;
    if (status == NMO_OK) {
        *state = next;
        chunk = nmo_object_get_chunk(mesh_object);
        if (chunk == NULL) {
            chunk = nmo_chunk_create(document_arena);
            if (chunk == NULL) {
                status = NMO_ERR_NOMEM;
            } else {
                status = nmo_object_set_chunk(mesh_object, chunk);
            }
        }
    }
    if (status == NMO_OK) {
        chunk->class_id = NMO_CID_MESH;
        chunk->chunk_class_id = (uint8_t)(NMO_CID_MESH & 0xFFu);
        chunk->chunk_version = 7u;
        nmo_chunk_set_data_version(chunk, 9u);
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        status = nmo_chunk_start_write(chunk);
    }
    if (status == NMO_OK) {
        nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
            document_arena,
            nmo_workspace_internal_repository(edit->workspace),
            NMO_SERIALIZE_FLAG_FILE_MODE,
            0);
        status = nmo_mesh_serialize(state, chunk, NULL, &ser_ctx);
    }
    if (status == NMO_OK) {
        nmo_workspace_edit_mark(
            edit,
            NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    }

    nmo_arena_destroy(scratch);
    return status;
}

static uint8_t *workspace_edit_read_file_to_heap(
    const char *path,
    size_t *out_size)
{
    if (path == NULL || out_size == NULL) {
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size_long = ftell(file);
    if (size_long < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    size_t size = (size_t)size_long;
    uint8_t *bytes = (uint8_t *)malloc(size > 0 ? size : 1u);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(bytes, 1u, size, file);
    fclose(file);
    if (read_size != size) {
        free(bytes);
        return NULL;
    }

    *out_size = size;
    return bytes;
}

nmo_status_t nmo_asset_edit_set_texture_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const char *path)
{
    if (edit == NULL || texture_id == 0u || path == NULL || *path == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t size = 0u;
    uint8_t *bytes = workspace_edit_read_file_to_heap(path, &size);
    if (bytes == NULL) {
        return NMO_ERR_CANT_OPEN_FILE;
    }
    if (size > (size_t)INT_MAX) {
        free(bytes);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_arena_t *decode_arena = nmo_arena_create(NULL, 0);
    if (decode_arena == NULL) {
        free(bytes);
        return NMO_ERR_NOMEM;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t *pixels = nmo_stbi_load_from_memory(
        decode_arena,
        bytes,
        (int)size,
        &width,
        &height,
        &channels,
        4);
    free(bytes);
    if (pixels == NULL || width <= 0 || height <= 0) {
        nmo_arena_destroy(decode_arena);
        return NMO_ERR_INVALID_FORMAT;
    }

    nmo_status_t status = nmo_asset_edit_set_texture_rgba(
        edit,
        texture_id,
        pixels,
        (uint32_t)width,
        (uint32_t)height);
    nmo_arena_destroy(decode_arena);
    return status;
}

nmo_status_t nmo_asset_edit_set_obj_mesh_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const char *path,
    const nmo_asset_mesh_import_options_t *options)
{
    if (edit == NULL || mesh_id == 0 || path == NULL || *path == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t size = 0;
    uint8_t *bytes = workspace_edit_read_file_to_heap(path, &size);
    if (bytes == NULL) {
        return NMO_ERR_CANT_OPEN_FILE;
    }

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 0);
    if (parse_arena == NULL) {
        free(bytes);
        return NMO_ERR_NOMEM;
    }

    nmo_obj_data_t obj_data = {0};
    nmo_status_t status =
        nmo_obj_parse(parse_arena, (const char *)bytes, size, &obj_data);
    free(bytes);
    if (status == NMO_OK) {
        status = nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj_data, options);
    }
    nmo_arena_destroy(parse_arena);
    return status;
}

nmo_status_t nmo_asset_edit_set_primitive_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    nmo_primitive_mesh_t primitive,
    nmo_object_id_t material_id)
{
    if (primitive != NMO_PRIMITIVE_CUBE) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *mesh_object = NULL;
    nmo_mesh_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_object(
        edit,
        mesh_id,
        NMO_CID_MESH,
        &mesh_object,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    if (material_id != 0) {
        status = workspace_edit_require_object_class(edit, material_id, NMO_CID_MATERIAL);
        if (status != NMO_OK) {
            return status;
        }
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_vertex_t *vertices = NULL;
    nmo_face_t *faces = NULL;
    uint16_t *indices = NULL;
    uint32_t *vertex_colors = NULL;
    uint32_t *vertex_specular = NULL;
    nmo_material_group_t *groups = NULL;
    status = workspace_edit_alloc_cube_mesh(
        arena,
        &vertices,
        &faces,
        &indices,
        &vertex_colors,
        &vertex_specular,
        &groups);
    if (status != NMO_OK) {
        return status;
    }
    groups[0].material = nmo_ref_from_id(material_id);

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }
    status = nmo_workspace_edit_snapshot_object_chunk(edit, mesh_id);
    if (status != NMO_OK) {
        return status;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(mesh_object);
    if (chunk == NULL) {
        chunk = nmo_chunk_create(arena);
        if (chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
        status = nmo_object_set_chunk(mesh_object, chunk);
        if (status != NMO_OK) {
            return status;
        }
    }
    nmo_chunk_set_data_version(chunk, 9u);

    state->flags = 0u;
    state->bary_center = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->radius = 0.8660254f;
    state->local_box_min = (nmo_vector_t){-0.5f, -0.5f, -0.5f};
    state->local_box_max = (nmo_vector_t){0.5f, 0.5f, 0.5f};
    state->face_count = 12u;
    state->faces = faces;
    state->face_vertex_indices = indices;
    state->line_count = 0u;
    state->line_indices = NULL;
    state->vertex_count = 8u;
    state->vertices = vertices;
    state->vertex_colors = vertex_colors;
    state->vertex_specular = vertex_specular;
    state->vertex_weights = NULL;
    state->vertex_weight_count = 0u;
    state->material_group_count = 1u;
    state->material_groups = groups;
    state->material_channel_count = 0u;
    state->material_channels = NULL;
    state->is_valid = true;

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

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
        return session_object_derives(registry, object, NMO_CID_CAMERA)
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
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static bool workspace_edit_class_is_entity_target(nmo_class_id_t class_id)
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

static bool workspace_edit_object_is_entity_target(
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
    return session_object_derives(registry, object, NMO_CID_3DENTITY);
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
        parent_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    nmo_object_t *parent = nmo_object_repository_find_by_id(repo, parent_id);
    if (object == NULL || parent == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!workspace_edit_object_is_entity_target(registry, object) ||
        !workspace_edit_object_is_entity_target(registry, parent)) {
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

    state->parent = nmo_ref_from_id(parent_id);
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
        return session_object_derives(registry, object, NMO_CID_TARGETCAMERA)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
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
    if (settings->type < VX_LIGHTPOINT || settings->type > VX_LIGHTPARA) {
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
        return session_object_derives(registry, object, NMO_CID_LIGHT)
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
        return session_object_derives(registry, object, NMO_CID_TARGETLIGHT)
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status =
        nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }
    state->has_target = 1u;
    state->target = nmo_ref_from_id(target_id);
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

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
        return session_object_derives(
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
            uint32_t key_size = nmo_objanim_controller_key_size(controller->type);
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
    } else if (session_object_derives(registry, sound, NMO_CID_WAVESOUND)) {
        wave_state = (nmo_wavesound_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry, sound, CKPGUID_WAVESOUND);
        sound_state = wave_state != NULL ? &wave_state->base : NULL;
    } else if (session_object_derives(registry, sound, NMO_CID_SOUND)) {
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

nmo_status_t nmo_object_edit_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    return nmo_object_edit_set_parameter_value_ex(
        edit, parameter_id, value_str, NULL);
}

nmo_status_t nmo_object_edit_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    return nmo_object_edit_set_parameter_bytes_ex(
        edit, parameter_id, bytes, byte_count, NULL);
}

nmo_status_t nmo_workspace_edit_commit(nmo_workspace_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
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
    if (edit == NULL || edit->arena == NULL || size == 0) {
        return NULL;
    }
    return nmo_arena_alloc(edit->arena, size, align);
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

    status = workspace_edit_push_cleanup(
        edit, cleanup_behavior_state_snapshot, snapshot);
    if (status != NMO_OK) {
        (void)cleanup_behavior_state_snapshot(edit, snapshot);
        return status;
    }

    status = workspace_edit_push_rollback(
        edit, rollback_behavior_state, snapshot);
    if (status != NMO_OK) {
        edit->cleanup_count--;
        (void)cleanup_behavior_state_snapshot(edit, snapshot);
        return status;
    }
    return NMO_OK;
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
        workspace_edit_push_rollback(edit, action_remove_object, rollback);
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

nmo_status_t nmo_runtime_apply_edit_flags(nmo_session_t *session, uint32_t flags)
{
    const uint32_t known_flags =
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
        NMO_WORKSPACE_EDIT_REFERENCES |
        NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
        NMO_WORKSPACE_EDIT_NAMES |
        NMO_WORKSPACE_EDIT_RESOURCES;

    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~known_flags) != 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_session_is_partial_load(session)) {
        return NMO_ERR_INVALID_STATE;
    }
    if ((flags & NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_behavior_index(session);
        nmo_session_invalidate_ref_graph(session);
    } else if ((flags & NMO_WORKSPACE_EDIT_REFERENCES) != 0u) {
        nmo_session_invalidate_ref_graph(session);
    }
    if ((flags & NMO_WORKSPACE_EDIT_NAMES) != 0u) {
        nmo_session_invalidate_object_query(session, NMO_OBJECT_QUERY_INDEX_NAMES);
        return nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    }
    if ((flags & NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_object_query(
            session,
            NMO_OBJECT_QUERY_INDEX_MEMBERSHIP);
    }
    if ((flags & NMO_WORKSPACE_EDIT_RESOURCES) != 0u) {
        /* Resource edits affect save output. No resource-derived query cache exists yet. */
    }
    return NMO_OK;
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

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].field_name == NULL || fields[i].value_str == NULL) {
            result.failed++;
            workspace_edit_abort_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_INVALID_ARGUMENT;
        }

        const nmo_type_field_t *field =
            nmo_type_get_field_by_name(type, fields[i].field_name);
        if (field == NULL) {
            result.failed++;
            workspace_edit_abort_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_NOT_FOUND;
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
            return snapshot_result;
        }

        nmo_status_t set_result =
            nmo_type_set_field(
                state, type, registry, fields[i].field_name, fields[i].value_str);
        if (set_result != NMO_OK) {
            result.failed++;
            workspace_edit_abort_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return set_result;
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
    return NMO_OK;
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

    const char *old_name = nmo_object_get_name(object);
    const char *old_name_copy = NULL;
    if (old_name != NULL) {
        size_t old_name_len = strlen(old_name) + 1u;
        char *copy = (char *)nmo_workspace_edit_alloc(edit, old_name_len, 1);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, old_name, old_name_len);
        old_name_copy = copy;
    }

    rename_object_action_t *rollback =
        (rename_object_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*rollback), _Alignof(rename_object_action_t));
    if (rollback == NULL) {
        return NMO_ERR_NOMEM;
    }
    rollback->id = object_id;
    rollback->name = old_name_copy;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_rename_object, rollback);
    if (push_result != NMO_OK) {
        return push_result;
    }

    nmo_status_t rename_result =
        nmo_object_repository_rename(repo, object_id, new_name);
    if (rename_result != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, rename_result);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_NAMES);
    return NMO_OK;
}

static nmo_status_t workspace_edit_snapshot_parameter_buffer(
    nmo_workspace_edit_t *edit,
    nmo_parameter_state_t *state,
    workspace_edit_checkpoint_t checkpoint)
{
    if (edit == NULL || state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parameter_buffer_snapshot_t *snapshot =
        (parameter_buffer_snapshot_t *)nmo_workspace_edit_alloc(
            edit,
            sizeof(*snapshot) + state->buffer_data.count,
            _Alignof(parameter_buffer_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->state = state;
    snapshot->count = state->buffer_data.count;
    if (state->buffer_data.count > 0 && state->buffer_data.data != NULL) {
        memcpy(snapshot->bytes, state->buffer_data.data, state->buffer_data.count);
    }

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_parameter_buffer, snapshot);
    if (push_result != NMO_OK) {
        return push_result;
    }
    return NMO_OK;
}

static nmo_status_t workspace_edit_prepare_parameter_buffer_write(
    nmo_workspace_edit_t *edit,
    nmo_parameter_state_t *state,
    workspace_edit_checkpoint_t checkpoint,
    size_t target_size,
    bool resize)
{
    nmo_status_t snapshot_result =
        workspace_edit_snapshot_parameter_buffer(edit, state, checkpoint);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    if (target_size != state->buffer_data.count && resize) {
        nmo_status_t resize_result =
            nmo_array_resize(&state->buffer_data, target_size);
        if (resize_result != NMO_OK) {
            return workspace_edit_abort_status(edit, checkpoint, resize_result);
        }
    }
    return NMO_OK;
}

nmo_status_t nmo_object_edit_set_parameter_value_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_parameter_state_t *state = workspace_edit_parameter_state(
        registry, object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (state->mode == CKPARAM_MODE_OBJECT) {
        nmo_status_t snapshot_result =
            workspace_edit_push_bytes_snapshot_or_abort(
                edit, checkpoint, &state->object_ref, sizeof(state->object_ref));
        if (snapshot_result != NMO_OK) {
            return snapshot_result;
        }

        nmo_object_id_t new_id = 0;
        nmo_status_t parse_result = parse_object_id_text(value_str, &new_id);
        if (parse_result != NMO_OK) {
            return workspace_edit_abort_status(edit, checkpoint, parse_result);
        }
        state->object_ref = nmo_ref_from_id(new_id);
        nmo_workspace_edit_mark(
            edit, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
        return NMO_OK;
    }

    if (state->mode == CKPARAM_MODE_MANAGER) {
        nmo_guid_t new_guid = NMO_GUID_NULL;
        uint32_t new_value = 0u;
        nmo_status_t parse_result =
            workspace_edit_prepare_manager_parameter_value(
                edit, state, value_str, options, &new_guid, &new_value);
        if (parse_result != NMO_OK) {
            return parse_result;
        }

        parameter_manager_snapshot_t *snapshot =
            (parameter_manager_snapshot_t *)nmo_workspace_edit_alloc(
                edit, sizeof(*snapshot), _Alignof(parameter_manager_snapshot_t));
        if (snapshot == NULL) {
            return workspace_edit_abort_status(edit, checkpoint, NMO_ERR_NOMEM);
        }
        snapshot->state = state;
        snapshot->manager_guid = state->manager_guid;
        snapshot->manager_value = state->manager_value;
        nmo_status_t push_result =
            workspace_edit_push_rollback_or_abort(
                edit, checkpoint, rollback_parameter_manager, snapshot);
        if (push_result != NMO_OK) {
            return push_result;
        }

        state->manager_guid = new_guid;
        state->manager_value = new_value;
        nmo_workspace_edit_mark(
            edit, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                  NMO_WORKSPACE_EDIT_REFERENCES);
        return NMO_OK;
    }

    if (state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, state->type_guid);
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    if (nmo_guid_equals(state->type_guid, CKPGUID_STRING)) {
        size_t required_size = strlen(value_str) + 1u;
        bool allow_resize = (options == NULL) ? true : options->resize;
        if (required_size > state->buffer_data.count && !allow_resize) {
            return NMO_ERR_OUT_OF_BOUNDS;
        }

        nmo_status_t prepare_result =
            workspace_edit_prepare_parameter_buffer_write(
                edit, state, checkpoint, required_size, true);
        if (prepare_result != NMO_OK) {
            return prepare_result;
        }
        memcpy(state->buffer_data.data, value_str, required_size);
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        return NMO_OK;
    }

    size_t buffer_size = type->size > 0 ? type->size : state->buffer_data.count;
    bool allow_resize = options != NULL && options->resize;
    if (buffer_size > state->buffer_data.count && !allow_resize) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    nmo_status_t prepare_result =
        workspace_edit_prepare_parameter_buffer_write(
            edit, state, checkpoint, buffer_size, allow_resize);
    if (prepare_result != NMO_OK) {
        return prepare_result;
    }

    uint8_t *tmp = (uint8_t *)calloc(1, buffer_size);
    if (tmp == NULL) {
        return workspace_edit_abort_status(edit, checkpoint, NMO_ERR_NOMEM);
    }

    nmo_status_t parse_result =
        nmo_type_value_from_string(tmp, type, registry, value_str);
    if (parse_result == NMO_OK) {
        size_t copy_len =
            buffer_size < state->buffer_data.count ? buffer_size : state->buffer_data.count;
        memcpy(state->buffer_data.data, tmp, copy_len);
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        if (nmo_guid_equals(state->type_guid, CKPGUID_ID) ||
            nmo_guid_equals(state->type_guid, CKPGUID_OBJECT)) {
            nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
        }
    }
    free(tmp);

    if (parse_result != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, parse_result);
    }
    return NMO_OK;
}

static nmo_status_t workspace_edit_read_message_manager_names(
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
    if (workspace_edit_seek_message_manager_identifier(chunk) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }

    int32_t count = 0;
    if (nmo_chunk_read_int(chunk, &count) != NMO_OK || count < 0) {
        return NMO_ERR_INVALID_STATE;
    }
    if (count == 0) {
        return NMO_OK;
    }
    if ((size_t)count > SIZE_MAX / sizeof(const char *)) {
        return NMO_ERR_NOMEM;
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

static nmo_status_t workspace_edit_write_message_manager_chunk(
    nmo_session_t *session,
    const char *const *names,
    uint32_t count,
    nmo_chunk_t **out_chunk)
{
    if (session == NULL || out_chunk == NULL ||
        (count > 0u && names == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(session));
    if (chunk == NULL) {
        return NMO_ERR_NOMEM;
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_write(chunk));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(chunk, 0x53u));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_string(
            chunk, names[i] ? names[i] : ""));
    }
    nmo_chunk_close(chunk);
    *out_chunk = chunk;
    return NMO_OK;
}

static nmo_status_t workspace_edit_read_attribute_manager_state(
    nmo_session_t *session,
    const nmo_manager_data_t *manager,
    nmo_attributemanager_state_t *out_state)
{
    if (session == NULL || manager == NULL || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_state, 0, sizeof(*out_state));
    if (manager->chunk == NULL) {
        return NMO_OK;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_chunk_t *chunk = nmo_chunk_clone(manager->chunk, arena);
    if (chunk == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (workspace_edit_seek_attribute_manager_identifier(chunk) != NMO_OK) {
        return NMO_OK;
    }

    int32_t category_count = 0;
    int32_t attribute_count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &category_count));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &attribute_count));
    if (category_count < 0 || attribute_count < 0) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    out_state->category_count = (uint32_t)category_count;
    out_state->attribute_count = (uint32_t)attribute_count;
    if (category_count > 0) {
        out_state->categories =
            (nmo_attribute_category_t *)nmo_arena_alloc(
                arena,
                (size_t)category_count * sizeof(*out_state->categories),
                _Alignof(nmo_attribute_category_t));
        if (out_state->categories == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(out_state->categories, 0,
               (size_t)category_count * sizeof(*out_state->categories));
    }
    for (int32_t i = 0; i < category_count; ++i) {
        int32_t present = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &present));
        out_state->categories[i].present = present != 0;
        if (present != 0) {
            char *name = NULL;
            size_t name_length = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
                chunk, &name, &name_length));
            (void)name_length;
            out_state->categories[i].name =
                nmo_arena_strdup(arena, name != NULL ? name : "");
            if (out_state->categories[i].name == NULL) {
                return NMO_ERR_NOMEM;
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_dword(chunk, &out_state->categories[i].flags));
        }
    }

    if (attribute_count > 0) {
        out_state->attributes =
            (nmo_attribute_descriptor_t *)nmo_arena_alloc(
                arena,
                (size_t)attribute_count * sizeof(*out_state->attributes),
                _Alignof(nmo_attribute_descriptor_t));
        if (out_state->attributes == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(out_state->attributes, 0,
               (size_t)attribute_count * sizeof(*out_state->attributes));
    }
    for (int32_t i = 0; i < attribute_count; ++i) {
        int32_t present = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &present));
        out_state->attributes[i].present = present != 0;
        if (present != 0) {
            char *name = NULL;
            size_t name_length = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
                chunk, &name, &name_length));
            (void)name_length;
            out_state->attributes[i].name =
                nmo_arena_strdup(arena, name != NULL ? name : "");
            if (out_state->attributes[i].name == NULL) {
                return NMO_ERR_NOMEM;
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(
                chunk, &out_state->attributes[i].parameter_type_guid));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
                chunk, &out_state->attributes[i].category_index));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
                chunk, &out_state->attributes[i].compatible_class_id));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &out_state->attributes[i].flags));
        }
    }
    return NMO_OK;
}

static nmo_status_t workspace_edit_write_attribute_manager_chunk(
    nmo_session_t *session,
    const nmo_attributemanager_state_t *state,
    nmo_chunk_t **out_chunk)
{
    if (session == NULL || state == NULL || out_chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(session));
    if (chunk == NULL) {
        return NMO_ERR_NOMEM;
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_write(chunk));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(chunk, 0x52u));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
        chunk, (int32_t)state->category_count));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
        chunk, (int32_t)state->attribute_count));
    for (uint32_t i = 0; i < state->category_count; ++i) {
        const nmo_attribute_category_t *cat = &state->categories[i];
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
            chunk, cat->present ? 1 : 0));
        if (cat->present) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_string(
                chunk, cat->name != NULL ? cat->name : ""));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, cat->flags));
        }
    }
    for (uint32_t i = 0; i < state->attribute_count; ++i) {
        const nmo_attribute_descriptor_t *attr = &state->attributes[i];
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
            chunk, attr->present ? 1 : 0));
        if (attr->present) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_string(
                chunk, attr->name != NULL ? attr->name : ""));
            NMO_RETURN_IF_ERROR(
                nmo_chunk_write_guid(chunk, attr->parameter_type_guid));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
                chunk, attr->category_index));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
                chunk, attr->compatible_class_id));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, attr->flags));
        }
    }
    nmo_chunk_close(chunk);
    *out_chunk = chunk;
    return NMO_OK;
}

nmo_status_t nmo_object_edit_ensure_message_manager_entry(
    nmo_workspace_edit_t *edit,
    const char *name,
    uint32_t *out_value)
{
    if (edit == NULL || edit->finished || name == NULL || name[0] == '\0' ||
        out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_session_t *session = nmo_workspace_internal_session(edit->workspace);
    const nmo_file_state_t *file_state =
        session ? nmo_session_get_file_state(session) : NULL;
    if (session == NULL || file_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    uint32_t manager_index = UINT32_MAX;
    const char **names = NULL;
    uint32_t name_count = 0u;
    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE)) {
            continue;
        }
        manager_index = i;
        NMO_RETURN_IF_ERROR(workspace_edit_read_message_manager_names(
            session, manager, &names, &name_count));
        for (uint32_t j = 0; j < name_count; ++j) {
            if (names[j] != NULL && strcmp(names[j], name) == 0) {
                *out_value = j;
                return NMO_OK;
            }
        }
        break;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    uint32_t new_name_count = name_count + 1u;
    const char **new_names = (const char **)nmo_arena_alloc(
        arena, (size_t)new_name_count * sizeof(*new_names),
        _Alignof(const char *));
    if (new_names == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (uint32_t i = 0; i < name_count; ++i) {
        new_names[i] = names[i] ? names[i] : "";
    }
    new_names[name_count] = nmo_arena_strdup(arena, name);
    if (new_names[name_count] == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_chunk_t *new_chunk = NULL;
    NMO_RETURN_IF_ERROR(workspace_edit_write_message_manager_chunk(
        session, new_names, new_name_count, &new_chunk));

    uint32_t old_manager_count = file_state->manager_data_count;
    nmo_manager_data_t *old_manager_data = file_state->manager_data;
    uint32_t new_manager_count = 0u;
    nmo_manager_data_t *new_manager_data = NULL;
    NMO_RETURN_IF_ERROR(workspace_edit_build_manager_data_update(
        arena,
        file_state,
        &manager_index,
        NMO_MANAGER_GUID_MESSAGE,
        new_chunk,
        &new_manager_data,
        &new_manager_count));

    NMO_RETURN_IF_ERROR(workspace_edit_replace_manager_data(
        edit,
        session,
        old_manager_data,
        old_manager_count,
        new_manager_data,
        new_manager_count));
    *out_value = name_count;
    return NMO_OK;
}

nmo_status_t nmo_object_edit_ensure_attribute_manager_entry(
    nmo_workspace_edit_t *edit,
    const char *name,
    const nmo_manager_entry_create_options_t *create_options,
    uint32_t *out_value)
{
    if (edit == NULL || edit->finished || name == NULL || name[0] == '\0' ||
        out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_session_t *session = nmo_workspace_internal_session(edit->workspace);
    const nmo_file_state_t *file_state =
        session != NULL ? nmo_session_get_file_state(session) : NULL;
    if (session == NULL || file_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    uint32_t manager_index = UINT32_MAX;
    nmo_attributemanager_state_t state = {0};
    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
            continue;
        }
        manager_index = i;
        NMO_RETURN_IF_ERROR(workspace_edit_read_attribute_manager_state(
            session, manager, &state));
        break;
    }

    for (uint32_t i = 0; i < state.attribute_count; ++i) {
        const nmo_attribute_descriptor_t *attr = &state.attributes[i];
        if (attr->present && attr->name != NULL &&
            strcmp(attr->name, name) == 0) {
            *out_value = i;
            return NMO_OK;
        }
    }

    if (create_options == NULL || !create_options->enabled ||
        nmo_guid_is_null(create_options->attribute_type_guid) ||
        create_options->category == NULL ||
        create_options->category[0] == '\0' ||
        !create_options->has_compatible_class_id ||
        !create_options->has_flags) {
        return NMO_ERR_NOT_FOUND;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    uint32_t category_index = UINT32_MAX;
    for (uint32_t i = 0; i < state.category_count; ++i) {
        const nmo_attribute_category_t *cat = &state.categories[i];
        if (cat->present && cat->name != NULL &&
            strcmp(cat->name, create_options->category) == 0) {
            category_index = i;
            break;
        }
    }

    uint32_t new_category_count = state.category_count;
    if (category_index == UINT32_MAX) {
        category_index = state.category_count;
        new_category_count = state.category_count + 1u;
    }
    uint32_t new_attribute_count = state.attribute_count + 1u;

    nmo_attribute_category_t *categories =
        (nmo_attribute_category_t *)nmo_arena_alloc(
            arena,
            (size_t)new_category_count * sizeof(*categories),
            _Alignof(nmo_attribute_category_t));
    nmo_attribute_descriptor_t *attributes =
        (nmo_attribute_descriptor_t *)nmo_arena_alloc(
            arena,
            (size_t)new_attribute_count * sizeof(*attributes),
            _Alignof(nmo_attribute_descriptor_t));
    if (categories == NULL || attributes == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(categories, 0, (size_t)new_category_count * sizeof(*categories));
    memset(attributes, 0, (size_t)new_attribute_count * sizeof(*attributes));
    if (state.category_count > 0u && state.categories != NULL) {
        memcpy(categories, state.categories,
               (size_t)state.category_count * sizeof(*categories));
    }
    if (state.attribute_count > 0u && state.attributes != NULL) {
        memcpy(attributes, state.attributes,
               (size_t)state.attribute_count * sizeof(*attributes));
    }
    if (new_category_count != state.category_count) {
        categories[category_index].present = true;
        categories[category_index].name =
            nmo_arena_strdup(arena, create_options->category);
        if (categories[category_index].name == NULL) {
            return NMO_ERR_NOMEM;
        }
        categories[category_index].flags = 0u;
    }

    uint32_t new_attribute_index = state.attribute_count;
    attributes[new_attribute_index].present = true;
    attributes[new_attribute_index].name = nmo_arena_strdup(arena, name);
    if (attributes[new_attribute_index].name == NULL) {
        return NMO_ERR_NOMEM;
    }
    attributes[new_attribute_index].parameter_type_guid =
        create_options->attribute_type_guid;
    attributes[new_attribute_index].category_index = (int32_t)category_index;
    attributes[new_attribute_index].compatible_class_id =
        (int32_t)create_options->compatible_class_id;
    attributes[new_attribute_index].flags = create_options->flags;

    nmo_attributemanager_state_t new_state = {
        .category_count = new_category_count,
        .categories = categories,
        .attribute_count = new_attribute_count,
        .attributes = attributes,
    };
    nmo_chunk_t *new_chunk = NULL;
    NMO_RETURN_IF_ERROR(workspace_edit_write_attribute_manager_chunk(
        session, &new_state, &new_chunk));

    uint32_t old_manager_count = file_state->manager_data_count;
    nmo_manager_data_t *old_manager_data = file_state->manager_data;
    uint32_t new_manager_count = 0u;
    nmo_manager_data_t *new_manager_data = NULL;
    NMO_RETURN_IF_ERROR(workspace_edit_build_manager_data_update(
        arena,
        file_state,
        &manager_index,
        NMO_MANAGER_GUID_ATTRIBUTE,
        new_chunk,
        &new_manager_data,
        &new_manager_count));

    NMO_RETURN_IF_ERROR(workspace_edit_replace_manager_data(
        edit,
        session,
        old_manager_data,
        old_manager_count,
        new_manager_data,
        new_manager_count));
    *out_value = new_attribute_index;
    return NMO_OK;
}

nmo_status_t nmo_object_edit_set_parameter_bytes_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    if (edit == NULL || edit->finished || (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_parameter_state_t *state = workspace_edit_parameter_state(
        registry, object);
    if (state == NULL || state->mode != CKPARAM_MODE_BUFFER) {
        return NMO_ERR_INVALID_STATE;
    }
    if (state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return NMO_ERR_INVALID_STATE;
    }
    bool allow_resize = options != NULL && options->resize;
    if (byte_count > state->buffer_data.count && !allow_resize) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    nmo_status_t prepare_result =
        workspace_edit_prepare_parameter_buffer_write(
            edit, state, checkpoint, byte_count, allow_resize);
    if (prepare_result != NMO_OK) {
        return prepare_result;
    }

    if (byte_count > 0) {
        memcpy(state->buffer_data.data, bytes, byte_count);
    }
    if (byte_count < state->buffer_data.count) {
        memset((uint8_t *)state->buffer_data.data + byte_count, 0,
               state->buffer_data.count - byte_count);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t nmo_object_edit_set_dataarray_cell(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_edit_checkpoint_t checkpoint = workspace_edit_checkpoint(edit);
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, dataarray_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
    if (!session_object_derives(registry, object, NMO_CID_DATAARRAY)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_DATAARRAY);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_dataarray_cell_t new_cell;
    bool is_ref = false;
    nmo_status_t parse_result =
        parse_dataarray_cell(
            state, nmo_workspace_internal_document_arena(edit->workspace),
            row, col, value_str, &new_cell, &is_ref);
    if (parse_result != NMO_OK) {
        return parse_result;
    }
    if (is_ref) {
        CK_ARRAYTYPE col_type = state->column_formats[col].type;
        nmo_object_id_t ref_id =
            col_type == CKARRAYTYPE_OBJECT
                ? nmo_ref_runtime_id(&new_cell.object_ref)
                : nmo_ref_runtime_id(&new_cell.parameter.ref);
        if (ref_id != 0) {
            nmo_object_t *ref = nmo_object_repository_find_by_id(repo, ref_id);
            if (ref == NULL) {
                return NMO_ERR_NOT_FOUND;
            }
            if (col_type == CKARRAYTYPE_PARAMETER) {
                if (!session_is_parameter_reference_object(registry, ref)) {
                    return NMO_ERR_NOT_FOUND;
                }
            }
        }
    }

    nmo_dataarray_cell_t *target_cell = &state->rows[row].cells[col];
    dataarray_cell_snapshot_t *snapshot =
        (dataarray_cell_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(dataarray_cell_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->cell = target_cell;
    snapshot->old_cell = *target_cell;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_dataarray_cell, snapshot);
    if (push_result != NMO_OK) {
        return push_result;
    }

    *target_cell = new_cell;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    if (is_ref) {
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
    }
    return NMO_OK;
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
    if (!session_object_derives(registry, parent_obj, NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_object_repository_find_by_id(repo, from_io_id) == NULL ||
        nmo_object_repository_find_by_id(repo, to_io_id) == NULL) {
        return NMO_ERR_NOT_FOUND;
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
        return create_result;
    }

    object_id_action_t *object_action = NULL;
    nmo_status_t object_action_result =
        workspace_edit_make_object_id_action(edit, link_id, &object_action);
    if (object_action_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return object_action_result;
    }
    nmo_status_t push_object_result =
        workspace_edit_push_rollback(edit, action_remove_object, object_action);
    if (push_object_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return push_object_result;
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        return workspace_edit_abort_status(edit, checkpoint, NMO_ERR_INTERNAL);
    }
    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (link_state == NULL) {
        return workspace_edit_abort_status(edit, checkpoint, NMO_ERR_INTERNAL);
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
        return workspace_edit_abort_status(edit, checkpoint, NMO_ERR_INTERNAL);
    }
    nmo_status_t append_result =
        nmo_behavior_ref_array_append(
            &parent_state->sub_behavior_links, link_id, NULL);
    if (append_result != NMO_OK) {
        return workspace_edit_abort_status(edit, checkpoint, append_result);
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
        return workspace_edit_abort_status(edit, checkpoint, array_action_result);
    }
    nmo_status_t push_array_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_remove_array_id, array_action);
    if (push_array_result != NMO_OK) {
        return push_array_result;
    }

    if (out_link_id != NULL) {
        *out_link_id = link_id;
    }
    parent_state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    parent_state->has_save_flags = true;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
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
    if (!session_object_derives(registry, link_obj, NMO_CID_BEHAVIORLINK)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (parent_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!session_object_derives(registry, parent_obj, NMO_CID_BEHAVIOR)) {
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

    array_id_action_t *array_action = NULL;
    nmo_status_t array_action_result =
        workspace_edit_make_array_id_action(
            edit,
            &parent_state->sub_behavior_links,
            link_id,
            index,
            &array_action);
    if (array_action_result != NMO_OK) {
        return array_action_result;
    }
    object_id_action_t *object_action = NULL;
    nmo_status_t object_action_result =
        workspace_edit_make_object_id_action(edit, link_id, &object_action);
    if (object_action_result != NMO_OK) {
        return object_action_result;
    }

    nmo_status_t remove_result =
        nmo_array_remove(&parent_state->sub_behavior_links, index, NULL);
    if (remove_result != NMO_OK) {
        return remove_result;
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
        return rollback_result;
    }
    nmo_status_t commit_result =
        workspace_edit_push_commit_or_abort(
            edit, checkpoint, commit_destroy_object, object_action);
    if (commit_result != NMO_OK) {
        return commit_result;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
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
    if (!session_object_derives(registry, behavior_obj, NMO_CID_BEHAVIOR)) {
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



