#include "nmo_edit_report_json.h"

#include "nmo_cli_json.h"

#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_manager_guids.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

const char *nmo_cli_edit_report_op_kind_string(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return "set_parameter_value";
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return "set_parameter_bytes";
    case NMO_EDIT_OP_ADD_NODE:
        return "add_node";
    case NMO_EDIT_OP_REMOVE_NODE:
        return "remove_node";
    case NMO_EDIT_OP_ADD_IO:
        return "add_io";
    case NMO_EDIT_OP_RENAME_IO:
        return "rename_io";
    case NMO_EDIT_OP_REMOVE_IO:
        return "remove_io";
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return "add_behavior_link";
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
        return "rewire_behavior_link";
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
        return "set_behavior_link_delay";
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return "remove_behavior_link";
    case NMO_EDIT_OP_ADD_PARAMETER:
        return "add_parameter";
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        return "connect_parameter";
    case NMO_EDIT_OP_DISCONNECT_PARAMETER:
        return "disconnect_parameter";
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return "remove_parameter";
    case NMO_EDIT_OP_ADD_OPERATION:
        return "add_operation";
    case NMO_EDIT_OP_REWIRE_OPERATION:
        return "rewire_operation";
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return "remove_operation";
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return "interface_policy";
    case NMO_EDIT_OP_SET_DATA_CELL:
        return "set_data_cell";
    case NMO_EDIT_OP_FOLD:
        return "fold";
    case NMO_EDIT_OP_REPLACE_BB:
        return "replace_bb";
    default:
        return "unknown";
    }
}

void nmo_cli_edit_report_add_schema_v2_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report,
    bool dry_run)
{
    yyjson_mut_obj_add_bool(doc, obj, "ok",
                            report != NULL && report->ok);
    yyjson_mut_obj_add_bool(doc, obj, "dry_run", dry_run);
    if (report != NULL && report->output_path != NULL) {
        nmo_cli_json_add_str_safe(doc, obj, "output_path",
                                  report->output_path);
    }
    yyjson_mut_obj_add_val(doc, obj, "errors", yyjson_mut_arr(doc));
    yyjson_mut_obj_add_val(doc, obj, "warnings", yyjson_mut_arr(doc));
    nmo_cli_edit_report_add_operations_json(doc, obj, report);
    nmo_cli_edit_report_add_impact_array_json(
        doc, obj, "changed_objects",
        report != NULL ? report->changed_objects : NULL,
        report != NULL ? report->changed_object_count : 0u);
    nmo_cli_edit_report_add_impact_array_json(
        doc, obj, "created_objects",
        report != NULL ? report->created_objects : NULL,
        report != NULL ? report->created_object_count : 0u);
    nmo_cli_edit_report_add_impact_array_json(
        doc, obj, "deleted_objects",
        report != NULL ? report->deleted_objects : NULL,
        report != NULL ? report->deleted_object_count : 0u);
    nmo_cli_edit_report_add_semantic_risks_json(doc, obj, report);
    nmo_cli_edit_report_add_validation_json(doc, obj, report);
    nmo_cli_edit_report_add_diff_json(doc, obj, report);
}

static const char *semantic_risk_severity_string(
    nmo_behavior_semantic_risk_severity_t severity)
{
    switch (severity) {
    case NMO_BEHAVIOR_SEMANTIC_RISK_SAFE:
        return "safe";
    case NMO_BEHAVIOR_SEMANTIC_RISK_WARN:
        return "warn";
    case NMO_BEHAVIOR_SEMANTIC_RISK_REJECT:
        return "reject";
    default:
        return "warn";
    }
}

