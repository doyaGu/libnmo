#include "lua/nmo_lua_module.h"

#include "lauxlib.h"

nmo_status_t nmo_lua_module_register(lua_State *state,
                                     const nmo_lua_module_t *module)
{
    if (state == NULL || module == NULL || module->name == NULL ||
        module->name[0] == '\0' || module->open_fn == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua module registration requires state, name, and open function");
    }

    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua package table is unavailable");
    }

    lua_getfield(state, -1, "preload");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 2);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua package.preload table is unavailable");
    }

    lua_pushcfunction(state, module->open_fn);
    lua_setfield(state, -2, module->name);
    lua_pop(state, 2);

    NMO_RETURN_OK();
}
