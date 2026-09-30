/**
 * @file edit_plan_report.c
 * @brief Edit report bookkeeping: impacts, before/after snapshots, and handles.
 */

#include "edit_plan_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    return NMO_OK;
}

void nmo_edit_report_dispose(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return;
    }
    for (size_t i = 0; i < report->operation_count; ++i) {
        if (report->operations != NULL) {
            for (size_t j = 0; j < report->operations[i].handle_count; ++j) {
                free((void *)report->operations[i].handles[j].name);
            }
            free((void *)report->operations[i].diagnostic_code);
            free((void *)report->operations[i].diagnostic_message);
            free(report->operations[i].handles);
        }
    }
    free(report->operations);
    free(report->changed_objects);
    free(report->created_objects);
    free(report->deleted_objects);
    free(report->semantic_risks);
    free(report->output_path);
    nmo_probe_analysis_dispose(&report->probe_selector_analysis);
    memset(report, 0, sizeof(*report));
}

nmo_status_t nmo_edit_report_set_output_path(
    nmo_edit_report_t *report,
    const char *output_path)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = edit_plan_strdup(output_path);
    if (output_path != NULL && copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    free(report->output_path);
    report->output_path = copy;
    return NMO_OK;
}

static nmo_status_t edit_report_ensure_operations(
    nmo_edit_report_t *report,
    size_t needed)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= report->operation_count) {
        return NMO_OK;
    }
    nmo_edit_operation_result_t *ops =
        (nmo_edit_operation_result_t *)realloc(
            report->operations, needed * sizeof(*ops));
    if (ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(ops + report->operation_count, 0,
           (needed - report->operation_count) * sizeof(*ops));
    report->operations = ops;
    report->operation_count = needed;
    return NMO_OK;
}

nmo_status_t edit_report_prepare(nmo_edit_report_t *report,
                                        const nmo_edit_plan_t *plan,
                                        bool dry_run)
{
    if (report == NULL || plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_edit_report_dispose(report);
    report->dry_run = dry_run;
    report->operation_count = plan->count;
    if (plan->has_probe_selector_analysis) {
        nmo_status_t rc = edit_plan_probe_analysis_copy(
            &report->probe_selector_analysis,
            &plan->probe_selector_analysis);
        if (rc != NMO_OK) {
            nmo_edit_report_dispose(report);
            return rc;
        }
        report->has_probe_selector_analysis = true;
    }
    if (plan->count == 0) {
        return NMO_OK;
    }
    report->operations =
        (nmo_edit_operation_result_t *)calloc(plan->count, sizeof(*report->operations));
    if (report->operations == NULL) {
        nmo_edit_report_dispose(report);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static bool edit_report_has_changed_object(
    const nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    for (size_t i = 0; i < report->changed_object_count; i++) {
        const char *existing_role = report->changed_objects[i].role;
        bool same_role = existing_role == role ||
            (existing_role != NULL && role != NULL &&
             strcmp(existing_role, role) == 0);
        if (report->changed_objects[i].id == id &&
            report->changed_objects[i].cause == cause &&
            same_role) {
            return true;
        }
    }
    return false;
}

static nmo_status_t edit_report_add_impact(
    nmo_edit_object_impact_t **items,
    size_t *count,
    size_t *capacity,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (items == NULL || count == NULL || capacity == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity == 0u ? 8u : *capacity * 2u;
        nmo_edit_object_impact_t *next =
            (nmo_edit_object_impact_t *)realloc(
                *items, next_capacity * sizeof(*next));
        if (next == NULL) {
            return NMO_ERR_NOMEM;
        }
        *items = next;
        *capacity = next_capacity;
    }
    (*items)[*count] = (nmo_edit_object_impact_t){
        .id = id,
        .cause = cause,
        .role = role,
    };
    ++(*count);
    return NMO_OK;
}

nmo_edit_object_impact_t *edit_report_find_impact(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    for (size_t i = 0; items != NULL && i < count; ++i) {
        const char *existing_role = items[i].role;
        bool same_role = existing_role == role ||
            (existing_role != NULL && role != NULL &&
             strcmp(existing_role, role) == 0);
        if (items[i].id == id && items[i].cause == cause && same_role) {
            return &items[i];
        }
    }
    return NULL;
}

void edit_report_set_control_link_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_control_link_after = true;
    impact->after_from_io_id = from_io_id;
    impact->after_to_io_id = to_io_id;
    impact->after_activation_delay = activation_delay;
}

void edit_report_set_control_link_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_control_link_before = true;
    impact->before_from_io_id = from_io_id;
    impact->before_to_io_id = to_io_id;
    impact->before_activation_delay = activation_delay;
}

void edit_report_set_parameter_edge_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_parameter_edge_before = true;
    impact->before_source_parameter_id = source_parameter_id;
    impact->before_target_parameter_id = target_parameter_id;
}

