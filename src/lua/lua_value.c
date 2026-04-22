#include "lua/nmo_lua_value.h"

#include "lauxlib.h"

void nmo_lua_value_push(lua_State *state, const nmo_lua_value_t *value)
{
    if (state == NULL || value == NULL) {
        return;
    }

    switch (value->kind) {
    case NMO_LUA_VALUE_NIL:
        lua_pushnil(state);
        break;
    case NMO_LUA_VALUE_BOOLEAN:
        lua_pushboolean(state, value->as.boolean_value ? 1 : 0);
        break;
    case NMO_LUA_VALUE_INTEGER:
        lua_pushinteger(state, value->as.integer_value);
        break;
    case NMO_LUA_VALUE_NUMBER:
        lua_pushnumber(state, value->as.number_value);
        break;
    case NMO_LUA_VALUE_STRING:
        lua_pushlstring(state,
                        value->as.string_value.data,
                        value->as.string_value.length);
        break;
    default:
        lua_pushnil(state);
        break;
    }
}

nmo_status_t nmo_lua_value_read(
    lua_State *state,
    int index,
    nmo_lua_value_t *out_value)
{
    if (state == NULL || out_value == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and output value must be non-null");
    }

    int type = lua_type(state, index);
    switch (type) {
    case LUA_TNIL:
        out_value->kind = NMO_LUA_VALUE_NIL;
        break;
    case LUA_TBOOLEAN:
        out_value->kind = NMO_LUA_VALUE_BOOLEAN;
        out_value->as.boolean_value = lua_toboolean(state, index) != 0;
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index)) {
            out_value->kind = NMO_LUA_VALUE_INTEGER;
            out_value->as.integer_value = lua_tointeger(state, index);
        } else {
            out_value->kind = NMO_LUA_VALUE_NUMBER;
            out_value->as.number_value = lua_tonumber(state, index);
        }
        break;
    case LUA_TSTRING:
        out_value->kind = NMO_LUA_VALUE_STRING;
        out_value->as.string_value.data =
            lua_tolstring(state, index, &out_value->as.string_value.length);
        break;
    default:
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Unsupported Lua value type '%s'",
                         luaL_typename(state, index));
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_value_read_expected(
    lua_State *state,
    int index,
    nmo_lua_value_kind_t expected_kind,
    bool allow_nil,
    nmo_lua_value_t *out_value)
{
    nmo_status_t status = nmo_lua_value_read(state, index, out_value);
    if (status != NMO_OK) {
        return status;
    }

    if (out_value->kind == NMO_LUA_VALUE_NIL && allow_nil) {
        NMO_RETURN_OK();
    }
    if (out_value->kind != expected_kind) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected Lua value kind %d but found %d",
                         (int)expected_kind,
                         (int)out_value->kind);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_value_get_field(
    lua_State *state,
    int index,
    const char *field_name,
    nmo_lua_value_kind_t expected_kind,
    bool allow_nil,
    nmo_lua_value_t *out_value)
{
    if (state == NULL || field_name == NULL || out_value == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state, field name, and output value must be non-null");
    }

    int table_index = lua_absindex(state, index);
    if (!lua_istable(state, table_index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected Lua table for field lookup");
    }

    lua_getfield(state, table_index, field_name);
    nmo_status_t status = nmo_lua_value_read_expected(state,
                                                      -1,
                                                      expected_kind,
                                                      allow_nil,
                                                      out_value);
    lua_pop(state, 1);
    return status;
}
