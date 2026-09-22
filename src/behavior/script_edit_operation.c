/**
 * @file script_edit_operation.c
 * @brief Script edit primitives for parameter operations.
 */

#include "script_edit_internal.h"

#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "type/nmo_operation_system.h"

#include <stdint.h>
#include <string.h>

static bool script_edit_parameter_belongs_to_parent_graph(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return false;
    }
    if (!script_edit_behavior_is_direct_graph_member(session,
                                                     parent_behavior_id,
                                                     owner->owner_id)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static nmo_status_t script_edit_parameterout_append_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_ref_t *new_ids = NULL;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (script_edit_parameterout_has_destination(state, destination_id)) {
        return NMO_OK;
    }
    if (state->destination_count == UINT32_MAX) {
        return NMO_ERR_INVALID_FORMAT;
    }

    new_ids = (nmo_ref_t *)nmo_arena_alloc(
        nmo_workspace_internal_document_arena(tx->workspace),
        (size_t)(state->destination_count + 1u) * sizeof(*new_ids),
        _Alignof(nmo_ref_t));
    if (!new_ids) {
        return NMO_ERR_NOMEM;
    }

    if (state->destination_count > 0u && state->destination_ids) {
        memcpy(new_ids, state->destination_ids,
               (size_t)state->destination_count * sizeof(*new_ids));
    }
    new_ids[state->destination_count] = nmo_ref_from_id(destination_id);
    state->destination_ids = new_ids;
    state->destination_count += 1u;
    return NMO_OK;
}

