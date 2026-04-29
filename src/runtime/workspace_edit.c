/**
 * @file workspace_edit.c
 * @brief Workspace-scoped edit transaction implementation.
 */

#include "runtime/nmo_workspace.h"
#include "object/nmo_object_edit.h"
#include "behavior/nmo_behavior_edit.h"

#include "runtime_internal.h"
#include "runtime_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "object/nmo_statesave_ids.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_status_t (*nmo_workspace_edit_action_fn)(nmo_workspace_edit_t *edit, void *payload);

typedef struct nmo_workspace_edit_action {
    nmo_workspace_edit_action_fn fn;
    void *payload;
} nmo_workspace_edit_action_t;

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

nmo_status_t workspace_edit_set_object_fields(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_session_field_edit_t *fields,
    size_t field_count,
    nmo_session_field_edit_result_t *out_result);
nmo_status_t workspace_edit_rename_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name);
nmo_status_t workspace_edit_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str);
nmo_status_t workspace_edit_set_parameter_value_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options);
nmo_status_t workspace_edit_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count);
nmo_status_t workspace_edit_set_parameter_bytes_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options);
nmo_status_t workspace_edit_set_dataarray_cell(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str);
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
nmo_status_t workspace_edit_add_behavior_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);
nmo_status_t workspace_edit_remove_behavior_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);
nmo_status_t workspace_edit_mark_behavior_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id);

static void workspace_edit_free(nmo_workspace_edit_t *edit)
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

static nmo_status_t rollback_remove_array_id(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
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

static nmo_status_t rollback_insert_array_id(nmo_workspace_edit_t *edit, void *payload)
{
    (void)edit;
    array_id_action_t *action = (array_id_action_t *)payload;
    if (action == NULL || action->array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
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
    return nmo_object_repository_remove(repo, action->id);
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

static bool session_class_derives(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_class_id_t base_class_id)
{
    return registry != NULL &&
           nmo_type_registry_is_class_derived_from(
               registry, (uint32_t)class_id, (uint32_t)base_class_id);
}

static bool session_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
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
    nmo_status_t explicit_result =
        parse_manager_parameter_text(value_str, out_guid, out_value);
    if (explicit_result == NMO_OK ||
        workspace_edit_has_manager_value_separator(value_str) ||
        edit == NULL ||
        state == NULL ||
        !nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_MESSAGE)) {
        return explicit_result;
    }

    const char *name_begin = value_str;
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

    nmo_manager_entry_options_t manager_entry =
        options != NULL ? options->manager_entry
                        : nmo_manager_entry_options_default();
    if (manager_entry.manager != NMO_MANAGER_ENTRY_MANAGER_AUTO &&
        manager_entry.manager != NMO_MANAGER_ENTRY_MANAGER_MESSAGE) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    if (!nmo_guid_is_null(manager_entry.manager_guid) &&
        !nmo_guid_equals(manager_entry.manager_guid, NMO_MANAGER_GUID_MESSAGE)) {
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

nmo_status_t nmo_object_edit_set_fields(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_session_field_edit_t *fields,
    size_t field_count,
    nmo_session_field_edit_result_t *out_result)
{
    return workspace_edit_set_object_fields(
        (nmo_workspace_edit_t *)edit,
        object_id,
        fields,
        field_count,
        out_result);
}

nmo_status_t nmo_object_edit_rename(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name)
{
    return workspace_edit_rename_object(
        (nmo_workspace_edit_t *)edit,
        object_id,
        new_name);
}

nmo_status_t nmo_object_edit_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    return workspace_edit_set_parameter_value_ex(edit, parameter_id, value_str, NULL);
}

nmo_status_t nmo_object_edit_set_parameter_value_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    return workspace_edit_set_parameter_value_ex(
        (nmo_workspace_edit_t *)edit,
        parameter_id,
        value_str,
        options);
}

nmo_status_t nmo_object_edit_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    return workspace_edit_set_parameter_bytes_ex(edit, parameter_id, bytes, byte_count, NULL);
}

nmo_status_t nmo_object_edit_set_parameter_bytes_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    return workspace_edit_set_parameter_bytes_ex(
        (nmo_workspace_edit_t *)edit,
        parameter_id,
        bytes,
        byte_count,
        options);
}

