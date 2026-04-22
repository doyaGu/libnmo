#include "lua/nmo_lua_handles.h"

#include "core/nmo_refcount.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "lauxlib.h"

struct nmo_lua_handle_scope {
    nmo_refcount_t refcount;
    uint64_t generation;
    bool alive;
};

typedef struct nmo_lua_handle_userdata {
    const nmo_lua_handle_descriptor_t *descriptor;
    void *resource;
    nmo_lua_handle_scope_t *scope;
    nmo_lua_handle_scope_t *owner_scope;
    nmo_lua_handle_release_fn release_fn;
    void *release_user_data;
} nmo_lua_handle_userdata_t;

static uint64_t g_next_scope_generation = 1;

static void nmo_lua_handle_scope_dispose(void *user_data)
{
    free(user_data);
}

static int nmo_lua_handle_gc(lua_State *state)
{
    nmo_lua_handle_userdata_t *handle =
        (nmo_lua_handle_userdata_t *)lua_touserdata(state, 1);
    if (handle == NULL) {
        return 0;
    }

    if (handle->release_fn != NULL && handle->resource != NULL) {
        if (handle->scope != NULL) {
            nmo_lua_handle_scope_invalidate(handle->scope);
        }
        handle->release_fn(handle->resource, handle->release_user_data);
    }

    handle->resource = NULL;
    handle->release_fn = NULL;
    handle->release_user_data = NULL;

    if (handle->scope != NULL) {
        nmo_lua_handle_scope_release(handle->scope);
        handle->scope = NULL;
    }
    if (handle->owner_scope != NULL) {
        nmo_lua_handle_scope_release(handle->owner_scope);
        handle->owner_scope = NULL;
    }

    return 0;
}

static int nmo_lua_handle_tostring(lua_State *state)
{
    nmo_lua_handle_userdata_t *handle =
        (nmo_lua_handle_userdata_t *)lua_touserdata(state, 1);
    const char *debug_name = "handle";
    uint64_t generation = 0;

    if (handle != NULL && handle->descriptor != NULL &&
        handle->descriptor->debug_name != NULL &&
        handle->descriptor->debug_name[0] != '\0') {
        debug_name = handle->descriptor->debug_name;
    }
    if (handle != NULL && handle->scope != NULL) {
        generation = handle->scope->generation;
    }

    lua_pushfstring(state, "%s(%p)#%" PRIu64, debug_name, handle, generation);
    return 1;
}

