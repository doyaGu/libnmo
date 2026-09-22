/**
 * @file edit_plan_state.c
 * @brief Read-only document state lookups shared by the edit report and executor.
 */

#include "edit_plan_internal.h"

#include <stdlib.h>
#include <string.h>

void edit_plan_manager_snapshot_dispose(
    edit_plan_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (snapshot->message_names != NULL) {
        for (uint32_t i = 0; i < snapshot->message_name_count; ++i) {
            free((void *)snapshot->message_names[i]);
        }
        free(snapshot->message_names);
    }
    if (snapshot->attribute_entries != NULL) {
        for (uint32_t i = 0; i < snapshot->attribute_entry_count; ++i) {
            free((void *)snapshot->attribute_entries[i].name);
            free((void *)snapshot->attribute_entries[i].category);
        }
        free(snapshot->attribute_entries);
    }
    memset(snapshot, 0, sizeof(*snapshot));
}

nmo_status_t edit_plan_read_manager_snapshot(
    nmo_workspace_t *workspace,
    edit_plan_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    const nmo_file_state_t *file_state =
        session != NULL ? nmo_session_get_file_state(session) : NULL;
    if (session == NULL || file_state == NULL ||
        file_state->manager_data == NULL) {
        return NMO_OK;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            manager->chunk == NULL) {
            continue;
        }
        snapshot->has_message_manager = true;
        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(session));
        if (chunk == NULL) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_NOMEM;
        }
        nmo_status_t status = nmo_chunk_start_read(chunk);
        if (status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return status;
        }
        size_t section_dwords = 0u;
        status = nmo_chunk_seek_identifier_with_size(
            chunk, 0x53u, &section_dwords);
        if (status == NMO_ERR_NOT_FOUND) {
            return NMO_OK;
        }
        if (status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return status;
        }
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t count = 0;
        status = nmo_chunk_read_int(chunk, &count);
        if (status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return status;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (count < 0 || count > 10000) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_VALIDATION_FAILED;
        }
        if ((size_t)count > section_end - nmo_chunk_get_position(chunk)) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        snapshot->message_names =
            (const char **)calloc((size_t)count, sizeof(char *));
        if (count > 0 && snapshot->message_names == NULL) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_NOMEM;
        }
        snapshot->message_name_count = (uint32_t)count;
        for (int32_t index = 0; index < count; ++index) {
            char *name = NULL;
            status =
                nmo_chunk_read_string_checked(chunk, &name, NULL);
            if (status != NMO_OK) {
                edit_plan_manager_snapshot_dispose(snapshot);
                return status;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                edit_plan_manager_snapshot_dispose(snapshot);
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            snapshot->message_names[index] = edit_plan_strdup(name ? name : "");
            if (snapshot->message_names[index] == NULL) {
                edit_plan_manager_snapshot_dispose(snapshot);
                return NMO_ERR_NOMEM;
            }
        }
        break;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE) ||
            manager->chunk == NULL) {
            continue;
        }
        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(session));
        if (chunk == NULL) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_NOMEM;
        }
        nmo_status_t attribute_status = nmo_chunk_start_read(chunk);
        if (attribute_status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return attribute_status;
        }
        size_t section_dwords = 0u;
        attribute_status = nmo_chunk_seek_identifier_with_size(
            chunk, 0x52u, &section_dwords);
        if (attribute_status == NMO_ERR_NOT_FOUND) {
            break;
        }
        if (attribute_status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return attribute_status;
        }
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t category_count = 0;
        int32_t attribute_count = 0;
        attribute_status = nmo_chunk_read_int(chunk, &category_count);
        if (attribute_status == NMO_OK) {
            attribute_status = nmo_chunk_read_int(chunk, &attribute_count);
        }
        if (attribute_status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return attribute_status;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (category_count < 0 || category_count > 10000 ||
            attribute_count < 0 || attribute_count > 100000) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_VALIDATION_FAILED;
        }
        const size_t minimum_entry_dwords =
            (size_t)category_count + (size_t)attribute_count;
        if (minimum_entry_dwords >
            section_end - nmo_chunk_get_position(chunk)) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        const char **categories =
            (const char **)calloc((size_t)category_count, sizeof(char *));
        if (category_count > 0 && categories == NULL) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return NMO_ERR_NOMEM;
        }
        attribute_status = NMO_OK;
        for (int32_t cat = 0; cat < category_count; ++cat) {
            int32_t present = 0;
            attribute_status = nmo_chunk_read_int(chunk, &present);
            if (attribute_status != NMO_OK) {
                goto attribute_snapshot_cleanup;
            }
            if (present != 0) {
                char *name = NULL;
                uint32_t flags = 0u;
                attribute_status =
                    nmo_chunk_read_string_checked(chunk, &name, NULL);
                if (attribute_status != NMO_OK) {
                    goto attribute_snapshot_cleanup;
                }
                attribute_status = nmo_chunk_read_dword(chunk, &flags);
                if (attribute_status != NMO_OK) {
                    goto attribute_snapshot_cleanup;
                }
                categories[cat] = edit_plan_strdup(name ? name : "");
                if (categories[cat] == NULL) {
                    attribute_status = NMO_ERR_NOMEM;
                    goto attribute_snapshot_cleanup;
                }
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                attribute_status = NMO_ERR_TRUNCATED_CHUNK;
                goto attribute_snapshot_cleanup;
            }
        }
        snapshot->attribute_entries =
            (struct edit_plan_attribute_entry *)calloc(
                (size_t)attribute_count, sizeof(*snapshot->attribute_entries));
        if (attribute_count > 0 && snapshot->attribute_entries == NULL) {
            attribute_status = NMO_ERR_NOMEM;
            goto attribute_snapshot_cleanup;
        }
        snapshot->attribute_entry_count = (uint32_t)attribute_count;
        for (int32_t attr_index = 0; attr_index < attribute_count; ++attr_index) {
            int32_t present = 0;
            attribute_status = nmo_chunk_read_int(chunk, &present);
            if (attribute_status != NMO_OK) {
                goto attribute_snapshot_cleanup;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                attribute_status = NMO_ERR_TRUNCATED_CHUNK;
                goto attribute_snapshot_cleanup;
            }
            if (present == 0) {
                continue;
            }
            char *name = NULL;
            nmo_guid_t type_guid = {0};
            int32_t category_index = -1;
            int32_t compatible_class_id = 0;
            uint32_t flags = 0u;
            attribute_status =
                nmo_chunk_read_string_checked(chunk, &name, NULL);
            if (attribute_status == NMO_OK) {
                attribute_status = nmo_chunk_read_guid(chunk, &type_guid);
            }
            if (attribute_status == NMO_OK) {
                attribute_status = nmo_chunk_read_int(chunk, &category_index);
            }
            if (attribute_status == NMO_OK) {
                attribute_status =
                    nmo_chunk_read_int(chunk, &compatible_class_id);
            }
            if (attribute_status == NMO_OK) {
                attribute_status = nmo_chunk_read_dword(chunk, &flags);
            }
            if (attribute_status != NMO_OK) {
                goto attribute_snapshot_cleanup;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                attribute_status = NMO_ERR_TRUNCATED_CHUNK;
                goto attribute_snapshot_cleanup;
            }
            snapshot->attribute_entries[attr_index].name =
                edit_plan_strdup(name ? name : "");
            if (snapshot->attribute_entries[attr_index].name == NULL) {
                attribute_status = NMO_ERR_NOMEM;
                goto attribute_snapshot_cleanup;
            }
            if (category_index >= 0 && category_index < category_count &&
                categories[category_index] != NULL) {
                snapshot->attribute_entries[attr_index].category =
                    edit_plan_strdup(categories[category_index]);
                if (snapshot->attribute_entries[attr_index].category == NULL) {
                    attribute_status = NMO_ERR_NOMEM;
                    goto attribute_snapshot_cleanup;
                }
            }
            snapshot->attribute_entries[attr_index].type_guid = type_guid;
            snapshot->attribute_entries[attr_index].compatible_class_id =
                compatible_class_id < 0 ? 0u : (uint32_t)compatible_class_id;
            snapshot->attribute_entries[attr_index].flags = flags;
        }
