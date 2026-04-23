#include "lua_bindings_internal.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "core/nmo_guid.h"
#include "document/nmo_document.h"
#include "lua/nmo_lua_runtime.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "session/nmo_session_bridge.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_runtime_result.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"

const nmo_lua_handle_descriptor_t NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.context",
    .debug_name = "context"
};

const nmo_lua_handle_descriptor_t NMO_LUA_DOCUMENT_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.document",
    .debug_name = "document"
};

const nmo_lua_handle_descriptor_t NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.workspace",
    .debug_name = "workspace"
};

const nmo_lua_handle_descriptor_t NMO_LUA_SESSION_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.session",
    .debug_name = "session"
};

const nmo_lua_handle_descriptor_t NMO_LUA_OBJECT_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.object",
    .debug_name = "object"
};

const nmo_lua_handle_descriptor_t NMO_LUA_RUNTIME_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.runtime_handle",
    .debug_name = "runtime"
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

static void nmo_lua_release_document_handle(void *resource, void *user_data)
{
    (void)user_data;
    nmo_document_destroy((nmo_document_t *)resource);
}

static void nmo_lua_release_workspace_handle(void *resource, void *user_data)
{
    (void)user_data;
    nmo_workspace_destroy((nmo_workspace_t *)resource);
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

nmo_status_t nmo_lua_push_document_handle(lua_State *state, nmo_document_t *document)
{
    if (document == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Document handle resource must be non-null");
    }
    return nmo_lua_push_owned_handle(state,
                                     &NMO_LUA_DOCUMENT_HANDLE_DESCRIPTOR,
                                     document,
                                     nmo_lua_release_document_handle,
                                     NULL,
                                     NULL);
}

nmo_status_t nmo_lua_push_workspace_handle(lua_State *state,
                                           nmo_workspace_t *workspace,
                                           nmo_lua_handle_scope_t *document_scope)
{
    nmo_lua_handle_scope_t *workspace_scope = NULL;
    nmo_status_t status = NMO_OK;

    if (workspace == NULL || document_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Workspace handles require workspace and document scope");
    }

    workspace_scope = nmo_lua_handle_scope_create();
    if (workspace_scope == NULL) {
        return NMO_ERR_NOMEM;
    }

    status = nmo_lua_push_scoped_handle(state,
                                        &NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR,
                                        workspace,
                                        workspace_scope,
                                        document_scope,
                                        nmo_lua_release_workspace_handle,
                                        NULL);
    nmo_lua_handle_scope_release(workspace_scope);
    return status;
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

static nmo_status_t nmo_lua_push_borrowed_session_handle(
    lua_State *state,
    nmo_session_t *session,
    nmo_lua_handle_scope_t *owner_scope)
{
    if (session == NULL || owner_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Borrowed session handles require session and owner scope");
    }

    return nmo_lua_push_borrowed_handle(state,
                                        &NMO_LUA_SESSION_HANDLE_DESCRIPTOR,
                                        session,
                                        owner_scope,
                                        owner_scope);
}

nmo_status_t nmo_lua_push_object_handle(lua_State *state,
                                        nmo_document_t *document,
                                        nmo_lua_handle_scope_t *document_scope,
                                        nmo_object_id_t object_id)
{
    if (state == NULL || document == NULL || document_scope == NULL ||
        object_id == NMO_OBJECT_ID_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Object handles require state, document, scope, and object id");
    }

    nmo_lua_object_handle_data_t *data =
        (nmo_lua_object_handle_data_t *)calloc(1, sizeof(*data));
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua object handle data");
    }

    data->document = document;
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
                                                     document_scope,
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
                                                nmo_lua_handle_scope_t *workspace_scope)
{
    if (state == NULL || tx == NULL || workspace_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script edit transaction handles require state, tx, and workspace scope");
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
                                                     workspace_scope,
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

nmo_status_t nmo_lua_check_document_handle(lua_State *state,
                                           int index,
                                           nmo_document_t **out_document,
                                           nmo_lua_handle_scope_t **out_scope)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_DOCUMENT_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    if (out_document != NULL) {
        *out_document = (nmo_document_t *)resource;
    }
    if (out_scope != NULL) {
        return nmo_lua_handle_get_scope(state,
                                        index,
                                        &NMO_LUA_DOCUMENT_HANDLE_DESCRIPTOR,
                                        out_scope);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_check_workspace_handle(lua_State *state,
                                            int index,
                                            nmo_workspace_t **out_workspace,
                                            nmo_lua_handle_scope_t **out_scope,
                                            nmo_lua_handle_scope_t **out_document_scope)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(state,
                                               index,
                                               &NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR,
                                               NULL,
                                               &resource);
    if (status != NMO_OK) {
        return status;
    }

    if (out_workspace != NULL) {
        *out_workspace = (nmo_workspace_t *)resource;
    }
    if (out_scope != NULL) {
        status = nmo_lua_handle_get_scope(state,
                                          index,
                                          &NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR,
                                          out_scope);
        if (status != NMO_OK) {
            return status;
        }
    }
    if (out_document_scope != NULL) {
        status = nmo_lua_handle_get_owner_scope(state,
                                                index,
                                                &NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR,
                                                out_document_scope);
        if (status != NMO_OK) {
            return status;
        }
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
    if (handle == NULL || handle->document == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle has no owning document");
    }

    nmo_object_repository_t *repository =
        nmo_document_get_repository(handle->document);
    if (repository == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle document has no repository");
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
            if (handle == NULL || handle->document == NULL ||
                nmo_session_from_document(handle->document) != session) {
                lua_pop(state, 1);
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Object handle belongs to a different document");
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

static nmo_status_t nmo_lua_parse_query_integer_field(lua_State *state,
                                                      int table_index,
                                                      const char *field_name,
                                                      lua_Integer minimum,
                                                      lua_Integer *out_value,
                                                      bool *out_present)
{
    lua_getfield(state, table_index, field_name);
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        if (out_present != NULL) {
            *out_present = false;
        }
        NMO_RETURN_OK();
    }
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "query.%s must be an integer", field_name);
    }

    lua_Integer value = lua_tointeger(state, -1);
    lua_pop(state, 1);
    if (value < minimum) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "query.%s must be >= %lld", field_name, (long long)minimum);
    }

    if (out_value != NULL) {
        *out_value = value;
    }
    if (out_present != NULL) {
        *out_present = true;
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_parse_query_boolean_field(lua_State *state,
                                                      int table_index,
                                                      const char *field_name,
                                                      bool *out_value,
                                                      bool *out_present)
{
    lua_getfield(state, table_index, field_name);
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        if (out_present != NULL) {
            *out_present = false;
        }
        NMO_RETURN_OK();
    }
    if (!lua_isboolean(state, -1)) {
        lua_pop(state, 1);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "query.%s must be a boolean", field_name);
    }

    if (out_value != NULL) {
        *out_value = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);
    if (out_present != NULL) {
        *out_present = true;
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_parse_query_string_field(lua_State *state,
                                                     int table_index,
                                                     const char *field_name,
                                                     const char **out_value,
                                                     bool *out_present)
{
    lua_getfield(state, table_index, field_name);
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        if (out_present != NULL) {
            *out_present = false;
        }
        NMO_RETURN_OK();
    }
    if (!lua_isstring(state, -1)) {
        lua_pop(state, 1);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "query.%s must be a string", field_name);
    }

    if (out_value != NULL) {
        *out_value = lua_tostring(state, -1);
    }
    if (out_present != NULL) {
        *out_present = true;
    }
    lua_pop(state, 1);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_parse_object_query_name_mode_value(
    lua_State *state,
    int index,
    nmo_object_query_name_mode_t *out_mode)
{
    if (lua_isinteger(state, index)) {
        lua_Integer mode = lua_tointeger(state, index);
        if (mode < NMO_OBJECT_QUERY_NAME_NONE ||
            mode > NMO_OBJECT_QUERY_NAME_REGEX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "query.name_mode must be a valid name mode");
        }
        *out_mode = (nmo_object_query_name_mode_t)mode;
        NMO_RETURN_OK();
    }

    if (lua_isstring(state, index)) {
        const char *mode = lua_tostring(state, index);
        if (strcmp(mode, "none") == 0) {
            *out_mode = NMO_OBJECT_QUERY_NAME_NONE;
            NMO_RETURN_OK();
        }
        if (strcmp(mode, "exact") == 0) {
            *out_mode = NMO_OBJECT_QUERY_NAME_EXACT;
            NMO_RETURN_OK();
        }
        if (strcmp(mode, "substring") == 0) {
            *out_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING;
            NMO_RETURN_OK();
        }
        if (strcmp(mode, "wildcard") == 0) {
            *out_mode = NMO_OBJECT_QUERY_NAME_WILDCARD;
            NMO_RETURN_OK();
        }
        if (strcmp(mode, "regex") == 0) {
            *out_mode = NMO_OBJECT_QUERY_NAME_REGEX;
            NMO_RETURN_OK();
        }
    }

    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                     "query.name_mode must be one of none/exact/substring/wildcard/regex");
}

nmo_status_t nmo_lua_parse_object_query(lua_State *state,
                                        int index,
                                        nmo_object_query_t *out_query)
{
    int table_index = 0;
    lua_Integer integer_value = 0;
    const char *string_value = NULL;
    bool present = false;
    bool bool_value = false;

    if (state == NULL || out_query == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and output query must be non-null");
    }

    table_index = lua_absindex(state, index);
    if (!lua_istable(state, table_index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected query table");
    }

    memset(out_query, 0, sizeof(*out_query));

    nmo_status_t status = nmo_lua_parse_query_integer_field(
        state, table_index, "object_id", 0, &integer_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->object_id = (nmo_object_id_t)integer_value;
    }

    status = nmo_lua_parse_query_integer_field(
        state, table_index, "class_id", 0, &integer_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->class_id = (nmo_class_id_t)integer_value;
    }

    status = nmo_lua_parse_query_boolean_field(
        state, table_index, "include_derived_classes", &bool_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->include_derived_classes = bool_value;
    }

    status = nmo_lua_parse_query_boolean_field(
        state, table_index, "has_type_guid", &bool_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->has_type_guid = bool_value;
    }

    status = nmo_lua_parse_query_string_field(
        state, table_index, "type_guid", &string_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->type_guid = nmo_guid_parse(string_value);
        if (nmo_guid_is_null(out_query->type_guid)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Invalid query type_guid string '%s'", string_value);
        }
        out_query->has_type_guid = true;
    }

    status = nmo_lua_parse_query_string_field(
        state, table_index, "name", &string_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->name = string_value;
    }

    lua_getfield(state, table_index, "name_mode");
    if (!lua_isnoneornil(state, -1)) {
        status = nmo_lua_parse_object_query_name_mode_value(
            state, -1, &out_query->name_mode);
        lua_pop(state, 1);
        if (status != NMO_OK) {
            return status;
        }
    } else {
        lua_pop(state, 1);
    }

    status = nmo_lua_parse_query_boolean_field(
        state, table_index, "name_case_insensitive", &bool_value, &present);
    if (status != NMO_OK) {
        return status;
    }
    if (present) {
        out_query->name_case_insensitive = bool_value;
    }

    NMO_RETURN_OK();
}

void nmo_lua_push_object_query_name_modes(lua_State *state)
{
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_NAME_NONE);
    lua_setfield(state, -2, "none");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_NAME_EXACT);
    lua_setfield(state, -2, "exact");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_NAME_SUBSTRING);
    lua_setfield(state, -2, "substring");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_NAME_WILDCARD);
    lua_setfield(state, -2, "wildcard");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_NAME_REGEX);
    lua_setfield(state, -2, "regex");
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

