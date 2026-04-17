/**
 * @file session_edit.c
 * @brief Session-scoped edit transaction implementation.
 */

#include "session/nmo_session_edit.h"

#include "runtime_internal.h"

#include "session/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_parse.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_status_t (*nmo_session_edit_action_fn)(nmo_session_t *session, void *payload);

typedef struct nmo_session_edit_action {
    nmo_session_edit_action_fn fn;
    void *payload;
} nmo_session_edit_action_t;

struct nmo_session_edit {
    nmo_session_t *session;
    nmo_arena_t *arena;
    char *label;
    nmo_session_edit_action_t *rollback_actions;
    size_t rollback_count;
    size_t rollback_capacity;
    nmo_session_edit_action_t *commit_actions;
    size_t commit_count;
    size_t commit_capacity;
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

typedef struct dataarray_cell_snapshot {
    nmo_dataarray_cell_t *cell;
    nmo_dataarray_cell_t old_cell;
} dataarray_cell_snapshot_t;

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

static void session_edit_free(nmo_session_edit_t *edit)
{
    if (edit == NULL) {
        return;
    }
    free(edit->rollback_actions);
    free(edit->commit_actions);
    free(edit->label);
    if (edit->arena != NULL) {
        nmo_arena_destroy(edit->arena);
    }
    free(edit);
}

static nmo_status_t session_edit_push_action(
    nmo_session_edit_action_t **actions,
    size_t *count,
    size_t *capacity,
    nmo_session_edit_action_fn fn,
    void *payload)
{
    if (actions == NULL || count == NULL || capacity == NULL || fn == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 8u : (*capacity * 2u);
        nmo_session_edit_action_t *new_actions =
            (nmo_session_edit_action_t *)realloc(
                *actions, new_capacity * sizeof(nmo_session_edit_action_t));
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

static nmo_status_t session_edit_push_rollback(
    nmo_session_edit_t *edit,
    nmo_session_edit_action_fn fn,
    void *payload)
{
    return session_edit_push_action(
        &edit->rollback_actions,
        &edit->rollback_count,
        &edit->rollback_capacity,
        fn,
        payload);
}

static nmo_status_t session_edit_push_commit(
    nmo_session_edit_t *edit,
    nmo_session_edit_action_fn fn,
    void *payload)
{
    return session_edit_push_action(
        &edit->commit_actions,
        &edit->commit_count,
        &edit->commit_capacity,
        fn,
        payload);
}

static void session_edit_rollback_to(nmo_session_edit_t *edit, size_t checkpoint)
{
    if (edit == NULL) {
        return;
    }
    while (edit->rollback_count > checkpoint) {
        edit->rollback_count--;
        nmo_session_edit_action_t action = edit->rollback_actions[edit->rollback_count];
        (void)action.fn(edit->session, action.payload);
    }
}

static nmo_status_t rollback_field_bytes(nmo_session_t *session, void *payload)
{
    (void)session;
    field_bytes_snapshot_t *snapshot = (field_bytes_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->field_ptr == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(snapshot->field_ptr, snapshot->bytes, snapshot->size);
    return NMO_OK;
}

static nmo_status_t rollback_parameter_buffer(nmo_session_t *session, void *payload)
{
    (void)session;
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

static nmo_status_t rollback_dataarray_cell(nmo_session_t *session, void *payload)
{
    (void)session;
    dataarray_cell_snapshot_t *snapshot = (dataarray_cell_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->cell == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *snapshot->cell = snapshot->old_cell;
    return NMO_OK;
}

static nmo_status_t rollback_remove_array_id(nmo_session_t *session, void *payload)
{
    (void)session;
    array_id_action_t *action = (array_id_action_t *)payload;
    if (action == NULL || action->array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t index = 0;
    if (nmo_array_find(action->array, &action->id, &index) != 0) {
        return nmo_array_remove(action->array, index, NULL);
    }
    return NMO_OK;
}

static nmo_status_t rollback_insert_array_id(nmo_session_t *session, void *payload)
{
    (void)session;
    array_id_action_t *action = (array_id_action_t *)payload;
    if (action == NULL || action->array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (action->index <= action->array->count) {
        return nmo_array_insert(action->array, action->index, &action->id);
    }
    return nmo_array_append(action->array, &action->id);
}

static nmo_status_t action_remove_object(nmo_session_t *session, void *payload)
{
    object_id_action_t *action = (object_id_action_t *)payload;
    if (session == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_object_repository_find_by_id(repo, action->id) == NULL) {
        return NMO_OK;
    }
    return nmo_object_repository_remove(repo, action->id);
}

static nmo_status_t commit_destroy_object(nmo_session_t *session, void *payload)
{
    object_id_action_t *action = (object_id_action_t *)payload;
    if (session == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_session_destroy_objects(
        session,
        &action->id,
        1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION,
        NULL);
}

static nmo_status_t rollback_rename_object(nmo_session_t *session, void *payload)
{
    rename_object_action_t *action = (rename_object_action_t *)payload;
    if (session == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    return nmo_object_repository_rename(repo, action->id, action->name);
}

static nmo_status_t rollback_object_chunk(nmo_session_t *session, void *payload)
{
    object_chunk_snapshot_t *snapshot = (object_chunk_snapshot_t *)payload;
    if (session == NULL || snapshot == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, snapshot->id);
    if (object == NULL) {
        return NMO_OK;
    }
    return nmo_object_set_chunk(object, snapshot->chunk);
}

static const nmo_type_registry_t *session_type_registry(nmo_session_t *session)
{
    nmo_context_t *ctx = nmo_session_get_context(session);
    return ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL;
}

static bool session_class_derives(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_class_id_t base_class_id)
{
    return registry != NULL &&
           nmo_type_registry_is_class_derived_from(
               registry, (uint32_t)class_id, (uint32_t)base_class_id);
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
        if (nmo_parse_object_id(value_str, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        if (col_type == CKARRAYTYPE_OBJECT) {
            new_cell.object_id = value;
        } else {
            new_cell.parameter_id = value;
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
    return nmo_parse_object_id(value_str, out_id);
}

nmo_status_t nmo_session_edit_begin(
    nmo_session_t *session,
    const char *label,
    nmo_session_edit_t **out_edit)
{
    if (session == NULL || out_edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_edit = NULL;
    if (nmo_session_is_partial_load(session)) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_edit_t *edit = (nmo_session_edit_t *)calloc(1, sizeof(*edit));
    if (edit == NULL) {
        return NMO_ERR_NOMEM;
    }
    edit->arena = nmo_arena_create(NULL, 16u * 1024u);
    if (edit->arena == NULL) {
        free(edit);
        return NMO_ERR_NOMEM;
    }
    edit->session = session;

    if (label != NULL) {
        size_t len = strlen(label);
        edit->label = (char *)malloc(len + 1u);
        if (edit->label == NULL) {
            session_edit_free(edit);
            return NMO_ERR_NOMEM;
        }
        memcpy(edit->label, label, len + 1u);
    }

    *out_edit = edit;
    return NMO_OK;
}

nmo_status_t nmo_session_edit_commit(nmo_session_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < edit->commit_count; i++) {
        nmo_status_t action_result =
            edit->commit_actions[i].fn(edit->session, edit->commit_actions[i].payload);
        if (action_result != NMO_OK) {
            session_edit_rollback_to(edit, 0);
            edit->finished = true;
            session_edit_free(edit);
            return action_result;
        }
    }

    nmo_status_t apply_result = nmo_session_apply_edit_flags(edit->session, edit->flags);
    edit->finished = true;
    session_edit_free(edit);
    return apply_result;
}

void nmo_session_edit_rollback(nmo_session_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return;
    }
    session_edit_rollback_to(edit, 0);
    edit->finished = true;
    session_edit_free(edit);
}

void *nmo_session_edit_alloc(nmo_session_edit_t *edit, size_t size, size_t align)
{
    if (edit == NULL || edit->arena == NULL || size == 0) {
        return NULL;
    }
    return nmo_arena_alloc(edit->arena, size, align);
}

nmo_status_t nmo_session_edit_snapshot_bytes(
    nmo_session_edit_t *edit,
    void *target,
    size_t size)
{
    if (edit == NULL || edit->finished || target == NULL || size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    field_bytes_snapshot_t *snapshot =
        (field_bytes_snapshot_t *)nmo_session_edit_alloc(
            edit, sizeof(*snapshot) + size, _Alignof(field_bytes_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->field_ptr = target;
    snapshot->size = size;
    memcpy(snapshot->bytes, target, size);

    return session_edit_push_rollback(edit, rollback_field_bytes, snapshot);
}

nmo_status_t nmo_session_edit_track_created_object(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id)
{
    if (edit == NULL || edit->finished || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (nmo_object_repository_find_by_id(repo, object_id) == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    object_id_action_t *rollback =
        (object_id_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*rollback), _Alignof(object_id_action_t));
    if (rollback == NULL) {
        return NMO_ERR_NOMEM;
    }
    rollback->id = object_id;

    nmo_status_t push_result =
        session_edit_push_rollback(edit, action_remove_object, rollback);
    if (push_result != NMO_OK) {
        return push_result;
    }
    nmo_session_edit_mark(
        edit,
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_REFERENCES |
        NMO_SESSION_EDIT_NAMES);
    return NMO_OK;
}

nmo_status_t nmo_session_edit_snapshot_object_chunk(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id)
{
    if (edit == NULL || edit->finished || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
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
        snapshot_chunk = nmo_chunk_clone(current, nmo_session_get_arena(edit->session));
        if (snapshot_chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    object_chunk_snapshot_t *snapshot =
        (object_chunk_snapshot_t *)nmo_session_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(object_chunk_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->id = object_id;
    snapshot->chunk = snapshot_chunk;

    nmo_status_t push_result =
        session_edit_push_rollback(edit, rollback_object_chunk, snapshot);
    if (push_result != NMO_OK) {
        return push_result;
    }
    nmo_session_edit_mark(
        edit,
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_REFERENCES |
        NMO_SESSION_EDIT_RESOURCES);
    return NMO_OK;
}

void nmo_session_edit_mark(nmo_session_edit_t *edit, uint32_t flags)
{
    if (edit != NULL) {
        edit->flags |= flags;
    }
}

nmo_status_t nmo_session_apply_edit_flags(nmo_session_t *session, uint32_t flags)
{
    const uint32_t known_flags =
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_REFERENCES |
        NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
        NMO_SESSION_EDIT_NAMES |
        NMO_SESSION_EDIT_RESOURCES;

    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~known_flags) != 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_session_is_partial_load(session)) {
        return NMO_ERR_INVALID_STATE;
    }
    if ((flags & NMO_SESSION_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_behavior_index(session);
        nmo_session_invalidate_ref_graph(session);
    } else if ((flags & NMO_SESSION_EDIT_REFERENCES) != 0u) {
        nmo_session_invalidate_ref_graph(session);
    }
    if ((flags & NMO_SESSION_EDIT_NAMES) != 0u) {
        nmo_session_invalidate_object_query(session, NMO_OBJECT_QUERY_INDEX_NAMES);
        return nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    }
    if ((flags & NMO_SESSION_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_object_query(
            session,
            NMO_OBJECT_QUERY_INDEX_MEMBERSHIP);
    }
    if ((flags & NMO_SESSION_EDIT_RESOURCES) != 0u) {
        /* Resource edits affect save output. No resource-derived query cache exists yet. */
    }
    return NMO_OK;
}

nmo_status_t nmo_session_edit_set_object_fields(
    nmo_session_edit_t *edit,
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

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    const nmo_type_registry_t *registry = session_type_registry(edit->session);
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
        nmo_type_registry_find_by_class_id_inherited(
            registry, nmo_object_get_class_id(object));
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].field_name == NULL || fields[i].value_str == NULL) {
            result.failed++;
            session_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_INVALID_ARGUMENT;
        }

        const nmo_type_field_t *field =
            nmo_type_get_field_by_name(type, fields[i].field_name);
        if (field == NULL) {
            result.failed++;
            session_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_NOT_FOUND;
        }

        void *field_ptr = (uint8_t *)state + field->offset;
        field_bytes_snapshot_t *snapshot =
            (field_bytes_snapshot_t *)nmo_session_edit_alloc(
                edit, sizeof(*snapshot) + field->size, _Alignof(field_bytes_snapshot_t));
        if (snapshot == NULL) {
            result.failed++;
            session_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_NOMEM;
        }
        snapshot->field_ptr = field_ptr;
        snapshot->size = field->size;
        memcpy(snapshot->bytes, field_ptr, field->size);

        nmo_status_t push_result =
            session_edit_push_rollback(edit, rollback_field_bytes, snapshot);
        if (push_result != NMO_OK) {
            result.failed++;
            session_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return push_result;
        }

        nmo_status_t set_result =
            nmo_type_set_field(
                state, type, registry, fields[i].field_name, fields[i].value_str);
        if (set_result != NMO_OK) {
            result.failed++;
            session_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return set_result;
        }

        result.applied++;
        nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE);
        if (nmo_field_is_reference(field)) {
            nmo_session_edit_mark(edit, NMO_SESSION_EDIT_REFERENCES);
        }
        if (strcmp(field->name, "name") == 0) {
            nmo_session_edit_mark(edit, NMO_SESSION_EDIT_NAMES);
        }
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    return NMO_OK;
}

nmo_status_t nmo_session_edit_rename_object(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
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
        char *copy = (char *)nmo_session_edit_alloc(edit, old_name_len, 1);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, old_name, old_name_len);
        old_name_copy = copy;
    }

    rename_object_action_t *rollback =
        (rename_object_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*rollback), _Alignof(rename_object_action_t));
    if (rollback == NULL) {
        return NMO_ERR_NOMEM;
    }
    rollback->id = object_id;
    rollback->name = old_name_copy;

    nmo_status_t push_result =
        session_edit_push_rollback(edit, rollback_rename_object, rollback);
    if (push_result != NMO_OK) {
        return push_result;
    }

    nmo_status_t rename_result =
        nmo_object_repository_rename(repo, object_id, new_name);
    if (rename_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return rename_result;
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_NAMES);
    return NMO_OK;
}

nmo_status_t nmo_session_edit_set_parameter_value(
    nmo_session_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    const nmo_type_registry_t *registry = session_type_registry(edit->session);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (state->mode == CKPARAM_MODE_OBJECT) {
        field_bytes_snapshot_t *snapshot =
            (field_bytes_snapshot_t *)nmo_session_edit_alloc(
                edit,
                sizeof(*snapshot) + sizeof(state->object_id),
                _Alignof(field_bytes_snapshot_t));
        if (snapshot == NULL) {
            return NMO_ERR_NOMEM;
        }
        snapshot->field_ptr = &state->object_id;
        snapshot->size = sizeof(state->object_id);
        memcpy(snapshot->bytes, &state->object_id, sizeof(state->object_id));

        nmo_status_t push_result =
            session_edit_push_rollback(edit, rollback_field_bytes, snapshot);
        if (push_result != NMO_OK) {
            session_edit_rollback_to(edit, checkpoint);
            return push_result;
        }

        nmo_object_id_t new_id = 0;
        nmo_status_t parse_result = parse_object_id_text(value_str, &new_id);
        if (parse_result != NMO_OK) {
            session_edit_rollback_to(edit, checkpoint);
            return parse_result;
        }
        state->object_id = new_id;
        nmo_session_edit_mark(
            edit, NMO_SESSION_EDIT_OBJECT_STATE | NMO_SESSION_EDIT_REFERENCES);
        return NMO_OK;
    }

    if (state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return NMO_ERR_INVALID_STATE;
    }

    parameter_buffer_snapshot_t *snapshot =
        (parameter_buffer_snapshot_t *)nmo_session_edit_alloc(
            edit,
            sizeof(*snapshot) + state->buffer_data.count,
            _Alignof(parameter_buffer_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->state = state;
    snapshot->count = state->buffer_data.count;
    memcpy(snapshot->bytes, state->buffer_data.data, state->buffer_data.count);

    nmo_status_t push_result =
        session_edit_push_rollback(edit, rollback_parameter_buffer, snapshot);
    if (push_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return push_result;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, state->type_guid);
    if (type == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_NOT_FOUND;
    }

    size_t buffer_size = type->size > 0 ? type->size : state->buffer_data.count;
    uint8_t *tmp = (uint8_t *)calloc(1, buffer_size);
    if (tmp == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_NOMEM;
    }

    nmo_status_t parse_result =
        nmo_type_value_from_string(tmp, type, registry, value_str);
    if (parse_result == NMO_OK) {
        size_t copy_len =
            buffer_size < state->buffer_data.count ? buffer_size : state->buffer_data.count;
        memcpy(state->buffer_data.data, tmp, copy_len);
        nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE);
        if (nmo_guid_equals(state->type_guid, CKPGUID_ID) ||
            nmo_guid_equals(state->type_guid, CKPGUID_OBJECT)) {
            nmo_session_edit_mark(edit, NMO_SESSION_EDIT_REFERENCES);
        }
    }
    free(tmp);

    if (parse_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return parse_result;
    }
    return NMO_OK;
}

nmo_status_t nmo_session_edit_set_parameter_bytes(
    nmo_session_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    if (edit == NULL || edit->finished || (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(object);
    if (state == NULL || state->mode != CKPARAM_MODE_BUFFER) {
        return NMO_ERR_INVALID_STATE;
    }
    if (state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return NMO_ERR_INVALID_STATE;
    }
    if (byte_count > state->buffer_data.count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    parameter_buffer_snapshot_t *snapshot =
        (parameter_buffer_snapshot_t *)nmo_session_edit_alloc(
            edit,
            sizeof(*snapshot) + state->buffer_data.count,
            _Alignof(parameter_buffer_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->state = state;
    snapshot->count = state->buffer_data.count;
    memcpy(snapshot->bytes, state->buffer_data.data, state->buffer_data.count);

    nmo_status_t push_result =
        session_edit_push_rollback(edit, rollback_parameter_buffer, snapshot);
    if (push_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return push_result;
    }

    if (byte_count > 0) {
        memcpy(state->buffer_data.data, bytes, byte_count);
    }
    if (byte_count < state->buffer_data.count) {
        memset((uint8_t *)state->buffer_data.data + byte_count, 0,
               state->buffer_data.count - byte_count);
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE | NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t nmo_session_edit_set_dataarray_cell(
    nmo_session_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, dataarray_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_object_get_class_id(object) != NMO_CID_DATAARRAY) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_dataarray_cell_t new_cell;
    bool is_ref = false;
    nmo_status_t parse_result =
        parse_dataarray_cell(
            state, nmo_session_get_arena(edit->session), row, col, value_str, &new_cell, &is_ref);
    if (parse_result != NMO_OK) {
        return parse_result;
    }

    nmo_dataarray_cell_t *target_cell = &state->rows[row].cells[col];
    dataarray_cell_snapshot_t *snapshot =
        (dataarray_cell_snapshot_t *)nmo_session_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(dataarray_cell_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->cell = target_cell;
    snapshot->old_cell = *target_cell;

    nmo_status_t push_result =
        session_edit_push_rollback(edit, rollback_dataarray_cell, snapshot);
    if (push_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return push_result;
    }

    *target_cell = new_cell;
    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE);
    if (is_ref) {
        nmo_session_edit_mark(edit, NMO_SESSION_EDIT_REFERENCES);
    }
    return NMO_OK;
}

nmo_status_t nmo_session_edit_add_behavior_link(
    nmo_session_edit_t *edit,
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

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    const nmo_type_registry_t *registry = session_type_registry(edit->session);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (parent_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!session_class_derives(registry, nmo_object_get_class_id(parent_obj), NMO_CID_BEHAVIOR)) {
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
    nmo_status_t create_result = nmo_session_execute(edit->session, &request, NULL);
    if (create_result != NMO_OK) {
        return create_result;
    }

    object_id_action_t *object_action =
        (object_id_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*object_action), _Alignof(object_id_action_t));
    if (object_action == NULL) {
        (void)nmo_object_repository_remove(repo, link_id);
        return NMO_ERR_NOMEM;
    }
    object_action->id = link_id;
    nmo_status_t push_object_result =
        session_edit_push_rollback(edit, action_remove_object, object_action);
    if (push_object_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return push_object_result;
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (link_state == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    link_state->in_io_id = from_io_id;
    link_state->out_io_id = to_io_id;
    link_state->activation_delay = activation_delay;
    link_state->initial_activation_delay = activation_delay;
    link_state->use_new_format = true;
    link_state->has_format = true;

    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (parent_state == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    nmo_status_t append_result =
        nmo_array_append(&parent_state->sub_behavior_links, &link_id);
    if (append_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return append_result;
    }

    array_id_action_t *array_action =
        (array_id_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*array_action), _Alignof(array_id_action_t));
    if (array_action == NULL) {
        session_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_NOMEM;
    }
    array_action->array = &parent_state->sub_behavior_links;
    array_action->id = link_id;
    array_action->index = parent_state->sub_behavior_links.count - 1u;
    nmo_status_t push_array_result =
        session_edit_push_rollback(edit, rollback_remove_array_id, array_action);
    if (push_array_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return push_array_result;
    }

    if (out_link_id != NULL) {
        *out_link_id = link_id;
    }
    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_BEHAVIOR_GRAPH | NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t nmo_session_edit_remove_behavior_link(
    nmo_session_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    const nmo_type_registry_t *registry = session_type_registry(edit->session);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_object_get_class_id(link_obj) != NMO_CID_BEHAVIORLINK) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (parent_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!session_class_derives(registry, nmo_object_get_class_id(parent_obj), NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (parent_state == NULL) {
        return NMO_ERR_INTERNAL;
    }

    size_t index = 0;
    if (nmo_array_find(&parent_state->sub_behavior_links, &link_id, &index) == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    array_id_action_t *array_action =
        (array_id_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*array_action), _Alignof(array_id_action_t));
    object_id_action_t *object_action =
        (object_id_action_t *)nmo_session_edit_alloc(
            edit, sizeof(*object_action), _Alignof(object_id_action_t));
    if (array_action == NULL || object_action == NULL) {
        return NMO_ERR_NOMEM;
    }
    array_action->array = &parent_state->sub_behavior_links;
    array_action->id = link_id;
    array_action->index = index;
    object_action->id = link_id;

    nmo_status_t remove_result =
        nmo_array_remove(&parent_state->sub_behavior_links, index, NULL);
    if (remove_result != NMO_OK) {
        return remove_result;
    }

    nmo_status_t rollback_result =
        session_edit_push_rollback(edit, rollback_insert_array_id, array_action);
    if (rollback_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return rollback_result;
    }
    nmo_status_t commit_result =
        session_edit_push_commit(edit, commit_destroy_object, object_action);
    if (commit_result != NMO_OK) {
        session_edit_rollback_to(edit, checkpoint);
        return commit_result;
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_BEHAVIOR_GRAPH | NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t nmo_session_edit_mark_behavior_interface(
    nmo_session_edit_t *edit,
    nmo_object_id_t behavior_id)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(edit->session);
    const nmo_type_registry_t *registry = session_type_registry(edit->session);
    if (repo == NULL || registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    if (behavior_obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!session_class_derives(registry, nmo_object_get_class_id(behavior_obj), NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    if (state == NULL || state->interface_data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_edit_mark(
        edit,
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
        NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}