void nmo_cli_edit_report_add_operations_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    yyjson_mut_val *ops = yyjson_mut_arr(doc);
    for (size_t i = 0; report != NULL && i < report->operation_count; ++i) {
        const nmo_edit_operation_result_t *op = &report->operations[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_val *handles = yyjson_mut_arr(doc);
        const char *kind = nmo_cli_edit_report_op_kind_string(op->kind);
        yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)(i + 1u));
        nmo_cli_json_add_str_safe(doc, item, "op", kind);
        nmo_cli_json_add_str_safe(doc, item, "kind", kind);
        yyjson_mut_obj_add_uint(doc, item, "primary_id",
                                (uint64_t)op->primary_id);
        yyjson_mut_obj_add_uint(doc, item, "result_id",
                                (uint64_t)op->result_id);
        yyjson_mut_obj_add_uint(doc, item, "status",
                                (uint64_t)op->status);
        nmo_cli_json_add_str_safe(doc, item, "status_name",
                                  nmo_error_string(op->status));
        if (op->diagnostic_code != NULL) {
            nmo_cli_json_add_str_safe(doc, item, "diagnostic_code",
                                      op->diagnostic_code);
        }
        if (op->diagnostic_message != NULL) {
            nmo_cli_json_add_str_safe(doc, item, "diagnostic_message",
                                      op->diagnostic_message);
        }
        for (size_t j = 0; j < op->handle_count; ++j) {
            yyjson_mut_val *handle = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, handle, "name",
                                      op->handles[j].name);
            yyjson_mut_obj_add_uint(doc, handle, "object_id",
                                    (uint64_t)op->handles[j].id);
            yyjson_mut_obj_add_uint(doc, handle, "id",
                                    (uint64_t)op->handles[j].id);
            yyjson_mut_arr_add_val(handles, handle);
        }
        yyjson_mut_obj_add_val(doc, item, "handles", handles);
        yyjson_mut_arr_add_val(ops, item);
    }
    yyjson_mut_obj_add_val(doc, obj, "operations", ops);
}

void nmo_cli_edit_report_add_impact_array_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *name,
    const nmo_edit_object_impact_t *items,
    size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; items != NULL && i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "object_id",
                                (uint64_t)items[i].id);
        yyjson_mut_obj_add_uint(doc, item, "id", (uint64_t)items[i].id);
        nmo_cli_json_add_str_safe(
            doc, item, "cause",
            nmo_cli_edit_report_op_kind_string(items[i].cause));
        nmo_cli_json_add_str_safe(doc, item, "role", items[i].role);
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, name, arr);
}

static yyjson_mut_val *nmo_cli_edit_report_make_impact_array_json(
    yyjson_mut_doc *doc,
    const nmo_edit_object_impact_t *items,
    size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; items != NULL && i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "object_id",
                                (uint64_t)items[i].id);
        yyjson_mut_obj_add_uint(doc, item, "id", (uint64_t)items[i].id);
        nmo_cli_json_add_str_safe(
            doc, item, "cause",
            nmo_cli_edit_report_op_kind_string(items[i].cause));
        nmo_cli_json_add_str_safe(doc, item, "role", items[i].role);
        yyjson_mut_arr_add_val(arr, item);
    }
    return arr;
}

static yyjson_mut_val *nmo_cli_edit_report_make_control_link_snapshot_json(
    yyjson_mut_doc *doc,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, snapshot, "from_io_id",
                            (uint64_t)from_io_id);
    yyjson_mut_obj_add_uint(doc, snapshot, "to_io_id", (uint64_t)to_io_id);
    yyjson_mut_obj_add_uint(doc, snapshot, "activation_delay",
                            (uint64_t)activation_delay);
    return snapshot;
}

static yyjson_mut_val *nmo_cli_edit_report_make_parameter_edge_snapshot_json(
    yyjson_mut_doc *doc,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, snapshot, "source_parameter_id",
                            (uint64_t)source_parameter_id);
    yyjson_mut_obj_add_uint(doc, snapshot, "target_parameter_id",
                            (uint64_t)target_parameter_id);
    return snapshot;
}

static yyjson_mut_val *nmo_cli_edit_report_make_operation_slot_snapshot_json(
    yyjson_mut_doc *doc,
    nmo_guid_t operation_guid,
    bool has_in1,
    nmo_object_id_t in1_parameter_id,
    bool has_in2,
    nmo_object_id_t in2_parameter_id,
    bool has_out,
    nmo_object_id_t out_parameter_id)
{
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    char guid_text[32];
    if (nmo_guid_format(operation_guid, guid_text, sizeof(guid_text)) > 0) {
        yyjson_mut_obj_add_strcpy(doc, snapshot, "operation_guid", guid_text);
    }
    yyjson_mut_obj_add_bool(doc, snapshot, "has_in1", has_in1);
    yyjson_mut_obj_add_uint(doc, snapshot, "in1_parameter_id",
                            (uint64_t)in1_parameter_id);
    yyjson_mut_obj_add_bool(doc, snapshot, "has_in2", has_in2);
    yyjson_mut_obj_add_uint(doc, snapshot, "in2_parameter_id",
                            (uint64_t)in2_parameter_id);
    yyjson_mut_obj_add_bool(doc, snapshot, "has_out", has_out);
    yyjson_mut_obj_add_uint(doc, snapshot, "out_parameter_id",
                            (uint64_t)out_parameter_id);
    return snapshot;
}