void edit_report_set_parameter_edge_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_parameter_edge_after = true;
    impact->after_source_parameter_id = source_parameter_id;
    impact->after_target_parameter_id = target_parameter_id;
}

void edit_report_set_operation_slot_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_operation_slot_before = true;
    impact->before_operation_guid = state->operation_guid;
    impact->before_in1_parameter_id =
        state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0u;
    impact->before_has_in1_parameter =
        impact->before_in1_parameter_id != 0u;
    impact->before_in2_parameter_id =
        state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0u;
    impact->before_has_in2_parameter =
        impact->before_in2_parameter_id != 0u;
    impact->before_out_parameter_id =
        state->has_out ? nmo_parameteroperation_out_id(state) : 0u;
    impact->before_has_out_parameter =
        impact->before_out_parameter_id != 0u;
}

void edit_report_set_operation_slot_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_operation_slot_after = true;
    impact->after_operation_guid = state->operation_guid;
    impact->after_in1_parameter_id =
        state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0u;
    impact->after_has_in1_parameter =
        impact->after_in1_parameter_id != 0u;
    impact->after_in2_parameter_id =
        state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0u;
    impact->after_has_in2_parameter =
        impact->after_in2_parameter_id != 0u;
    impact->after_out_parameter_id =
        state->has_out ? nmo_parameteroperation_out_id(state) : 0u;
    impact->after_has_out_parameter =
        impact->after_out_parameter_id != 0u;
}

static void edit_plan_format_data_cell_value(
    const nmo_dataarray_cell_t *cell,
    uint32_t type,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u) {
        return;
    }
    buffer[0] = '\0';
    if (cell == NULL) {
        return;
    }
    switch ((CK_ARRAYTYPE)type) {
    case CKARRAYTYPE_INT:
        snprintf(buffer, buffer_size, "%d", cell->int_value);
        break;
    case CKARRAYTYPE_FLOAT:
        snprintf(buffer, buffer_size, "%.9g", (double)cell->float_value);
        break;
    case CKARRAYTYPE_STRING:
        snprintf(buffer, buffer_size, "%s",
                 cell->string_value ? cell->string_value : "");
        break;
    case CKARRAYTYPE_OBJECT:
        snprintf(buffer, buffer_size, "%u",
                 nmo_ref_serialized_id(&cell->object_ref));
        break;
    case CKARRAYTYPE_PARAMETER:
        snprintf(buffer, buffer_size, "%u",
                 nmo_ref_serialized_id(&cell->parameter.ref));
        break;
    default:
        break;
    }
}

void edit_report_set_interface_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_interface_before = true;
    impact->before_interface_behavior_id = id;
    impact->before_has_interface = state->has_interface;
    impact->before_has_interface_chunk = state->interface_chunk != NULL;
    impact->before_has_interface_data = state->interface_data != NULL;
    impact->before_interface_ids_are_runtime =
        state->interface_ids_are_runtime;
    impact->before_interface_version =
        state->interface_data ? state->interface_data->version : 0u;
    impact->before_interface_sub_count =
        state->interface_data ? (uint32_t)state->interface_data->sub_count : 0u;
}

void edit_report_set_interface_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_interface_after = true;
    impact->after_interface_behavior_id = id;
    impact->after_has_interface = state->has_interface;
    impact->after_has_interface_chunk = state->interface_chunk != NULL;
    impact->after_has_interface_data = state->interface_data != NULL;
    impact->after_interface_ids_are_runtime =
        state->interface_ids_are_runtime;
    impact->after_interface_version =
        state->interface_data ? state->interface_data->version : 0u;
    impact->after_interface_sub_count =
        state->interface_data ? (uint32_t)state->interface_data->sub_count : 0u;
}

