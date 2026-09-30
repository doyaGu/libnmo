/**
 * @file edit_plan_executor.c
 * @brief Edit plan executor: handle resolution, operation dispatch, and the transaction driver.
 */

#include "edit_plan_internal.h"

#include <stdlib.h>

nmo_edit_executor_options_t nmo_edit_executor_options_default(void)
{
    nmo_edit_executor_options_t options = {0};
    options.validation_flags =
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY |
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE;
    return options;
}

static void edit_executor_set_diagnostic(
    const char **out_diagnostic_code,
    const char **out_diagnostic_message,
    const char *diagnostic_code,
    const char *diagnostic_message)
{
    if (out_diagnostic_code != NULL) {
        *out_diagnostic_code = diagnostic_code;
    }
    if (out_diagnostic_message != NULL) {
        *out_diagnostic_message = diagnostic_message;
    }
}

typedef struct edit_executor_handle_slot {
    const edit_plan_handle_ref_t *ref;
    nmo_object_id_t *id;
    const char *diagnostic_code;
    const char *diagnostic_message;
    bool resolve_input_parameter_source;
    bool source_requires_ref;
} edit_executor_handle_slot_t;

static nmo_status_t edit_executor_resolve_handle_ref(
    const nmo_edit_report_t *report,
    edit_plan_handle_ref_t ref,
    nmo_object_id_t *in_out_id,
    const char *diagnostic_code,
    const char *diagnostic_message,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    if (!ref.has_ref) {
        return NMO_OK;
    }
    nmo_status_t rc = edit_report_resolve_operation_handle(
        report, ref.operation_index, ref.handle_name, in_out_id);
    if (rc != NMO_OK) {
        edit_executor_set_diagnostic(
            out_diagnostic_code,
            out_diagnostic_message,
            diagnostic_code,
            diagnostic_message);
    }
    return rc;
}

