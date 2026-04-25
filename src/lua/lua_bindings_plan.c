#include "lua_bindings_internal.h"

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_guid.h"
#include "lua/nmo_lua_runtime.h"

#include "lauxlib.h"

#include <string.h>

const nmo_lua_handle_descriptor_t NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.edit_plan",
    .debug_name = "edit_plan",
};

static void nmo_lua_plan_release(void *resource, void *user_data)
{
    (void)user_data;
    nmo_edit_plan_destroy((nmo_edit_plan_t *)resource);
}

static nmo_status_t nmo_lua_push_edit_plan_handle(
    lua_State *state,
    nmo_edit_plan_t *plan)
{
    return nmo_lua_push_owned_handle(
        state,
        &NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR,
        plan,
        nmo_lua_plan_release,
        NULL,
        NULL);
}

static nmo_status_t nmo_lua_check_edit_plan_handle(
    lua_State *state,
    int index,
    nmo_edit_plan_t **out_plan)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(
        state,
        index,
        &NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR,
        NULL,
        &resource);
    if (status != NMO_OK) {
        return status;
    }
    *out_plan = (nmo_edit_plan_t *)resource;
    return NMO_OK;
}

static int nmo_lua_plan_new(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_edit_plan_create(&plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create edit plan");
    }

    status = nmo_lua_push_edit_plan_handle(state, plan);
    if (status != NMO_OK) {
        nmo_edit_plan_destroy(plan);
        return nmo_lua_raise_last_error(state, status, "Failed to push edit plan");
    }
    return 1;
}

static int nmo_lua_plan_count(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    lua_pushinteger(state, (lua_Integer)nmo_edit_plan_count(plan));
    return 1;
}

static int nmo_lua_plan_add_node(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *guid_text = luaL_checkstring(state, 3);
    const char *name = luaL_checkstring(state, 4);
    nmo_guid_t guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(guid)) {
        return luaL_error(state, "invalid building block GUID");
    }

    status = nmo_edit_plan_add_node(plan, behavior_id, guid, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add node op");
    }
    return 0;
}

static int nmo_lua_plan_add_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *kind_text = luaL_checkstring(state, 3);
    const char *name = luaL_checkstring(state, 4);
    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    if (strcmp(kind_text, "input") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_INPUT;
    } else if (strcmp(kind_text, "output") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
    } else {
        return luaL_error(state, "io kind must be 'input' or 'output'");
    }

    status = nmo_edit_plan_add_io(plan, behavior_id, kind, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add io op");
    }
    return 0;
}

static int nmo_lua_plan_rename_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *name = luaL_checkstring(state, 3);

    status = nmo_edit_plan_add_rename_io(plan, io_id, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add rename io op");
    }
    return 0;
}

static int nmo_lua_plan_remove_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    bool detach_links = lua_toboolean(state, 3) != 0;

    status = nmo_edit_plan_add_remove_io(plan, io_id, detach_links);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add remove io op");
    }
    return 0;
}

static int nmo_lua_plan_remove_node(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t node_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    uint32_t delete_flags = (uint32_t)luaL_optinteger(state, 4, 0);

    status = nmo_edit_plan_add_remove_node(
        plan, parent_id, node_id, delete_flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add remove node op");
    }
    return 0;
}

static int nmo_lua_plan_add_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_object_id_t to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);
    uint32_t delay = (uint32_t)luaL_optinteger(state, 5, 0);

    status = nmo_edit_plan_add_behavior_link(
        plan, parent_id, from_io_id, to_io_id, delay);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add behavior link op");
    }
    return 0;
}

static int nmo_lua_plan_rewire_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_object_id_t to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);

    status = nmo_edit_plan_add_rewire_behavior_link(
        plan, link_id, from_io_id, to_io_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add rewire behavior link op");
    }
    return 0;
}

static int nmo_lua_plan_set_behavior_link_delay(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    uint32_t delay = (uint32_t)luaL_checkinteger(state, 3);

    status = nmo_edit_plan_add_set_behavior_link_delay(plan, link_id, delay);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add behavior link delay op");
    }
    return 0;
}