static const char *nmo_cli_edit_report_data_cell_type_name(uint32_t type)
{
    switch (type) {
    case CKARRAYTYPE_INT:
        return "int";
    case CKARRAYTYPE_FLOAT:
        return "float";
    case CKARRAYTYPE_STRING:
        return "string";
    case CKARRAYTYPE_OBJECT:
        return "object";
    case CKARRAYTYPE_PARAMETER:
        return "parameter";
    default:
        return "unknown";
    }
}

static yyjson_mut_val *nmo_cli_edit_report_make_interface_snapshot_json(
    yyjson_mut_doc *doc,
    nmo_object_id_t behavior_id,
    bool has_interface,
    bool has_interface_chunk,
    bool has_interface_data,
    bool interface_ids_are_runtime,
    uint32_t version,
    uint32_t sub_count)
{
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, snapshot, "behavior_id",
                            (uint64_t)behavior_id);
    yyjson_mut_obj_add_bool(doc, snapshot, "has_interface", has_interface);
    yyjson_mut_obj_add_bool(doc, snapshot, "has_interface_chunk",
                            has_interface_chunk);
    yyjson_mut_obj_add_bool(doc, snapshot, "has_interface_data",
                            has_interface_data);
    yyjson_mut_obj_add_bool(doc, snapshot, "interface_ids_are_runtime",
                            interface_ids_are_runtime);
    yyjson_mut_obj_add_uint(doc, snapshot, "version", (uint64_t)version);
    yyjson_mut_obj_add_uint(doc, snapshot, "sub_count",
                            (uint64_t)sub_count);
    return snapshot;
}

static yyjson_mut_val *nmo_cli_edit_report_make_data_cell_snapshot_json(
    yyjson_mut_doc *doc,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const char *value)
{
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, snapshot, "row", (uint64_t)row);
    yyjson_mut_obj_add_uint(doc, snapshot, "col", (uint64_t)col);
    nmo_cli_json_add_str_safe(
        doc, snapshot, "type",
        nmo_cli_edit_report_data_cell_type_name(type));
    nmo_cli_json_add_str_safe(doc, snapshot, "value", value);
    return snapshot;
}