static nmo_status_t edit_executor_resolve_input_parameter_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t *parameter_id)
{
    if (tx == NULL || parameter_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (edit_plan_get_object_state(
            tx,
            *parameter_id,
            NMO_CID_PARAMETERIN,
            CKPGUID_PARAMETERIN) != NULL) {
        return nmo_script_edit_ensure_input_parameter_source(
            tx, *parameter_id, parameter_id);
    }
    return NMO_OK;
}

static nmo_status_t edit_executor_resolve_handle_slots(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_report_t *report,
    const edit_executor_handle_slot_t *slots,
    size_t slot_count,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    if (slot_count == 0u) {
        return NMO_OK;
    }
    if (slots == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < slot_count; ++i) {
        const edit_executor_handle_slot_t *slot = &slots[i];
        if (slot->id == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        bool has_ref = slot->ref != NULL && slot->ref->has_ref;
        if (has_ref) {
            nmo_status_t rc = edit_executor_resolve_handle_ref(
                report,
                *slot->ref,
                slot->id,
                slot->diagnostic_code,
                slot->diagnostic_message,
                out_diagnostic_code,
                out_diagnostic_message);
            if (rc != NMO_OK) {
                return rc;
            }
        }

        if (slot->resolve_input_parameter_source &&
            (has_ref || !slot->source_requires_ref)) {
            nmo_status_t rc =
                edit_executor_resolve_input_parameter_source(tx, slot->id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_executor_apply_op(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_op_t *op,
    nmo_object_id_t *out_result_id,
    bool dry_run,
    nmo_edit_report_t *report,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    nmo_workspace_edit_t *edit = NULL;
    if (tx == NULL || op == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (out_result_id != NULL) {
        *out_result_id = 0;
    }
    edit = nmo_script_edit_workspace_edit(tx);
    if (edit == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE: {
        nmo_object_id_t parameter_id = op->primary_id;
        edit_executor_handle_slot_t parameter_slot = {
            .ref = &op->data.set_value.parameter_ref,
            .id = &parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation handle was not found",
            .resolve_input_parameter_source = true,
            .source_requires_ref = false,
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &parameter_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        nmo_status_t write_rc = nmo_object_edit_set_parameter_value_ex(
            edit,
            parameter_id,
            op->data.set_value.value,
            op->data.set_value.has_options ? &op->data.set_value.options : NULL);
        if (write_rc == NMO_OK) {
            nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                                     NMO_WORKSPACE_EDIT_REFERENCES);
        }
        return write_rc;
    }
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
    {
        nmo_object_id_t parameter_id = op->primary_id;
        edit_executor_handle_slot_t parameter_slot = {
            .ref = &op->data.set_bytes.parameter_ref,
            .id = &parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation parameter handle was not found",
            .resolve_input_parameter_source = true,
            .source_requires_ref = true,
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &parameter_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        return nmo_object_edit_set_parameter_bytes_ex(
            edit,
            parameter_id,
            op->data.set_bytes.bytes,
            op->data.set_bytes.byte_count,
            op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
    }
    case NMO_EDIT_OP_ADD_NODE:
        return nmo_script_edit_add_node_ex(
            tx,
            op->data.add_node.parent_behavior_id,
            op->data.add_node.bb_guid,
            op->data.add_node.name,
            op->data.add_node.has_options
                ? &(nmo_script_edit_add_node_options_t){
                      .manager_entry =
                          op->data.add_node.options.manager_entry,
                  }
                : NULL,
            out_result_id);
    case NMO_EDIT_OP_REMOVE_NODE:
    {
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_owned_deleted_objects(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_io_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_parameter_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        return nmo_script_edit_remove_node(
            tx,
            op->data.remove_node.parent_behavior_id,
            op->data.remove_node.node_id,
            op->data.remove_node.delete_flags);
    }
    case NMO_EDIT_OP_ADD_IO:
        return nmo_script_edit_add_io(
            tx,
            op->data.add_io.behavior_id,
            op->data.add_io.kind,
            op->data.add_io.name,
            out_result_id);
    case NMO_EDIT_OP_RENAME_IO:
        return nmo_script_edit_rename_io(
            tx,
            op->data.rename_io.io_id,
            op->data.rename_io.name);
    case NMO_EDIT_OP_REMOVE_IO:
    {
        if (op->data.remove_io.detach_links) {
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx,
                report,
                NMO_EDIT_OP_REMOVE_IO,
                op->data.remove_io.io_id));
        }
        return nmo_script_edit_remove_io(
            tx,
            op->data.remove_io.io_id,
            op->data.remove_io.detach_links);
    }
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK: {
        nmo_object_id_t from_io_id = op->data.add_link.from_io_id;
        nmo_object_id_t to_io_id = op->data.add_link.to_io_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.add_link.from_io_ref,
                .id = &from_io_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation output IO handle was not found",
            },
            {
                .ref = &op->data.add_link.to_io_ref,
                .id = &to_io_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input IO handle was not found",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_add_behavior_link(
            tx,
            op->data.add_link.parent_behavior_id,
            from_io_id,
            to_io_id,
            op->data.add_link.activation_delay,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_ADD_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK: {
        nmo_object_id_t before_from_io_id = 0u;
        nmo_object_id_t before_to_io_id = 0u;
        uint32_t before_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.rewire_link.link_id,
            &before_from_io_id,
            &before_to_io_id,
            &before_activation_delay);
        nmo_status_t rc = nmo_script_edit_rewire_behavior_link(
            tx,
            op->data.rewire_link.link_id,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_from_io_id = 0u;
        nmo_object_id_t after_to_io_id = 0u;
        uint32_t after_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.rewire_link.link_id,
            &after_from_io_id,
            &after_to_io_id,
            &after_activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary"));
        edit_report_set_control_link_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary",
            before_from_io_id,
            before_to_io_id,
            before_activation_delay);
        edit_report_set_control_link_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary",
            after_from_io_id,
            after_to_io_id,
            after_activation_delay);
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
    }
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY: {
        nmo_object_id_t before_from_io_id = 0u;
        nmo_object_id_t before_to_io_id = 0u;
        uint32_t before_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.set_link_delay.link_id,
            &before_from_io_id,
            &before_to_io_id,
            &before_activation_delay);
        nmo_status_t rc = nmo_script_edit_set_behavior_link_delay(
            tx,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_from_io_id = 0u;
        nmo_object_id_t after_to_io_id = 0u;
        uint32_t after_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.set_link_delay.link_id,
            &after_from_io_id,
            &after_to_io_id,
            &after_activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary"));
        edit_report_set_control_link_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary",
            before_from_io_id,
            before_to_io_id,
            before_activation_delay);
        edit_report_set_control_link_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary",
            after_from_io_id,
            after_to_io_id,
            after_activation_delay);
        return NMO_OK;
    }
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
    {
        nmo_object_id_t from_io_id = 0u;
        nmo_object_id_t to_io_id = 0u;
        uint32_t activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.remove_link.link_id,
            &from_io_id,
            &to_io_id,
            &activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            op->data.remove_link.link_id,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            "primary"));
        edit_report_set_control_link_before(
            report->deleted_objects,
            report->deleted_object_count,
            op->data.remove_link.link_id,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            "primary",
            from_io_id,
            to_io_id,
            activation_delay);
        nmo_status_t rc = nmo_script_edit_remove_behavior_link(
            tx,
            op->data.remove_link.parent_behavior_id,
            op->data.remove_link.link_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_ADD_PARAMETER:
        return nmo_script_edit_add_parameter(
            tx,
            op->data.add_parameter.owner_behavior_id,
            op->data.add_parameter.kind,
            op->data.add_parameter.type_guid,
            op->data.add_parameter.name,
            out_result_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER: {
        nmo_object_id_t target_parameter_id =
            op->data.connect_parameter.target_parameter_id;
        edit_executor_handle_slot_t target_slot = {
            .ref = &op->data.connect_parameter.target_parameter_ref,
            .id = &target_parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation parameter handle was not found",
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &target_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_object_id_t before_source_parameter_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        nmo_status_t rc = nmo_script_edit_connect_parameter(
            tx,
            op->data.connect_parameter.source_parameter_id,
            target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        if (out_result_id != NULL) {
            *out_result_id = target_parameter_id;
        }
        nmo_object_id_t after_source_parameter_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary"));
        edit_report_set_parameter_edge_before(
            report->changed_objects,
            report->changed_object_count,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary",
            before_source_parameter_id,
            target_parameter_id);
        edit_report_set_parameter_edge_after(
            report->changed_objects,
            report->changed_object_count,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary",
            after_source_parameter_id,
            target_parameter_id);
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            op->data.connect_parameter.source_parameter_id);
    }
    case NMO_EDIT_OP_DISCONNECT_PARAMETER: {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.disconnect_parameter.target_parameter_id);
        nmo_status_t rc = nmo_script_edit_disconnect_parameter(
            tx,
            op->data.disconnect_parameter.target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.disconnect_parameter.target_parameter_id);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary"));
        edit_report_set_parameter_edge_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary",
            old_source_parameter_id,
            op->data.disconnect_parameter.target_parameter_id);
        edit_report_set_parameter_edge_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary",
            after_source_parameter_id,
            op->data.disconnect_parameter.target_parameter_id);
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_REMOVE_PARAMETER:
    {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.remove_parameter.parameter_id);
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            op->data.remove_parameter.parameter_id));
        nmo_status_t rc = nmo_script_edit_remove_parameter(
            tx,
            op->data.remove_parameter.parameter_id,
            op->data.remove_parameter.detach);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_ADD_OPERATION:
    {
        nmo_object_id_t in1_parameter_id =
            op->data.add_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.add_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.add_operation.out_parameter_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.add_operation.in1_parameter_ref,
                .id = &in1_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input parameter handle was not found",
            },
            {
                .ref = &op->data.add_operation.in2_parameter_ref,
                .id = &in2_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input parameter handle was not found",
            },
            {
                .ref = &op->data.add_operation.out_parameter_ref,
                .id = &out_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation output parameter handle was not found",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_add_operation(
            tx,
            op->data.add_operation.parent_behavior_id,
            op->data.add_operation.operation_guid,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_ADD_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
    case NMO_EDIT_OP_REWIRE_OPERATION:
    {
        const nmo_parameteroperation_state_t *before_state =
            edit_plan_get_operation_state(
                tx,
                op->data.rewire_operation.operation_id);
        nmo_parameteroperation_state_t before_state_copy;
        const nmo_parameteroperation_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_object_id_t in1_parameter_id =
            op->data.rewire_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.rewire_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.rewire_operation.out_parameter_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.rewire_operation.in1_parameter_ref,
                .id = &in1_parameter_id,
                .diagnostic_code = "missing_in1_handle",
                .diagnostic_message =
                    "Failed to resolve in1 parameter handle",
            },
            {
                .ref = &op->data.rewire_operation.in2_parameter_ref,
                .id = &in2_parameter_id,
                .diagnostic_code = "missing_in2_handle",
                .diagnostic_message =
                    "Failed to resolve in2 parameter handle",
            },
            {
                .ref = &op->data.rewire_operation.out_parameter_ref,
                .id = &out_parameter_id,
                .diagnostic_code = "missing_out_handle",
                .diagnostic_message =
                    "Failed to resolve out parameter handle",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_rewire_operation(
            tx,
            op->data.rewire_operation.operation_id,
            op->data.rewire_operation.slot_flags,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
        if (rc != NMO_OK || report == NULL) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary"));
        edit_report_set_operation_slot_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary",
            before_snapshot);
        edit_report_set_operation_slot_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary",
            edit_plan_get_operation_state(
                tx,
                op->data.rewire_operation.operation_id));
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REWIRE_OPERATION,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u ? in1_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u ? in2_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u ? out_parameter_id : 0u);
    }
    case NMO_EDIT_OP_REMOVE_OPERATION: {
        const nmo_parameteroperation_state_t *before_state =
            edit_plan_get_operation_state(
                tx,
                op->data.remove_operation.operation_id);
        nmo_parameteroperation_state_t before_state_copy;
        const nmo_parameteroperation_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_object_id_t in1_parameter_id = 0u;
        nmo_object_id_t in2_parameter_id = 0u;
        nmo_object_id_t out_parameter_id = 0u;
        edit_plan_get_parameter_operation_slots(
            tx,
            op->data.remove_operation.operation_id,
            &in1_parameter_id,
            &in2_parameter_id,
            &out_parameter_id);
        nmo_status_t rc = nmo_script_edit_remove_operation(
            tx,
            op->data.remove_operation.operation_id);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(edit_report_note_operation_slot_deleted_objects(
            tx, report, NMO_EDIT_OP_REMOVE_OPERATION, before_snapshot));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            op->data.remove_operation.operation_id,
            NMO_EDIT_OP_REMOVE_OPERATION,
            "primary"));
        edit_report_set_operation_slot_before(
            report->deleted_objects,
            report->deleted_object_count,
            op->data.remove_operation.operation_id,
            NMO_EDIT_OP_REMOVE_OPERATION,
            "primary",
            before_snapshot);
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REMOVE_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
    case NMO_EDIT_OP_INTERFACE_POLICY:
    {
        const nmo_behavior_state_t *before_state =
            edit_plan_get_behavior_state(
                tx,
                op->data.interface_policy.behavior_id);
        nmo_behavior_state_t before_state_copy;
        const nmo_behavior_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_status_t rc = nmo_script_edit_apply_interface_policy(
            tx,
            op->data.interface_policy.behavior_id,
            op->data.interface_policy.mode);
        if (rc != NMO_OK || report == NULL) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary"));
        edit_report_set_interface_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary",
            before_snapshot);
        edit_report_set_interface_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary",
            edit_plan_get_behavior_state(
                tx,
                op->data.interface_policy.behavior_id));
        return NMO_OK;
    }
    case NMO_EDIT_OP_SET_DATA_CELL:
    {
        uint32_t before_type = 0u;
        const nmo_dataarray_cell_t *before_cell =
            edit_plan_get_data_cell(
                tx,
                op->data.data_cell.dataarray_id,
                op->data.data_cell.row,
                op->data.data_cell.col,
                &before_type);
        nmo_dataarray_cell_t before_cell_copy;
        const nmo_dataarray_cell_t *before_snapshot = NULL;
        if (before_cell != NULL) {
            before_cell_copy = *before_cell;
            before_snapshot = &before_cell_copy;
        }
        nmo_status_t rc = nmo_object_edit_set_dataarray_cell(
            edit,
            op->data.data_cell.dataarray_id,
            op->data.data_cell.row,
            op->data.data_cell.col,
            op->data.data_cell.value);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell"));
        edit_report_set_data_cell_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell",
            op->data.data_cell.row,
            op->data.data_cell.col,
            before_type,
            before_snapshot);
        uint32_t after_type = 0u;
        const nmo_dataarray_cell_t *after_cell =
            edit_plan_get_data_cell(
                tx,
                op->data.data_cell.dataarray_id,
                op->data.data_cell.row,
                op->data.data_cell.col,
                &after_type);
        edit_report_set_data_cell_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell",
            op->data.data_cell.row,
            op->data.data_cell.col,
            after_type,
            after_cell);
        return NMO_OK;
    }
    case NMO_EDIT_OP_REPLACE_BB: {
        nmo_behavior_replace_report_t replace_report = {0};
        nmo_status_t rc = nmo_behavior_edit_replace_bb_in_edit(
            nmo_script_edit_workspace(tx),
            edit,
            &op->data.replace_bb.desc,
            &replace_report);
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = replace_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = replace_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = op->data.replace_bb.desc.behavior_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                replace_report.semantic_risks,
                replace_report.semantic_risk_count);
        }
        free(replace_report.semantic_risks);
        return rc;
    }
    case NMO_EDIT_OP_FOLD: {
        nmo_behavior_fold_report_t fold_report = {0};
        nmo_status_t rc = dry_run
            ? nmo_behavior_edit_fold_analyze(
                  nmo_script_edit_workspace(tx),
                  &op->data.fold.desc,
                  &fold_report)
            : nmo_behavior_edit_fold_in_script_tx(
                  tx,
                  &op->data.fold.desc,
                  &fold_report);
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = fold_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = fold_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = fold_report.anchor_id != 0u
                ? fold_report.anchor_id
                : op->data.fold.desc.anchor_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                fold_report.semantic_risks,
                fold_report.semantic_risk_count);
            if (rc == NMO_OK) {
                rc = edit_report_note_fold_impact(
                    report, &fold_report, op->data.fold.desc.parent_id);
            }
        }
        nmo_behavior_edit_fold_report_free(&fold_report);
        return rc;
    }
    default:
        return NMO_ERR_NOT_SUPPORTED;
    }
}

