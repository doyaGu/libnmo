#include "lua_bindings_internal.h"

#include "app/nmo_load.h"
#include "app/nmo_save.h"
#include "core/nmo_guid.h"
#include "lua/nmo_lua_runtime.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_runtime_result.h"
#include "session/nmo_session_query.h"

#include <stdlib.h>

#include "lauxlib.h"

const nmo_lua_handle_descriptor_t NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.context",
    .debug_name = "context"
};

const nmo_lua_handle_descriptor_t NMO_LUA_SESSION_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.session",
    .debug_name = "session"
};

const nmo_lua_handle_descriptor_t NMO_LUA_OBJECT_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.object",
    .debug_name = "object"
};

const nmo_lua_handle_descriptor_t NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.script_edit_tx",
    .debug_name = "script_edit_tx"
};

static void nmo_lua_release_context_handle(void *resource, void *user_data)
{
    (void)user_data;
    nmo_context_release((nmo_context_t *)resource);
}

static void nmo_lua_release_session_handle(void *resource, void *user_data)
{
    (void)user_data;
    nmo_session_destroy((nmo_session_t *)resource);
}

static void nmo_lua_release_object_handle(void *resource, void *user_data)
{
    (void)user_data;
    free(resource);
}

static void nmo_lua_release_script_edit_tx_handle(void *resource, void *user_data)
{
    (void)user_data;
    nmo_lua_script_edit_tx_handle_data_t *handle =
        (nmo_lua_script_edit_tx_handle_data_t *)resource;
    if (handle == NULL) {
        return;
    }
    if (!handle->finished && handle->tx != NULL) {
        nmo_script_edit_rollback(handle->tx);
    }
    free(handle);
}

int nmo_lua_raise_last_error(lua_State *state,
                             nmo_status_t status,
                             const char *fallback_message)
{
    const char *message = nmo_last_error_message();
    if (message == NULL || message[0] == '\0') {
        message = (fallback_message != NULL) ? fallback_message : nmo_error_string(status);
    }
    return luaL_error(state, "%s", message);
}

nmo_status_t nmo_lua_push_context_handle(lua_State *state, nmo_context_t *context)
{
    if (context == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Context handle resource must be non-null");
    }
    return nmo_lua_push_owned_handle(state,
                                     &NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR,
                                     context,
                                     nmo_lua_release_context_handle,
                                     NULL,
                                     NULL);
}

nmo_status_t nmo_lua_push_session_handle(lua_State *state, nmo_session_t *session)
{
    if (session == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Session handle resource must be non-null");
    }
    return nmo_lua_push_owned_handle(state,
                                     &NMO_LUA_SESSION_HANDLE_DESCRIPTOR,
                                     session,
                                     nmo_lua_release_session_handle,
                                     NULL,
                                     NULL);
}

nmo_status_t nmo_lua_push_object_handle(lua_State *state,
                                        nmo_session_t *session,
                                        nmo_lua_handle_scope_t *session_scope,
                                        nmo_object_id_t object_id)
{
    if (state == NULL || session == NULL || session_scope == NULL ||
        object_id == NMO_OBJECT_ID_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Object handles require state, session, scope, and object id");
    }

    nmo_lua_object_handle_data_t *data =
        (nmo_lua_object_handle_data_t *)calloc(1, sizeof(*data));
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua object handle data");
    }

    data->session = session;
    data->object_id = object_id;

    nmo_lua_handle_scope_t *object_scope = nmo_lua_handle_scope_create();
    if (object_scope == NULL) {
        free(data);
        return NMO_ERR_NOMEM;
    }

    nmo_status_t status = nmo_lua_push_scoped_handle(state,
                                                     &NMO_LUA_OBJECT_HANDLE_DESCRIPTOR,
                                                     data,
                                                     object_scope,
                                                     session_scope,
                                                     nmo_lua_release_object_handle,
                                                     NULL);
    if (status != NMO_OK) {
        free(data);
    }
    nmo_lua_handle_scope_release(object_scope);
    return status;
}