static const char *nmo_cli_edit_report_manager_kind(nmo_guid_t manager_guid)
{
    if (nmo_guid_equals(manager_guid, NMO_MANAGER_GUID_MESSAGE)) {
        return "message";
    }
    if (nmo_guid_equals(manager_guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
        return "attribute";
    }
    return "unknown";
}

static const char *nmo_cli_edit_report_manager_schema(
    nmo_manager_entry_schema_t schema,
    nmo_guid_t manager_guid)
{
    switch (schema) {
    case NMO_MANAGER_ENTRY_SCHEMA_MESSAGE:
        return "message";
    case NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE:
        return "attribute";
    case NMO_MANAGER_ENTRY_SCHEMA_AUTO:
    default:
        return nmo_cli_edit_report_manager_kind(manager_guid);
    }
}

static yyjson_mut_val *nmo_cli_edit_report_make_manager_entry_snapshot_json(
    yyjson_mut_doc *doc,
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
    yyjson_mut_val *snapshot = yyjson_mut_obj(doc);
    char guid_text[32];
    if (nmo_guid_format(manager_guid, guid_text, sizeof(guid_text)) > 0) {
        yyjson_mut_obj_add_strcpy(doc, snapshot, "manager_guid", guid_text);
    }
    nmo_cli_json_add_str_safe(
        doc, snapshot, "manager_kind",
        nmo_cli_edit_report_manager_kind(manager_guid));
    nmo_cli_json_add_str_safe(
        doc, snapshot, "schema",
        nmo_cli_edit_report_manager_schema(schema, manager_guid));
    nmo_cli_json_add_str_safe(doc, snapshot, "key", key);
    if (category != NULL && category[0] != '\0') {
        nmo_cli_json_add_str_safe(doc, snapshot, "category", category);
    }
    if (!nmo_guid_is_null(type_guid)) {
        char type_guid_text[32];
        if (nmo_guid_format(type_guid, type_guid_text,
                            sizeof(type_guid_text)) > 0) {
            yyjson_mut_obj_add_strcpy(
                doc, snapshot, "type_guid", type_guid_text);
        }
    }
    yyjson_mut_obj_add_uint(doc, snapshot, "entry_index",
                            (uint64_t)entry_index);
    yyjson_mut_obj_add_uint(doc, snapshot, "entry_value",
                            (uint64_t)entry_value);
    yyjson_mut_obj_add_uint(doc, snapshot, "compatible_class_id",
                            (uint64_t)compatible_class_id);
    yyjson_mut_obj_add_uint(doc, snapshot, "flags", (uint64_t)flags);
    yyjson_mut_obj_add_bool(doc, snapshot, "created", created);
    yyjson_mut_obj_add_bool(doc, snapshot, "manager_chunk_changed",
                            manager_chunk_changed);
    return snapshot;
}

static void nmo_cli_edit_report_add_impact_before_after_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return;
    }
    if (impact->has_control_link_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_control_link_snapshot_json(
                doc,
                impact->before_from_io_id,
                impact->before_to_io_id,
                impact->before_activation_delay));
    } else if (impact->has_control_link_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_control_link_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_control_link_snapshot_json(
                doc,
                impact->after_from_io_id,
                impact->after_to_io_id,
                impact->after_activation_delay));
    } else if (impact->has_control_link_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
    if (impact->has_parameter_edge_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_parameter_edge_snapshot_json(
                doc,
                impact->before_source_parameter_id,
                impact->before_target_parameter_id));
    } else if (impact->has_parameter_edge_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_parameter_edge_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_parameter_edge_snapshot_json(
                doc,
                impact->after_source_parameter_id,
                impact->after_target_parameter_id));
    } else if (impact->has_parameter_edge_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
    if (impact->has_operation_slot_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_operation_slot_snapshot_json(
                doc,
                impact->before_operation_guid,
                impact->before_has_in1_parameter,
                impact->before_in1_parameter_id,
                impact->before_has_in2_parameter,
                impact->before_in2_parameter_id,
                impact->before_has_out_parameter,
                impact->before_out_parameter_id));
    } else if (impact->has_operation_slot_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_operation_slot_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_operation_slot_snapshot_json(
                doc,
                impact->after_operation_guid,
                impact->after_has_in1_parameter,
                impact->after_in1_parameter_id,
                impact->after_has_in2_parameter,
                impact->after_in2_parameter_id,
                impact->after_has_out_parameter,
                impact->after_out_parameter_id));
    } else if (impact->has_operation_slot_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
    if (impact->has_interface_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_interface_snapshot_json(
                doc,
                impact->before_interface_behavior_id,
                impact->before_has_interface,
                impact->before_has_interface_chunk,
                impact->before_has_interface_data,
                impact->before_interface_ids_are_runtime,
                impact->before_interface_version,
                impact->before_interface_sub_count));
    } else if (impact->has_interface_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_interface_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_interface_snapshot_json(
                doc,
                impact->after_interface_behavior_id,
                impact->after_has_interface,
                impact->after_has_interface_chunk,
                impact->after_has_interface_data,
                impact->after_interface_ids_are_runtime,
                impact->after_interface_version,
                impact->after_interface_sub_count));
    } else if (impact->has_interface_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
    if (impact->has_data_cell_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_data_cell_snapshot_json(
                doc,
                impact->before_data_cell_row,
                impact->before_data_cell_col,
                impact->before_data_cell_type,
                impact->before_data_cell_value));
    } else if (impact->has_data_cell_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_data_cell_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_data_cell_snapshot_json(
                doc,
                impact->after_data_cell_row,
                impact->after_data_cell_col,
                impact->after_data_cell_type,
                impact->after_data_cell_value));
    } else if (impact->has_data_cell_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
    if (impact->has_manager_entry_before) {
        yyjson_mut_obj_add_val(
            doc, item, "before",
            nmo_cli_edit_report_make_manager_entry_snapshot_json(
                doc,
                impact->before_manager_guid,
                impact->before_manager_entry_schema,
                impact->before_manager_entry_key,
                impact->before_manager_entry_category,
                impact->before_manager_entry_type_guid,
                impact->before_manager_entry_index,
                impact->before_manager_entry_value,
                impact->before_manager_entry_compatible_class_id,
                impact->before_manager_entry_flags,
                impact->before_manager_entry_created,
                impact->before_manager_chunk_changed));
    } else if (impact->has_manager_entry_after) {
        yyjson_mut_obj_add_null(doc, item, "before");
    }
    if (impact->has_manager_entry_after) {
        yyjson_mut_obj_add_val(
            doc, item, "after",
            nmo_cli_edit_report_make_manager_entry_snapshot_json(
                doc,
                impact->after_manager_guid,
                impact->after_manager_entry_schema,
                impact->after_manager_entry_key,
                impact->after_manager_entry_category,
                impact->after_manager_entry_type_guid,
                impact->after_manager_entry_index,
                impact->after_manager_entry_value,
                impact->after_manager_entry_compatible_class_id,
                impact->after_manager_entry_flags,
                impact->after_manager_entry_created,
                impact->after_manager_chunk_changed));
    } else if (impact->has_manager_entry_before) {
        yyjson_mut_obj_add_null(doc, item, "after");
    }
}

