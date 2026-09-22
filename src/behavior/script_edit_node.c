/**
 * @file script_edit_node.c
 * @brief Script edit primitives for behavior nodes and their IO ports.
 */

#include "script_edit_internal.h"

#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"

#include <stdlib.h>

static bool script_edit_io_is_linked_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t io_id)
{
    if (!registry || !repo || io_id == 0u) {
        return false;
    }

    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        const nmo_behavior_state_t *behavior_state =
            (const nmo_behavior_state_t *)script_edit_get_object_state(
                registry,
                behavior_obj,
                NMO_CID_BEHAVIOR,
                CKPGUID_BEHAVIOR);
        if (!behavior_state || !behavior_state->sub_behavior_links.data) {
            continue;
        }

        for (size_t j = 0; j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &behavior_state->sub_behavior_links, j);
            nmo_object_t *link_obj = link_id != 0u
                ? nmo_object_repository_find_by_id(repo, link_id)
                : NULL;
            const nmo_behaviorlink_state_t *link_state =
                (const nmo_behaviorlink_state_t *)script_edit_get_object_state(
                    registry,
                    link_obj,
                    NMO_CID_BEHAVIORLINK,
                    CKPGUID_BEHAVIORLINK);
            if (link_state &&
                (nmo_behaviorlink_in_io_id(link_state) == io_id ||
                 nmo_behaviorlink_out_io_id(link_state) == io_id)) {
                return true;
            }
        }
    }
    return false;
}

static nmo_status_t script_edit_remove_links_for_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_object_id_t deleted_root_id,
    nmo_object_id_t io_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    typedef struct matched_link {
        nmo_object_id_t parent_behavior_id;
        nmo_object_id_t link_id;
    } matched_link_t;

    matched_link_t *matched_links = NULL;
    size_t matched_count = 0u;
    size_t matched_capacity = 0u;
    size_t object_count = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->workspace || behavior_id == 0u || io_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    (void)behavior_id;

    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (repo == NULL) {
        return NMO_OK;
    }
    if (registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *behavior_state =
            (nmo_behavior_state_t *)script_edit_get_object_state(
                registry,
                behavior_obj,
                NMO_CID_BEHAVIOR,
                CKPGUID_BEHAVIOR);
        if (behavior_state == NULL ||
            behavior_state->sub_behavior_links.data == NULL) {
            continue;
        }

        for (size_t j = 0; j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &behavior_state->sub_behavior_links, j);
            if (link_id == 0) continue;
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link_state =
                (const nmo_behaviorlink_state_t *)script_edit_get_object_state(
                    registry,
                    link_obj,
                    NMO_CID_BEHAVIORLINK,
                    CKPGUID_BEHAVIORLINK);
            if (!link_state ||
                (nmo_behaviorlink_in_io_id(link_state) != io_id &&
                 nmo_behaviorlink_out_io_id(link_state) != io_id)) {
                continue;
            }
            if (deleted_root_id != 0u &&
                script_edit_behavior_is_graph_member(
                    tx->session,
                    deleted_root_id,
                    nmo_object_get_id(behavior_obj))) {
                continue;
            }

            if (matched_count == matched_capacity) {
                size_t next_capacity =
                    matched_capacity == 0u ? 4u : matched_capacity * 2u;
                matched_link_t *next = (matched_link_t *)realloc(
                    matched_links, next_capacity * sizeof(*next));
                if (next == NULL) {
                    free(matched_links);
                    return NMO_ERR_NOMEM;
                }
                matched_links = next;
                matched_capacity = next_capacity;
            }
            matched_links[matched_count++] = (matched_link_t){
                .parent_behavior_id = nmo_object_get_id(behavior_obj),
                .link_id = link_id,
            };
        }
    }

    for (size_t i = 0; i < matched_count; ++i) {
        rc = nmo_script_edit_remove_behavior_link(
            tx,
            matched_links[i].parent_behavior_id,
            matched_links[i].link_id);
        if (rc != NMO_OK) {
            free(matched_links);
            return rc;
        }
    }

    free(matched_links);
    return NMO_OK;
}