static int nmo_lua_session_from_document(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_session_from_document(document);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Document has no backing session");
    }

    status = nmo_lua_push_borrowed_session_handle(state, session, document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push session handle");
    }
    return 1;
}

static int nmo_lua_session_from_workspace(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_session_from_workspace(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Workspace has no backing session");
    }

    status = nmo_lua_push_borrowed_session_handle(state, session, document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push session handle");
    }
    return 1;
}

static void nmo_lua_session_push_index_stats(lua_State *state,
                                             const nmo_index_stats_t *stats)
{
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)stats->total_objects);
    lua_setfield(state, -2, "total_objects");
    lua_pushinteger(state, (lua_Integer)stats->class_index_entries);
    lua_setfield(state, -2, "class_index_entries");
    lua_pushinteger(state, (lua_Integer)stats->name_index_entries);
    lua_setfield(state, -2, "name_index_entries");
    lua_pushinteger(state, (lua_Integer)stats->guid_index_entries);
    lua_setfield(state, -2, "guid_index_entries");
    lua_pushinteger(state, (lua_Integer)stats->memory_usage);
    lua_setfield(state, -2, "memory_usage");
}

static int nmo_lua_session_rebuild_indexes(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t flags = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_lua_check_optional_flags_arg(state, 2, 0u, &flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid index rebuild flags");
    }

    status = nmo_session_rebuild_indexes(session, flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to rebuild session indexes");
    }

    return 0;
}