nmo_status_t nmo_lua_push_script_edit_tx_handle(lua_State *state,
                                                nmo_script_edit_tx_t *tx,
                                                nmo_lua_handle_scope_t *session_scope)
{
    if (state == NULL || tx == NULL || session_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script edit transaction handles require state, tx, and session scope");
    }

    nmo_lua_script_edit_tx_handle_data_t *data =
        (nmo_lua_script_edit_tx_handle_data_t *)calloc(1, sizeof(*data));
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua script edit handle");
    }
    data->tx = tx;

    nmo_lua_handle_scope_t *tx_scope = nmo_lua_handle_scope_create();
    if (tx_scope == NULL) {
        free(data);
        return NMO_ERR_NOMEM;
    }

    nmo_status_t status = nmo_lua_push_scoped_handle(state,
                                                     &NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR,
                                                     data,
                                                     tx_scope,
                                                     session_scope,
                                                     nmo_lua_release_script_edit_tx_handle,
                                                     NULL);
    if (status != NMO_OK) {
        free(data);
    }
    nmo_lua_handle_scope_release(tx_scope);
    return status;
}

nmo_status_t nmo_lua_check_context_handle(lua_State *state,
                                          int index,
                                          nmo_context_t **out_context,
                                          nmo_lua_handle_scope_t **out_scope)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    if (out_context != NULL) {
        *out_context = (nmo_context_t *)resource;
    }
    if (out_scope != NULL) {
        return nmo_lua_handle_get_scope(state,
                                        index,
                                        &NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR,
                                        out_scope);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_check_session_handle(lua_State *state,
                                          int index,
                                          nmo_session_t **out_session,
                                          nmo_lua_handle_scope_t **out_scope)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_SESSION_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    if (out_session != NULL) {
        *out_session = (nmo_session_t *)resource;
    }
    if (out_scope != NULL) {
        return nmo_lua_handle_get_scope(state,
                                        index,
                                        &NMO_LUA_SESSION_HANDLE_DESCRIPTOR,
                                        out_scope);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_check_object_handle(lua_State *state,
                                         int index,
                                         nmo_lua_object_handle_data_t **out_handle,
                                         nmo_object_t **out_object)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_OBJECT_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    nmo_lua_object_handle_data_t *handle = (nmo_lua_object_handle_data_t *)resource;
    if (handle == NULL || handle->session == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle has no owning session");
    }

    nmo_object_repository_t *repository =
        nmo_session_get_repository(handle->session);
    if (repository == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle session has no repository");
    }

    nmo_object_t *object =
        nmo_object_repository_find_by_id(repository, handle->object_id);
    if (object == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle is stale: target is no longer present");
    }

    if (out_handle != NULL) {
        *out_handle = handle;
    }
    if (out_object != NULL) {
        *out_object = object;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_check_script_edit_tx_handle(lua_State *state,
                                                 int index,
                                                 nmo_lua_script_edit_tx_handle_data_t **out_handle)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    nmo_lua_script_edit_tx_handle_data_t *handle =
        (nmo_lua_script_edit_tx_handle_data_t *)resource;
    if (handle == NULL || handle->finished || handle->tx == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua script edit transaction handle is stale");
    }

    if (out_handle != NULL) {
        *out_handle = handle;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_collect_object_id_array(lua_State *state,
                                             int index,
                                             nmo_session_t *session,
                                             nmo_arena_t *arena,
                                             nmo_object_id_t **out_ids,
                                             size_t *out_count)
{
    int table_index = 0;
    size_t count = 0u;
    nmo_object_id_t *ids = NULL;

    if (state == NULL || session == NULL || arena == NULL ||
        out_ids == NULL || out_count == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Session, arena, and output ids must be non-null");
    }

    table_index = lua_absindex(state, index);
    if (!lua_istable(state, table_index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected Lua table of object ids or object handles");
    }

    count = (size_t)lua_rawlen(state, table_index);
    if (count == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected at least one object id or object handle");
    }

    ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(*ids) * count, alignof(nmo_object_id_t));
    if (ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate object id list");
    }

    for (size_t i = 0u; i < count; ++i) {
        lua_geti(state, table_index, (lua_Integer)(i + 1u));
        if (lua_isinteger(state, -1)) {
            ids[i] = (nmo_object_id_t)lua_tointeger(state, -1);
            lua_pop(state, 1);
            continue;
        }

        if (lua_isuserdata(state, -1)) {
            nmo_lua_object_handle_data_t *handle = NULL;
            nmo_object_t *object = NULL;
            nmo_status_t status =
                nmo_lua_check_object_handle(state, -1, &handle, &object);
            if (status != NMO_OK) {
                lua_pop(state, 1);
                if (status == NMO_ERR_INVALID_STATE) {
                    return status;
                }
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Expected object ids or object handles");
            }
            if (handle == NULL || handle->session != session) {
                lua_pop(state, 1);
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Object handle belongs to a different session");
            }
            ids[i] = nmo_object_get_id(object);
            lua_pop(state, 1);
            continue;
        }

        lua_pop(state, 1);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected object ids or object handles");
    }

    *out_ids = ids;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_check_optional_flags_arg(lua_State *state,
                                              int index,
                                              uint32_t default_flags,
                                              uint32_t *out_flags)
{
    if (state == NULL || out_flags == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and output flags must be non-null");
    }

    if (lua_gettop(state) < index || lua_isnoneornil(state, index)) {
        *out_flags = default_flags;
        NMO_RETURN_OK();
    }

    if (!lua_isinteger(state, index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "flags must be an integer");
    }

    lua_Integer value = lua_tointeger(state, index);
    if (value < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "flags must be a non-negative integer");
    }

    *out_flags = (uint32_t)value;
    NMO_RETURN_OK();
}

static int nmo_lua_session_create_context(lua_State *state)
{
    nmo_context_t *context = nmo_context_create(NULL);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to create context");
    }

    nmo_status_t status = nmo_lua_push_context_handle(state, context);
    if (status != NMO_OK) {
        nmo_context_release(context);
        return nmo_lua_raise_last_error(state, status, "Failed to push context handle");
    }

    return 1;
}

static int nmo_lua_session_create(lua_State *state)
{
    nmo_context_t *context = NULL;
    nmo_status_t status =
        nmo_lua_check_context_handle(state, 1, &context, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    nmo_session_t *session = nmo_session_create(context);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to create session");
    }

    status = nmo_lua_push_session_handle(state, session);
    if (status != NMO_OK) {
        nmo_session_destroy(session);
        return nmo_lua_raise_last_error(state, status, "Failed to push session handle");
    }

    return 1;
}

static int nmo_lua_session_load_file(lua_State *state)
{
    nmo_context_t *context = NULL;
    nmo_status_t status =
        nmo_lua_check_context_handle(state, 1, &context, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    const char *path = luaL_checkstring(state, 2);
    nmo_session_t *session = nmo_session_create(context);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to create session");
    }

    status = nmo_load_file(session, path, NULL);
    if (status != NMO_OK) {
        nmo_session_destroy(session);
        return nmo_lua_raise_last_error(state, status, "Failed to load session file");
    }

    status = nmo_lua_push_session_handle(state, session);
    if (status != NMO_OK) {
        nmo_session_destroy(session);
        return nmo_lua_raise_last_error(state, status, "Failed to push session handle");
    }

    return 1;
}

static int nmo_lua_session_object_count(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    size_t count = 0;
    status = nmo_session_query_count_objects(session, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to count session objects");
    }

    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_session_find_object_by_name(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, &session_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    const char *name = luaL_checkstring(state, 2);
    nmo_object_t *object = NULL;
    status = nmo_session_query_find_object_by_name(session, name, &object);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK || object == NULL) {
        return nmo_lua_raise_last_error(state, status, "Failed to find object by name");
    }

    status = nmo_lua_push_object_handle(state,
                                        session,
                                        session_scope,
                                        nmo_object_get_id(object));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }

    return 1;
}

static int nmo_lua_session_save_file(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    const char *path = luaL_checkstring(state, 2);
    status = nmo_save_file(session, path, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to save session file");
    }

    return 0;
}

static int nmo_lua_session_create_object(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_class_id_t class_id = 0;
    const char *name = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    nmo_object_id_t created_id = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, &session_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    name = luaL_checkstring(state, 3);
    if (lua_gettop(state) >= 4 && !lua_isnoneornil(state, 4)) {
        const char *guid_text = luaL_checkstring(state, 4);
        type_guid = nmo_guid_parse(guid_text);
        if (nmo_guid_is_null(type_guid)) {
            return luaL_error(state, "Invalid type guid string '%s'", guid_text);
        }
    }

    status = nmo_session_create_object(session,
                                       class_id,
                                       name,
                                       type_guid,
                                       &created_id,
                                       &report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create object");
    }

    status = nmo_lua_push_object_handle(state, session, session_scope, created_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_session_copy_objects(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate object id arena");
    }
    status = nmo_lua_collect_object_id_array(state, 2, session, arena, &ids, &count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status == NMO_OK) {
        status = nmo_session_copy_objects(session, ids, count, flags, &report);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to copy objects");
    }

    lua_pushinteger(state, (lua_Integer)report.copied_objects);
    return 1;
}

static void nmo_lua_session_push_copy_result(lua_State *state,
                                             const nmo_copy_result_t *result)
{
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)result->copied_count);
    lua_setfield(state, -2, "copied_count");
    lua_pushinteger(state, (lua_Integer)result->affected_count);
    lua_setfield(state, -2, "affected_count");
    lua_pushinteger(state, (lua_Integer)result->manager_event_errors);
    lua_setfield(state, -2, "manager_event_errors");
    lua_pushinteger(state, (lua_Integer)result->object_hook_errors);
    lua_setfield(state, -2, "object_hook_errors");
}

static int nmo_lua_session_copy_objects_info(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_copy_result_t result = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate object id arena");
    }
    status = nmo_lua_collect_object_id_array(state, 2, session, arena, &ids, &count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status == NMO_OK) {
        status = nmo_session_copy_objects_result(session, ids, count, flags, &result);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to copy objects");
    }

    nmo_lua_session_push_copy_result(state, &result);
    return 1;
}

