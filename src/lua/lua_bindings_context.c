#include "lua_bindings_internal.h"

#include "lua/nmo_lua_runtime.h"

#include "lauxlib.h"

static int nmo_lua_context_create(lua_State *state)
{
    nmo_context_desc_t desc = {0};
    const nmo_context_desc_t *desc_ptr = NULL;
    nmo_context_t *context = NULL;
    nmo_status_t status = NMO_OK;

    if (lua_gettop(state) >= 1 && !lua_isnil(state, 1)) {
        if (lua_istable(state, 1)) {
            lua_getfield(state, 1, "data_dir");
            if (!lua_isnil(state, -1)) {
                desc.data_dir = luaL_checkstring(state, -1);
            }
            lua_pop(state, 1);
        } else if (lua_isstring(state, 1)) {
            desc.data_dir = lua_tostring(state, 1);
        } else {
            return luaL_error(
                state,
                "context.create expects nil, a data_dir string, or an options table");
        }
        desc_ptr = &desc;
    }

    context = nmo_context_create(desc_ptr);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to create context");
    }

    status = nmo_lua_push_context_handle(state, context);
    if (status != NMO_OK) {
        nmo_context_release(context);
        return nmo_lua_raise_last_error(state, status, "Failed to push context handle");
    }

    return 1;
}

static int nmo_lua_open_context_module(lua_State *state)
{
    lua_createtable(state, 0, 1);
    lua_pushcfunction(state, nmo_lua_context_create);
    lua_setfield(state, -2, "create");
    return 1;
}

nmo_status_t nmo_lua_register_context_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.context",
        .open_fn = nmo_lua_open_context_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
