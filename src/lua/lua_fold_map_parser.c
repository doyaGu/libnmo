#include "lua/nmo_lua_fold_map_parser.h"

#include <stdint.h>
#include <stdlib.h>

static bool nmo_lua_fold_map_read_u32_field(lua_State *state,
                                            int table_index,
                                            const char *name,
                                            bool required,
                                            uint32_t *out_value,
                                            const char **out_error)
{
    table_index = lua_absindex(state, table_index);
    lua_getfield(state, table_index, name);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        if (!required) {
            return true;
        }
        *out_error = name;
        return false;
    }
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        *out_error = name;
        return false;
    }
    lua_Integer value = lua_tointeger(state, -1);
    lua_pop(state, 1);
    if (value < 0 || value > (lua_Integer)UINT32_MAX) {
        *out_error = name;
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

static bool nmo_lua_fold_map_read_u32_alias_field(lua_State *state,
                                                  int table_index,
                                                  const char *primary_name,
                                                  const char *alias_name,
                                                  uint32_t *out_value,
                                                  const char **out_error)
{
    table_index = lua_absindex(state, table_index);
    lua_getfield(state, table_index, primary_name);
    bool has_primary = !lua_isnil(state, -1);
    lua_pop(state, 1);
    if (has_primary) {
        return nmo_lua_fold_map_read_u32_field(
            state, table_index, primary_name, false, out_value, out_error);
    }
    return nmo_lua_fold_map_read_u32_field(
        state, table_index, alias_name, false, out_value, out_error);
}

static const char *nmo_lua_fold_map_old_id_alias(
    nmo_behavior_fold_map_kind_t kind)
{
    return kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
        ? "old_parameter_id"
        : "old_io_id";
}

static const char *nmo_lua_fold_map_new_id_alias(
    nmo_behavior_fold_map_kind_t kind)
{
    return kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
        ? "new_parameter_id"
        : "new_io_id";
}

bool nmo_lua_fold_map_parse(lua_State *state,
                            int options_index,
                            const char *field_name,
                            nmo_behavior_fold_map_kind_t kind,
                            nmo_behavior_fold_map_t **out_maps,
                            size_t *out_count,
                            const char **out_error)
{
    *out_maps = NULL;
    *out_count = 0u;
    options_index = lua_absindex(state, options_index);
    lua_getfield(state, options_index, field_name);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        *out_error = field_name;
        return false;
    }

    int maps_index = lua_absindex(state, -1);
    size_t count = lua_rawlen(state, maps_index);
    if (count == 0u) {
        lua_pop(state, 1);
        return true;
    }
    nmo_behavior_fold_map_t *maps =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*maps));
    if (maps == NULL) {
        lua_pop(state, 1);
        *out_error = "out_of_memory";
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(state, maps_index, (lua_Integer)i + 1);
        if (!lua_istable(state, -1)) {
            free(maps);
            lua_pop(state, 2);
            *out_error = field_name;
            return false;
        }
        int item_index = lua_absindex(state, -1);
        maps[i].kind = kind;
        if (!nmo_lua_fold_map_read_u32_field(
                state, item_index, "old_index", true, &maps[i].old_index,
                out_error) ||
            !nmo_lua_fold_map_read_u32_field(
                state, item_index, "new_index", true, &maps[i].new_index,
                out_error) ||
            !nmo_lua_fold_map_read_u32_alias_field(
                state, item_index, "old_id",
                nmo_lua_fold_map_old_id_alias(kind),
                &maps[i].old_id, out_error) ||
            !nmo_lua_fold_map_read_u32_alias_field(
                state, item_index, "new_id",
                nmo_lua_fold_map_new_id_alias(kind),
                &maps[i].new_id, out_error)) {
            free(maps);
            lua_pop(state, 2);
            return false;
        }
        lua_getfield(state, item_index, "label");
        if (!lua_isnil(state, -1)) {
            if (!lua_isstring(state, -1)) {
                free(maps);
                lua_pop(state, 3);
                *out_error = "label";
                return false;
            }
            maps[i].label = lua_tostring(state, -1);
        }
        lua_pop(state, 2);
    }

    lua_pop(state, 1);
    *out_maps = maps;
    *out_count = count;
    return true;
}