void edit_report_set_data_cell_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || cell == NULL) {
        return;
    }
    impact->has_data_cell_before = true;
    impact->before_data_cell_row = row;
    impact->before_data_cell_col = col;
    impact->before_data_cell_type = type;
    edit_plan_format_data_cell_value(
        cell, type, impact->before_data_cell_value,
        sizeof(impact->before_data_cell_value));
}

void edit_report_set_data_cell_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || cell == NULL) {
        return;
    }
    impact->has_data_cell_after = true;
    impact->after_data_cell_row = row;
    impact->after_data_cell_col = col;
    impact->after_data_cell_type = type;
    edit_plan_format_data_cell_value(
        cell, type, impact->after_data_cell_value,
        sizeof(impact->after_data_cell_value));
}

void edit_report_set_manager_entry_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_guid_t manager_guid,
    nmo_manager_entry_schema_t schema,
    const char *key,
    const char *category,
    nmo_guid_t type_guid,
    uint32_t entry_index,
    uint32_t entry_value,
    uint32_t compatible_class_id,
    uint32_t flags,
    bool created,
    bool manager_chunk_changed)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_manager_entry_after = true;
    impact->after_manager_guid = manager_guid;
    impact->after_manager_entry_schema = schema;
    if (key != NULL) {
        snprintf(impact->after_manager_entry_key,
                 sizeof(impact->after_manager_entry_key),
                 "%s",
                 key);
    }
    if (category != NULL) {
        snprintf(impact->after_manager_entry_category,
                 sizeof(impact->after_manager_entry_category),
                 "%s",
                 category);
    }
    impact->after_manager_entry_type_guid = type_guid;
    impact->after_manager_entry_index = entry_index;
    impact->after_manager_entry_value = entry_value;
    impact->after_manager_entry_compatible_class_id = compatible_class_id;
    impact->after_manager_entry_flags = flags;
    impact->after_manager_entry_created = created;
    impact->after_manager_chunk_changed = manager_chunk_changed;
}

nmo_status_t edit_report_note_manager_entry_after(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    const char *key,
    uint32_t entry_index,
    bool created,
    bool manager_chunk_changed)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t rc = nmo_edit_report_add_changed_object(
        report, NMO_EDIT_MANAGER_ENTRY_IMPACT_ID, cause, "manager_entry");
    if (rc != NMO_OK) {
        return rc;
    }
    edit_report_set_manager_entry_after(
        report->changed_objects,
        report->changed_object_count,
        NMO_EDIT_MANAGER_ENTRY_IMPACT_ID,
        cause,
        "manager_entry",
        NMO_MANAGER_GUID_MESSAGE,
        NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
        key,
        NULL,
        (nmo_guid_t){0},
        entry_index,
        entry_index,
        0u,
        0u,
        created,
        manager_chunk_changed);
    return NMO_OK;
}

nmo_status_t nmo_edit_report_add_changed_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || id == 0 ||
        edit_report_has_changed_object(report, id, cause, role)) {
        return report && id ? NMO_OK : NMO_ERR_INVALID_ARGUMENT;
    }
    size_t capacity = report->changed_object_count;
    if (report->changed_objects != NULL) {
        capacity = report->changed_object_count;
        while (capacity > 0 &&
               report->changed_objects[capacity - 1].id == 0) {
            capacity--;
        }
    }
    capacity = report->changed_object_count;
    return edit_report_add_impact(
        &report->changed_objects,
        &report->changed_object_count,
        &capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_created_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->created_objects,
        &report->created_object_count,
        &report->created_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_deleted_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->deleted_objects,
        &report->deleted_object_count,
        &report->deleted_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_operation_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *name,
    nmo_object_id_t id)
{
    if (report == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    nmo_edit_operation_handle_t *next =
        (nmo_edit_operation_handle_t *)realloc(
            op->handles,
            (op->handle_count + 1u) * sizeof(*next));
    if (next == NULL) {
        return NMO_ERR_NOMEM;
    }
    op->handles = next;
    const char *name_copy = edit_plan_strdup(name);
    if (name && !name_copy) {
        return NMO_ERR_NOMEM;
    }
    op->handles[op->handle_count++] =
        (nmo_edit_operation_handle_t){.name = name_copy, .id = id};
    return NMO_OK;
}

nmo_status_t edit_report_set_operation_diagnostic(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *code,
    const char *message)
{
    if (report == NULL || (code == NULL && message == NULL)) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    char *code_copy = edit_plan_strdup(code);
    if (code && !code_copy) {
        return NMO_ERR_NOMEM;
    }
    char *message_copy = edit_plan_strdup(message);
    if (message && !message_copy) {
        free(code_copy);
        return NMO_ERR_NOMEM;
    }
    free((void *)op->diagnostic_code);
    free((void *)op->diagnostic_message);
    op->diagnostic_code = code_copy;
    op->diagnostic_message = message_copy;
    return NMO_OK;
}

nmo_status_t edit_report_note_created_objects(
    nmo_edit_report_t *report,
    const nmo_object_id_t *ids,
    size_t count,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_created_object(
            report, ids[i], cause, role));
    }
    return NMO_OK;
}

