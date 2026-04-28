#include "lua_bindings_internal.h"

#include "core/nmo_error.h"
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
        lua_pushinteger(state, (lua_Integer)items[i].id);
        lua_setfield(state, -2, "object_id");
        lua_pushinteger(state, (lua_Integer)items[i].id);
        lua_setfield(state, -2, "id");
        lua_pushstring(state, nmo_lua_edit_op_kind_string(items[i].cause));
        lua_setfield(state, -2, "cause");
        lua_pushstring(state, items[i].role != NULL ? items[i].role : "");
        lua_setfield(state, -2, "role");
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
        lua_pushinteger(state, (lua_Integer)items[i].id);
        lua_setfield(state, -2, "object_id");
        lua_pushinteger(state, (lua_Integer)items[i].id);
        lua_setfield(state, -2, "id");
        lua_pushstring(state, nmo_lua_edit_op_kind_string(items[i].cause));
        lua_setfield(state, -2, "cause");
        lua_pushstring(state, items[i].role != NULL ? items[i].role : "");
        lua_setfield(state, -2, "role");
        lua_rawseti(state, -2, out_index++);
    }
}

static void nmo_lua_push_structural_edit_diff(
    lua_State *state,
    const nmo_edit_report_t *report,
    bool (*predicate)(const nmo_edit_object_impact_t *))
{
    lua_createtable(state, 0, 3);
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
        lua_pushstring(
            state,
            operation->handles[i].name != NULL ? operation->handles[i].name : "");
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)operation->handles[i].id);
        lua_setfield(state, -2, "object_id");
        lua_pushinteger(state, (lua_Integer)operation->handles[i].id);
        lua_setfield(state, -2, "id");
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
        lua_pushinteger(state, (lua_Integer)i + 1);
        lua_setfield(state, -2, "index");
        lua_pushstring(state, kind);
        lua_setfield(state, -2, "op");
        lua_pushstring(state, kind);
        lua_setfield(state, -2, "kind");
        lua_pushinteger(state, (lua_Integer)operation->primary_id);
        lua_setfield(state, -2, "primary_id");
        lua_pushinteger(state, (lua_Integer)operation->result_id);
        lua_setfield(state, -2, "result_id");
        lua_pushinteger(state, (lua_Integer)operation->status);
        lua_setfield(state, -2, "status");
        lua_pushstring(state, nmo_error_string(operation->status));
        lua_setfield(state, -2, "status_name");
        if (operation->diagnostic_code != NULL) {
            lua_pushstring(state, operation->diagnostic_code);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "diagnostic_code");
        if (operation->diagnostic_message != NULL) {
            lua_pushstring(state, operation->diagnostic_message);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "diagnostic_message");
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
    lua_pushinteger(state, (lua_Integer)validation->final_status);
    lua_setfield(state, -2, "final_status");
    lua_pushinteger(state, (lua_Integer)validation->roundtrip_status);
    lua_setfield(state, -2, "roundtrip_status");
    lua_pushinteger(state, (lua_Integer)validation->reference_status);
    lua_setfield(state, -2, "reference_status");
    lua_pushinteger(state, (lua_Integer)validation->behavior_index_status);
    lua_setfield(state, -2, "behavior_index_status");
    lua_pushinteger(state, (lua_Integer)validation->interface_status);
    lua_setfield(state, -2, "interface_status");
}

static void nmo_lua_push_edit_diff(lua_State *state,
                                   const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)report->changed_object_count);
    lua_setfield(state, -2, "changed_object_count");
    lua_pushinteger(state, (lua_Integer)report->created_object_count);
    lua_setfield(state, -2, "created_object_count");
    lua_pushinteger(state, (lua_Integer)report->deleted_object_count);
    lua_setfield(state, -2, "deleted_object_count");
    lua_pushinteger(state, (lua_Integer)report->semantic_risk_count);
    lua_setfield(state, -2, "semantic_risk_count");

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
    lua_pushinteger(state, (lua_Integer)report->operation_count);
    lua_setfield(state, -2, "operation_count");
    lua_pushinteger(state, (lua_Integer)report->changed_object_count);
    lua_setfield(state, -2, "changed_object_count");
    lua_pushinteger(state, (lua_Integer)report->created_object_count);
    lua_setfield(state, -2, "created_object_count");
    lua_pushinteger(state, (lua_Integer)report->deleted_object_count);
    lua_setfield(state, -2, "deleted_object_count");
    lua_pushinteger(state, (lua_Integer)report->semantic_risk_count);
    lua_setfield(state, -2, "semantic_risk_count");
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
        lua_pushstring(state, nmo_lua_edit_risk_severity_string(risk->severity));
        lua_setfield(state, -2, "severity");
        lua_pushstring(state, risk->code != NULL ? risk->code : "");
        lua_setfield(state, -2, "code");
        lua_pushstring(state, risk->message != NULL ? risk->message : "");
        lua_setfield(state, -2, "message");
        lua_pushinteger(state, (lua_Integer)risk->object_id);
        lua_setfield(state, -2, "object_id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

void nmo_lua_push_edit_report(lua_State *state, const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 10);
    lua_pushboolean(state, report != NULL && report->ok);
    lua_setfield(state, -2, "ok");
    lua_pushboolean(state, report != NULL && report->dry_run);
    lua_setfield(state, -2, "dry_run");
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
    if (report != NULL && report->output_path != NULL) {
        lua_pushstring(state, report->output_path);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "output_path");
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
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, "pending");
    free(report.operations);
}
