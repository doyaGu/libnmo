#include "lua_bindings_internal.h"

#include "nmo.h"
#include "lua/nmo_lua_runtime.h"

#include "lauxlib.h"

static int nmo_lua_core_version(lua_State *state)
{
    lua_pushstring(state, nmo_version());
    return 1;
}

static int nmo_lua_open_core_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "version", nmo_lua_core_version },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count);
    nmo_lua_set_functions(state, functions, function_count);
    return 1;
}

nmo_status_t nmo_lua_register_core_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.core",
        .open_fn = nmo_lua_open_core_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}

nmo_status_t nmo_lua_register_platform_bindings(nmo_lua_runtime_t *runtime)
{
    nmo_status_t status = nmo_lua_register_core_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_context_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_document_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_workspace_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_object_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_behavior_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_plan_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_session_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_runtime_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_type_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_register_format_bindings(runtime);
    if (status != NMO_OK) {
        return status;
    }

    return NMO_OK;
}