static bool impact_role_contains(const nmo_edit_object_impact_t *impact,
                                 const char *needle)
{
    return impact != NULL && impact->role != NULL &&
           strstr(impact->role, needle) != NULL;
}

static bool impact_is_graph_edge(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    switch (impact->cause) {
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
    case NMO_EDIT_OP_FOLD:
    case NMO_EDIT_OP_REPLACE_BB:
        return true;
    default:
        return impact_role_contains(impact, "control_link") ||
               impact_role_contains(impact, "owned_link");
    }
}

static bool impact_is_parameter_edge(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    switch (impact->cause) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
    case NMO_EDIT_OP_ADD_PARAMETER:
    case NMO_EDIT_OP_CONNECT_PARAMETER:
    case NMO_EDIT_OP_DISCONNECT_PARAMETER:
    case NMO_EDIT_OP_REMOVE_PARAMETER:
    case NMO_EDIT_OP_SET_DATA_CELL:
    case NMO_EDIT_OP_FOLD:
    case NMO_EDIT_OP_REPLACE_BB:
        return true;
    default:
        return impact_role_contains(impact, "parameter");
    }
}

static bool impact_is_operation_graph(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    switch (impact->cause) {
    case NMO_EDIT_OP_ADD_OPERATION:
    case NMO_EDIT_OP_REWIRE_OPERATION:
    case NMO_EDIT_OP_REMOVE_OPERATION:
    case NMO_EDIT_OP_FOLD:
    case NMO_EDIT_OP_REPLACE_BB:
        return true;
    default:
        return impact_role_contains(impact, "operation");
    }
}

static bool impact_is_interface(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    return impact->cause == NMO_EDIT_OP_INTERFACE_POLICY ||
           impact_role_contains(impact, "interface");
}

static bool impact_is_data_cell(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    return impact->cause == NMO_EDIT_OP_SET_DATA_CELL ||
           impact_role_contains(impact, "data_cell");
}

static bool impact_is_manager_entry(const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return false;
    }
    return impact->id == NMO_EDIT_MANAGER_ENTRY_IMPACT_ID ||
           impact_role_contains(impact, "manager_entry");
}

static yyjson_mut_val *nmo_cli_edit_report_make_filtered_impact_array_json(
    yyjson_mut_doc *doc,
    const nmo_edit_object_impact_t *items,
    size_t count,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; items != NULL && i < count; ++i) {
        if (predicate != NULL && !predicate(&items[i])) {
            continue;
        }
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "object_id",
                                (uint64_t)items[i].id);
        yyjson_mut_obj_add_uint(doc, item, "id", (uint64_t)items[i].id);
        nmo_cli_json_add_str_safe(
            doc, item, "cause",
            nmo_cli_edit_report_op_kind_string(items[i].cause));
        nmo_cli_json_add_str_safe(doc, item, "role", items[i].role);
        nmo_cli_edit_report_add_impact_before_after_json(
            doc, item, &items[i]);
        yyjson_mut_arr_add_val(arr, item);
    }
    return arr;
}