static int nmo_lua_session_object_index_stats(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_index_stats_t stats = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_session_get_object_index_stats(session, &stats);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to get session index stats");
    }

    nmo_lua_session_push_index_stats(state, &stats);
    return 1;
}

static int nmo_lua_session_invalidate_object_query(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t flags = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_lua_check_optional_flags_arg(state, 2, NMO_OBJECT_QUERY_INDEX_ALL, &flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid query index flags");
    }

    nmo_session_invalidate_object_query(session, flags);
    return 0;
}

static int nmo_lua_session_add_included_file(lua_State *state)
{
    nmo_session_t *session = NULL;
    const char *name = NULL;
    size_t data_size = 0u;
    const char *data = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *owner_ids = NULL;
    size_t owner_count = 0u;
    nmo_included_file_metadata_t meta = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    name = luaL_checkstring(state, 2);
    data = luaL_checklstring(state, 3, &data_size);
    if (data_size > UINT32_MAX) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_ARGUMENT,
                                        "Included file payload is too large");
    }

    if (!lua_isnoneornil(state, 4)) {
        int owner_index = lua_absindex(state, 4);
        if (!lua_istable(state, owner_index)) {
            return luaL_error(state, "included file owners must be a table");
        }

        owner_count = (size_t)lua_rawlen(state, owner_index);
        if (owner_count > 0u) {
            arena = nmo_arena_create(NULL, 0);
            if (arena == NULL) {
                return nmo_lua_raise_last_error(state,
                                                NMO_ERR_NOMEM,
                                                "Failed to allocate included file owner arena");
            }

            status = nmo_lua_collect_object_id_array(state,
                                                     4,
                                                     session,
                                                     arena,
                                                     &owner_ids,
                                                     &owner_count);
            if (status != NMO_OK) {
                nmo_arena_destroy(arena);
                return nmo_lua_raise_last_error(state,
                                                status,
                                                "Failed to collect included file owners");
            }
            meta.owner_ids = owner_ids;
            meta.owner_count = (uint32_t)owner_count;
        }
    }

    status = nmo_session_add_included_file_ex(session,
                                              name,
                                              data,
                                              (uint32_t)data_size,
                                              owner_count > 0u ? &meta : NULL);
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add included file");
    }

    return 0;
}

