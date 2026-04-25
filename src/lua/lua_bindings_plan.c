#include "lua_bindings_internal.h"

#include "behavior/nmo_edit_plan.h"
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

static const char *nmo_lua_plan_op_kind_string(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_ADD_IO:
        return "add_io";
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

static void nmo_lua_plan_push_report(
    lua_State *state,
    const nmo_edit_report_t *report)
{
    lua_createtable(state, 0, 8);
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
    lua_pushcfunction(state, nmo_lua_plan_add_io);
    lua_setfield(state, -2, "add_io");
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