static size_t nmo_cli_edit_report_count_filtered_impacts(
    const nmo_edit_object_impact_t *items,
    size_t count,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    size_t out_count = 0u;
    for (size_t i = 0; items != NULL && i < count; ++i) {
        if (predicate == NULL || predicate(&items[i])) {
            ++out_count;
        }
    }
    return out_count;
}

static yyjson_mut_val *nmo_cli_edit_report_make_structural_diff_json(
    yyjson_mut_doc *doc,
    const nmo_edit_report_t *report,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    yyjson_mut_val *diff = yyjson_mut_obj(doc);
    const nmo_edit_object_impact_t *changed =
        report != NULL ? report->changed_objects : NULL;
    const nmo_edit_object_impact_t *created =
        report != NULL ? report->created_objects : NULL;
    const nmo_edit_object_impact_t *deleted =
        report != NULL ? report->deleted_objects : NULL;
    size_t changed_count = report != NULL ? report->changed_object_count : 0u;
    size_t created_count = report != NULL ? report->created_object_count : 0u;
    size_t deleted_count = report != NULL ? report->deleted_object_count : 0u;

    yyjson_mut_obj_add_uint(
        doc, diff, "changed_count",
        (uint64_t)nmo_cli_edit_report_count_filtered_impacts(
            changed, changed_count, predicate));
    yyjson_mut_obj_add_uint(
        doc, diff, "created_count",
        (uint64_t)nmo_cli_edit_report_count_filtered_impacts(
            created, created_count, predicate));
    yyjson_mut_obj_add_uint(
        doc, diff, "deleted_count",
        (uint64_t)nmo_cli_edit_report_count_filtered_impacts(
            deleted, deleted_count, predicate));
    yyjson_mut_obj_add_val(
        doc, diff, "changed",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc, changed, changed_count, predicate));
    yyjson_mut_obj_add_val(
        doc, diff, "created",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc, created, created_count, predicate));
    yyjson_mut_obj_add_val(
        doc, diff, "deleted",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc, deleted, deleted_count, predicate));
    return diff;
}

void nmo_cli_edit_report_add_semantic_risks_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    const nmo_behavior_semantic_risk_t *risks =
        report != NULL ? report->semantic_risks : NULL;
    size_t risk_count = report != NULL ? report->semantic_risk_count : 0u;
    nmo_cli_edit_report_add_semantic_risk_array_json(
        doc, obj, risks, risk_count);
}

void nmo_cli_edit_report_add_semantic_risk_array_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_behavior_semantic_risk_t *risks,
    size_t risk_count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; risks != NULL && i < risk_count; ++i) {
        yyjson_mut_val *risk = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(
            doc, risk, "severity",
            semantic_risk_severity_string(risks[i].severity));
        nmo_cli_json_add_str_safe(doc, risk, "code", risks[i].code);
        nmo_cli_json_add_str_safe(doc, risk, "message", risks[i].message);
        yyjson_mut_obj_add_uint(doc, risk, "object_id",
                                (uint64_t)risks[i].object_id);
        yyjson_mut_arr_add_val(arr, risk);
    }
    yyjson_mut_obj_add_val(doc, obj, "semantic_risks", arr);
}

void nmo_cli_edit_report_add_validation_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    yyjson_mut_val *validation = yyjson_mut_obj(doc);
    const nmo_edit_validation_report_t zero = {0};
    const nmo_edit_validation_report_t *v =
        report != NULL ? &report->validation : &zero;
    yyjson_mut_obj_add_uint(doc, validation, "final_status",
                            (uint64_t)v->final_status);
    nmo_cli_json_add_str_safe(doc, validation, "final_status_name",
                              nmo_error_string(v->final_status));
    yyjson_mut_obj_add_uint(doc, validation, "roundtrip_status",
                            (uint64_t)v->roundtrip_status);
    nmo_cli_json_add_str_safe(doc, validation, "roundtrip_status_name",
                              nmo_error_string(v->roundtrip_status));
    yyjson_mut_obj_add_uint(doc, validation, "reference_status",
                            (uint64_t)v->reference_status);
    nmo_cli_json_add_str_safe(doc, validation, "reference_status_name",
                              nmo_error_string(v->reference_status));
    yyjson_mut_obj_add_uint(doc, validation, "behavior_index_status",
                            (uint64_t)v->behavior_index_status);
    nmo_cli_json_add_str_safe(doc, validation, "behavior_index_status_name",
                              nmo_error_string(v->behavior_index_status));
    yyjson_mut_obj_add_uint(doc, validation, "interface_status",
                            (uint64_t)v->interface_status);
    nmo_cli_json_add_str_safe(doc, validation, "interface_status_name",
                              nmo_error_string(v->interface_status));
    yyjson_mut_obj_add_val(doc, obj, "validation", validation);
}

