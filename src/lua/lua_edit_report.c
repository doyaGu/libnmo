#include "lua_bindings_internal.h"

#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_enum_defs.h"
#include "lauxlib.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

const char *nmo_lua_edit_op_kind_string(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE: return "set_parameter_value";
    case NMO_EDIT_OP_SET_PARAMETER_BYTES: return "set_parameter_bytes";
    case NMO_EDIT_OP_ADD_NODE: return "add_node";
    case NMO_EDIT_OP_REMOVE_NODE: return "remove_node";
    case NMO_EDIT_OP_ADD_IO: return "add_io";
    case NMO_EDIT_OP_RENAME_IO: return "rename_io";
    case NMO_EDIT_OP_REMOVE_IO: return "remove_io";
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK: return "add_behavior_link";
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK: return "rewire_behavior_link";
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY: return "set_behavior_link_delay";
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK: return "remove_behavior_link";
    case NMO_EDIT_OP_ADD_PARAMETER: return "add_parameter";
    case NMO_EDIT_OP_CONNECT_PARAMETER: return "connect_parameter";
    case NMO_EDIT_OP_DISCONNECT_PARAMETER: return "disconnect_parameter";
    case NMO_EDIT_OP_REMOVE_PARAMETER: return "remove_parameter";
    case NMO_EDIT_OP_ADD_OPERATION: return "add_operation";
    case NMO_EDIT_OP_REWIRE_OPERATION: return "rewire_operation";
    case NMO_EDIT_OP_REMOVE_OPERATION: return "remove_operation";
    case NMO_EDIT_OP_INTERFACE_POLICY: return "interface_policy";
    case NMO_EDIT_OP_SET_DATA_CELL: return "set_data_cell";
    case NMO_EDIT_OP_FOLD: return "fold";
    case NMO_EDIT_OP_REPLACE_BB: return "replace_bb";
    default: return "unknown";
    }
}

const char *nmo_lua_edit_op_result_handle_name(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_ADD_NODE: return "node";
    case NMO_EDIT_OP_ADD_IO: return "io";
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK: return "link";
    case NMO_EDIT_OP_ADD_PARAMETER: return "parameter";
    case NMO_EDIT_OP_ADD_OPERATION: return "operation";
    default: return NULL;
    }
}

