/**
 * @file workspace_edit_param.c
 * @brief Parameter and data array edits.
 */

#include "object/nmo_object_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_data.h"
#include "core/nmo_parse.h"
#include "type/nmo_type_guids.h"

#include <ctype.h>
#include <stdlib.h>

#include "workspace_edit_internal.h"

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

static nmo_status_t parse_object_id_text(const char *value_str, nmo_object_id_t *out_id);

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

static bool session_is_parameter_reference_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object)
{
    return workspace_edit_session_object_derives(registry, object, NMO_CID_PARAMETER);
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
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    if (state->mode == CKPARAM_MODE_OBJECT) {
        nmo_status_t snapshot_result =
            workspace_edit_push_bytes_snapshot_or_abort(
                edit, checkpoint, &state->object_ref, sizeof(state->object_ref));
        if (snapshot_result != NMO_OK) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, snapshot_result);
        }

        nmo_object_id_t new_id = 0;
        nmo_status_t parse_result = parse_object_id_text(value_str, &new_id);
        if (parse_result != NMO_OK) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, parse_result);
        }
        state->object_ref = nmo_ref_from_id(new_id);
        nmo_workspace_edit_mark(
            edit, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
        return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
    }

    if (state->mode == CKPARAM_MODE_MANAGER) {
        nmo_guid_t new_guid = NMO_GUID_NULL;
        uint32_t new_value = 0u;
        nmo_status_t parse_result =
            workspace_edit_prepare_manager_parameter_value(
                edit, state, value_str, options, &new_guid, &new_value);
        if (parse_result != NMO_OK) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, parse_result);
        }

        parameter_manager_snapshot_t *snapshot =
            (parameter_manager_snapshot_t *)nmo_workspace_edit_alloc(
                edit, sizeof(*snapshot), _Alignof(parameter_manager_snapshot_t));
        if (snapshot == NULL) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, NMO_ERR_NOMEM);
        }
        snapshot->state = state;
        snapshot->manager_guid = state->manager_guid;
        snapshot->manager_value = state->manager_value;
        nmo_status_t push_result =
            workspace_edit_push_rollback_or_abort(
                edit, checkpoint, rollback_parameter_manager, snapshot);
        if (push_result != NMO_OK) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, push_result);
        }

        state->manager_guid = new_guid;
        state->manager_value = new_value;
        nmo_workspace_edit_mark(
            edit, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                  NMO_WORKSPACE_EDIT_REFERENCES);
        return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
    }

    if (state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_INVALID_STATE);
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, state->type_guid);
    if (type == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_NOT_FOUND);
    }

    if (nmo_guid_equals(state->type_guid, CKPGUID_STRING)) {
        size_t required_size = strlen(value_str) + 1u;
        bool allow_resize = (options == NULL) ? true : options->resize;
        if (required_size > state->buffer_data.count && !allow_resize) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, NMO_ERR_OUT_OF_BOUNDS);
        }

        nmo_status_t prepare_result =
            workspace_edit_prepare_parameter_buffer_write(
                edit, state, checkpoint, required_size, true);
        if (prepare_result != NMO_OK) {
            return workspace_edit_abort_arena_status(
                edit, checkpoint, prepare_result);
        }
        memcpy(state->buffer_data.data, value_str, required_size);
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
    }

    size_t buffer_size = type->size > 0 ? type->size : state->buffer_data.count;
    bool allow_resize = options != NULL && options->resize;
    if (buffer_size > state->buffer_data.count && !allow_resize) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_OUT_OF_BOUNDS);
    }

    nmo_status_t prepare_result =
        workspace_edit_prepare_parameter_buffer_write(
            edit, state, checkpoint, buffer_size, allow_resize);
    if (prepare_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, prepare_result);
    }

    uint8_t *tmp = (uint8_t *)calloc(1, buffer_size);
    if (tmp == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_NOMEM);
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
        return workspace_edit_abort_arena_status(
            edit, checkpoint, parse_result);
    }
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
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
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    nmo_status_t prepare_result =
        workspace_edit_prepare_parameter_buffer_write(
            edit, state, checkpoint, byte_count, allow_resize);
    if (prepare_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, prepare_result);
    }

    if (byte_count > 0) {
        memcpy(state->buffer_data.data, bytes, byte_count);
    }
    if (byte_count < state->buffer_data.count) {
        memset((uint8_t *)state->buffer_data.data + byte_count, 0,
               state->buffer_data.count - byte_count);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
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
    if (!workspace_edit_session_object_derives(registry, object, NMO_CID_DATAARRAY)) {
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
                    return NMO_ERR_INVALID_ARGUMENT;
                }
                nmo_parameter_state_t *parameter_state =
                    workspace_edit_parameter_state(registry, ref);
                if (parameter_state == NULL ||
                    (!nmo_guid_is_null(state->column_formats[col].parameter_type_guid) &&
                     !nmo_guid_equals(parameter_state->type_guid,
                                      state->column_formats[col].parameter_type_guid))) {
                    return NMO_ERR_INVALID_ARGUMENT;
                }
            }
        }
    }
    nmo_status_t arena_mark_result =
        workspace_edit_checkpoint_mark_arena(edit, &checkpoint);
    if (arena_mark_result != NMO_OK) {
        return arena_mark_result;
    }

    nmo_dataarray_cell_t *target_cell = &state->rows[row].cells[col];
    dataarray_cell_snapshot_t *snapshot =
        (dataarray_cell_snapshot_t *)nmo_workspace_edit_alloc(
            edit, sizeof(*snapshot), _Alignof(dataarray_cell_snapshot_t));
    if (snapshot == NULL) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, NMO_ERR_NOMEM);
    }
    snapshot->cell = target_cell;
    snapshot->old_cell = *target_cell;

    nmo_status_t push_result =
        workspace_edit_push_rollback_or_abort(
            edit, checkpoint, rollback_dataarray_cell, snapshot);
    if (push_result != NMO_OK) {
        return workspace_edit_abort_arena_status(
            edit, checkpoint, push_result);
    }

    *target_cell = new_cell;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    if (is_ref) {
        nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
    }
    return workspace_edit_checkpoint_release_arena(edit, &checkpoint);
}
