#ifndef NMO_LUA_HANDLES_H
#define NMO_LUA_HANDLES_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>

#include "lua.h"

#define NMO_LUA_HANDLES_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_HANDLES_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_lua_handle_scope nmo_lua_handle_scope_t;

typedef void (*nmo_lua_handle_release_fn)(void *resource, void *user_data);

typedef struct nmo_lua_handle_descriptor {
    const char *metatable_name;
    const char *debug_name;
} nmo_lua_handle_descriptor_t;

NMO_API nmo_lua_handle_scope_t *nmo_lua_handle_scope_create(void);
NMO_API void nmo_lua_handle_scope_retain(nmo_lua_handle_scope_t *scope);
NMO_API void nmo_lua_handle_scope_release(nmo_lua_handle_scope_t *scope);
NMO_API void nmo_lua_handle_scope_invalidate(nmo_lua_handle_scope_t *scope);
NMO_API bool nmo_lua_handle_scope_is_alive(const nmo_lua_handle_scope_t *scope);

NMO_API nmo_status_t nmo_lua_handle_register_metatable(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor);

NMO_API nmo_status_t nmo_lua_push_owned_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_release_fn release_fn,
    void *release_user_data,
    nmo_lua_handle_scope_t **out_scope);

NMO_API nmo_status_t nmo_lua_push_borrowed_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_scope_t *scope,
    nmo_lua_handle_scope_t *owner_scope);

NMO_API nmo_status_t nmo_lua_push_scoped_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_scope_t *scope,
    nmo_lua_handle_scope_t *owner_scope,
    nmo_lua_handle_release_fn release_fn,
    void *release_user_data);

NMO_API nmo_status_t nmo_lua_handle_check(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    const nmo_lua_handle_scope_t *expected_owner_scope,
    void **out_resource);

NMO_API nmo_status_t nmo_lua_handle_get_scope(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    nmo_lua_handle_scope_t **out_scope);

NMO_API nmo_status_t nmo_lua_handle_get_owner_scope(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    nmo_lua_handle_scope_t **out_scope);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_HANDLES_H */