static void nmo_lua_push_edit_impacts(lua_State *state,
                                      const nmo_edit_object_impact_t *items,
                                      size_t count)
{
    lua_createtable(state, (int)count, 0);
    for (size_t i = 0; i < count; ++i) {
        lua_createtable(state, 0, 4);
        nmo_lua_set_integer_field(state, "object_id", (lua_Integer)items[i].id);
        nmo_lua_set_integer_field(state, "id", (lua_Integer)items[i].id);
        nmo_lua_set_string_field(
            state, "cause", nmo_lua_edit_op_kind_string(items[i].cause));
        nmo_lua_set_string_field(
            state, "role", items[i].role != NULL ? items[i].role : "");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
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

static void nmo_lua_push_control_link_snapshot(
    lua_State *state,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    lua_createtable(state, 0, 3);
    nmo_lua_set_integer_field(state, "from_io_id", (lua_Integer)from_io_id);
    nmo_lua_set_integer_field(state, "to_io_id", (lua_Integer)to_io_id);
    nmo_lua_set_integer_field(
        state, "activation_delay", (lua_Integer)activation_delay);
}

static void nmo_lua_push_parameter_edge_snapshot(
    lua_State *state,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    lua_createtable(state, 0, 2);
    nmo_lua_set_integer_field(
        state, "source_parameter_id", (lua_Integer)source_parameter_id);
    nmo_lua_set_integer_field(
        state, "target_parameter_id", (lua_Integer)target_parameter_id);
}

static void nmo_lua_push_operation_slot_snapshot(
    lua_State *state,
    nmo_guid_t operation_guid,
    bool has_in1,
    nmo_object_id_t in1_parameter_id,
    bool has_in2,
    nmo_object_id_t in2_parameter_id,
    bool has_out,
    nmo_object_id_t out_parameter_id)
{
    lua_createtable(state, 0, 7);
    char guid_text[32];
    if (nmo_guid_format(operation_guid, guid_text, sizeof(guid_text)) > 0) {
        nmo_lua_set_string_field(state, "operation_guid", guid_text);
    }
    nmo_lua_set_boolean_field(state, "has_in1", has_in1);
    nmo_lua_set_integer_field(
        state, "in1_parameter_id", (lua_Integer)in1_parameter_id);
    nmo_lua_set_boolean_field(state, "has_in2", has_in2);
    nmo_lua_set_integer_field(
        state, "in2_parameter_id", (lua_Integer)in2_parameter_id);
    nmo_lua_set_boolean_field(state, "has_out", has_out);
    nmo_lua_set_integer_field(
        state, "out_parameter_id", (lua_Integer)out_parameter_id);
}

static const char *nmo_lua_data_cell_type_name(uint32_t type)
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

static void nmo_lua_push_interface_snapshot(
    lua_State *state,
    nmo_object_id_t behavior_id,
    bool has_interface,
    bool has_interface_chunk,
    bool has_interface_data,
    bool interface_ids_are_runtime,
    uint32_t version,
    uint32_t sub_count)
{
    lua_createtable(state, 0, 7);
    nmo_lua_set_integer_field(state, "behavior_id", (lua_Integer)behavior_id);
    nmo_lua_set_boolean_field(state, "has_interface", has_interface);
    nmo_lua_set_boolean_field(
        state, "has_interface_chunk", has_interface_chunk);
    nmo_lua_set_boolean_field(state, "has_interface_data", has_interface_data);
    nmo_lua_set_boolean_field(
        state, "interface_ids_are_runtime", interface_ids_are_runtime);
    nmo_lua_set_integer_field(state, "version", (lua_Integer)version);
    nmo_lua_set_integer_field(state, "sub_count", (lua_Integer)sub_count);
}

static void nmo_lua_push_data_cell_snapshot(
    lua_State *state,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const char *value)
{
    lua_createtable(state, 0, 4);
    nmo_lua_set_integer_field(state, "row", (lua_Integer)row);
    nmo_lua_set_integer_field(state, "col", (lua_Integer)col);
    nmo_lua_set_string_field(state, "type", nmo_lua_data_cell_type_name(type));
    nmo_lua_set_string_field(state, "value", value != NULL ? value : "");
}

static void nmo_lua_push_impact_before_after(
    lua_State *state,
    const nmo_edit_object_impact_t *impact)
{
    if (impact == NULL) {
        return;
    }
    if (impact->has_control_link_before) {
        nmo_lua_push_control_link_snapshot(
            state,
            impact->before_from_io_id,
            impact->before_to_io_id,
            impact->before_activation_delay);
        lua_setfield(state, -2, "before");
    } else if (impact->has_control_link_after) {
        lua_pushnil(state);
        lua_setfield(state, -2, "before");
    }
    if (impact->has_control_link_after) {
        nmo_lua_push_control_link_snapshot(
            state,
            impact->after_from_io_id,
            impact->after_to_io_id,
            impact->after_activation_delay);
        lua_setfield(state, -2, "after");
    } else if (impact->has_control_link_before) {
        lua_pushnil(state);
        lua_setfield(state, -2, "after");
    }
    if (impact->has_parameter_edge_before) {
        nmo_lua_push_parameter_edge_snapshot(
            state,
            impact->before_source_parameter_id,
            impact->before_target_parameter_id);
        lua_setfield(state, -2, "before");
    } else if (impact->has_parameter_edge_after) {
        lua_pushnil(state);
        lua_setfield(state, -2, "before");
    }
    if (impact->has_parameter_edge_after) {
        nmo_lua_push_parameter_edge_snapshot(
            state,
            impact->after_source_parameter_id,
            impact->after_target_parameter_id);
        lua_setfield(state, -2, "after");
    } else if (impact->has_parameter_edge_before) {
        lua_pushnil(state);
        lua_setfield(state, -2, "after");
    }
    if (impact->has_operation_slot_before) {
        nmo_lua_push_operation_slot_snapshot(
            state,
            impact->before_operation_guid,
            impact->before_has_in1_parameter,
            impact->before_in1_parameter_id,
            impact->before_has_in2_parameter,
            impact->before_in2_parameter_id,
            impact->before_has_out_parameter,
            impact->before_out_parameter_id);
        lua_setfield(state, -2, "before");
    } else if (impact->has_operation_slot_after) {
        lua_pushnil(state);
        lua_setfield(state, -2, "before");
    }
    if (impact->has_operation_slot_after) {
        nmo_lua_push_operation_slot_snapshot(
            state,
            impact->after_operation_guid,
            impact->after_has_in1_parameter,
            impact->after_in1_parameter_id,
            impact->after_has_in2_parameter,
            impact->after_in2_parameter_id,
            impact->after_has_out_parameter,
            impact->after_out_parameter_id);
        lua_setfield(state, -2, "after");
    } else if (impact->has_operation_slot_before) {
        lua_pushnil(state);
        lua_setfield(state, -2, "after");
    }
    if (impact->has_interface_before) {
        nmo_lua_push_interface_snapshot(
            state,
            impact->before_interface_behavior_id,
            impact->before_has_interface,
            impact->before_has_interface_chunk,
            impact->before_has_interface_data,
            impact->before_interface_ids_are_runtime,
            impact->before_interface_version,
            impact->before_interface_sub_count);
        lua_setfield(state, -2, "before");
    } else if (impact->has_interface_after) {
        lua_pushnil(state);
        lua_setfield(state, -2, "before");
    }
    if (impact->has_interface_after) {
        nmo_lua_push_interface_snapshot(
            state,
            impact->after_interface_behavior_id,
            impact->after_has_interface,
            impact->after_has_interface_chunk,
            impact->after_has_interface_data,
            impact->after_interface_ids_are_runtime,
            impact->after_interface_version,
            impact->after_interface_sub_count);
        lua_setfield(state, -2, "after");
    } else if (impact->has_interface_before) {
        lua_pushnil(state);
        lua_setfield(state, -2, "after");
    }
    if (impact->has_data_cell_before) {
        nmo_lua_push_data_cell_snapshot(
            state,
            impact->before_data_cell_row,
            impact->before_data_cell_col,
            impact->before_data_cell_type,
            impact->before_data_cell_value);
        lua_setfield(state, -2, "before");
    } else if (impact->has_data_cell_after) {
        lua_pushnil(state);
        lua_setfield(state, -2, "before");
    }
    if (impact->has_data_cell_after) {
        nmo_lua_push_data_cell_snapshot(
            state,
            impact->after_data_cell_row,
            impact->after_data_cell_col,
            impact->after_data_cell_type,
            impact->after_data_cell_value);
        lua_setfield(state, -2, "after");
    } else if (impact->has_data_cell_before) {
        lua_pushnil(state);
        lua_setfield(state, -2, "after");
    }
}

static void nmo_lua_push_filtered_edit_impacts(
    lua_State *state,
    const nmo_edit_object_impact_t *items,
    size_t count,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    lua_newtable(state);
    lua_Integer out_index = 1;
    for (size_t i = 0; i < count; ++i) {
        if (predicate != NULL && !predicate(&items[i])) {
            continue;
        }
        lua_createtable(state, 0, 4);
        nmo_lua_set_integer_field(state, "object_id", (lua_Integer)items[i].id);
        nmo_lua_set_integer_field(state, "id", (lua_Integer)items[i].id);
        nmo_lua_set_string_field(
            state, "cause", nmo_lua_edit_op_kind_string(items[i].cause));
        nmo_lua_set_string_field(
            state, "role", items[i].role != NULL ? items[i].role : "");
        nmo_lua_push_impact_before_after(state, &items[i]);
        lua_rawseti(state, -2, out_index++);
    }
}

static size_t nmo_lua_count_filtered_edit_impacts(
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

static void nmo_lua_push_structural_edit_diff(
    lua_State *state,
    const nmo_edit_report_t *report,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    lua_createtable(state, 0, 6);
    nmo_lua_set_integer_field(
        state,
        "changed_count",
        (lua_Integer)nmo_lua_count_filtered_edit_impacts(
            report->changed_objects, report->changed_object_count, predicate));
    nmo_lua_set_integer_field(
        state,
        "created_count",
        (lua_Integer)nmo_lua_count_filtered_edit_impacts(
            report->created_objects, report->created_object_count, predicate));
    nmo_lua_set_integer_field(
        state,
        "deleted_count",
        (lua_Integer)nmo_lua_count_filtered_edit_impacts(
            report->deleted_objects, report->deleted_object_count, predicate));
    nmo_lua_push_filtered_edit_impacts(
        state, report->changed_objects, report->changed_object_count,
        predicate);
    lua_setfield(state, -2, "changed");
    nmo_lua_push_filtered_edit_impacts(
        state, report->created_objects, report->created_object_count,
        predicate);
    lua_setfield(state, -2, "created");
    nmo_lua_push_filtered_edit_impacts(
        state, report->deleted_objects, report->deleted_object_count,
        predicate);
    lua_setfield(state, -2, "deleted");
}

static void nmo_lua_push_edit_operation_handles(
    lua_State *state,
    const nmo_edit_operation_result_t *operation)
{
    lua_createtable(state, (int)operation->handle_count, 0);
    for (size_t i = 0; i < operation->handle_count; ++i) {
        lua_createtable(state, 0, 3);
        nmo_lua_set_string_field(
            state,
            "name",
            operation->handles[i].name != NULL ? operation->handles[i].name : "");
        nmo_lua_set_integer_field(
            state, "object_id", (lua_Integer)operation->handles[i].id);
        nmo_lua_set_integer_field(
            state, "id", (lua_Integer)operation->handles[i].id);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_push_edit_report_operations(lua_State *state,
                                                const nmo_edit_report_t *report)
{
    lua_createtable(state, (int)report->operation_count, 0);
    for (size_t i = 0; i < report->operation_count; ++i) {
        const nmo_edit_operation_result_t *operation = &report->operations[i];
        const char *kind = nmo_lua_edit_op_kind_string(operation->kind);
        lua_createtable(state, 0, 10);
        nmo_lua_set_integer_field(state, "index", (lua_Integer)i + 1);
        nmo_lua_set_string_field(state, "op", kind);
        nmo_lua_set_string_field(state, "kind", kind);
        nmo_lua_set_integer_field(
            state, "primary_id", (lua_Integer)operation->primary_id);
        nmo_lua_set_integer_field(
            state, "result_id", (lua_Integer)operation->result_id);
        nmo_lua_set_integer_field(
            state, "status", (lua_Integer)operation->status);
        nmo_lua_set_string_field(
            state, "status_name", nmo_error_string(operation->status));
        nmo_lua_set_optional_string_field(
            state, "diagnostic_code", operation->diagnostic_code);
        nmo_lua_set_optional_string_field(
            state, "diagnostic_message", operation->diagnostic_message);
        nmo_lua_push_edit_operation_handles(state, operation);
        lua_setfield(state, -2, "handles");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_push_edit_validation(
    lua_State *state,
    const nmo_edit_validation_report_t *validation)
{
    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(
        state, "final_status", (lua_Integer)validation->final_status);
    nmo_lua_set_integer_field(
        state, "roundtrip_status", (lua_Integer)validation->roundtrip_status);
    nmo_lua_set_integer_field(
        state, "reference_status", (lua_Integer)validation->reference_status);
    nmo_lua_set_integer_field(
        state,
        "behavior_index_status",
        (lua_Integer)validation->behavior_index_status);
    nmo_lua_set_integer_field(
        state, "interface_status", (lua_Integer)validation->interface_status);
}

static void nmo_lua_push_edit_diff(lua_State *state,
                                   const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(
        state,
        "changed_object_count",
        (lua_Integer)report->changed_object_count);
    nmo_lua_set_integer_field(
        state,
        "created_object_count",
        (lua_Integer)report->created_object_count);
    nmo_lua_set_integer_field(
        state,
        "deleted_object_count",
        (lua_Integer)report->deleted_object_count);
    nmo_lua_set_integer_field(
        state,
        "semantic_risk_count",
        (lua_Integer)report->semantic_risk_count);

    lua_createtable(state, 0, 3);
    nmo_lua_push_edit_impacts(
        state, report->changed_objects, report->changed_object_count);
    lua_setfield(state, -2, "changed");
    nmo_lua_push_edit_impacts(
        state, report->created_objects, report->created_object_count);
    lua_setfield(state, -2, "created");
    nmo_lua_push_edit_impacts(
        state, report->deleted_objects, report->deleted_object_count);
    lua_setfield(state, -2, "deleted");
    lua_setfield(state, -2, "object_diff");

    nmo_lua_push_structural_edit_diff(state, report, impact_is_graph_edge);
    lua_setfield(state, -2, "graph_edge_diff");
    nmo_lua_push_structural_edit_diff(state, report, impact_is_parameter_edge);
    lua_setfield(state, -2, "parameter_edge_diff");
    nmo_lua_push_structural_edit_diff(state, report, impact_is_operation_graph);
    lua_setfield(state, -2, "operation_graph_diff");
    nmo_lua_push_structural_edit_diff(state, report, impact_is_interface);
    lua_setfield(state, -2, "interface_diff");
    nmo_lua_push_structural_edit_diff(state, report, impact_is_data_cell);
    lua_setfield(state, -2, "data_cell_diff");

    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(
        state, "operation_count", (lua_Integer)report->operation_count);
    nmo_lua_set_integer_field(
        state,
        "changed_object_count",
        (lua_Integer)report->changed_object_count);
    nmo_lua_set_integer_field(
        state,
        "created_object_count",
        (lua_Integer)report->created_object_count);
    nmo_lua_set_integer_field(
        state,
        "deleted_object_count",
        (lua_Integer)report->deleted_object_count);
    nmo_lua_set_integer_field(
        state,
        "semantic_risk_count",
        (lua_Integer)report->semantic_risk_count);
    lua_setfield(state, -2, "replay_summary");
}

static const char *nmo_lua_edit_risk_severity_string(
    nmo_behavior_semantic_risk_severity_t severity)
{
    switch (severity) {
    case NMO_BEHAVIOR_SEMANTIC_RISK_SAFE: return "safe";
    case NMO_BEHAVIOR_SEMANTIC_RISK_WARN: return "warn";
    case NMO_BEHAVIOR_SEMANTIC_RISK_REJECT: return "reject";
    default: return "warn";
    }
}

static void nmo_lua_push_semantic_risks(lua_State *state,
                                        const nmo_edit_report_t *report)
{
    lua_createtable(state, (int)report->semantic_risk_count, 0);
    for (size_t i = 0; i < report->semantic_risk_count; ++i) {
        const nmo_behavior_semantic_risk_t *risk = &report->semantic_risks[i];
        lua_createtable(state, 0, 4);
        nmo_lua_set_string_field(
            state,
            "severity",
            nmo_lua_edit_risk_severity_string(risk->severity));
        nmo_lua_set_string_field(
            state, "code", risk->code != NULL ? risk->code : "");
        nmo_lua_set_string_field(
            state, "message", risk->message != NULL ? risk->message : "");
        nmo_lua_set_integer_field(
            state, "object_id", (lua_Integer)risk->object_id);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_push_probe_selector_diagnostics(
    lua_State *state,
    const nmo_probe_selector_result_t *analysis)
{
    lua_createtable(state, 0, 8);
    nmo_lua_set_string_field(
        state, "mode", nmo_probe_selector_mode_name(analysis->mode));
    nmo_lua_set_string_field(
        state, "status", nmo_probe_selector_status_name(analysis->status));
    nmo_lua_set_string_field(
        state, "rejection_code", analysis->rejection_code);
    nmo_lua_set_integer_field(
        state, "selected_node_id", (lua_Integer)analysis->selected_node_id);
    nmo_lua_set_integer_field(
        state, "selected_link_id", (lua_Integer)analysis->selected_link_id);
    nmo_lua_set_integer_field(
        state,
        "selected_operation_id",
        (lua_Integer)analysis->selected_operation_id);

    lua_createtable(state, (int)analysis->candidate_count, 0);
    for (size_t i = 0; i < analysis->candidate_count; ++i) {
        const nmo_probe_selector_candidate_t *candidate =
            &analysis->candidates[i];
        lua_createtable(state, 0, 10);
        nmo_lua_set_integer_field(
            state, "node_id", (lua_Integer)candidate->node_id);
        nmo_lua_set_integer_field(
            state, "parent_id", (lua_Integer)candidate->parent_id);
        nmo_lua_set_integer_field(
            state,
            "boundary_behavior_id",
            (lua_Integer)candidate->boundary_behavior_id);
        nmo_lua_set_integer_field(
            state, "link_id", (lua_Integer)candidate->link_id);
        nmo_lua_set_integer_field(
            state, "operation_id", (lua_Integer)candidate->operation_id);
        nmo_lua_set_integer_field(
            state,
            "source_parameter_id",
            (lua_Integer)candidate->source_parameter_id);
        nmo_lua_set_integer_field(
            state,
            "value_parameter_id",
            (lua_Integer)candidate->value_parameter_id);
        nmo_lua_set_integer_field(
            state, "dataarray_id", (lua_Integer)candidate->dataarray_id);
        nmo_lua_set_string_field(
            state, "role", nmo_probe_candidate_role_name(candidate->role));
        nmo_lua_set_string_field(
            state, "rejection_code", candidate->rejection_code);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "candidates");

    const nmo_probe_safe_insertion_t *safe = &analysis->safe_insertion;
    lua_createtable(state, 0, 8);
    nmo_lua_set_boolean_field(state, "selected", safe->selected);
    nmo_lua_set_integer_field(
        state, "selected_node_id", (lua_Integer)safe->selected_node_id);
    nmo_lua_set_integer_field(
        state, "selected_link_id", (lua_Integer)safe->selected_link_id);
    nmo_lua_set_integer_field(
        state,
        "selected_operation_id",
        (lua_Integer)safe->selected_operation_id);
    nmo_lua_set_integer_field(
        state, "remove_link_id", (lua_Integer)safe->remove_link_id);
    nmo_lua_set_integer_field(
        state, "insert_from_io_id", (lua_Integer)safe->insert_from_io_id);
    nmo_lua_set_integer_field(
        state, "insert_to_io_id", (lua_Integer)safe->insert_to_io_id);
    nmo_lua_set_integer_field(
        state, "preserved_delay", (lua_Integer)safe->preserved_delay);
    lua_setfield(state, -2, "safe_insertion");
}

void nmo_lua_push_edit_report(lua_State *state, const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 10);
    nmo_lua_set_boolean_field(state, "ok", report != NULL && report->ok);
    nmo_lua_set_boolean_field(
        state, "dry_run", report != NULL && report->dry_run);
    if (report != NULL) {
        nmo_lua_push_edit_report_operations(state, report);
    } else {
        lua_createtable(state, 0, 0);
    }
    lua_setfield(state, -2, "operations");
    nmo_lua_push_edit_impacts(
        state,
        report != NULL ? report->changed_objects : NULL,
        report != NULL ? report->changed_object_count : 0u);
    lua_setfield(state, -2, "changed_objects");
    nmo_lua_push_edit_impacts(
        state,
        report != NULL ? report->created_objects : NULL,
        report != NULL ? report->created_object_count : 0u);
    lua_setfield(state, -2, "created_objects");
    nmo_lua_push_edit_impacts(
        state,
        report != NULL ? report->deleted_objects : NULL,
        report != NULL ? report->deleted_object_count : 0u);
    lua_setfield(state, -2, "deleted_objects");
    if (report != NULL) {
        nmo_lua_push_semantic_risks(state, report);
    } else {
        lua_createtable(state, 0, 0);
    }
    lua_setfield(state, -2, "semantic_risks");
    if (report != NULL) {
        nmo_lua_push_edit_validation(state, &report->validation);
    } else {
        nmo_edit_validation_report_t validation = {0};
        nmo_lua_push_edit_validation(state, &validation);
    }
    lua_setfield(state, -2, "validation");
    if (report != NULL) {
        nmo_lua_push_edit_diff(state, report);
    } else {
        nmo_edit_report_t empty = {0};
        nmo_lua_push_edit_diff(state, &empty);
    }
    lua_setfield(state, -2, "diff");
    nmo_lua_set_optional_string_field(
        state,
        "output_path",
        report != NULL ? report->output_path : NULL);
    if (report != NULL && report->has_probe_selector_analysis) {
        nmo_lua_push_probe_selector_diagnostics(
            state, &report->probe_selector_analysis);
    } else {
        lua_createtable(state, 0, 0);
    }
    lua_setfield(state, -2, "probe_selector_diagnostics");
}

void nmo_lua_push_pending_edit_plan_report(lua_State *state,
                                           const nmo_edit_plan_t *plan)
{
    size_t count = nmo_edit_plan_count(plan);
    nmo_edit_report_t report = {0};
    report.ok = false;
    report.dry_run = false;
    report.status = NMO_OK;
    report.operation_count = count;
    if (count > 0u) {
        report.operations =
            (nmo_edit_operation_result_t *)calloc(count, sizeof(*report.operations));
    }
    if (count > 0u && report.operations == NULL) {
        luaL_error(state, "failed to allocate pending edit report");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const nmo_edit_op_t *op = nmo_edit_plan_get(plan, i);
        report.operations[i].kind = op != NULL ? op->kind : 0;
        report.operations[i].primary_id = op != NULL ? op->primary_id : 0u;
        report.operations[i].status = NMO_OK;
    }
    report.validation.final_status = NMO_OK;
    nmo_lua_push_edit_report(state, &report);
    nmo_lua_set_boolean_field(state, "pending", true);
    free(report.operations);
}