attribute_snapshot_cleanup:
        for (int32_t cat = 0; cat < category_count; ++cat) {
            free((void *)categories[cat]);
        }
        free(categories);
        if (attribute_status != NMO_OK) {
            edit_plan_manager_snapshot_dispose(snapshot);
            return attribute_status;
        }
        break;
    }

    return NMO_OK;
}

static bool edit_plan_manager_snapshot_has_name(
    const edit_plan_manager_snapshot_t *snapshot,
    const char *name)
{
    if (snapshot == NULL || name == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < snapshot->message_name_count; ++i) {
        if (snapshot->message_names[i] != NULL &&
            strcmp(snapshot->message_names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

bool edit_plan_find_created_message_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    uint32_t *out_index)
{
    if (after == NULL || after->message_names == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < after->message_name_count; ++i) {
        const char *name = after->message_names[i];
        if (name != NULL && !edit_plan_manager_snapshot_has_name(before, name)) {
            if (out_key != NULL) {
                *out_key = name;
            }
            if (out_index != NULL) {
                *out_index = i;
            }
            return true;
        }
    }
    return false;
}

bool edit_plan_find_created_attribute_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    const char **out_category,
    nmo_guid_t *out_type_guid,
    uint32_t *out_index,
    uint32_t *out_compatible_class_id,
    uint32_t *out_flags)
{
    if (after == NULL || after->attribute_entries == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < after->attribute_entry_count; ++i) {
        const struct edit_plan_attribute_entry *entry =
            &after->attribute_entries[i];
        if (entry->name == NULL || entry->name[0] == '\0') {
            continue;
        }
        bool existed = false;
        if (before != NULL && before->attribute_entries != NULL) {
            for (uint32_t j = 0; j < before->attribute_entry_count; ++j) {
                const char *before_name = before->attribute_entries[j].name;
                if (before_name != NULL && strcmp(before_name, entry->name) == 0) {
                    existed = true;
                    break;
                }
            }
        }
        if (existed) {
            continue;
        }
        if (out_key != NULL) {
            *out_key = entry->name;
        }
        if (out_category != NULL) {
            *out_category = entry->category;
        }
        if (out_type_guid != NULL) {
            *out_type_guid = entry->type_guid;
        }
        if (out_index != NULL) {
            *out_index = i;
        }
        if (out_compatible_class_id != NULL) {
            *out_compatible_class_id = entry->compatible_class_id;
        }
        if (out_flags != NULL) {
            *out_flags = entry->flags;
        }
        return true;
    }
    return false;
}

const nmo_manager_entry_options_t *edit_plan_op_manager_entry(
    const nmo_edit_op_t *op)
{
    if (op == NULL) {
        return NULL;
    }
    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return &op->data.set_value.options.manager_entry;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return &op->data.set_bytes.options.manager_entry;
    case NMO_EDIT_OP_ADD_NODE:
        return &op->data.add_node.options.manager_entry;
    default:
        return NULL;
    }
}

void *edit_plan_get_typed_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid)
{
    if (registry == NULL || object == NULL) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == class_id) {
        return nmo_object_get_state(object);
    }
    return nmo_type_query_object_get_ancestor_state_by_guid(
        registry, object, type_guid);
}