static int nmo_lua_session_destroy_objects(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate object id arena");
    }
    status = nmo_lua_collect_object_id_array(state, 2, session, arena, &ids, &count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status == NMO_OK) {
        status = nmo_session_destroy_objects(session, ids, count, flags, &report);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to destroy objects");
    }

    lua_pushinteger(state, (lua_Integer)report.deleted_objects);
    return 1;
}

static void nmo_lua_session_push_destroy_result(lua_State *state,
                                                const nmo_destroy_result_t *result)
{
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)result->deleted_count);
    lua_setfield(state, -2, "deleted_count");
    lua_pushinteger(state, (lua_Integer)result->affected_count);
    lua_setfield(state, -2, "affected_count");
    lua_pushinteger(state, (lua_Integer)result->manager_event_errors);
    lua_setfield(state, -2, "manager_event_errors");
    lua_pushinteger(state, (lua_Integer)result->object_hook_errors);
    lua_setfield(state, -2, "object_hook_errors");
}

static int nmo_lua_session_destroy_objects_info(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_destroy_result_t result = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate object id arena");
    }
    status = nmo_lua_collect_object_id_array(state, 2, session, arena, &ids, &count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status == NMO_OK) {
        status = nmo_session_destroy_objects_result(session, ids, count, flags, &result);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to destroy objects");
    }

    nmo_lua_session_push_destroy_result(state, &result);
    return 1;
}