static int nmo_lua_plan_remove_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 3);

    status = nmo_edit_plan_add_remove_behavior_link(plan, parent_id, link_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add remove behavior link op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_value(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *value = luaL_checkstring(state, 3);

    status = nmo_edit_plan_add_set_parameter_value(plan, parameter_id, value, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter value op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_bytes(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    size_t byte_count = 0u;
    const char *bytes = luaL_checklstring(state, 3, &byte_count);
    nmo_parameter_write_options_t options = {0};
    if (lua_istable(state, 4)) {
        lua_getfield(state, 4, "resize");
        if (!lua_isnil(state, -1)) {
            options.resize = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
    }

    status = nmo_edit_plan_add_set_parameter_bytes(
        plan, parameter_id, (const uint8_t *)bytes, byte_count, &options);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter bytes op");
    }
    return 0;
}

static int nmo_lua_plan_set_data_cell(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t dataarray_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    lua_Integer row_arg = luaL_checkinteger(state, 3);
    lua_Integer col_arg = luaL_checkinteger(state, 4);
    const char *value = luaL_checkstring(state, 5);
    if (row_arg < 0 || col_arg < 0) {
        return luaL_error(state, "row and col must be non-negative");
    }
    uint32_t row = (uint32_t)row_arg;
    uint32_t col = (uint32_t)col_arg;

    status = nmo_edit_plan_add_data_cell(plan, dataarray_id, row, col, value);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set data cell op");
    }
    return 0;
}

static int nmo_lua_plan_interface_policy(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *mode_text = luaL_checkstring(state, 3);
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    if (strcmp(mode_text, "preserve") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    } else if (strcmp(mode_text, "canonicalize") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
    } else if (strcmp(mode_text, "remove") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
    } else {
        return luaL_error(
            state,
            "interface mode must be 'preserve', 'canonicalize', or 'remove'");
    }

    status = nmo_edit_plan_add_interface_policy(plan, behavior_id, mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add interface policy op");
    }
    return 0;
}

static const char *nmo_lua_plan_op_kind_string(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return "set_parameter_value";
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return "set_parameter_bytes";
    case NMO_EDIT_OP_ADD_NODE:
        return "add_node";
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
    case NMO_EDIT_OP_REMOVE_NODE:
        return "remove_node";
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return "interface_policy";
    case NMO_EDIT_OP_SET_DATA_CELL:
        return "set_data_cell";
    default:
        return "unknown";
    }
}

static void nmo_lua_plan_push_handles(
    lua_State *state,
    const nmo_edit_operation_result_t *operation)
{
    lua_createtable(state, (int)operation->handle_count, 0);
    for (size_t i = 0; i < operation->handle_count; ++i) {
        lua_createtable(state, 0, 3);
        lua_pushstring(state, operation->handles[i].name);
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)operation->handles[i].id);
        lua_setfield(state, -2, "object_id");
        lua_pushinteger(state, (lua_Integer)operation->handles[i].id);
        lua_setfield(state, -2, "id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_plan_push_operations(
    lua_State *state,
    const nmo_edit_report_t *report)
{
    lua_createtable(state, (int)report->operation_count, 0);
    for (size_t i = 0; i < report->operation_count; ++i) {
        const nmo_edit_operation_result_t *op = &report->operations[i];
        const char *kind = nmo_lua_plan_op_kind_string(op->kind);
        lua_createtable(state, 0, 8);
        lua_pushinteger(state, (lua_Integer)i + 1);
        lua_setfield(state, -2, "index");
        lua_pushstring(state, kind);
        lua_setfield(state, -2, "op");
        lua_pushstring(state, kind);
        lua_setfield(state, -2, "kind");
        lua_pushinteger(state, (lua_Integer)op->primary_id);
        lua_setfield(state, -2, "primary_id");
        lua_pushinteger(state, (lua_Integer)op->result_id);
        lua_setfield(state, -2, "result_id");
        lua_pushinteger(state, (lua_Integer)op->status);
        lua_setfield(state, -2, "status");
        lua_pushstring(state, nmo_error_string(op->status));
        lua_setfield(state, -2, "status_name");
        nmo_lua_plan_push_handles(state, op);
        lua_setfield(state, -2, "handles");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_plan_push_impacts(
    lua_State *state,
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
        lua_pushstring(state, nmo_lua_plan_op_kind_string(items[i].cause));
        lua_setfield(state, -2, "cause");
        lua_pushstring(state, items[i].role);
        lua_setfield(state, -2, "role");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_plan_push_validation(
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

static void nmo_lua_plan_push_diff(
    lua_State *state,
    const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)report->changed_object_count);
    lua_setfield(state, -2, "changed_object_count");
    lua_pushinteger(state, (lua_Integer)report->created_object_count);
    lua_setfield(state, -2, "created_object_count");
    lua_pushinteger(state, (lua_Integer)report->deleted_object_count);
    lua_setfield(state, -2, "deleted_object_count");
    lua_pushinteger(state, (lua_Integer)report->semantic_risk_count);
    lua_setfield(state, -2, "semantic_risk_count");
}

static const char *nmo_lua_plan_risk_severity_string(
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

static void nmo_lua_plan_push_semantic_risks(
    lua_State *state,
    const nmo_edit_report_t *report)
{
    lua_createtable(state, (int)report->semantic_risk_count, 0);
    for (size_t i = 0; i < report->semantic_risk_count; ++i) {
        const nmo_behavior_semantic_risk_t *risk = &report->semantic_risks[i];
        lua_createtable(state, 0, 4);
        lua_pushstring(
            state, nmo_lua_plan_risk_severity_string(risk->severity));
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

static void nmo_lua_plan_push_report(
    lua_State *state,
    const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 11);
    lua_pushboolean(state, report->ok);
    lua_setfield(state, -2, "ok");
    lua_pushboolean(state, report->dry_run);
    lua_setfield(state, -2, "dry_run");
    lua_pushinteger(state, (lua_Integer)report->operation_count);
    lua_setfield(state, -2, "operation_count");
    nmo_lua_plan_push_operations(state, report);
    lua_setfield(state, -2, "operations");
    nmo_lua_plan_push_impacts(
        state, report->changed_objects, report->changed_object_count);
    lua_setfield(state, -2, "changed_objects");
    nmo_lua_plan_push_impacts(
        state, report->created_objects, report->created_object_count);
    lua_setfield(state, -2, "created_objects");
    nmo_lua_plan_push_impacts(
        state, report->deleted_objects, report->deleted_object_count);
    lua_setfield(state, -2, "deleted_objects");
    nmo_lua_plan_push_validation(state, &report->validation);
    lua_setfield(state, -2, "validation");
    nmo_lua_plan_push_diff(state, report);
    lua_setfield(state, -2, "diff");
    nmo_lua_plan_push_semantic_risks(state, report);
    lua_setfield(state, -2, "semantic_risks");
}

static int nmo_lua_plan_execute(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_report_t report;
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }
    status = nmo_lua_check_workspace_handle(state, 2, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }
    if (lua_istable(state, 3)) {
        lua_getfield(state, 3, "dry_run");
        if (!lua_isnil(state, -1)) {
            options.dry_run = lua_toboolean(state, -1);
        }
        lua_pop(state, 1);
    }

    status = nmo_edit_report_init(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create edit report");
    }
    status = nmo_edit_executor_execute(workspace, plan, &options, &report);
    if (status != NMO_OK) {
        nmo_edit_report_dispose(&report);
        return nmo_lua_raise_last_error(state, status, "Failed to execute edit plan");
    }
    nmo_lua_plan_push_report(state, &report);
    nmo_edit_report_dispose(&report);
    return 1;
}

static int nmo_lua_open_plan_module(lua_State *state)
{
    lua_createtable(state, 0, 4);
    lua_pushcfunction(state, nmo_lua_plan_new);
    lua_setfield(state, -2, "new");
    lua_pushcfunction(state, nmo_lua_plan_count);
    lua_setfield(state, -2, "count");
    lua_pushcfunction(state, nmo_lua_plan_add_node);
    lua_setfield(state, -2, "add_node");
    lua_pushcfunction(state, nmo_lua_plan_add_io);
    lua_setfield(state, -2, "add_io");
    lua_pushcfunction(state, nmo_lua_plan_rename_io);
    lua_setfield(state, -2, "rename_io");
    lua_pushcfunction(state, nmo_lua_plan_remove_io);
    lua_setfield(state, -2, "remove_io");
    lua_pushcfunction(state, nmo_lua_plan_remove_node);
    lua_setfield(state, -2, "remove_node");
    lua_pushcfunction(state, nmo_lua_plan_add_behavior_link);
    lua_setfield(state, -2, "add_behavior_link");
    lua_pushcfunction(state, nmo_lua_plan_rewire_behavior_link);
    lua_setfield(state, -2, "rewire_behavior_link");
    lua_pushcfunction(state, nmo_lua_plan_set_behavior_link_delay);
    lua_setfield(state, -2, "set_behavior_link_delay");
    lua_pushcfunction(state, nmo_lua_plan_remove_behavior_link);
    lua_setfield(state, -2, "remove_behavior_link");
    lua_pushcfunction(state, nmo_lua_plan_set_parameter_value);
    lua_setfield(state, -2, "set_parameter_value");
    lua_pushcfunction(state, nmo_lua_plan_set_parameter_bytes);
    lua_setfield(state, -2, "set_parameter_bytes");
    lua_pushcfunction(state, nmo_lua_plan_set_data_cell);
    lua_setfield(state, -2, "set_data_cell");
    lua_pushcfunction(state, nmo_lua_plan_interface_policy);
    lua_setfield(state, -2, "interface_policy");
    lua_pushcfunction(state, nmo_lua_plan_execute);
    lua_setfield(state, -2, "execute");
    return 1;
}

nmo_status_t nmo_lua_register_plan_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.plan",
        .open_fn = nmo_lua_open_plan_module,
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