nmo_status_t nmo_object_edit_set_dataarray_cell(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str)
{
    return workspace_edit_set_dataarray_cell(
        (nmo_workspace_edit_t *)edit,
        dataarray_id,
        row,
        col,
        value_str);
}

nmo_status_t nmo_behavior_edit_add_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id)
{
    return workspace_edit_add_behavior_link(
        (nmo_workspace_edit_t *)edit,
        parent_behavior_id,
        from_io_id,
        to_io_id,
        activation_delay,
        out_link_id);
}

nmo_status_t nmo_behavior_edit_remove_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    return workspace_edit_remove_behavior_link(
        (nmo_workspace_edit_t *)edit,
        parent_behavior_id,
        link_id);
}

nmo_status_t nmo_behavior_edit_mark_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id)
{
    return workspace_edit_mark_behavior_interface(
        (nmo_workspace_edit_t *)edit,
        behavior_id);
}

nmo_status_t nmo_workspace_edit_commit(nmo_workspace_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < edit->commit_count; i++) {
        nmo_status_t action_result =
            edit->commit_actions[i].fn(edit, edit->commit_actions[i].payload);
        if (action_result != NMO_OK) {
            workspace_edit_rollback_to(edit, 0);
            edit->finished = true;
            workspace_edit_free(edit);
            return action_result;
        }
    }

    nmo_status_t apply_result =
        nmo_workspace_internal_apply_edit_flags(edit->workspace, edit->flags);
    edit->finished = true;
    workspace_edit_free(edit);
    return apply_result;
}

void nmo_workspace_edit_rollback(nmo_workspace_edit_t *edit)
{
    if (edit == NULL || edit->finished) {
        return;
    }
    workspace_edit_rollback_to(edit, 0);
    edit->finished = true;
    workspace_edit_free(edit);
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

    field_bytes_snapshot_t *snapshot =
        (field_bytes_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot) + size, _Alignof(field_bytes_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->field_ptr = target;
    snapshot->size = size;
    memcpy(snapshot->bytes, target, size);

    return workspace_edit_push_rollback(edit, rollback_field_bytes, snapshot);
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

    object_id_action_t *rollback =
        (object_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*rollback), _Alignof(object_id_action_t));
    if (rollback == NULL) {
        return NMO_ERR_NOMEM;
    }
    rollback->id = object_id;

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

nmo_status_t workspace_edit_set_object_fields(
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

    size_t checkpoint = edit->rollback_count;
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
        nmo_type_registry_find_by_class_id_inherited(
            registry, nmo_object_get_class_id(object));
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].field_name == NULL || fields[i].value_str == NULL) {
            result.failed++;
            workspace_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_INVALID_ARGUMENT;
        }

        const nmo_type_field_t *field =
            nmo_type_get_field_by_name(type, fields[i].field_name);
        if (field == NULL) {
            result.failed++;
            workspace_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_NOT_FOUND;
        }

        void *field_ptr = (uint8_t *)state + field->offset;
        field_bytes_snapshot_t *snapshot =
            (field_bytes_snapshot_t *)nmo_workspace_edit_alloc(
                edit, sizeof(*snapshot) + field->size, _Alignof(field_bytes_snapshot_t));
        if (snapshot == NULL) {
            result.failed++;
            workspace_edit_rollback_to(edit, checkpoint);
            if (out_result != NULL) {
                *out_result = result;
            }
            return NMO_ERR_NOMEM;
        }
        snapshot->field_ptr = field_ptr;
        snapshot->size = field->size;
        memcpy(snapshot->bytes, field_ptr, field->size);

        nmo_status_t push_result =
            workspace_edit_push_rollback(edit, rollback_field_bytes, snapshot);
        if (push_result != NMO_OK) {
            result.failed++;
            workspace_edit_rollback_to(edit, checkpoint);
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
            workspace_edit_rollback_to(edit, checkpoint);
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

nmo_status_t workspace_edit_rename_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
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
        workspace_edit_push_rollback(edit, rollback_rename_object, rollback);
    if (push_result != NMO_OK) {
        return push_result;
    }

    nmo_status_t rename_result =
        nmo_object_repository_rename(repo, object_id, new_name);
    if (rename_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return rename_result;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_NAMES);
    return NMO_OK;
}

nmo_status_t workspace_edit_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    return workspace_edit_set_parameter_value_ex(edit, parameter_id, value_str, NULL);
}