nmo_status_t edit_report_note_operation_slot_parameters(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (in1_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in1_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (in2_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in2_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (out_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            out_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    return NMO_OK;
}

nmo_status_t edit_report_note_control_link_endpoints(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (from_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            from_io_id,
            cause,
            "control_link_endpoint"));
    }
    if (to_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            to_io_id,
            cause,
            "control_link_endpoint"));
    }
    return NMO_OK;
}

nmo_status_t edit_report_note_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t io_id)
{
    if (tx == NULL || report == NULL || io_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    const nmo_type_registry_t *registry =
        nmo_workspace_internal_type_registry(nmo_script_edit_workspace(tx));
    if (repo == NULL || registry == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0u; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *behavior_state = (nmo_behavior_state_t *)
            edit_plan_get_typed_object_state(
                registry,
                behavior_obj,
                NMO_CID_BEHAVIOR,
                CKPGUID_BEHAVIOR);
        for (size_t j = 0u; behavior_state != NULL &&
                            j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &behavior_state->sub_behavior_links, j);
            if (link_id == 0) continue;
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link_state =
                (const nmo_behaviorlink_state_t *)
                    edit_plan_get_typed_object_state(
                        registry,
                        link_obj,
                        NMO_CID_BEHAVIORLINK,
                        CKPGUID_BEHAVIORLINK);
            if (link_state == NULL ||
                (nmo_behaviorlink_in_io_id(link_state) != io_id &&
                 nmo_behaviorlink_out_io_id(link_state) != io_id)) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                link_id,
                cause,
                "detached_control_link"));
            NMO_RETURN_IF_ERROR(edit_report_note_control_link_endpoints(
                report,
                cause,
                nmo_behaviorlink_in_io_id(link_state),
                nmo_behaviorlink_out_io_id(link_state)));
        }
    }

    return NMO_OK;
}

nmo_status_t edit_report_note_parameter_edge_source(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t source_parameter_id)
{
    if (report == NULL || source_parameter_id == 0u) {
        return NMO_OK;
    }
    return nmo_edit_report_add_changed_object(
        report,
        source_parameter_id,
        cause,
        "parameter_edge_source");
}

nmo_status_t edit_report_note_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t parameter_id)
{
    if (tx == NULL || report == NULL || parameter_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    const nmo_type_registry_t *registry =
        nmo_workspace_internal_type_registry(nmo_script_edit_workspace(tx));
    if (repo == NULL || registry == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }

        void *connection_state = NULL;
        switch (edit_plan_get_parameter_connection_state(
            registry, object, &connection_state)) {
        case NMO_CID_PARAMETERIN: {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)connection_state;
            if (nmo_parameterin_source_id(state) == parameter_id) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "parameter_edge_target"));
            }
            break;
        }
        case NMO_CID_PARAMETEROPERATION: {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)connection_state;

            if ((state->has_in1 && nmo_parameteroperation_in1_id(state) == parameter_id) ||
                (state->has_in2 && nmo_parameteroperation_in2_id(state) == parameter_id) ||
                (state->has_out && nmo_parameteroperation_out_id(state) == parameter_id)) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "operation_slot_owner"));
            }
            break;
        }
        default:
            break;
        }
    }

    return NMO_OK;
}

