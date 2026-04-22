#ifndef NMO_LUA_VALUE_H
#define NMO_LUA_VALUE_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#include "lua.h"

#define NMO_LUA_VALUE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_VALUE_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_lua_value_kind {
    NMO_LUA_VALUE_NIL = 0,
    NMO_LUA_VALUE_BOOLEAN = 1,
    NMO_LUA_VALUE_INTEGER = 2,
    NMO_LUA_VALUE_NUMBER = 3,
    NMO_LUA_VALUE_STRING = 4
} nmo_lua_value_kind_t;

typedef struct nmo_lua_string_view {
    const char *data;
    size_t length;
} nmo_lua_string_view_t;

typedef struct nmo_lua_value {
    nmo_lua_value_kind_t kind;
    union {
        bool boolean_value;
        lua_Integer integer_value;
        lua_Number number_value;
        nmo_lua_string_view_t string_value;
    } as;
} nmo_lua_value_t;

NMO_API void nmo_lua_value_push(lua_State *state, const nmo_lua_value_t *value);

NMO_API nmo_status_t nmo_lua_value_read(
    lua_State *state,
    int index,
    nmo_lua_value_t *out_value);

NMO_API nmo_status_t nmo_lua_value_read_expected(
    lua_State *state,
    int index,
    nmo_lua_value_kind_t expected_kind,
    bool allow_nil,
    nmo_lua_value_t *out_value);

NMO_API nmo_status_t nmo_lua_value_get_field(
    lua_State *state,
    int index,
    const char *field_name,
    nmo_lua_value_kind_t expected_kind,
    bool allow_nil,
    nmo_lua_value_t *out_value);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_VALUE_H */