static uint32_t nmo_lua_session_check_included_file_index(lua_State *state,
                                                          int index)
{
    lua_Integer lua_index = luaL_checkinteger(state, index);
    if (lua_index < 1) {
        luaL_error(state, "included file index must be 1-based");
        return 0u;
    }
    if ((uint64_t)lua_index > (uint64_t)UINT32_MAX) {
        luaL_error(state, "included file index is too large");
        return 0u;
    }
    return (uint32_t)(lua_index - 1);
}

static int nmo_lua_session_replace_included_file(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t file_index = 0u;
    size_t data_size = 0u;
    const char *data = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    file_index = nmo_lua_session_check_included_file_index(state, 2);
    data = luaL_checklstring(state, 3, &data_size);
    if (data_size > UINT32_MAX) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_ARGUMENT,
                                        "Included file payload is too large");
    }

    status = nmo_session_replace_included_file(session,
                                               file_index,
                                               data,
                                               (uint32_t)data_size);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to replace included file");
    }

    return 0;
}

static int nmo_lua_session_set_included_file_owners(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t file_index = 0u;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *owner_ids = NULL;
    size_t owner_count = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    file_index = nmo_lua_session_check_included_file_index(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        int owner_index = lua_absindex(state, 3);
        if (!lua_istable(state, owner_index)) {
            return luaL_error(state, "included file owners must be a table");
        }
        owner_count = (size_t)lua_rawlen(state, owner_index);
        if (owner_count > 0u) {
            arena = nmo_arena_create(NULL, 0);
            if (arena == NULL) {
                return nmo_lua_raise_last_error(state,
                                                NMO_ERR_NOMEM,
                                                "Failed to allocate included file owner arena");
            }

            status = nmo_lua_collect_object_id_array(state,
                                                     3,
                                                     session,
                                                     arena,
                                                     &owner_ids,
                                                     &owner_count);
            if (status != NMO_OK) {
                nmo_arena_destroy(arena);
                return nmo_lua_raise_last_error(state,
                                                status,
                                                "Failed to collect included file owners");
            }
        }
    }

    status = nmo_session_set_included_file_owners(session,
                                                  file_index,
                                                  owner_ids,
                                                  (uint32_t)owner_count);
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to set included file owners");
    }

    return 0;
}