void nmo_cli_edit_report_add_diff_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    yyjson_mut_val *diff = yyjson_mut_obj(doc);
    yyjson_mut_val *replay = yyjson_mut_obj(doc);
    yyjson_mut_val *object_diff = yyjson_mut_obj(doc);
    size_t operation_count = report != NULL ? report->operation_count : 0u;
    size_t changed_object_count =
        report != NULL ? report->changed_object_count : 0u;
    size_t created_object_count =
        report != NULL ? report->created_object_count : 0u;
    size_t deleted_object_count =
        report != NULL ? report->deleted_object_count : 0u;
    size_t semantic_risk_count =
        report != NULL ? report->semantic_risk_count : 0u;

    yyjson_mut_obj_add_uint(doc, diff, "changed_object_count",
                            (uint64_t)changed_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "created_object_count",
                            (uint64_t)created_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "deleted_object_count",
                            (uint64_t)deleted_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "semantic_risk_count",
                            (uint64_t)semantic_risk_count);
    yyjson_mut_obj_add_uint(doc, replay, "operation_count",
                            (uint64_t)operation_count);
    yyjson_mut_obj_add_uint(doc, replay, "changed_object_count",
                            (uint64_t)changed_object_count);
    yyjson_mut_obj_add_uint(doc, replay, "created_object_count",
                            (uint64_t)created_object_count);
    yyjson_mut_obj_add_uint(doc, replay, "deleted_object_count",
                            (uint64_t)deleted_object_count);
    yyjson_mut_obj_add_uint(doc, replay, "semantic_risk_count",
                            (uint64_t)semantic_risk_count);
    yyjson_mut_obj_add_val(
        doc, object_diff, "changed",
        nmo_cli_edit_report_make_impact_array_json(
            doc,
            report != NULL ? report->changed_objects : NULL,
            changed_object_count));
    yyjson_mut_obj_add_val(
        doc, object_diff, "created",
        nmo_cli_edit_report_make_impact_array_json(
            doc,
            report != NULL ? report->created_objects : NULL,
            created_object_count));
    yyjson_mut_obj_add_val(
        doc, object_diff, "deleted",
        nmo_cli_edit_report_make_impact_array_json(
            doc,
            report != NULL ? report->deleted_objects : NULL,
            deleted_object_count));
    yyjson_mut_obj_add_val(doc, diff, "object_diff", object_diff);
    yyjson_mut_obj_add_val(
        doc, diff, "graph_edge_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_graph_edge));
    yyjson_mut_obj_add_val(
        doc, diff, "parameter_edge_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_parameter_edge));
    yyjson_mut_obj_add_val(
        doc, diff, "operation_graph_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_operation_graph));
    yyjson_mut_obj_add_val(
        doc, diff, "interface_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_interface));
    yyjson_mut_obj_add_val(
        doc, diff, "data_cell_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_data_cell));
    yyjson_mut_obj_add_val(
        doc, diff, "manager_entry_diff",
        nmo_cli_edit_report_make_structural_diff_json(
            doc, report, impact_is_manager_entry));
    yyjson_mut_obj_add_val(doc, diff, "replay_summary", replay);
    yyjson_mut_obj_add_val(doc, obj, "diff", diff);
}

void nmo_cli_edit_report_add_diff_counts_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    size_t changed_object_count,
    size_t created_object_count,
    size_t deleted_object_count,
    size_t semantic_risk_count)
{
    yyjson_mut_val *diff = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, diff, "changed_object_count",
                            (uint64_t)changed_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "created_object_count",
                            (uint64_t)created_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "deleted_object_count",
                            (uint64_t)deleted_object_count);
    yyjson_mut_obj_add_uint(doc, diff, "semantic_risk_count",
                            (uint64_t)semantic_risk_count);
    yyjson_mut_obj_add_val(doc, obj, "diff", diff);
}
