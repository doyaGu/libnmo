/**
 * @file script_edit_parameter.c
 * @brief Script edit primitives for parameters and parameter connections.
 */

#include "script_edit_internal.h"

#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "type/nmo_type_guids.h"

#include <string.h>

static bool script_edit_is_symbolic_manager_default_type(nmo_guid_t type_guid)
{
    return nmo_guid_equals(type_guid, CKPGUID_MESSAGE) ||
           nmo_guid_equals(type_guid, CKPGUID_ATTRIBUTE);
}

static nmo_status_t script_edit_find_message_manager_value(
    nmo_script_edit_tx_t *tx,
    const char *name,
    uint32_t *out_value)
{
    const nmo_file_state_t *file_state = NULL;
    if (!tx || !tx->session || !name || !out_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    file_state = nmo_session_get_file_state(tx->session);
    if (!file_state || !file_state->manager_data) {
        return NMO_ERR_NOT_FOUND;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            !manager->chunk) {
            continue;
        }

        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(tx->session));
        if (!chunk) {
            return NMO_ERR_NOMEM;
        }
        nmo_status_t status = nmo_chunk_start_read(chunk);
        if (status != NMO_OK) {
            return status;
        }
        size_t section_dwords = 0u;
        status = nmo_chunk_seek_identifier_with_size(
            chunk, 0x53u, &section_dwords);
        if (status == NMO_ERR_NOT_FOUND) {
            continue;
        }
        if (status != NMO_OK) {
            return status;
        }
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;

        int32_t count = 0;
        status = nmo_chunk_read_int(chunk, &count);
        if (status != NMO_OK) {
            return status;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (count < 0 || count > 10000) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if ((size_t)count > section_end - nmo_chunk_get_position(chunk)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        for (int32_t index = 0; index < count; ++index) {
            char *entry_name = NULL;
            status = nmo_chunk_read_string_checked(chunk, &entry_name, NULL);
            if (status != NMO_OK) {
                return status;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            if (entry_name && strcmp(entry_name, name) == 0) {
                *out_value = (uint32_t)index;
                return NMO_OK;
            }
        }
    }

    return NMO_ERR_NOT_FOUND;
}

static nmo_status_t script_edit_find_attribute_manager_value(
    nmo_script_edit_tx_t *tx,
    const char *name,
    uint32_t *out_value)
{
    const nmo_file_state_t *file_state = NULL;
    if (!tx || !tx->session || !name || !out_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    file_state = nmo_session_get_file_state(tx->session);
    if (!file_state || !file_state->manager_data) {
        return NMO_ERR_NOT_FOUND;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE) ||
            !manager->chunk) {
            continue;
        }

        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(tx->session));
        if (!chunk) {
            return NMO_ERR_NOMEM;
        }
        nmo_status_t status = nmo_chunk_start_read(chunk);
        if (status != NMO_OK) {
            return status;
        }
        size_t section_dwords = 0u;
        status = nmo_chunk_seek_identifier_with_size(
            chunk, 0x52u, &section_dwords);
        if (status == NMO_ERR_NOT_FOUND) {
            continue;
        }
        if (status != NMO_OK) {
            return status;
        }
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;

        int32_t category_count = 0;
        int32_t attribute_count = 0;
        status = nmo_chunk_read_int(chunk, &category_count);
        if (status == NMO_OK) {
            status = nmo_chunk_read_int(chunk, &attribute_count);
        }
        if (status != NMO_OK) {
            return status;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (category_count < 0 || category_count > 10000 ||
            attribute_count < 0 || attribute_count > 100000) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        const size_t minimum_entry_dwords =
            (size_t)category_count + (size_t)attribute_count;
        if (minimum_entry_dwords >
            section_end - nmo_chunk_get_position(chunk)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }

        for (int32_t category = 0; category < category_count; ++category) {
            int32_t present = 0;
            status = nmo_chunk_read_int(chunk, &present);
            if (status != NMO_OK) {
                return status;
            }
            if (present) {
                char *category_name = NULL;
                uint32_t flags = 0;
                status = nmo_chunk_read_string_checked(
                    chunk, &category_name, NULL);
                if (status == NMO_OK) {
                    status = nmo_chunk_read_dword(chunk, &flags);
                }
                if (status != NMO_OK) {
                    return status;
                }
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
        }

        for (int32_t attr = 0; attr < attribute_count; ++attr) {
            int32_t present = 0;
            status = nmo_chunk_read_int(chunk, &present);
            if (status != NMO_OK) {
                return status;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            if (!present) {
                continue;
            }

            char *attr_name = NULL;
            nmo_guid_t parameter_type_guid = NMO_GUID_NULL;
            int32_t category_index = 0;
            int32_t compatible_class_id = 0;
            uint32_t flags = 0;
            status = nmo_chunk_read_string_checked(
                chunk, &attr_name, NULL);
            if (status == NMO_OK) {
                status = nmo_chunk_read_guid(chunk, &parameter_type_guid);
            }
            if (status == NMO_OK) {
                status = nmo_chunk_read_int(chunk, &category_index);
            }
            if (status == NMO_OK) {
                status = nmo_chunk_read_int(chunk, &compatible_class_id);
            }
            if (status == NMO_OK) {
                status = nmo_chunk_read_dword(chunk, &flags);
            }
            if (status != NMO_OK) {
                return status;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            if (attr_name && strcmp(attr_name, name) == 0) {
                *out_value = (uint32_t)attr;
                return NMO_OK;
            }
        }
    }

    return NMO_ERR_NOT_FOUND;
}

static nmo_status_t script_edit_resolve_symbolic_manager_default(
    nmo_script_edit_tx_t *tx,
    nmo_guid_t type_guid,
    const char *default_value,
    nmo_guid_t *out_manager_guid,
    uint32_t *out_manager_value)
{
    if (!default_value || default_value[0] == '\0' ||
        !out_manager_guid || !out_manager_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_guid_equals(type_guid, CKPGUID_MESSAGE)) {
        *out_manager_guid = NMO_MANAGER_GUID_MESSAGE;
        return script_edit_find_message_manager_value(
            tx, default_value, out_manager_value);
    }
    if (nmo_guid_equals(type_guid, CKPGUID_ATTRIBUTE)) {
        *out_manager_guid = NMO_MANAGER_GUID_ATTRIBUTE;
        return script_edit_find_attribute_manager_value(
            tx, default_value, out_manager_value);
    }

    return NMO_ERR_INVALID_ARGUMENT;
}

static nmo_status_t script_edit_apply_symbolic_manager_default(
    nmo_script_edit_tx_t *tx,
    nmo_object_t *parameter_obj,
    nmo_guid_t type_guid,
    const char *default_value,
    const nmo_manager_entry_options_t *manager_entry)
{
    nmo_parameter_state_t *state = parameter_obj
        ? nmo_parameter_get_mutable_state(parameter_obj)
        : NULL;
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_guid_t manager_guid = NMO_GUID_NULL;
    uint32_t manager_value = 0u;
    nmo_manager_entry_options_t effective =
        manager_entry != NULL ? *manager_entry
                              : nmo_manager_entry_options_default();
    if (effective.schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE ||
        (!nmo_guid_is_null(effective.manager_guid) &&
         !nmo_guid_equals(effective.manager_guid, NMO_MANAGER_GUID_MESSAGE))) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    const char *entry_key =
        effective.key != NULL && effective.key[0] != '\0'
            ? effective.key
            : default_value;
    nmo_status_t rc = script_edit_resolve_symbolic_manager_default(
        tx, type_guid, entry_key, &manager_guid, &manager_value);
    if (rc != NMO_OK) {
        if (effective.policy != NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING ||
            !nmo_guid_equals(type_guid, CKPGUID_MESSAGE)) {
            return rc;
        }
        manager_guid = NMO_MANAGER_GUID_MESSAGE;
        rc = nmo_object_edit_ensure_message_manager_entry(
            tx->edit, entry_key, &manager_value);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(script_edit_note_changed_id(
            tx, NMO_OBJECT_ID_INVALID));
    }

    nmo_array_dispose(&state->buffer_data);
    state->mode = CKPARAM_MODE_MANAGER;
    state->manager_guid = manager_guid;
    state->manager_value = manager_value;
    state->has_state = true;
    return NMO_OK;
}

nmo_status_t script_edit_create_parameter_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_guid_t type_guid,
    const char *default_value,
    const nmo_manager_entry_options_t *manager_entry,
    nmo_object_id_t *out_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *parameter = NULL;
    nmo_parameterin_state_t *input_state = NULL;
    const nmo_type_descriptor_t *type_desc = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, class_id, name, NMO_GUID_NULL, out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, *out_parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_INVALID_STATE;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }
    type_desc = nmo_type_registry_find_by_guid(registry, type_guid);
    if (!type_desc) {
        return NMO_ERR_NOT_FOUND;
    }

    if (class_id == NMO_CID_PARAMETERIN) {
        input_state = (nmo_parameterin_state_t *)nmo_object_get_state(object);
        if (!input_state) {
            return NMO_ERR_INVALID_STATE;
        }
        input_state->type_guid = type_guid;
        nmo_parameterin_set_owner_id(input_state, owner_id);
        nmo_parameterin_set_source_id(input_state, NMO_OBJECT_ID_NONE);
        input_state->is_shared = 0u;
        input_state->is_disabled = 0u;
        if (default_value != NULL) {
            nmo_object_id_t source_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETER, owner_id, name, type_guid, NULL,
                manager_entry,
                &source_id);
            if (rc != NMO_OK) {
                return rc;
            }
            if (default_value[0] != '\0') {
                rc = nmo_object_edit_set_parameter_value_ex(
                    tx->edit, source_id, default_value, NULL);
                if (rc != NMO_OK) {
                    if (!script_edit_is_symbolic_manager_default_type(type_guid)) {
                        return rc;
                    }
                    nmo_object_t *source_obj =
                        nmo_object_repository_find_by_id(repo, source_id);
                    rc = script_edit_apply_symbolic_manager_default(
                        tx, source_obj, type_guid, default_value,
                        manager_entry);
                    if (rc != NMO_OK) {
                        return rc;
                    }
                }
            }
            nmo_parameterin_set_source_id(input_state, source_id);
            input_state->is_shared = 0u;
        }
    } else {
        parameter = nmo_parameter_get_mutable_state(object);
        if (!parameter) {
            return NMO_ERR_INVALID_STATE;
        }

        parameter->type_guid = type_guid;
        parameter->has_state = true;
        parameter->manager_guid = NMO_GUID_NULL;
        parameter->manager_value = 0;
        parameter->subchunk = NULL;
        parameter->object_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_array_dispose(&parameter->buffer_data);

        if ((type_desc->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0u) {
            parameter->mode = CKPARAM_MODE_OBJECT;
        } else {
            size_t buffer_size = type_desc->size;
            if (buffer_size == 0u && nmo_guid_equals(type_guid, CKPGUID_STRING)) {
                buffer_size = 1u;
            }
            if (buffer_size == 0u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            parameter->mode = CKPARAM_MODE_BUFFER;
            rc = nmo_array_alloc(&parameter->buffer_data, sizeof(uint8_t),
                                 buffer_size, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
            memset(parameter->buffer_data.data, 0, buffer_size);
        }
    }

    if (class_id == NMO_CID_PARAMETEROUT) {
        nmo_parameterout_state_t *state =
            (nmo_parameterout_state_t *)nmo_object_get_state(object);
        nmo_parameterout_set_owner_id(state, owner_id);
        state->destination_ids = NULL;
        state->destination_count = 0u;
    } else if (class_id == NMO_CID_PARAMETERLOCAL) {
        nmo_parameterlocal_state_t *state =
            (nmo_parameterlocal_state_t *)nmo_object_get_state(object);
        state->owner = nmo_ref_from_id(owner_id);
        state->is_myself = 0u;
        state->is_setting = 0u;
    }

    if (class_id != NMO_CID_PARAMETERIN &&
        default_value && default_value[0] != '\0') {
        rc = nmo_object_edit_set_parameter_value_ex(
            tx->edit, *out_parameter_id, default_value, NULL);
        if (rc != NMO_OK) {
            if (!script_edit_is_symbolic_manager_default_type(type_guid)) {
                return rc;
            }
            rc = script_edit_apply_symbolic_manager_default(
                tx, object, type_guid, default_value,
                manager_entry);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static bool script_edit_is_parameter_owner_kind(nmo_port_kind_t kind)
{
    return kind == NMO_PORT_PARAM_IN ||
           kind == NMO_PORT_PARAM_OUT ||
           kind == NMO_PORT_PARAM_LOCAL;
}

bool script_edit_find_parameter_owner(
    const nmo_behavior_index_t *index,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!index || parameter_id == 0) {
        return false;
    }

    owner = nmo_behavior_index_find(index, parameter_id);
    if (!owner || !script_edit_is_parameter_owner_kind(owner->kind)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_resolve_common_parent_graph(
    nmo_session_t *session,
    nmo_object_id_t left_owner_id,
    nmo_object_id_t right_owner_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    nmo_object_id_t left_parent_id = 0u;
    nmo_object_id_t right_parent_id = 0u;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!session || left_owner_id == 0u || right_owner_id == 0u) {
        return false;
    }

    if (left_owner_id == right_owner_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (script_edit_behavior_is_direct_graph_member(session,
                                                    right_owner_id,
                                                    left_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = right_owner_id;
        }
        return true;
    }
    if (script_edit_behavior_is_direct_graph_member(session,
                                                    left_owner_id,
                                                    right_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (!script_edit_find_direct_parent_behavior(
            session, left_owner_id, &left_parent_id) ||
        !script_edit_find_direct_parent_behavior(
            session, right_owner_id, &right_parent_id)) {
        return false;
    }

    if (left_parent_id != 0u && left_parent_id == right_parent_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_parent_id;
        }
        return true;
    }
    return false;
}

static bool script_edit_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

bool script_edit_is_parameter_reference_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (!registry || !object) {
        return false;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        script_edit_is_parameter_reference_class(
            nmo_object_get_class_id(object))) {
        return true;
    }
    return script_edit_get_object_state(
               registry,
               object,
               NMO_CID_PARAMETERIN,
               CKPGUID_PARAMETERIN) != NULL ||
           script_edit_get_object_state(
               registry,
               object,
               NMO_CID_PARAMETER,
               CKPGUID_PARAMETER) != NULL ||
           script_edit_get_object_state(
               registry,
               object,
               NMO_CID_PARAMETEROPERATION,
               CKPGUID_PARAMETEROPERATION) != NULL;
}

static bool script_edit_parameter_class_holds_value(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL;
}

nmo_parameter_state_t *script_edit_get_value_parameter_state(
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
    return (nmo_parameter_state_t *)script_edit_get_object_state(
        registry, object, NMO_CID_PARAMETER, CKPGUID_PARAMETER);
}

bool script_edit_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    if (!state || destination_id == 0u) {
        return false;
    }
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (nmo_parameterout_destination_id(state, i) == destination_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t script_edit_parameterout_remove_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_ref_t *new_ids = NULL;
    uint32_t kept = 0u;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (state->destination_count == 0u || !state->destination_ids) {
        return NMO_OK;
    }

    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (nmo_parameterout_destination_id(state, i) != destination_id) {
            kept++;
        }
    }
    if (kept == state->destination_count) {
        return NMO_OK;
    }
    if (kept > 0u) {
        uint32_t out_index = 0u;
        new_ids = (nmo_ref_t *)nmo_arena_alloc(
            nmo_workspace_internal_document_arena(tx->workspace),
            (size_t)kept * sizeof(*new_ids),
            _Alignof(nmo_ref_t));
        if (!new_ids) {
            return NMO_ERR_NOMEM;
        }
        for (uint32_t i = 0; i < state->destination_count; ++i) {
            if (nmo_parameterout_destination_id(state, i) != destination_id) {
                new_ids[out_index++] = state->destination_ids[i];
            }
        }
    }

    state->destination_ids = new_ids;
    state->destination_count = kept;
    return NMO_OK;
}

nmo_guid_t script_edit_parameter_type_guid_from_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object)
{
    if (!registry || !object) {
        return NMO_GUID_NULL;
    }
    const nmo_parameterin_state_t *input_state =
        (const nmo_parameterin_state_t *)script_edit_get_object_state(
            registry,
            object,
            NMO_CID_PARAMETERIN,
            CKPGUID_PARAMETERIN);
    if (input_state) {
        return input_state->type_guid;
    }

    const nmo_parameter_state_t *state =
        (const nmo_parameter_state_t *)script_edit_get_object_state(
            registry,
            object,
            NMO_CID_PARAMETER,
            CKPGUID_PARAMETER);
    return state ? state->type_guid : NMO_GUID_NULL;
}

static nmo_status_t script_edit_disconnect_parameter_internal(
    nmo_script_edit_tx_t *tx,
    nmo_parameterin_state_t *target_state,
    nmo_object_id_t target_parameter_id)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || !target_state || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_object_id_t source_id =
        nmo_parameterin_source_id(target_state);
    if (source_id == 0u) {
        return NMO_OK;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, target_state,
                                         sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_parameterin_set_source_id(target_state, NMO_OBJECT_ID_NONE);
    target_state->is_shared = 0u;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static bool script_edit_parameter_has_live_connections(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    size_t object_count = 0;

    if (!tx || !tx->session || parameter_id == 0u) {
        return false;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!repo || !registry) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        void *connection_state = NULL;
        if (!object ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        switch (script_edit_get_parameter_connection_state(
            registry, object, &connection_state)) {
        case NMO_CID_PARAMETERIN: {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)connection_state;
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(state);
            if (nmo_object_get_id(object) == parameter_id && source_id != 0u) {
                return true;
            }
            if (source_id == parameter_id) {
                return true;
            }
            break;
        }
        case NMO_CID_PARAMETEROUT: {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)connection_state;
            if (nmo_object_get_id(object) == parameter_id &&
                nmo_parameterout_valid_destination_count(state) > 0u) {
                return true;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                if (nmo_parameterout_destination_id(state, j) == parameter_id) {
                    return true;
                }
            }
            break;
        }
        case NMO_CID_PARAMETEROPERATION: {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)connection_state;
            if ((state->has_in1 &&
                 nmo_parameteroperation_in1_id(state) == parameter_id) ||
                (state->has_in2 &&
                 nmo_parameteroperation_in2_id(state) == parameter_id) ||
                (state->has_out &&
                 nmo_parameteroperation_out_id(state) == parameter_id)) {
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return false;
}

static nmo_status_t script_edit_detach_parameter_references(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *object = NULL;
    size_t object_count = 0;

    if (!tx || !tx->edit || !tx->session || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object || !registry) {
        return NMO_ERR_NOT_FOUND;
    }

    nmo_parameterin_state_t *target_state = (nmo_parameterin_state_t *)
        script_edit_get_object_state(
            registry,
            object,
            NMO_CID_PARAMETERIN,
            CKPGUID_PARAMETERIN);
    if (target_state) {
        nmo_status_t rc = script_edit_disconnect_parameter_internal(
            tx, target_state, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *other = nmo_object_repository_get_by_index(repo, i);
        void *connection_state = NULL;
        nmo_status_t rc = NMO_OK;

        if (!other || nmo_object_get_id(other) == parameter_id ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(other))) {
            continue;
        }

        switch (script_edit_get_parameter_connection_state(
            registry, other, &connection_state)) {
        case NMO_CID_PARAMETERIN: {
            nmo_parameterin_state_t *state =
                (nmo_parameterin_state_t *)connection_state;
            if (nmo_parameterin_source_id(state) == parameter_id) {
                rc = nmo_workspace_edit_snapshot_bytes(
                    tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                nmo_parameterin_set_source_id(state, NMO_OBJECT_ID_NONE);
                state->is_shared = 0u;
            }
            break;
        }
        case NMO_CID_PARAMETEROUT: {
            nmo_parameterout_state_t *state =
                (nmo_parameterout_state_t *)connection_state;
            if (script_edit_parameterout_has_destination(state, parameter_id)) {
                rc = nmo_workspace_edit_snapshot_bytes(
                    tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                rc = script_edit_parameterout_remove_destination(
                    tx, state, parameter_id);
                if (rc != NMO_OK) {
                    return rc;
                }
            }
            break;
        }
        case NMO_CID_PARAMETEROPERATION: {
            nmo_parameteroperation_state_t *state =
                (nmo_parameteroperation_state_t *)connection_state;
            if ((state->has_in1 &&
                 nmo_parameteroperation_in1_id(state) == parameter_id) ||
                (state->has_in2 &&
                 nmo_parameteroperation_in2_id(state) == parameter_id) ||
                (state->has_out &&
                 nmo_parameteroperation_out_id(state) == parameter_id)) {
                rc = nmo_workspace_edit_snapshot_bytes(
                    tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                if (state->has_in1 &&
                    nmo_parameteroperation_in1_id(state) == parameter_id) {
                    state->has_in1 = 0u;
                    nmo_parameteroperation_set_in1_id(state, 0u);
                }
                if (state->has_in2 &&
                    nmo_parameteroperation_in2_id(state) == parameter_id) {
                    state->has_in2 = 0u;
                    nmo_parameteroperation_set_in2_id(state, 0u);
                }
                if (state->has_out &&
                    nmo_parameteroperation_out_id(state) == parameter_id) {
                    state->has_out = 0u;
                    nmo_parameteroperation_set_out_id(state, 0u);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name,
    nmo_object_id_t *out_parameter_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_class_id_t class_id = 0;
    nmo_object_id_t parameter_id = 0;
    nmo_status_t rc = NMO_OK;

    if (out_parameter_id) {
        *out_parameter_id = 0u;
    }
    if (!tx || !tx->edit || owner_behavior_id == 0u ||
        nmo_guid_is_null(type_guid) || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        owner_behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    switch (kind) {
    case NMO_SCRIPT_EDIT_PARAM_IN:
    case NMO_SCRIPT_EDIT_PARAM_SHARED:
        class_id = NMO_CID_PARAMETERIN;
        array = &behavior->in_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_OUT:
        class_id = NMO_CID_PARAMETEROUT;
        array = &behavior->out_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_LOCAL:
        class_id = NMO_CID_PARAMETERLOCAL;
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_parameter_object(tx, class_id, owner_behavior_id, name,
                                             type_guid, NULL, NULL,
                                             &parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (kind == NMO_SCRIPT_EDIT_PARAM_SHARED) {
        nmo_parameterin_state_t *state =
            script_edit_find_parameterin_state_in_repo(
                nmo_workspace_internal_type_registry(tx->workspace),
                nmo_workspace_internal_repository(tx->workspace),
                parameter_id,
                NULL);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        state->is_shared = 1u;
    }

    rc = nmo_behavior_ref_array_append(array, parameter_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner_behavior_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                               NMO_WORKSPACE_EDIT_NAMES);

    if (out_parameter_id) {
        *out_parameter_id = parameter_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u || !value_str) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }
    object = nmo_object_repository_find_by_id(nmo_workspace_internal_repository(tx->workspace),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    state = script_edit_get_value_parameter_state(registry, object);
    if (!state) {
        return nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
               script_edit_parameter_class_holds_value(
                   nmo_object_get_class_id(object))
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_object_edit_set_parameter_value(tx->edit, parameter_id, value_str);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_bytes(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u ||
        (bytes == NULL && byte_count > 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }
    object = nmo_object_repository_find_by_id(nmo_workspace_internal_repository(tx->workspace),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    state = script_edit_get_value_parameter_state(registry, object);
    if (!state) {
        return nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
               script_edit_parameter_class_holds_value(
                   nmo_object_get_class_id(object))
            ? NMO_ERR_INVALID_STATE
            : NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_object_edit_set_parameter_bytes(tx->edit, parameter_id, bytes,
                                              byte_count);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_ensure_input_parameter_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_in_id,
    nmo_object_id_t *out_source_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *input_object = NULL;
    nmo_parameterin_state_t *input_state = NULL;
    nmo_object_id_t source_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (out_source_parameter_id != NULL) {
        *out_source_parameter_id = 0u;
    }
    if (tx == NULL || tx->edit == NULL || parameter_in_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    input_object = repo ? nmo_object_repository_find_by_id(repo, parameter_in_id) : NULL;
    if (input_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    input_state = (nmo_parameterin_state_t *)script_edit_get_object_state(
        registry,
        input_object,
        NMO_CID_PARAMETERIN,
        CKPGUID_PARAMETERIN);
    if (input_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_object_id_t existing_source_id =
        nmo_parameterin_source_id(input_state);
    if (existing_source_id != 0u) {
        if (out_source_parameter_id != NULL) {
            *out_source_parameter_id = existing_source_id;
        }
        return NMO_OK;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, input_state, sizeof(*input_state));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = script_edit_create_parameter_object(
        tx,
        NMO_CID_PARAMETER,
        nmo_parameterin_owner_id(input_state),
        nmo_object_get_name(input_object),
        input_state->type_guid,
        NULL,
        NULL,
        &source_id);
    if (rc != NMO_OK) {
        return rc;
    }

    nmo_parameterin_set_source_id(input_state, source_id);
    input_state->is_shared = 0u;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);

    if (out_source_parameter_id != NULL) {
        *out_source_parameter_id = source_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_connect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *source_owner = NULL;
    const nmo_port_owner_t *target_owner = NULL;
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *source_object = NULL;
    nmo_object_t *target_object = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_guid_t source_type = NMO_GUID_NULL;
    nmo_guid_t target_type = NMO_GUID_NULL;
    nmo_object_id_t common_parent_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || source_parameter_id == 0u || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, source_parameter_id, &source_owner) ||
        !script_edit_find_parameter_owner(index, target_parameter_id, &target_owner)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_resolve_common_parent_graph(tx->session,
                                                 source_owner->owner_id,
                                                 target_owner->owner_id,
                                                 &common_parent_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    source_object = repo ? nmo_object_repository_find_by_id(repo, source_parameter_id) : NULL;
    target_object = repo ? nmo_object_repository_find_by_id(repo, target_parameter_id) : NULL;
    if (!source_object || !target_object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_is_parameter_reference_object(
            registry, source_object)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    target_state = (nmo_parameterin_state_t *)script_edit_get_object_state(
        registry,
        target_object,
        NMO_CID_PARAMETERIN,
        CKPGUID_PARAMETERIN);
    if (!target_state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    source_type = script_edit_parameter_type_guid_from_object(
        registry, source_object);
    target_type = script_edit_parameter_type_guid_from_object(
        registry, target_object);
    if (!nmo_guid_equals(source_type, target_type)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    const nmo_object_id_t existing_source_id =
        nmo_parameterin_source_id(target_state);
    if (existing_source_id != 0u &&
        existing_source_id != source_parameter_id) {
        rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                       target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, target_state, sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_parameterin_set_source_id(target_state, source_parameter_id);
    target_state->is_shared = script_edit_get_object_state(
        registry,
        source_object,
        NMO_CID_PARAMETERIN,
        CKPGUID_PARAMETERIN) != NULL ? 1u : 0u;

    (void)common_parent_id;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_disconnect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, target_parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    target_state = script_edit_find_parameterin_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        target_parameter_id,
        NULL);
    if (!target_state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                   target_parameter_id);
    if (rc == NMO_OK) {
        tx->report.rewired_parameters++;
    }
    return rc;
}

NMO_API nmo_status_t nmo_script_edit_remove_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    bool detach)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return NMO_ERR_NOT_FOUND;
    }

    if (!detach && script_edit_parameter_has_live_connections(tx, parameter_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (detach) {
        rc = script_edit_detach_parameter_references(tx, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        owner->owner_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }

    switch (owner->kind) {
    case NMO_PORT_PARAM_IN:
        array = &behavior->in_parameters;
        break;
    case NMO_PORT_PARAM_OUT:
        array = &behavior->out_parameters;
        break;
    case NMO_PORT_PARAM_LOCAL:
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}