nmo_status_t edit_report_note_operation_slot_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    const nmo_parameteroperation_state_t *operation)
{
    if (tx == NULL || report == NULL || operation == NULL) {
        return NMO_OK;
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
    for (size_t i = 0u; i < sizeof(slot_ids) / sizeof(slot_ids[0]); ++i) {
        if (slot_ids[i] == 0u ||
            edit_plan_get_object_state(
                tx, slot_ids[i], slot_classes[i], slot_guids[i]) == NULL) {
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report, slot_ids[i], cause, "operation_slot"));
    }
    return NMO_OK;
}

nmo_status_t edit_report_note_behavior_owned_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    const nmo_behavior_state_t *state =
        edit_plan_get_behavior_state(tx, behavior_id);
    if (state == NULL) {
        return NMO_OK;
    }

    for (size_t i = 0u; i < state->operations.count; ++i) {
        const nmo_object_id_t operation_id =
            nmo_behavior_ref_array_get_id(&state->operations, i);
        const nmo_parameteroperation_state_t *operation =
            edit_plan_get_operation_state(tx, operation_id);
        if (operation == NULL) {
            continue;
        }

        NMO_RETURN_IF_ERROR(edit_report_note_operation_slot_deleted_objects(
            tx, report, cause, operation));
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            target_parameter_id,
            cause,
            "target_parameter"));
    }

    const struct {
        const nmo_array_t *array;
        const char *role;
    } owned_arrays[] = {
        { &state->inputs, "owned_io" },
        { &state->outputs, "owned_io" },
        { &state->in_parameters, "owned_parameter" },
        { &state->out_parameters, "owned_parameter" },
        { &state->local_parameters, "owned_parameter" },
        { &state->operations, "owned_operation" },
        { &state->sub_behavior_links, "owned_link" },
    };

    for (size_t i = 0u; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        const nmo_array_t *array = owned_arrays[i].array;
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                id,
                cause,
                owned_arrays[i].role));
        }
    }

    for (size_t i = 0u; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (sub_id == 0) continue;
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            sub_id,
            cause,
            "owned_node"));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_owned_deleted_objects(
            tx,
            report,
            cause,
            sub_id));
    }

    return NMO_OK;
}

nmo_status_t edit_report_note_behavior_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    const nmo_behavior_state_t *state =
        edit_plan_get_behavior_state(tx, behavior_id);
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_array_t *io_arrays[] = {
        &state->inputs,
        &state->outputs,
    };
    for (size_t i = 0u; i < sizeof(io_arrays) / sizeof(io_arrays[0]); ++i) {
        const nmo_array_t *array = io_arrays[i];
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx, report, cause, id));
        }
    }

    return NMO_OK;
}

nmo_status_t edit_report_note_behavior_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    const nmo_behavior_state_t *state =
        edit_plan_get_behavior_state(tx, behavior_id);
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_array_t *param_arrays[] = {
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
    };
    for (size_t i = 0u; i < sizeof(param_arrays) / sizeof(param_arrays[0]); ++i) {
        const nmo_array_t *array = param_arrays[i];
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            nmo_object_id_t source_id =
                edit_plan_get_parameterin_source(tx, id);
            NMO_RETURN_IF_ERROR(edit_report_note_parameter_edge_source(
                report, cause, source_id));
            NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
                tx, report, cause, id));
        }
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        nmo_object_id_t source_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_edge_source(
            report, cause, source_id));
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
            tx, report, cause, target_parameter_id));
    }

    return NMO_OK;
}