static int nmo_lua_session_remove_included_file(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t file_index = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    file_index = nmo_lua_session_check_included_file_index(state, 2);
    status = nmo_session_remove_included_file(session, file_index);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to remove included file");
    }

    return 0;
}

static void nmo_lua_session_push_guid_string(lua_State *state, nmo_guid_t guid)
{
    char guid_buffer[32];
    if (nmo_guid_is_null(guid)) {
        lua_pushnil(state);
        return;
    }

    if (nmo_guid_format(guid, guid_buffer, sizeof(guid_buffer)) <= 0) {
        lua_pushnil(state);
        return;
    }

    lua_pushstring(state, guid_buffer);
}

static void nmo_lua_session_push_runtime_load_stats(
    lua_State *state,
    const nmo_runtime_load_stats_t *stats)
{
    size_t i = 0u;
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)stats->total_objects);
    lua_setfield(state, -2, "total_objects");
    lua_pushinteger(state, (lua_Integer)stats->flags);
    lua_setfield(state, -2, "flags");

    lua_createtable(state, 0, 6);
    lua_pushinteger(state, (lua_Integer)stats->references.total);
    lua_setfield(state, -2, "total");
    lua_pushinteger(state, (lua_Integer)stats->references.resolved);
    lua_setfield(state, -2, "resolved");
    lua_pushinteger(state, (lua_Integer)stats->references.unresolved);
    lua_setfield(state, -2, "unresolved");
    lua_pushinteger(state, (lua_Integer)stats->references.ambiguous);
    lua_setfield(state, -2, "ambiguous");
    lua_pushinteger(state, (lua_Integer)stats->references.unresolved_preview_count);
    lua_setfield(state, -2, "unresolved_preview_count");
    lua_createtable(state, (int)stats->references.unresolved_preview_count, 0);
    for (i = 0u; i < stats->references.unresolved_preview_count &&
                 i < (sizeof(stats->references.unresolved_preview) /
                       sizeof(stats->references.unresolved_preview[0]));
         ++i) {
        lua_createtable(state, 0, 2);
        lua_pushinteger(state,
                        (lua_Integer)stats->references.unresolved_preview[i].id);
        lua_setfield(state, -2, "id");
        lua_pushinteger(
            state,
            (lua_Integer)stats->references.unresolved_preview[i].class_id);
        lua_setfield(state, -2, "class_id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "unresolved_preview");
    lua_setfield(state, -2, "references");

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)stats->indexes.class_entries);
    lua_setfield(state, -2, "class_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.name_entries);
    lua_setfield(state, -2, "name_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.guid_entries);
    lua_setfield(state, -2, "guid_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.memory_usage);
    lua_setfield(state, -2, "memory_usage");
    lua_setfield(state, -2, "indexes");

    lua_createtable(state, 0, 2);
    lua_pushinteger(state, (lua_Integer)stats->object_postload.invoked);
    lua_setfield(state, -2, "invoked");
    lua_pushinteger(state, (lua_Integer)stats->object_postload.errors);
    lua_setfield(state, -2, "errors");
    lua_setfield(state, -2, "object_postload");

    lua_pushinteger(state, (lua_Integer)stats->manager_errors);
    lua_setfield(state, -2, "manager_errors");
}

static void nmo_lua_session_push_plugin_diagnostics(
    lua_State *state,
    const nmo_session_plugin_diagnostics_t *diag)
{
    size_t i = 0u;
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)diag->entry_count);
    lua_setfield(state, -2, "entry_count");
    lua_pushinteger(state, (lua_Integer)diag->missing_count);
    lua_setfield(state, -2, "missing_count");
    lua_pushinteger(state, (lua_Integer)diag->outdated_count);
    lua_setfield(state, -2, "outdated_count");
    lua_pushboolean(state, diag->extension_registry_available ? 1 : 0);
    lua_setfield(state, -2, "extension_registry_available");

    lua_createtable(state, (int)diag->entry_count, 0);
    for (i = 0u; i < diag->entry_count; ++i) {
        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        lua_createtable(state, 0, 7);
        nmo_lua_session_push_guid_string(state, entry->guid);
        lua_setfield(state, -2, "guid");
        lua_pushinteger(state, (lua_Integer)entry->category);
        lua_setfield(state, -2, "category");
        lua_pushinteger(state, (lua_Integer)entry->required_version);
        lua_setfield(state, -2, "required_version");
        lua_pushinteger(state, (lua_Integer)entry->resolved_version);
        lua_setfield(state, -2, "resolved_version");
        if (entry->resolved_name != NULL) {
            lua_pushstring(state, entry->resolved_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "resolved_name");
        lua_pushinteger(state, (lua_Integer)entry->status_flags);
        lua_setfield(state, -2, "status_flags");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "entries");
}

static void nmo_lua_session_push_behavior_interface_diagnostics(
    lua_State *state,
    const nmo_session_behavior_interface_diagnostics_t *diag)
{
    lua_createtable(state, 0, 12);
    lua_pushboolean(state, diag->attempted ? 1 : 0);
    lua_setfield(state, -2, "attempted");
    lua_pushboolean(state, diag->available ? 1 : 0);
    lua_setfield(state, -2, "available");
    lua_pushinteger(state, (lua_Integer)diag->status);
    lua_setfield(state, -2, "status");
    lua_pushinteger(state, (lua_Integer)diag->attempted_count);
    lua_setfield(state, -2, "attempted_count");
    lua_pushinteger(state, (lua_Integer)diag->parsed_count);
    lua_setfield(state, -2, "parsed_count");
    lua_pushinteger(state, (lua_Integer)diag->failed_count);
    lua_setfield(state, -2, "failed_count");
    lua_pushinteger(state, (lua_Integer)diag->skipped_no_arena_count);
    lua_setfield(state, -2, "skipped_no_arena_count");
    lua_pushinteger(state, (lua_Integer)diag->allocation_failure_count);
    lua_setfield(state, -2, "allocation_failure_count");
    lua_pushinteger(state, (lua_Integer)diag->first_error_object_id);
    lua_setfield(state, -2, "first_error_object_id");
    lua_pushinteger(state, (lua_Integer)diag->first_error_file_id);
    lua_setfield(state, -2, "first_error_file_id");
    lua_pushinteger(state, (lua_Integer)diag->first_error_chunk_version);
    lua_setfield(state, -2, "first_error_chunk_version");
    lua_pushinteger(state, (lua_Integer)diag->first_error_data_version);
    lua_setfield(state, -2, "first_error_data_version");
    lua_pushinteger(state, (lua_Integer)diag->first_error_reader_offset);
    lua_setfield(state, -2, "first_error_reader_offset");
    lua_pushinteger(state, (lua_Integer)diag->first_error_chunk_dwords);
    lua_setfield(state, -2, "first_error_chunk_dwords");
}

static void nmo_lua_session_push_included_files(lua_State *state,
                                                const nmo_included_file_t *files,
                                                size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        const nmo_included_file_t *entry = &files[i];
        const nmo_object_id_t *owner_ids =
            (const nmo_object_id_t *)entry->owner_ids.data;
        size_t owner_count = entry->owner_ids.count;
        size_t owner_index = 0u;
        lua_createtable(state, 0, 5);
        if (entry->name != NULL) {
            lua_pushstring(state, entry->name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)entry->size);
        lua_setfield(state, -2, "size");
        lua_pushinteger(state, (lua_Integer)entry->attributes);
        lua_setfield(state, -2, "attributes");
        if (entry->data != NULL && entry->size > 0u) {
            lua_pushlstring(state, (const char *)entry->data, entry->size);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "data");
        lua_createtable(state, (int)owner_count, 0);
        for (owner_index = 0u; owner_index < owner_count; ++owner_index) {
            lua_pushinteger(state, (lua_Integer)owner_ids[owner_index]);
            lua_rawseti(state, -2, (lua_Integer)owner_index + 1);
        }
        lua_setfield(state, -2, "owner_ids");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static int nmo_lua_session_is_partial_load(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    lua_pushboolean(state, nmo_session_is_partial_load(session) ? 1 : 0);
    return 1;
}

static int nmo_lua_session_has_materialized_load_state(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    lua_pushboolean(state,
                    nmo_session_has_materialized_load_state(session) ? 1 : 0);
    return 1;
}

static int nmo_lua_session_runtime_load_stats(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_runtime_load_stats_t stats = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_session_get_runtime_load_stats(session, &stats);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to get runtime load stats");
    }

    nmo_lua_session_push_runtime_load_stats(state, &stats);
    return 1;
}

static int nmo_lua_session_plugin_diagnostics(lua_State *state)
{
    nmo_session_t *session = NULL;
    const nmo_session_plugin_diagnostics_t *diag = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    diag = nmo_session_get_plugin_diagnostics(session);
    if (diag == NULL) {
        lua_pushnil(state);
        return 1;
    }

    nmo_lua_session_push_plugin_diagnostics(state, diag);
    return 1;
}

static int nmo_lua_session_behavior_interface_diagnostics(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_session_behavior_interface_diagnostics_t diag = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_session_get_behavior_interface_diagnostics(session, &diag);
    nmo_lua_session_push_behavior_interface_diagnostics(state, &diag);
    return 1;
}

static int nmo_lua_session_included_files(lua_State *state)
{
    nmo_session_t *session = NULL;
    uint32_t count = 0u;
    nmo_included_file_t *files = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    files = nmo_session_get_included_files(session, &count);
    if (files == NULL || count == 0u) {
        lua_createtable(state, 0, 0);
        return 1;
    }

    nmo_lua_session_push_included_files(state, files, count);
    return 1;
}

static int nmo_lua_open_session_module(lua_State *state)
{
    lua_createtable(state, 0, 15);

    lua_pushcfunction(state, nmo_lua_session_create);
    lua_setfield(state, -2, "create");

    lua_pushcfunction(state, nmo_lua_session_from_document);
    lua_setfield(state, -2, "from_document");

    lua_pushcfunction(state, nmo_lua_session_from_workspace);
    lua_setfield(state, -2, "from_workspace");

    lua_pushcfunction(state, nmo_lua_session_add_included_file);
    lua_setfield(state, -2, "add_included_file");

    lua_pushcfunction(state, nmo_lua_session_replace_included_file);
    lua_setfield(state, -2, "replace_included_file");

    lua_pushcfunction(state, nmo_lua_session_set_included_file_owners);
    lua_setfield(state, -2, "set_included_file_owners");

    lua_pushcfunction(state, nmo_lua_session_remove_included_file);
    lua_setfield(state, -2, "remove_included_file");

    lua_pushcfunction(state, nmo_lua_session_rebuild_indexes);
    lua_setfield(state, -2, "rebuild_indexes");

    lua_pushcfunction(state, nmo_lua_session_object_index_stats);
    lua_setfield(state, -2, "object_index_stats");

    lua_pushcfunction(state, nmo_lua_session_invalidate_object_query);
    lua_setfield(state, -2, "invalidate_object_query");

    lua_pushcfunction(state, nmo_lua_session_is_partial_load);
    lua_setfield(state, -2, "is_partial_load");

    lua_pushcfunction(state, nmo_lua_session_has_materialized_load_state);
    lua_setfield(state, -2, "has_materialized_load_state");

    lua_pushcfunction(state, nmo_lua_session_runtime_load_stats);
    lua_setfield(state, -2, "runtime_load_stats");

    lua_pushcfunction(state, nmo_lua_session_plugin_diagnostics);
    lua_setfield(state, -2, "plugin_diagnostics");

    lua_pushcfunction(state, nmo_lua_session_behavior_interface_diagnostics);
    lua_setfield(state, -2, "behavior_interface_diagnostics");

    lua_pushcfunction(state, nmo_lua_session_included_files);
    lua_setfield(state, -2, "included_files");

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)NMO_INDEX_BUILD_CLASS);
    lua_setfield(state, -2, "class");
    lua_pushinteger(state, (lua_Integer)NMO_INDEX_BUILD_NAME);
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, (lua_Integer)NMO_INDEX_BUILD_GUID);
    lua_setfield(state, -2, "guid");
    lua_pushinteger(state, (lua_Integer)NMO_INDEX_BUILD_ALL);
    lua_setfield(state, -2, "all");
    lua_setfield(state, -2, "index_build_flags");

    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_INDEX_MEMBERSHIP);
    lua_setfield(state, -2, "membership");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_INDEX_NAMES);
    lua_setfield(state, -2, "names");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_INDEX_TEXT);
    lua_setfield(state, -2, "text");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_INDEX_TYPE_GUID);
    lua_setfield(state, -2, "type_guid");
    lua_pushinteger(state, (lua_Integer)NMO_OBJECT_QUERY_INDEX_ALL);
    lua_setfield(state, -2, "all");
    lua_setfield(state, -2, "query_index_flags");

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

