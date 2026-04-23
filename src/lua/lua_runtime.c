#include "lua/nmo_lua_runtime.h"

#include <stdlib.h>

#include "lua/nmo_lua_module.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

struct nmo_lua_runtime {
    lua_State *state;
};

static int nmo_lua_runtime_traceback(lua_State *state)
{
    const char *message = lua_tostring(state, 1);
    if (message == NULL) {
        if (!lua_isnoneornil(state, 1) && luaL_callmeta(state, 1, "__tostring")) {
            message = lua_tostring(state, -1);
        } else {
            lua_pushliteral(state, "(error object is not a string)");
            message = lua_tostring(state, -1);
        }
    }

    luaL_traceback(state, state, message, 1);
    return 1;
}

nmo_lua_runtime_t *nmo_lua_runtime_create(void)
{
    lua_State *state = luaL_newstate();
    if (state == NULL) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Failed to allocate Lua state");
        return NULL;
    }

    nmo_lua_runtime_t *runtime =
        (nmo_lua_runtime_t *)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        lua_close(state);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Failed to allocate Lua runtime wrapper");
        return NULL;
    }

    luaL_openlibs(state);
    runtime->state = state;
    nmo_last_error_clear();
    return runtime;
}

void nmo_lua_runtime_destroy(nmo_lua_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    if (runtime->state != NULL) {
        lua_close(runtime->state);
    }

    free(runtime);
}

nmo_status_t nmo_lua_runtime_execute_string(nmo_lua_runtime_t *runtime,
                                            const char *chunk)
{
    if (runtime == NULL || runtime->state == NULL || chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua runtime and chunk must be non-null");
    }

    lua_settop(runtime->state, 0);
    lua_pushcfunction(runtime->state, nmo_lua_runtime_traceback);
    int traceback_index = lua_gettop(runtime->state);

    if (luaL_loadstring(runtime->state, chunk) != LUA_OK) {
        const char *message = lua_tostring(runtime->state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Lua load failed: %s",
                           (message != NULL) ? message : "unknown load error");
        lua_settop(runtime->state, 0);
        return NMO_ERR_VALIDATION_FAILED;
    }

    if (lua_pcall(runtime->state, 0, LUA_MULTRET, traceback_index) != LUA_OK) {
        const char *message = lua_tostring(runtime->state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Lua execution failed: %s",
                           (message != NULL) ? message : "unknown execution error");
        lua_settop(runtime->state, 0);
        return NMO_ERR_VALIDATION_FAILED;
    }

    lua_settop(runtime->state, 0);
    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_runtime_register_module(nmo_lua_runtime_t *runtime,
                                             const nmo_lua_module_t *module)
{
    if (runtime == NULL || runtime->state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua runtime must be non-null");
    }

    return nmo_lua_module_register(runtime->state, module);
}

lua_State *nmo_lua_runtime_state(nmo_lua_runtime_t *runtime)
{
    return runtime != NULL ? runtime->state : NULL;
}