static nmo_status_t nmo_lua_push_handle_common(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_scope_t *scope,
    nmo_lua_handle_scope_t *owner_scope,
    nmo_lua_handle_release_fn release_fn,
    void *release_user_data)
{
    nmo_status_t status =
        nmo_lua_handle_register_metatable(state, descriptor);
    if (status != NMO_OK) {
        return status;
    }

    nmo_lua_handle_userdata_t *handle =
        (nmo_lua_handle_userdata_t *)lua_newuserdatauv(state, sizeof(*handle), 0);
    if (handle == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua handle userdata");
    }

    handle->descriptor = descriptor;
    handle->resource = resource;
    handle->scope = scope;
    handle->owner_scope = owner_scope;
    handle->release_fn = release_fn;
    handle->release_user_data = release_user_data;

    if (scope != NULL) {
        nmo_lua_handle_scope_retain(scope);
    }
    if (owner_scope != NULL) {
        nmo_lua_handle_scope_retain(owner_scope);
    }

    luaL_getmetatable(state, descriptor->metatable_name);
    lua_setmetatable(state, -2);

    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_handle_validate_descriptor(
    const nmo_lua_handle_descriptor_t *descriptor)
{
    if (descriptor == NULL || descriptor->metatable_name == NULL ||
        descriptor->metatable_name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua handle descriptor requires a metatable name");
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_handle_get_userdata(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    nmo_lua_handle_userdata_t **out_handle)
{
    nmo_status_t status = nmo_lua_handle_validate_descriptor(descriptor);
    if (status != NMO_OK) {
        return status;
    }

    if (state == NULL || out_handle == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and output handle must be non-null");
    }

    nmo_lua_handle_userdata_t *handle =
        (nmo_lua_handle_userdata_t *)luaL_testudata(
            state, index, descriptor->metatable_name);
    if (handle == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected Lua %s handle", descriptor->debug_name != NULL
                             ? descriptor->debug_name
                             : descriptor->metatable_name);
    }

    *out_handle = handle;
    NMO_RETURN_OK();
}

nmo_lua_handle_scope_t *nmo_lua_handle_scope_create(void)
{
    nmo_lua_handle_scope_t *scope =
        (nmo_lua_handle_scope_t *)calloc(1, sizeof(*scope));
    if (scope == NULL) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Failed to allocate Lua handle scope");
        return NULL;
    }

    nmo_refcount_init(&scope->refcount);
    scope->generation = g_next_scope_generation++;
    scope->alive = true;
    nmo_last_error_clear();
    return scope;
}

void nmo_lua_handle_scope_retain(nmo_lua_handle_scope_t *scope)
{
    if (scope == NULL) {
        return;
    }
    (void)nmo_refcount_retain(&scope->refcount);
}

void nmo_lua_handle_scope_release(nmo_lua_handle_scope_t *scope)
{
    if (scope == NULL) {
        return;
    }
    (void)nmo_refcount_release_with(&scope->refcount,
                                    nmo_lua_handle_scope_dispose,
                                    scope);
}

void nmo_lua_handle_scope_invalidate(nmo_lua_handle_scope_t *scope)
{
    if (scope == NULL) {
        return;
    }
    scope->alive = false;
}

bool nmo_lua_handle_scope_is_alive(const nmo_lua_handle_scope_t *scope)
{
    return scope != NULL && scope->alive;
}

nmo_status_t nmo_lua_handle_register_metatable(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor)
{
    nmo_status_t status = nmo_lua_handle_validate_descriptor(descriptor);
    if (status != NMO_OK) {
        return status;
    }

    if (state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state must be non-null");
    }

    if (luaL_newmetatable(state, descriptor->metatable_name)) {
        lua_pushstring(state, descriptor->metatable_name);
        lua_setfield(state, -2, "__name");

        lua_pushcfunction(state, nmo_lua_handle_gc);
        lua_setfield(state, -2, "__gc");

        lua_pushcfunction(state, nmo_lua_handle_tostring);
        lua_setfield(state, -2, "__tostring");
    }

    lua_pop(state, 1);
    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_push_owned_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_release_fn release_fn,
    void *release_user_data,
    nmo_lua_handle_scope_t **out_scope)
{
    if (state == NULL || resource == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and resource must be non-null");
    }

    nmo_lua_handle_scope_t *scope = nmo_lua_handle_scope_create();
    if (scope == NULL) {
        if (release_fn != NULL) {
            release_fn(resource, release_user_data);
        }
        return NMO_ERR_NOMEM;
    }

    nmo_status_t status = nmo_lua_push_handle_common(state,
                                                     descriptor,
                                                     resource,
                                                     scope,
                                                     NULL,
                                                     release_fn,
                                                     release_user_data);
    if (status != NMO_OK) {
        nmo_lua_handle_scope_release(scope);
        if (release_fn != NULL) {
            release_fn(resource, release_user_data);
        }
        return status;
    }

    if (out_scope != NULL) {
        nmo_lua_handle_scope_retain(scope);
        *out_scope = scope;
    }
    nmo_lua_handle_scope_release(scope);

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_push_borrowed_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_scope_t *scope,
    nmo_lua_handle_scope_t *owner_scope)
{
    if (state == NULL || resource == NULL || scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state, resource, and liveness scope must be non-null");
    }

    return nmo_lua_push_handle_common(state,
                                      descriptor,
                                      resource,
                                      scope,
                                      owner_scope,
                                      NULL,
                                      NULL);
}

nmo_status_t nmo_lua_push_scoped_handle(
    lua_State *state,
    const nmo_lua_handle_descriptor_t *descriptor,
    void *resource,
    nmo_lua_handle_scope_t *scope,
    nmo_lua_handle_scope_t *owner_scope,
    nmo_lua_handle_release_fn release_fn,
    void *release_user_data)
{
    if (state == NULL || resource == NULL || scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state, resource, and scope must be non-null");
    }

    return nmo_lua_push_handle_common(state,
                                      descriptor,
                                      resource,
                                      scope,
                                      owner_scope,
                                      release_fn,
                                      release_user_data);
}

nmo_status_t nmo_lua_handle_check(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    const nmo_lua_handle_scope_t *expected_owner_scope,
    void **out_resource)
{
    nmo_lua_handle_userdata_t *handle = NULL;
    nmo_status_t status =
        nmo_lua_handle_get_userdata(state, index, descriptor, &handle);
    if (status != NMO_OK) {
        return status;
    }

    if (out_resource == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Output resource pointer must be non-null");
    }
    if (handle->resource == NULL || handle->scope == NULL ||
        !nmo_lua_handle_scope_is_alive(handle->scope)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua %s handle is stale",
                         descriptor->debug_name != NULL
                             ? descriptor->debug_name
                             : descriptor->metatable_name);
    }
    if (handle->owner_scope != NULL &&
        !nmo_lua_handle_scope_is_alive(handle->owner_scope)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua %s handle owner scope is stale",
                         descriptor->debug_name != NULL
                             ? descriptor->debug_name
                             : descriptor->metatable_name);
    }
    if (expected_owner_scope != NULL &&
        !nmo_lua_handle_scope_is_alive(expected_owner_scope)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Expected Lua owner scope is stale");
    }
    if (expected_owner_scope != NULL && handle->owner_scope != expected_owner_scope) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua %s handle belongs to a different owner scope",
                         descriptor->debug_name != NULL
                             ? descriptor->debug_name
                             : descriptor->metatable_name);
    }

    *out_resource = handle->resource;
    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_handle_get_scope(
    lua_State *state,
    int index,
    const nmo_lua_handle_descriptor_t *descriptor,
    nmo_lua_handle_scope_t **out_scope)
{
    nmo_lua_handle_userdata_t *handle = NULL;
    nmo_status_t status =
        nmo_lua_handle_get_userdata(state, index, descriptor, &handle);
    if (status != NMO_OK) {
        return status;
    }

    if (out_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Output scope pointer must be non-null");
    }
    if (handle->scope == NULL || !nmo_lua_handle_scope_is_alive(handle->scope)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua %s handle is stale",
                         descriptor->debug_name != NULL
                             ? descriptor->debug_name
                             : descriptor->metatable_name);
    }

    *out_scope = handle->scope;
    NMO_RETURN_OK();
}
