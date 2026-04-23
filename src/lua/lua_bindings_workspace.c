#include "lua_bindings_internal.h"

#include "lua/nmo_lua_runtime.h"

#include "lauxlib.h"

static int nmo_lua_workspace_create(lua_State *state)
{
    nmo_context_t *context = NULL;
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status =
        nmo_lua_check_context_handle(state, 1, &context, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    status = nmo_lua_check_document_handle(state, 2, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_workspace_create(context, document, &workspace);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create workspace");
    }

    status = nmo_lua_push_workspace_handle(state, workspace, document_scope);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        return nmo_lua_raise_last_error(state, status, "Failed to push workspace handle");
    }

    return 1;
}

static int nmo_lua_workspace_apply_edit_flags(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    uint32_t flags = 0u;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    flags = (uint32_t)luaL_checkinteger(state, 2);
    status = nmo_workspace_apply_edit_flags(workspace, flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to apply workspace edit flags");
    }

    return 0;
}

static int nmo_lua_open_workspace_module(lua_State *state)
{
    lua_createtable(state, 0, 3);

    lua_pushcfunction(state, nmo_lua_workspace_create);
    lua_setfield(state, -2, "create");

    lua_pushcfunction(state, nmo_lua_workspace_apply_edit_flags);
    lua_setfield(state, -2, "apply_edit_flags");

    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)NMO_WORKSPACE_EDIT_OBJECT_STATE);
    lua_setfield(state, -2, "object_state");
    lua_pushinteger(state, (lua_Integer)NMO_WORKSPACE_EDIT_REFERENCES);
    lua_setfield(state, -2, "references");
    lua_pushinteger(state, (lua_Integer)NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    lua_setfield(state, -2, "behavior_graph");
    lua_pushinteger(state, (lua_Integer)NMO_WORKSPACE_EDIT_NAMES);
    lua_setfield(state, -2, "names");
    lua_pushinteger(state, (lua_Integer)NMO_WORKSPACE_EDIT_RESOURCES);
    lua_setfield(state, -2, "resources");
    lua_setfield(state, -2, "edit_flags");

    return 1;
}

nmo_status_t nmo_lua_register_workspace_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.workspace",
        .open_fn = nmo_lua_open_workspace_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
