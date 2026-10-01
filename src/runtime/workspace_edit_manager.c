/**
 * @file workspace_edit_manager.c
 * @brief Message and attribute manager entry edits.
 */

#include "object/nmo_object_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"

#include "workspace_edit_internal.h"

typedef struct manager_data_snapshot {
    nmo_session_t *session;
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;
} manager_data_snapshot_t;

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

static nmo_status_t workspace_edit_seek_attribute_manager_identifier(
    nmo_chunk_t *chunk,
    size_t *out_section_end)
{
    if (chunk == NULL || out_section_end == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_read(chunk));
    size_t section_dwords = 0u;
    NMO_RETURN_IF_ERROR(nmo_chunk_seek_identifier_with_size(
        chunk, 0x52u, &section_dwords));
    *out_section_end = nmo_chunk_get_position(chunk) + section_dwords;
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
    size_t section_end = 0u;
    const nmo_status_t seek_status =
        workspace_edit_seek_attribute_manager_identifier(chunk, &section_end);
    if (seek_status == NMO_ERR_NOT_FOUND) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(seek_status);

    int32_t category_count = 0;
    int32_t attribute_count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &category_count));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &attribute_count));
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    if (category_count < 0 || attribute_count < 0) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    const size_t minimum_entry_dwords =
        (size_t)category_count + (size_t)attribute_count;
    if (minimum_entry_dwords >
        section_end - nmo_chunk_get_position(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    if ((size_t)category_count >
            SIZE_MAX / sizeof(*out_state->categories) ||
        (size_t)attribute_count >
            SIZE_MAX / sizeof(*out_state->attributes)) {
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
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
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
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
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