nmo_status_t nmo_edit_report_merge_semantic_risks(
    nmo_edit_report_t *report,
    const nmo_behavior_semantic_risk_t *risks,
    size_t risk_count)
{
    if (report == NULL || (risk_count > 0u && risks == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (risk_count == 0u) {
        return NMO_OK;
    }
    for (size_t i = 0; i < risk_count; ++i) {
        bool exists = false;
        for (size_t j = 0; j < report->semantic_risk_count; ++j) {
            const nmo_behavior_semantic_risk_t *existing =
                &report->semantic_risks[j];
            if (existing->severity == risks[i].severity &&
                existing->object_id == risks[i].object_id &&
                ((existing->code == NULL && risks[i].code == NULL) ||
                 (existing->code != NULL && risks[i].code != NULL &&
                  strcmp(existing->code, risks[i].code) == 0))) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        if (report->semantic_risk_count + 1u >
            report->semantic_risk_capacity) {
            size_t new_capacity = report->semantic_risk_capacity == 0u
                ? 8u
                : report->semantic_risk_capacity * 2u;
            nmo_behavior_semantic_risk_t *next =
                (nmo_behavior_semantic_risk_t *)realloc(
                    report->semantic_risks,
                    new_capacity * sizeof(*next));
            if (next == NULL) {
                return NMO_ERR_NOMEM;
            }
            report->semantic_risks = next;
            report->semantic_risk_capacity = new_capacity;
        }
        report->semantic_risks[report->semantic_risk_count++] = risks[i];
    }
    return NMO_OK;
}

nmo_status_t edit_report_note_fold_impact(
    nmo_edit_report_t *report,
    const nmo_behavior_fold_report_t *fold_report,
    nmo_object_id_t parent_id)
{
    if (report == NULL || fold_report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, parent_id, NMO_EDIT_OP_FOLD, "parent"));
    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, fold_report->anchor_id, NMO_EDIT_OP_FOLD, "anchor"));

    for (size_t i = 0; i < fold_report->selected_node_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            fold_report->selected_nodes[i],
            NMO_EDIT_OP_FOLD,
            "selected_node"));
    }
    for (size_t i = 0; i < fold_report->nodes_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->nodes_to_delete[i],
            NMO_EDIT_OP_FOLD,
            "folded_node"));
    }
    for (size_t i = 0; i < fold_report->control_links_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->control_links_to_delete[i].link_id,
            NMO_EDIT_OP_FOLD,
            "folded_control_link"));
    }

    const nmo_behavior_boundary_t *boundary = &fold_report->boundary;
    for (size_t i = 0; i < boundary->control_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_in[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->control_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_out[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->parameter_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    for (size_t i = 0; i < boundary->parameter_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_named_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    if (id == 0u) {
        return NMO_OK;
    }
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, id)
        : NULL;
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    char handle_name[160];
    if (name != NULL && name[0] != '\0') {
        snprintf(handle_name, sizeof(handle_name), "%s:%s", prefix, name);
    } else {
        snprintf(handle_name, sizeof(handle_name), "%s", prefix);
    }
    return nmo_edit_report_add_operation_handle(
        report, operation_index, handle_name, id);
}

static nmo_status_t edit_report_add_array_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, prefix, repo, id));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_input_parameter_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, "input_param", repo, id));
        nmo_object_t *param_obj =
            repo != NULL ? nmo_object_repository_find_by_id(repo, id) : NULL;
        nmo_parameterin_state_t *param_state = param_obj != NULL
            ? (nmo_parameterin_state_t *)nmo_object_get_state(param_obj)
            : NULL;
        const nmo_object_id_t source_id =
            nmo_parameterin_source_id(param_state);
        if (source_id != 0u) {
            NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
                report,
                operation_index,
                "input_param_source",
                repo,
                source_id));
        }
    }
    return NMO_OK;
}

nmo_status_t edit_report_add_node_child_handles(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_id_t node_id)
{
    if (tx == NULL || report == NULL || node_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_object_repository_t *repo =
        workspace != NULL ? nmo_workspace_internal_repository(workspace) : NULL;
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *node_obj = nmo_object_repository_find_by_id(repo, node_id);
    nmo_behavior_state_t *state = node_obj != NULL
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_operation_handle(
            report, operation_index, "target", target_parameter_id));
    }
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "input", repo, &state->inputs));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output", repo, &state->outputs));
    NMO_RETURN_IF_ERROR(edit_report_add_input_parameter_handles(
        report, operation_index, repo, &state->in_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output_param", repo, &state->out_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "local_param", repo, &state->local_parameters));
    return NMO_OK;
}

nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id)
{
    if (report == NULL || handle_name == NULL || out_id == NULL ||
        operation_index >= report->operation_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_edit_operation_result_t *operation =
        &report->operations[operation_index];
    for (size_t i = 0; i < operation->handle_count; ++i) {
        if (operation->handles[i].name != NULL &&
            strcmp(operation->handles[i].name, handle_name) == 0) {
            *out_id = operation->handles[i].id;
            return NMO_OK;
        }
    }
    return NMO_ERR_NOT_FOUND;
}