static nmo_status_t script_edit_remove_links_for_behavior_ios(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t deleted_root_id,
    const nmo_behavior_state_t *behavior)
{
    if (!tx || !behavior) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_array_t *io_arrays[] = {
        &behavior->inputs,
        &behavior->outputs,
    };
    for (size_t i = 0; i < sizeof(io_arrays) / sizeof(io_arrays[0]); ++i) {
        const nmo_array_t *array = io_arrays[i];
        for (size_t j = 0; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(script_edit_remove_links_for_io(
                tx,
                parent_behavior_id,
                deleted_root_id,
                id));
        }
    }
    for (size_t i = 0; i < behavior->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &behavior->sub_behaviors, i);
        if (sub_id == 0) continue;
        nmo_behavior_state_t *sub_state = script_edit_find_behavior_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            sub_id,
            NULL);
        if (sub_state == NULL) {
            continue;
        }
        NMO_RETURN_IF_ERROR(script_edit_remove_links_for_behavior_ios(
            tx,
            parent_behavior_id,
            deleted_root_id,
            sub_state));
    }
    return NMO_OK;
}

static nmo_status_t script_edit_append_behavior_owned_destroy_objects(
    nmo_script_edit_tx_t *tx,
    nmo_behavior_state_t *state)
{
    if (!tx || !state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < state->operations.count; ++i) {
        const nmo_object_id_t operation_id =
            nmo_behavior_ref_array_get_id(&state->operations, i);
        const nmo_parameteroperation_state_t *operation =
            script_edit_find_operation_state_in_repo(
                nmo_workspace_internal_type_registry(tx->workspace),
                nmo_workspace_internal_repository(tx->workspace),
                operation_id,
                NULL);
        if (operation == NULL) {
            continue;
        }

        NMO_RETURN_IF_ERROR(
            script_edit_append_operation_slot_destroy_objects(tx, operation));
    }

    nmo_array_t *owned_arrays[] = {
        &state->inputs,
        &state->outputs,
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
        &state->operations,
        &state->sub_behavior_links,
    };
    for (size_t i = 0; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        for (size_t j = 0; j < owned_arrays[i]->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                owned_arrays[i], j);
            if (id == 0) continue;
            nmo_status_t rc = script_edit_append_deferred_destroy(tx, id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (sub_id == 0) continue;
        nmo_behavior_state_t *sub_state = script_edit_find_behavior_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            sub_id,
            NULL);
        if (sub_state == NULL) {
            continue;
        }
        nmo_status_t rc =
            script_edit_append_behavior_owned_destroy_objects(tx, sub_state);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    nmo_object_id_t *out_node_id)
{
    return nmo_script_edit_add_node_ex(
        tx, parent_behavior_id, bb_guid, name, NULL, out_node_id);
}

NMO_API nmo_status_t nmo_script_edit_add_node_ex(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    const nmo_script_edit_add_node_options_t *options,
    nmo_object_id_t *out_node_id)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_behavior_proto_t *proto = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t node_id = 0;
    nmo_manager_entry_options_t manager_entry =
        options != NULL ? options->manager_entry
                        : nmo_manager_entry_options_default();

    if (!tx || !tx->edit || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    if (!parent_state) {
        return NMO_ERR_NOT_FOUND;
    }

    proto = nmo_behavior_registry_find(nmo_context_get_bb_registry(tx->ctx), bb_guid);
    if (!proto) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, parent_state);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_runtime_object(
        tx,
        NMO_CID_BEHAVIOR,
        (name && name[0] != '\0') ? name : proto->name,
        NMO_GUID_NULL,
        &node_id);
    if (rc != NMO_OK) {
        return rc;
    }

    node_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        node_id,
        NULL);
    if (!node_state) {
        return NMO_ERR_INVALID_STATE;
    }

    node_state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    node_state->flags &= ~CKBEHAVIOR_SCRIPT;
    node_state->flags |= proto->behavior_flags;
    node_state->compatible_class_id = proto->compatible_class_id;
    node_state->block_version = proto->version != 0u ? proto->version : 65536u;
    node_state->block_guid = bb_guid;
    node_state->priority = 0;
    nmo_behavior_set_owner_id(node_state, parent_behavior_id);

    {
        for (uint32_t i = 0; i < proto->input_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->inputs[i],
                                              NMO_SCRIPT_EDIT_IO_INPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(&node_state->inputs, io_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->outputs[i],
                                              NMO_SCRIPT_EDIT_IO_OUTPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(&node_state->outputs, io_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->input_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERIN, node_id,
                proto->input_params[i].name,
                proto->input_params[i].type_guid,
                proto->input_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->in_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETEROUT, node_id,
                proto->output_params[i].name,
                proto->output_params[i].type_guid,
                proto->output_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->out_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        if ((proto->behavior_flags & CKBEHAVIOR_TARGETABLE) != 0u) {
            nmo_type_registry_t *registry = nmo_context_get_type_registry(tx->ctx);
            nmo_guid_t target_type_guid = NMO_GUID_NULL;
            nmo_object_id_t target_parameter_id = 0;
            uint32_t target_class_id = proto->compatible_class_id != 0
                ? (uint32_t)proto->compatible_class_id
                : (uint32_t)NMO_CID_BEOBJECT;
            if (!registry) {
                return NMO_ERR_INVALID_STATE;
            }
            rc = nmo_type_registry_class_id_to_guid(
                registry,
                target_class_id,
                &target_type_guid);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERIN, node_id, "Target", target_type_guid,
                NULL,
                &manager_entry,
                &target_parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            nmo_behavior_set_target_parameter_id(
                node_state, target_parameter_id);
        }
        for (uint32_t i = 0; i < proto->local_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->local_params[i].name,
                proto->local_params[i].type_guid,
                proto->local_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->local_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->setting_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            nmo_object_t *parameter_obj = NULL;
            nmo_parameterlocal_state_t *parameter_state = NULL;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->settings[i].name,
                proto->settings[i].type_guid,
                proto->settings[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            parameter_obj = nmo_object_repository_find_by_id(
                nmo_workspace_internal_repository(tx->workspace),
                parameter_id);
            parameter_state = parameter_obj
                ? (nmo_parameterlocal_state_t *)nmo_object_get_state(parameter_obj)
                : NULL;
            if (!parameter_state) {
                return NMO_ERR_INVALID_STATE;
            }
            parameter_state->is_setting = 1u;
            rc = nmo_behavior_ref_array_append(
                &node_state->local_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    script_edit_update_behavior_save_flags(node_state);
    rc = nmo_behavior_ref_array_append(
        &parent_state->sub_behaviors, node_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
            NMO_WORKSPACE_EDIT_NAMES);

    if (out_node_id) {
        *out_node_id = node_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;
    size_t node_index = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parent_behavior_id == 0 || node_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    node_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        node_id,
        NULL);
    if (!parent_state || !node_state) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!nmo_behavior_ref_array_find(
            &parent_state->sub_behaviors, node_id, &node_index)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = script_edit_remove_links_for_behavior_ios(
        tx, parent_behavior_id, node_id, node_state);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_append_behavior_owned_destroy_objects(tx, node_state);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_internal_preview_destroy(
        tx->workspace,
        &node_id,
        1,
        delete_flags,
        nmo_workspace_internal_document_arena(tx->workspace),
        &expanded_ids,
        &expanded_count);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, parent_state);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&parent_state->sub_behaviors, node_index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    for (size_t i = 0; i < expanded_count; ++i) {
        rc = script_edit_append_deferred_destroy(tx, expanded_ids[i]);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name,
    nmo_object_id_t *out_io_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_object_id_t io_id = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || behavior_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_io_object(tx, name, kind, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    array = kind == NMO_SCRIPT_EDIT_IO_INPUT ? &behavior->inputs
                                             : &behavior->outputs;
    rc = nmo_behavior_ref_array_append(array, io_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
            NMO_WORKSPACE_EDIT_NAMES);
    rc = script_edit_note_changed_id(tx, behavior_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (out_io_id) {
        *out_io_id = io_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rename_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    const char *name)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || io_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_object_edit_rename(tx->edit, io_id, name);
    if (rc != NMO_OK) {
        return rc;
    }
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    bool detach_links)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || io_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!detach_links &&
        script_edit_io_is_linked_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (detach_links) {
        rc = script_edit_remove_links_for_io(tx, owner->owner_id, 0u, io_id);
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

    array = owner->kind == NMO_PORT_IO_IN ? &behavior->inputs : &behavior->outputs;
    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_removed_io_ref(tx,
                                           owner->owner_id,
                                           owner->kind,
                                           (size_t)owner->index);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_append_deferred_destroy(tx, io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}
