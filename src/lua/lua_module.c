#include "lua/nmo_lua_module.h"

#include "lua_bindings_internal.h"

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

void nmo_lua_set_functions(lua_State *state,
                           const nmo_lua_function_entry_t *entries,
                           size_t count)
{
    size_t i = 0u;
    for (i = 0u; i < count; ++i) {
        lua_pushcfunction(state, entries[i].fn);
        lua_setfield(state, -2, entries[i].name);
    }
}

void nmo_lua_set_integers(lua_State *state,
                          const nmo_lua_integer_entry_t *entries,
                          size_t count)
{
    size_t i = 0u;
    for (i = 0u; i < count; ++i) {
        lua_pushinteger(state, entries[i].value);
        lua_setfield(state, -2, entries[i].name);
    }
}

void nmo_lua_push_integer_table(lua_State *state,
                                const nmo_lua_integer_entry_t *entries,
                                size_t count)
{
    lua_createtable(state, 0, (int)count);
    nmo_lua_set_integers(state, entries, count);
}

void nmo_lua_set_integer_field(lua_State *state,
                               const char *field_name,
                               lua_Integer value)
{
    lua_pushinteger(state, value);
    lua_setfield(state, -2, field_name);
}

void nmo_lua_set_number_field(lua_State *state,
                              const char *field_name,
                              lua_Number value)
{
    lua_pushnumber(state, value);
    lua_setfield(state, -2, field_name);
}

void nmo_lua_set_boolean_field(lua_State *state,
                               const char *field_name,
                               bool value)
{
    lua_pushboolean(state, value ? 1 : 0);
    lua_setfield(state, -2, field_name);
}

void nmo_lua_set_optional_string_field(lua_State *state,
                                       const char *field_name,
                                       const char *value)
{
    if (value != NULL) {
        lua_pushstring(state, value);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, field_name);
}