static nmo_status_t workspace_edit_snapshot_parameter_buffer(
    nmo_workspace_edit_t *edit,
    nmo_parameter_state_t *state,
    size_t checkpoint)
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
        workspace_edit_push_rollback(edit, rollback_parameter_buffer, snapshot);
    if (push_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return push_result;
    }
    return NMO_OK;
}

nmo_status_t workspace_edit_set_parameter_value_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
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
            (field_bytes_snapshot_t *)nmo_workspace_edit_alloc(
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
            workspace_edit_push_rollback(edit, rollback_field_bytes, snapshot);
        if (push_result != NMO_OK) {
            workspace_edit_rollback_to(edit, checkpoint);
            return push_result;
        }

        nmo_object_id_t new_id = 0;
        nmo_status_t parse_result = parse_object_id_text(value_str, &new_id);
        if (parse_result != NMO_OK) {
            workspace_edit_rollback_to(edit, checkpoint);
            return parse_result;
        }
        state->object_id = new_id;
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
            workspace_edit_rollback_to(edit, checkpoint);
            return NMO_ERR_NOMEM;
        }
        snapshot->state = state;
        snapshot->manager_guid = state->manager_guid;
        snapshot->manager_value = state->manager_value;
        nmo_status_t push_result =
            workspace_edit_push_rollback(
                edit, rollback_parameter_manager, snapshot);
        if (push_result != NMO_OK) {
            workspace_edit_rollback_to(edit, checkpoint);
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

        nmo_status_t snapshot_result =
            workspace_edit_snapshot_parameter_buffer(edit, state, checkpoint);
        if (snapshot_result != NMO_OK) {
            return snapshot_result;
        }

        if (required_size != state->buffer_data.count) {
            nmo_status_t resize_result =
                nmo_array_resize(&state->buffer_data, required_size);
            if (resize_result != NMO_OK) {
                workspace_edit_rollback_to(edit, checkpoint);
                return resize_result;
            }
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

    nmo_status_t snapshot_result =
        workspace_edit_snapshot_parameter_buffer(edit, state, checkpoint);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }
    if (buffer_size != state->buffer_data.count && allow_resize) {
        nmo_status_t resize_result = nmo_array_resize(&state->buffer_data, buffer_size);
        if (resize_result != NMO_OK) {
            workspace_edit_rollback_to(edit, checkpoint);
            return resize_result;
        }
    }

    uint8_t *tmp = (uint8_t *)calloc(1, buffer_size);
    if (tmp == NULL) {
        workspace_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_NOMEM;
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
        workspace_edit_rollback_to(edit, checkpoint);
        return parse_result;
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

    const char **names = (const char **)nmo_arena_alloc(
        arena, (size_t)count * sizeof(*names), _Alignof(const char *));
    if (names == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(names, 0, (size_t)count * sizeof(*names));
    for (int32_t i = 0; i < count; ++i) {
        char *entry_name = NULL;
        (void)nmo_chunk_read_string(chunk, &entry_name);
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
    uint32_t new_manager_count =
        manager_index == UINT32_MAX ? old_manager_count + 1u : old_manager_count;
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
    if (manager_index == UINT32_MAX) {
        manager_index = old_manager_count;
        memset(&new_manager_data[manager_index], 0,
               sizeof(new_manager_data[manager_index]));
        new_manager_data[manager_index].guid = NMO_MANAGER_GUID_MESSAGE;
    }
    new_manager_data[manager_index].chunk = new_chunk;
    new_manager_data[manager_index].data_size =
        (uint32_t)nmo_chunk_get_size(new_chunk);
    new_manager_data[manager_index].flags = 0u;

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
    *out_value = name_count;
    return NMO_OK;
}

nmo_status_t workspace_edit_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    return workspace_edit_set_parameter_bytes_ex(edit, parameter_id, bytes, byte_count, NULL);
}

nmo_status_t workspace_edit_set_parameter_bytes_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    if (edit == NULL || edit->finished || (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
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
    bool allow_resize = options != NULL && options->resize;
    if (byte_count > state->buffer_data.count && !allow_resize) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    nmo_status_t snapshot_result =
        workspace_edit_snapshot_parameter_buffer(edit, state, checkpoint);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    if (byte_count != state->buffer_data.count && allow_resize) {
        nmo_status_t resize_result = nmo_array_resize(&state->buffer_data, byte_count);
        if (resize_result != NMO_OK) {
            workspace_edit_rollback_to(edit, checkpoint);
            return resize_result;
        }
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

nmo_status_t workspace_edit_set_dataarray_cell(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str)
{
    if (edit == NULL || edit->finished || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
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
            state, nmo_workspace_internal_document_arena(edit->workspace),
            row, col, value_str, &new_cell, &is_ref);
    if (parse_result != NMO_OK) {
        return parse_result;
    }
    if (is_ref) {
        CK_ARRAYTYPE col_type = state->column_formats[col].type;
        nmo_object_id_t ref_id =
            col_type == CKARRAYTYPE_OBJECT ? new_cell.object_id : new_cell.parameter_id;
        if (ref_id != 0) {
            nmo_object_t *ref = nmo_object_repository_find_by_id(repo, ref_id);
            if (ref == NULL) {
                return NMO_ERR_NOT_FOUND;
            }
            if (col_type == CKARRAYTYPE_PARAMETER) {
                if (!session_is_parameter_reference_class(nmo_object_get_class_id(ref))) {
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
        workspace_edit_push_rollback(edit, rollback_dataarray_cell, snapshot);
    if (push_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return push_result;
    }

    *target_cell = new_cell;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    if (is_ref) {
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
    }
    return NMO_OK;
}

nmo_status_t workspace_edit_add_behavior_link(
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

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
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
    nmo_status_t create_result =
        nmo_workspace_internal_execute_runtime_request(edit->workspace, &request, NULL);
    if (create_result != NMO_OK) {
        return create_result;
    }

    object_id_action_t *object_action =
        (object_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*object_action), _Alignof(object_id_action_t));
    if (object_action == NULL) {
        (void)nmo_object_repository_remove(repo, link_id);
        return NMO_ERR_NOMEM;
    }
    object_action->id = link_id;
    nmo_status_t push_object_result =
        workspace_edit_push_rollback(edit, action_remove_object, object_action);
    if (push_object_result != NMO_OK) {
        (void)nmo_object_repository_remove(repo, link_id);
        return push_object_result;
    }

    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (link_obj == NULL) {
        workspace_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (link_state == NULL) {
        workspace_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    link_state->in_io_id = to_io_id;
    link_state->out_io_id = from_io_id;
    link_state->activation_delay = activation_delay;
    link_state->initial_activation_delay = activation_delay;
    link_state->use_new_format = true;
    link_state->has_format = true;

    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (parent_state == NULL) {
        workspace_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_INTERNAL;
    }
    nmo_status_t append_result =
        nmo_array_append(&parent_state->sub_behavior_links, &link_id);
    if (append_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return append_result;
    }

    array_id_action_t *array_action =
        (array_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*array_action), _Alignof(array_id_action_t));
    if (array_action == NULL) {
        workspace_edit_rollback_to(edit, checkpoint);
        return NMO_ERR_NOMEM;
    }
    array_action->array = &parent_state->sub_behavior_links;
    array_action->id = link_id;
    array_action->index = parent_state->sub_behavior_links.count - 1u;
    nmo_status_t push_array_result =
        workspace_edit_push_rollback(edit, rollback_remove_array_id, array_action);
    if (push_array_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
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
    return nmo_workspace_internal_apply_edit_flags(workspace, flags);
}

nmo_status_t workspace_edit_remove_behavior_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    if (edit == NULL || edit->finished) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t checkpoint = edit->rollback_count;
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
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
        (array_id_action_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*array_action), _Alignof(array_id_action_t));
    object_id_action_t *object_action =
        (object_id_action_t *)nmo_workspace_edit_alloc(
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
    if (parent_state->sub_behavior_links.count == 0) {
        parent_state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
    } else {
        parent_state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    }
    parent_state->has_save_flags = true;

    nmo_status_t rollback_result =
        workspace_edit_push_rollback(edit, rollback_insert_array_id, array_action);
    if (rollback_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return rollback_result;
    }
    nmo_status_t commit_result =
        workspace_edit_push_commit(edit, commit_destroy_object, object_action);
    if (commit_result != NMO_OK) {
        workspace_edit_rollback_to(edit, checkpoint);
        return commit_result;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t workspace_edit_mark_behavior_interface(
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
    if (!session_class_derives(registry, nmo_object_get_class_id(behavior_obj), NMO_CID_BEHAVIOR)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
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