static int nmo_lua_open_session_module(lua_State *state)
{
    lua_createtable(state, 0, 11);

    lua_pushcfunction(state, nmo_lua_session_create_context);
    lua_setfield(state, -2, "create_context");

    lua_pushcfunction(state, nmo_lua_session_create);
    lua_setfield(state, -2, "create");

    lua_pushcfunction(state, nmo_lua_session_load_file);
    lua_setfield(state, -2, "load_file");

    lua_pushcfunction(state, nmo_lua_session_save_file);
    lua_setfield(state, -2, "save_file");

    lua_pushcfunction(state, nmo_lua_session_create_object);
    lua_setfield(state, -2, "create_object");

    lua_pushcfunction(state, nmo_lua_session_copy_objects);
    lua_setfield(state, -2, "copy_objects");

    lua_pushcfunction(state, nmo_lua_session_copy_objects_info);
    lua_setfield(state, -2, "copy_objects_info");

    lua_pushcfunction(state, nmo_lua_session_destroy_objects);
    lua_setfield(state, -2, "destroy_objects");

    lua_pushcfunction(state, nmo_lua_session_destroy_objects_info);
    lua_setfield(state, -2, "destroy_objects_info");

    lua_pushcfunction(state, nmo_lua_session_object_count);
    lua_setfield(state, -2, "object_count");

    lua_pushcfunction(state, nmo_lua_session_find_object_by_name);
    lua_setfield(state, -2, "find_object_by_name");

    return 1;
}

nmo_status_t nmo_lua_register_session_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.session",
        .open_fn = nmo_lua_open_session_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