void *edit_plan_get_object_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid)
{
    if (tx == NULL || object_id == 0u) {
        return NULL;
    }
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(workspace);
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, object_id)
        : NULL;
    return edit_plan_get_typed_object_state(
        nmo_workspace_internal_type_registry(workspace),
        object,
        class_id,
        type_guid);
}

uint32_t edit_plan_get_parameter_manager_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    const nmo_parameter_state_t *state =
        (const nmo_parameter_state_t *)edit_plan_get_object_state(
            tx, parameter_id, NMO_CID_PARAMETER, CKPGUID_PARAMETER);
    return state != NULL ? state->manager_value : 0u;
}

const nmo_behavior_state_t *edit_plan_get_behavior_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || behavior_id == 0u) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)edit_plan_get_object_state(
        tx, behavior_id, NMO_CID_BEHAVIOR, CKPGUID_BEHAVIOR);
}

const nmo_dataarray_cell_t *edit_plan_get_data_cell(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    uint32_t *out_type)
{
    if (out_type != NULL) {
        *out_type = 0u;
    }
    if (tx == NULL || dataarray_id == 0u) {
        return NULL;
    }
    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)edit_plan_get_object_state(
            tx, dataarray_id, NMO_CID_DATAARRAY, CKPGUID_DATAARRAY);
    if (state == NULL || row >= state->row_count ||
        col >= state->column_count || state->rows == NULL ||
        state->rows[row].cells == NULL || state->column_formats == NULL) {
        return NULL;
    }
    if (out_type != NULL) {
        *out_type = (uint32_t)state->column_formats[col].type;
    }
    return &state->rows[row].cells[col];
}

