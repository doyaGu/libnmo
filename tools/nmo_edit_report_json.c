#include "nmo_edit_report_json.h"

#include "nmo_cli_json.h"

#include "core/nmo_error.h"

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
        yyjson_mut_arr_add_val(arr, item);
    }
    return arr;
}

static yyjson_mut_val *nmo_cli_edit_report_make_structural_diff_json(
    yyjson_mut_doc *doc,
    const nmo_edit_report_t *report,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    yyjson_mut_val *diff = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(
        doc, diff, "changed",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc,
            report != NULL ? report->changed_objects : NULL,
            report != NULL ? report->changed_object_count : 0u,
            predicate));
    yyjson_mut_obj_add_val(
        doc, diff, "created",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc,
            report != NULL ? report->created_objects : NULL,
            report != NULL ? report->created_object_count : 0u,
            predicate));
    yyjson_mut_obj_add_val(
        doc, diff, "deleted",
        nmo_cli_edit_report_make_filtered_impact_array_json(
            doc,
            report != NULL ? report->deleted_objects : NULL,
            report != NULL ? report->deleted_object_count : 0u,
            predicate));
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