static nmo_object_id_t edit_op_deleted_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.node_id;
    case NMO_EDIT_OP_REMOVE_IO:
        return op->data.remove_io.io_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.link_id;
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return op->data.remove_parameter.parameter_id;
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return op->data.remove_operation.operation_id;
    default:
        return 0;
    }
}

static nmo_object_id_t edit_op_changed_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return op->data.add_node.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.parent_behavior_id;
    case NMO_EDIT_OP_ADD_IO:
        return op->data.add_io.behavior_id;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return op->data.add_link.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.parent_behavior_id;
    case NMO_EDIT_OP_ADD_PARAMETER:
        return op->data.add_parameter.owner_behavior_id;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        return op->data.connect_parameter.target_parameter_id;
    case NMO_EDIT_OP_ADD_OPERATION:
        return op->data.add_operation.parent_behavior_id;
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return op->data.interface_policy.behavior_id;
    default:
        return op->primary_id;
    }
}

static nmo_status_t edit_executor_validate(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    uint32_t validation_flags)
{
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u) {
        report->validation.roundtrip_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
        if (report->validation.roundtrip_status != NMO_OK) {
            report->validation.final_status = report->validation.roundtrip_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        report->validation.reference_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
        if (report->validation.reference_status != NMO_OK) {
            report->validation.final_status = report->validation.reference_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX) != 0u) {
        report->validation.behavior_index_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
        if (report->validation.behavior_index_status != NMO_OK) {
            report->validation.final_status =
                report->validation.behavior_index_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        report->validation.interface_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE);
        if (report->validation.interface_status != NMO_OK) {
            report->validation.final_status = report->validation.interface_status;
            return report->validation.final_status;
        }
    }
    report->validation.final_status = NMO_OK;
    return NMO_OK;
}