nmo_object_id_t edit_plan_get_parameterin_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    if (tx == NULL || target_parameter_id == 0u) {
        return 0u;
    }
    nmo_parameterin_state_t *state = (nmo_parameterin_state_t *)
        edit_plan_get_object_state(
            tx,
            target_parameter_id,
            NMO_CID_PARAMETERIN,
            CKPGUID_PARAMETERIN);
    return nmo_parameterin_source_id(state);
}

void edit_plan_get_behavior_link_endpoints(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t *out_from_io_id,
    nmo_object_id_t *out_to_io_id,
    uint32_t *out_activation_delay)
{
    if (out_from_io_id != NULL) {
        *out_from_io_id = 0u;
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = 0u;
    }
    if (out_activation_delay != NULL) {
        *out_activation_delay = 0u;
    }
    if (tx == NULL || link_id == 0u) {
        return;
    }

    nmo_behaviorlink_state_t *state = (nmo_behaviorlink_state_t *)
        edit_plan_get_object_state(
            tx, link_id, NMO_CID_BEHAVIORLINK, CKPGUID_BEHAVIORLINK);
    if (state == NULL) {
        return;
    }
    if (out_from_io_id != NULL) {
        *out_from_io_id = nmo_behaviorlink_in_io_id(state);
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = nmo_behaviorlink_out_io_id(state);
    }
    if (out_activation_delay != NULL) {
        *out_activation_delay =
            state->initial_activation_delay > 0
                ? (uint32_t)state->initial_activation_delay
                : 0u;
    }
}

void edit_plan_get_parameter_operation_slots(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    nmo_object_id_t *out_in1_parameter_id,
    nmo_object_id_t *out_in2_parameter_id,
    nmo_object_id_t *out_out_parameter_id)
{
    if (out_in1_parameter_id != NULL) {
        *out_in1_parameter_id = 0u;
    }
    if (out_in2_parameter_id != NULL) {
        *out_in2_parameter_id = 0u;
    }
    if (out_out_parameter_id != NULL) {
        *out_out_parameter_id = 0u;
    }
    if (tx == NULL || operation_id == 0u) {
        return;
    }

    nmo_parameteroperation_state_t *state =
        (nmo_parameteroperation_state_t *)edit_plan_get_object_state(
            tx,
            operation_id,
            NMO_CID_PARAMETEROPERATION,
            CKPGUID_PARAMETEROPERATION);
    if (state == NULL) {
        return;
    }
    if (out_in1_parameter_id != NULL && state->has_in1) {
        *out_in1_parameter_id = nmo_parameteroperation_in1_id(state);
    }
    if (out_in2_parameter_id != NULL && state->has_in2) {
        *out_in2_parameter_id = nmo_parameteroperation_in2_id(state);
    }
    if (out_out_parameter_id != NULL && state->has_out) {
        *out_out_parameter_id = nmo_parameteroperation_out_id(state);
    }
}

const nmo_parameteroperation_state_t *edit_plan_get_operation_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id)
{
    if (tx == NULL || operation_id == 0u) {
        return NULL;
    }

    return (const nmo_parameteroperation_state_t *)
        edit_plan_get_object_state(
            tx,
            operation_id,
            NMO_CID_PARAMETEROPERATION,
            CKPGUID_PARAMETEROPERATION);
}

nmo_class_id_t edit_plan_get_parameter_connection_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    void **out_state)
{
    if (out_state != NULL) {
        *out_state = NULL;
    }
    if (registry == NULL || object == NULL || out_state == NULL) {
        return 0;
    }

    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        if (class_id == NMO_CID_PARAMETERIN ||
            class_id == NMO_CID_PARAMETEROPERATION) {
            *out_state = nmo_object_get_state(object);
            return *out_state != NULL ? class_id : 0;
        }
    }

    *out_state = edit_plan_get_typed_object_state(
        registry, object, NMO_CID_PARAMETERIN, CKPGUID_PARAMETERIN);
    if (*out_state != NULL) {
        return NMO_CID_PARAMETERIN;
    }
    *out_state = edit_plan_get_typed_object_state(
        registry,
        object,
        NMO_CID_PARAMETEROPERATION,
        CKPGUID_PARAMETEROPERATION);
    return *out_state != NULL ? NMO_CID_PARAMETEROPERATION : 0;
}