static nmo_status_t script_edit_bind_operation_input(
    nmo_script_edit_tx_t *tx,
    nmo_parameterin_state_t *slot,
    nmo_object_id_t source_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *source = NULL;
    uint8_t is_shared = 0u;

    if (!tx || !tx->edit || !slot) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (source_id != 0u) {
        source = repo ? nmo_object_repository_find_by_id(repo, source_id) : NULL;
        if (!source || !registry) {
            return NMO_ERR_NOT_FOUND;
        }
        is_shared = script_edit_get_object_state(
            registry, source, NMO_CID_PARAMETERIN,
            CKPGUID_PARAMETERIN) != NULL;
        if (!is_shared && !script_edit_get_value_parameter_state(
                              registry, source)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    NMO_RETURN_IF_ERROR(nmo_workspace_edit_snapshot_bytes(
        tx->edit, slot, sizeof(*slot)));
    nmo_parameterin_set_source_id(slot, source_id);
    slot->is_shared = is_shared;
    slot->has_source = 1u;
    return NMO_OK;
}

static nmo_status_t script_edit_bind_operation_output(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *slot,
    nmo_object_id_t destination_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *destination = NULL;

    if (!tx || !tx->edit || !slot) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    repo = nmo_workspace_internal_repository(tx->workspace);
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (destination_id != 0u) {
        destination = repo
            ? nmo_object_repository_find_by_id(repo, destination_id)
            : NULL;
        if (!destination || !registry) {
            return NMO_ERR_NOT_FOUND;
        }
        if (!script_edit_get_value_parameter_state(registry, destination)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    NMO_RETURN_IF_ERROR(nmo_workspace_edit_snapshot_bytes(
        tx->edit, slot, sizeof(*slot)));
    slot->destination_ids = NULL;
    slot->destination_count = 0u;
    slot->has_destinations = destination_id != 0u;
    if (destination_id != 0u) {
        NMO_RETURN_IF_ERROR(script_edit_parameterout_append_destination(
            tx, slot, destination_id));
    }
    return NMO_OK;
}

static nmo_status_t script_edit_create_operation_input_slot(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_slot_id)
{
    nmo_status_t rc = NMO_OK;
    nmo_parameterin_state_t *slot = NULL;

    if (!nmo_guid_is_null(type_guid)) {
        return script_edit_create_parameter_object(
            tx, NMO_CID_PARAMETERIN, operation_id, name, type_guid,
            NULL, NULL, out_slot_id);
    }

    rc = script_edit_create_runtime_object(
        tx, NMO_CID_PARAMETERIN, name, NMO_GUID_NULL, out_slot_id);
    if (rc != NMO_OK) {
        return rc;
    }
    slot = script_edit_find_parameterin_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        *out_slot_id, NULL);
    if (!slot) {
        return NMO_ERR_INVALID_STATE;
    }
    slot->type_guid = NMO_GUID_NULL;
    nmo_parameterin_set_owner_id(slot, operation_id);
    nmo_parameterin_set_source_id(slot, NMO_OBJECT_ID_NONE);
    slot->is_shared = 0u;
    slot->is_disabled = 0u;
    slot->has_data = 1u;
    slot->has_source = 1u;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t script_edit_append_operation_slot_destroy_objects(
    nmo_script_edit_tx_t *tx,
    const nmo_parameteroperation_state_t *operation)
{
    if (!tx || !operation) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_object_id_t slot_ids[] = {
        nmo_parameteroperation_in1_id(operation),
        nmo_parameteroperation_in2_id(operation),
        nmo_parameteroperation_out_id(operation),
    };
    const nmo_class_id_t slot_classes[] = {
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETEROUT,
    };
    const nmo_guid_t slot_guids[] = {
        CKPGUID_PARAMETERIN,
        CKPGUID_PARAMETERIN,
        CKPGUID_PARAMETEROUT,
    };
    for (size_t i = 0; i < sizeof(slot_ids) / sizeof(slot_ids[0]); ++i) {
        if (script_edit_find_state_in_repo(
                nmo_workspace_internal_type_registry(tx->workspace),
                nmo_workspace_internal_repository(tx->workspace),
                slot_ids[i], slot_classes[i], slot_guids[i], NULL)) {
            NMO_RETURN_IF_ERROR(script_edit_append_deferred_destroy(
                tx, slot_ids[i]));
        }
    }
    return NMO_OK;
}

static const nmo_type_descriptor_t *script_edit_resolve_parameter_type_desc(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    if (!tx || !tx->ctx || !tx->session || parameter_id == 0u) {
        return NULL;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object) {
        return NULL;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    if (!registry) {
        return NULL;
    }
    type_guid = script_edit_parameter_type_guid_from_object(registry, object);
    if (nmo_guid_is_null(type_guid)) {
        return NULL;
    }

    return nmo_type_registry_find_by_guid(registry, type_guid);
}

static bool script_edit_type_matches_operation_guid(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *actual_type,
    nmo_guid_t expected_guid)
{
    nmo_type_id_t actual_id = NMO_TYPE_ID_INVALID;
    nmo_type_id_t expected_id = NMO_TYPE_ID_INVALID;

    if (!actual_type || nmo_guid_is_null(expected_guid)) {
        return false;
    }
    if (nmo_guid_equals(actual_type->guid, expected_guid)) {
        return true;
    }
    if (!registry) {
        return false;
    }

    actual_id = nmo_type_registry_guid_to_type_id(registry, actual_type->guid);
    expected_id = nmo_type_registry_guid_to_type_id(registry, expected_guid);
    if (actual_id == NMO_TYPE_ID_INVALID || expected_id == NMO_TYPE_ID_INVALID) {
        return false;
    }

    return nmo_type_get_derivation_depth((nmo_type_registry_t *)registry,
                                         actual_id, expected_id) >= 0;
}

static const nmo_operation_tree_cell_t *script_edit_find_operation_match(
    const nmo_operation_family_t *family,
    const nmo_type_registry_t *type_registry,
    const nmo_type_descriptor_t *in1_type,
    const nmo_type_descriptor_t *in2_type,
    const nmo_type_descriptor_t *out_type)
{
    if (!family) {
        return NULL;
    }

    for (size_t i = 0; i < family->p1_layers.count; ++i) {
        const nmo_operation_p1_layer_t *p1_layer =
            (const nmo_operation_p1_layer_t *)nmo_arena_array_get(
                (nmo_arena_array_t *)&family->p1_layers, i);
        if (!p1_layer) {
            continue;
        }
        if (in1_type &&
            !script_edit_type_matches_operation_guid(type_registry, in1_type,
                                                     p1_layer->p1_type_guid)) {
            continue;
        }

        for (size_t j = 0; j < p1_layer->p2_layers.count; ++j) {
            const nmo_operation_p2_layer_t *p2_layer =
                (const nmo_operation_p2_layer_t *)nmo_arena_array_get(
                    (nmo_arena_array_t *)&p1_layer->p2_layers, j);
            if (!p2_layer) {
                continue;
            }
            if (in2_type &&
                !script_edit_type_matches_operation_guid(type_registry, in2_type,
                                                         p2_layer->p2_type_guid)) {
                continue;
            }

            for (size_t k = 0; k < p2_layer->cells.count; ++k) {
                const nmo_operation_tree_cell_t *cell =
                    (const nmo_operation_tree_cell_t *)nmo_arena_array_get(
                        (nmo_arena_array_t *)&p2_layer->cells, k);
                if (!cell) {
                    continue;
                }
                if (out_type &&
                    !script_edit_type_matches_operation_guid(type_registry, out_type,
                                                             cell->desc.result_type_guid)) {
                    continue;
                }
                return cell;
            }
        }
    }

    return NULL;
}

static nmo_status_t script_edit_validate_operation_signature(
    nmo_script_edit_tx_t *tx,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id,
    const nmo_operation_tree_cell_t **out_cell)
{
    nmo_operation_registry_t *operation_registry = NULL;
    nmo_type_registry_t *type_registry = NULL;
    const nmo_operation_family_t *family = NULL;
    const nmo_type_descriptor_t *in1_type = NULL;
    const nmo_type_descriptor_t *in2_type = NULL;
    const nmo_type_descriptor_t *out_type = NULL;

    if (out_cell) {
        *out_cell = NULL;
    }
    if (!tx || !tx->ctx || nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    operation_registry = nmo_context_get_operation_registry(tx->ctx);
    type_registry = nmo_context_get_type_registry(tx->ctx);
    if (!operation_registry || !type_registry) {
        return NMO_ERR_INVALID_STATE;
    }

    family = nmo_operation_registry_get_family(operation_registry, &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u) {
        in1_type = script_edit_resolve_parameter_type_desc(tx, in1_parameter_id);
        if (!in1_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (in2_parameter_id != 0u) {
        in2_type = script_edit_resolve_parameter_type_desc(tx, in2_parameter_id);
        if (!in2_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (out_parameter_id != 0u) {
        out_type = script_edit_resolve_parameter_type_desc(tx, out_parameter_id);
        if (!out_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }

    const nmo_operation_tree_cell_t *cell =
        script_edit_find_operation_match(
            family, type_registry, in1_type, in2_type, out_type);
    if (!cell) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (out_cell) {
        *out_cell = cell;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id,
    nmo_object_id_t *out_operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_operation_family_t *family = NULL;
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_parameterin_state_t *in1_slot = NULL;
    nmo_parameterin_state_t *in2_slot = NULL;
    nmo_parameterout_state_t *out_slot = NULL;
    nmo_object_id_t operation_id = 0u;
    nmo_object_id_t in1_slot_id = 0u;
    nmo_object_id_t in2_slot_id = 0u;
    nmo_object_id_t out_slot_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (out_operation_id) {
        *out_operation_id = 0u;
    }
    if (!tx || !tx->edit || parent_behavior_id == 0u ||
        nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    family = nmo_operation_registry_get_family(
        nmo_context_get_operation_registry(tx->ctx), &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u || in2_parameter_id != 0u || out_parameter_id != 0u) {
        rc = script_edit_require_behavior_index(tx, &index);
        if (rc != NMO_OK) {
            return rc;
        }
        if ((in1_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in1_parameter_id,
                                                            NULL)) ||
            (in2_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in2_parameter_id,
                                                            NULL)) ||
            (out_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            out_parameter_id,
                                                            NULL))) {
            return NMO_ERR_VALIDATION_FAILED;
        }

    }

    rc = script_edit_validate_operation_signature(
        tx, operation_guid, in1_parameter_id, in2_parameter_id,
        out_parameter_id, &cell);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_runtime_object(tx, NMO_CID_PARAMETEROPERATION,
                                           family->name ? family->name : "Operation",
                                           NMO_GUID_NULL, &operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    state = script_edit_find_operation_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        operation_id,
        NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = script_edit_create_operation_input_slot(
        tx, operation_id, "Pin 0", cell->desc.p1_type_guid, &in1_slot_id);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = script_edit_create_operation_input_slot(
        tx, operation_id, "Pin 1", cell->desc.p2_type_guid, &in2_slot_id);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = script_edit_create_parameter_object(
        tx, NMO_CID_PARAMETEROUT, operation_id, "Pout 0",
        cell->desc.result_type_guid, NULL, NULL, &out_slot_id);
    if (rc != NMO_OK) {
        return rc;
    }

    in1_slot = script_edit_find_parameterin_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        in1_slot_id, NULL);
    in2_slot = script_edit_find_parameterin_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        in2_slot_id, NULL);
    out_slot = script_edit_find_parameterout_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        out_slot_id, NULL);
    if (!in1_slot || !in2_slot || !out_slot) {
        return NMO_ERR_INVALID_STATE;
    }
    NMO_RETURN_IF_ERROR(script_edit_bind_operation_input(
        tx, in1_slot, in1_parameter_id));
    NMO_RETURN_IF_ERROR(script_edit_bind_operation_input(
        tx, in2_slot, in2_parameter_id));
    NMO_RETURN_IF_ERROR(script_edit_bind_operation_output(
        tx, out_slot, out_parameter_id));

    state->operation_guid = operation_guid;
    nmo_parameteroperation_set_owner_id(state, parent_behavior_id);
    state->has_owner = 1u;
    state->has_in1 = 1u;
    nmo_parameteroperation_set_in1_id(state, in1_slot_id);
    state->has_in2 = 1u;
    nmo_parameteroperation_set_in2_id(state, in2_slot_id);
    state->has_out = 1u;
    nmo_parameteroperation_set_out_id(state, out_slot_id);
    state->in1.chunk = NULL;
    state->in2.chunk = NULL;
    state->out.chunk = NULL;

    rc = nmo_behavior_ref_array_append(&behavior->operations, operation_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_behavior_edit_mark_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                               NMO_WORKSPACE_EDIT_NAMES);

    if (out_operation_id) {
        *out_operation_id = operation_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rewire_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_parameterin_state_t *in1_slot = NULL;
    nmo_parameterin_state_t *in2_slot = NULL;
    nmo_parameterout_state_t *out_slot = NULL;
    nmo_object_id_t final_in1_parameter_id = 0u;
    nmo_object_id_t final_in2_parameter_id = 0u;
    nmo_object_id_t final_out_parameter_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u || slot_flags == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    if (((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u &&
         in1_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in1_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u &&
         in2_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in2_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u &&
         out_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        out_parameter_id,
                                                        NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    state = script_edit_find_operation_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        operation_id,
        NULL);
    if (!state) {
        return NMO_ERR_NOT_FOUND;
    }

    final_in1_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u)
            ? in1_parameter_id
            : (state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0u);
    final_in2_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u)
            ? in2_parameter_id
            : (state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0u);
    final_out_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u)
            ? out_parameter_id
            : (state->has_out ? nmo_parameteroperation_out_id(state) : 0u);

    rc = script_edit_validate_operation_signature(
        tx, state->operation_guid, final_in1_parameter_id,
        final_in2_parameter_id, final_out_parameter_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }

    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
        in1_slot = script_edit_find_parameterin_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            nmo_parameteroperation_in1_id(state), NULL);
        if (!in1_slot) {
            return NMO_ERR_INVALID_STATE;
        }
        NMO_RETURN_IF_ERROR(script_edit_bind_operation_input(
            tx, in1_slot, in1_parameter_id));
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
        in2_slot = script_edit_find_parameterin_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            nmo_parameteroperation_in2_id(state), NULL);
        if (!in2_slot) {
            return NMO_ERR_INVALID_STATE;
        }
        NMO_RETURN_IF_ERROR(script_edit_bind_operation_input(
            tx, in2_slot, in2_parameter_id));
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
        out_slot = script_edit_find_parameterout_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            nmo_parameteroperation_out_id(state), NULL);
        if (!out_slot) {
            return NMO_ERR_INVALID_STATE;
        }
        NMO_RETURN_IF_ERROR(script_edit_bind_operation_output(
            tx, out_slot, out_parameter_id));
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        owner->owner_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }
    state = script_edit_find_operation_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        operation_id, NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&behavior->operations, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    NMO_RETURN_IF_ERROR(
        script_edit_append_operation_slot_destroy_objects(tx, state));

    rc = script_edit_append_deferred_destroy(tx, operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}