static nmo_status_t edit_executor_validate_semantics(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    nmo_edit_report_t *report,
    bool allow_rewrite_analysis_failure)
{
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    nmo_status_t rc = nmo_semantic_validate_edit_plan(
        nmo_script_edit_workspace(tx), plan, &risks, &risk_count);
    if (rc != NMO_OK) {
        nmo_semantic_risks_free(risks);
        if (allow_rewrite_analysis_failure &&
            (rc == NMO_ERR_INVALID_STATE || rc == NMO_ERR_NOT_FOUND ||
             rc == NMO_ERR_VALIDATION_FAILED)) {
            return NMO_OK;
        }
        return rc;
    }
    rc = nmo_edit_report_merge_semantic_risks(report, risks, risk_count);
    nmo_semantic_risks_free(risks);
    return rc;
}

static bool edit_plan_contains_rewrite_op(const nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->ops[i].kind == NMO_EDIT_OP_FOLD ||
            plan->ops[i].kind == NMO_EDIT_OP_REPLACE_BB) {
            return true;
        }
    }
    return false;
}

nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (workspace == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = nmo_script_edit_begin(workspace, "edit plan", &tx);
    if (rc != NMO_OK) {
        report->status = rc;
        return rc;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    rc = nmo_edit_executor_execute_transaction(tx, plan, &effective, report);
    if (rc != NMO_OK) {
        nmo_script_edit_rollback(tx);
        return rc;
    }

    if (effective.dry_run) {
        nmo_script_edit_rollback(tx);
        report->ok = true;
        report->status = NMO_OK;
        return NMO_OK;
    }

    rc = nmo_script_edit_commit(tx);
    report->ok = rc == NMO_OK;
    report->status = rc;
    return rc;
}

nmo_status_t nmo_edit_executor_execute_transaction(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (tx == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    nmo_status_t rc = NMO_OK;
    NMO_RETURN_IF_ERROR(edit_report_prepare(report, plan, effective.dry_run));

    const bool has_rewrite_ops = edit_plan_contains_rewrite_op(plan);
    rc = edit_executor_validate_semantics(tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    for (size_t i = 0; i < plan->count; i++) {
        const nmo_edit_op_t *op = &plan->ops[i];
        const nmo_script_edit_report_t *tx_report_before =
            nmo_script_edit_report(tx);
        size_t created_start = tx_report_before
            ? tx_report_before->created_object_id_count
            : 0u;
        size_t changed_start = tx_report_before
            ? tx_report_before->changed_object_id_count
            : 0u;
        edit_plan_manager_snapshot_t manager_before = {0};
        edit_plan_manager_snapshot_t manager_after = {0};
        rc = edit_plan_read_manager_snapshot(
            nmo_script_edit_workspace(tx), &manager_before);
        if (rc != NMO_OK) {
            report->ok = false;
            report->status = rc;
            return rc;
        }
        nmo_object_id_t result_id = 0;
        const char *diagnostic_code = NULL;
        const char *diagnostic_message = NULL;
        nmo_status_t op_rc = edit_executor_apply_op(
            tx,
            op,
            &result_id,
            effective.dry_run,
            report,
            &diagnostic_code,
            &diagnostic_message);
        const nmo_script_edit_report_t *tx_report_after =
            nmo_script_edit_report(tx);
        report->operations[i] = (nmo_edit_operation_result_t){
            .kind = op->kind,
            .primary_id = op->primary_id,
            .result_id = result_id,
            .status = op_rc,
        };
        nmo_status_t diagnostic_rc = edit_report_set_operation_diagnostic(
            report, i, diagnostic_code, diagnostic_message);
        if (diagnostic_rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = diagnostic_rc;
            return diagnostic_rc;
        }
        if (op_rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = op_rc;
            return op_rc;
        }
        rc = edit_plan_read_manager_snapshot(
            nmo_script_edit_workspace(tx), &manager_after);
        if (rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = rc;
            return rc;
        }
        const char *created_manager_key = NULL;
        uint32_t created_manager_index = 0u;
        bool created_manager_entry = edit_plan_find_created_message_entry(
            &manager_before,
            &manager_after,
            &created_manager_key,
            &created_manager_index);
        nmo_guid_t created_attribute_type_guid = {0};
        const char *created_attribute_category = NULL;
        uint32_t created_attribute_index = 0u;
        uint32_t created_attribute_compatible_class_id = 0u;
        uint32_t created_attribute_flags = 0u;
        bool created_attribute_entry = edit_plan_find_created_attribute_entry(
            &manager_before,
            &manager_after,
            &created_manager_key,
            &created_attribute_category,
            &created_attribute_type_guid,
            &created_attribute_index,
            &created_attribute_compatible_class_id,
            &created_attribute_flags);
        const nmo_manager_entry_options_t *manager_entry_options =
            edit_plan_op_manager_entry(op);
        if (!created_attribute_entry && manager_entry_options != NULL &&
            manager_entry_options->policy ==
                NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING &&
            manager_entry_options->schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE &&
            manager_entry_options->key != NULL &&
            manager_entry_options->key[0] != '\0' &&
            manager_entry_options->create.enabled) {
            created_attribute_entry = true;
            created_manager_key = manager_entry_options->key;
            created_attribute_category =
                manager_entry_options->create.category;
            created_attribute_type_guid =
                manager_entry_options->create.attribute_type_guid;
            created_attribute_index =
                edit_plan_get_parameter_manager_value(tx, result_id);
            created_attribute_compatible_class_id =
                manager_entry_options->create.compatible_class_id;
            created_attribute_flags = manager_entry_options->create.flags;
        }
        if (created_attribute_entry) {
            created_manager_index = created_attribute_index;
        }
        if (result_id != 0u && nmo_edit_op_kind_creates_result(op->kind)) {
            nmo_status_t handle_rc = nmo_edit_report_add_operation_handle(
                report,
                i,
                nmo_edit_op_kind_result_handle(op->kind),
                result_id);
            if (handle_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = handle_rc;
                return handle_rc;
            }
            if (op->kind == NMO_EDIT_OP_ADD_NODE) {
                handle_rc = edit_report_add_node_child_handles(
                    tx, report, i, result_id);
                if (handle_rc != NMO_OK) {
                    edit_plan_manager_snapshot_dispose(&manager_after);
                    edit_plan_manager_snapshot_dispose(&manager_before);
                    report->ok = false;
                    report->status = handle_rc;
                    return handle_rc;
                }
            }
        }
        bool noted_created_objects = false;
        if (tx_report_after &&
            tx_report_after->created_object_ids &&
            tx_report_after->created_object_id_count > created_start) {
            nmo_status_t report_rc = edit_report_note_created_objects(
                report,
                tx_report_after->created_object_ids + created_start,
                tx_report_after->created_object_id_count - created_start,
                op->kind,
                "created");
            if (report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
            noted_created_objects = true;
        }
        if (nmo_edit_op_kind_creates_result(op->kind) &&
            !noted_created_objects) {
            nmo_status_t report_rc = nmo_edit_report_add_created_object(
                report, result_id, op->kind, "created");
            if (report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
        }
        if (op->kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK && result_id != 0u) {
            nmo_object_id_t after_from_io_id = 0u;
            nmo_object_id_t after_to_io_id = 0u;
            uint32_t after_activation_delay = 0u;
            edit_plan_get_behavior_link_endpoints(
                tx,
                result_id,
                &after_from_io_id,
                &after_to_io_id,
                &after_activation_delay);
            edit_report_set_control_link_after(
                report->created_objects,
                report->created_object_count,
                result_id,
                op->kind,
                "created",
                after_from_io_id,
                after_to_io_id,
                after_activation_delay);
        }
        if (op->kind == NMO_EDIT_OP_ADD_OPERATION && result_id != 0u) {
            edit_report_set_operation_slot_after(
                report->created_objects,
                report->created_object_count,
                result_id,
                op->kind,
                "created",
                edit_plan_get_operation_state(tx, result_id));
        }
        if (tx_report_after &&
            tx_report_after->changed_object_ids &&
            tx_report_after->changed_object_id_count > changed_start) {
            nmo_object_id_t primary_changed_id = edit_op_changed_id(op);
            if (primary_changed_id == 0u && result_id != 0u) {
                primary_changed_id = result_id;
            }
            const nmo_object_id_t *changed_ids =
                tx_report_after->changed_object_ids + changed_start;
            size_t changed_count =
                tx_report_after->changed_object_id_count - changed_start;
            for (size_t changed_i = 0; changed_i < changed_count; ++changed_i) {
                if (changed_ids[changed_i] == primary_changed_id) {
                    continue;
                }
                const char *changed_role =
                    changed_ids[changed_i] == NMO_EDIT_MANAGER_ENTRY_IMPACT_ID
                        ? "manager_entry"
                        : "changed";
                nmo_status_t report_rc = nmo_edit_report_add_changed_object(
                    report, changed_ids[changed_i], op->kind, changed_role);
                if (report_rc != NMO_OK) {
                    edit_plan_manager_snapshot_dispose(&manager_after);
                    edit_plan_manager_snapshot_dispose(&manager_before);
                    report->ok = false;
                    report->status = report_rc;
                    return report_rc;
                }
                if (changed_ids[changed_i] == NMO_EDIT_MANAGER_ENTRY_IMPACT_ID) {
                    const char *key = (created_manager_entry ||
                                       created_attribute_entry)
                        ? created_manager_key
                        : NULL;
                    uint32_t index = (created_manager_entry ||
                                      created_attribute_entry)
                        ? created_manager_index
                        : 0u;
                    edit_report_set_manager_entry_after(
                        report->changed_objects,
                        report->changed_object_count,
                        changed_ids[changed_i],
                        op->kind,
                        changed_role,
                        created_attribute_entry ? NMO_MANAGER_GUID_ATTRIBUTE
                                                : NMO_MANAGER_GUID_MESSAGE,
                        created_attribute_entry
                            ? NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE
                            : NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
                        key,
                        created_attribute_entry ? created_attribute_category : NULL,
                        created_attribute_entry ? created_attribute_type_guid
                                                : (nmo_guid_t){0},
                        index,
                        index,
                        created_attribute_entry
                            ? created_attribute_compatible_class_id
                            : 0u,
                        created_attribute_entry ? created_attribute_flags : 0u,
                        created_manager_entry || created_attribute_entry,
                        created_manager_entry || created_attribute_entry);
                }
            }
        }
        if (created_manager_entry || created_attribute_entry) {
            nmo_status_t manager_report_rc =
                edit_report_note_manager_entry_after(
                    report,
                    op->kind,
                    created_manager_key,
                    created_manager_index,
                    true,
                    true);
            if (manager_report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = manager_report_rc;
                return manager_report_rc;
            }
            if (created_attribute_entry) {
                edit_report_set_manager_entry_after(
                    report->changed_objects,
                    report->changed_object_count,
                    NMO_EDIT_MANAGER_ENTRY_IMPACT_ID,
                    op->kind,
                    "manager_entry",
                    NMO_MANAGER_GUID_ATTRIBUTE,
                    NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE,
                    created_manager_key,
                    created_attribute_category,
                    created_attribute_type_guid,
                    created_attribute_index,
                    created_attribute_index,
                    created_attribute_compatible_class_id,
                    created_attribute_flags,
                    true,
                    true);
            }
        }
        nmo_object_id_t deleted_id = edit_op_deleted_id(op);
        if (deleted_id != 0u) {
            if (edit_report_find_impact(
                    report->deleted_objects,
                    report->deleted_object_count,
                    deleted_id,
                    op->kind,
                    "primary") == NULL) {
                (void)nmo_edit_report_add_deleted_object(
                    report, deleted_id, op->kind, "primary");
            }
        }
        nmo_object_id_t changed_id = edit_op_changed_id(op);
        if (changed_id == 0u && result_id != 0u) {
            changed_id = result_id;
        }
        (void)nmo_edit_report_add_changed_object(
            report, changed_id, op->kind, "primary");
        edit_plan_manager_snapshot_dispose(&manager_after);
        edit_plan_manager_snapshot_dispose(&manager_before);
    }

    rc = edit_executor_validate_semantics(
        tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    rc = edit_executor_validate(tx, report, effective.validation_flags);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    report->ok = true;
    report->status = NMO_OK;
    return NMO_OK;
}
