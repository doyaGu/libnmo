#include "lua_bindings_internal.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "core/nmo_guid.h"
#include "document/nmo_document.h"
#include "lua/nmo_lua_runtime.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_runtime_result.h"
#include "session/nmo_session_pipeline.h"

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
    if (!handle->finished && handle->tx != NULL && handle->plan == NULL) {
        nmo_script_edit_rollback(handle->tx);
    }
    if (handle->plan != NULL) {
        nmo_edit_plan_destroy(handle->plan);
    }
    if (handle->has_report) {
        nmo_edit_report_dispose(&handle->report);
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
                                                nmo_workspace_t *workspace,
                                                nmo_edit_plan_t *plan,
                                                nmo_lua_handle_scope_t *workspace_scope)
{
    if (state == NULL || workspace == NULL || plan == NULL ||
        workspace_scope == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script edit transaction handles require state, workspace, plan, and workspace scope");
    }

    nmo_lua_script_edit_tx_handle_data_t *data =
        (nmo_lua_script_edit_tx_handle_data_t *)calloc(1, sizeof(*data));
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua script edit handle");
    }
    data->workspace = workspace;
    data->plan = plan;
    data->validation_flags = nmo_edit_executor_options_default().validation_flags;

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
    if (handle == NULL || handle->finished ||
        (handle->tx == NULL && handle->workspace == NULL)) {
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
                nmo_document_internal_session(handle->document) != session) {
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
    static const nmo_lua_integer_entry_t entries[] = {
        { "none", (lua_Integer)NMO_OBJECT_QUERY_NAME_NONE },
        { "exact", (lua_Integer)NMO_OBJECT_QUERY_NAME_EXACT },
        { "substring", (lua_Integer)NMO_OBJECT_QUERY_NAME_SUBSTRING },
        { "wildcard", (lua_Integer)NMO_OBJECT_QUERY_NAME_WILDCARD },
        { "regex", (lua_Integer)NMO_OBJECT_QUERY_NAME_REGEX },
    };
    nmo_lua_push_integer_table(state, entries, sizeof(entries) / sizeof(entries[0]));
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

static void nmo_lua_session_push_index_stats(lua_State *state,
                                             const nmo_index_stats_t *stats)
{
    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(
        state, "total_objects", (lua_Integer)stats->total_objects);
    nmo_lua_set_integer_field(
        state, "class_index_entries", (lua_Integer)stats->class_index_entries);
    nmo_lua_set_integer_field(
        state, "name_index_entries", (lua_Integer)stats->name_index_entries);
    nmo_lua_set_integer_field(
        state, "guid_index_entries", (lua_Integer)stats->guid_index_entries);
    nmo_lua_set_integer_field(state, "memory_usage", (lua_Integer)stats->memory_usage);
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

static void nmo_lua_session_push_behavior_interface_diagnostics(
    lua_State *state,
    const nmo_session_behavior_interface_diagnostics_t *diag)
{
    lua_createtable(state, 0, 12);
    nmo_lua_set_boolean_field(state, "attempted", diag->attempted);
    nmo_lua_set_boolean_field(state, "available", diag->available);
    nmo_lua_set_integer_field(state, "status", (lua_Integer)diag->status);
    nmo_lua_set_integer_field(
        state, "attempted_count", (lua_Integer)diag->attempted_count);
    nmo_lua_set_integer_field(state, "parsed_count", (lua_Integer)diag->parsed_count);
    nmo_lua_set_integer_field(state, "failed_count", (lua_Integer)diag->failed_count);
    nmo_lua_set_integer_field(
        state, "skipped_no_arena_count", (lua_Integer)diag->skipped_no_arena_count);
    nmo_lua_set_integer_field(
        state, "allocation_failure_count",
        (lua_Integer)diag->allocation_failure_count);
    nmo_lua_set_integer_field(
        state, "first_error_object_id", (lua_Integer)diag->first_error_object_id);
    nmo_lua_set_integer_field(
        state, "first_error_file_id", (lua_Integer)diag->first_error_file_id);
    nmo_lua_set_integer_field(
        state,
        "first_error_chunk_version",
        (lua_Integer)diag->first_error_chunk_version);
    nmo_lua_set_integer_field(
        state,
        "first_error_data_version",
        (lua_Integer)diag->first_error_data_version);
    nmo_lua_set_integer_field(
        state,
        "first_error_reader_offset",
        (lua_Integer)diag->first_error_reader_offset);
    nmo_lua_set_integer_field(
        state, "first_error_chunk_dwords", (lua_Integer)diag->first_error_chunk_dwords);
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

static int nmo_lua_open_session_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "create", nmo_lua_session_create },
        { "rebuild_indexes", nmo_lua_session_rebuild_indexes },
        { "object_index_stats", nmo_lua_session_object_index_stats },
        { "invalidate_object_query", nmo_lua_session_invalidate_object_query },
        { "behavior_interface_diagnostics",
          nmo_lua_session_behavior_interface_diagnostics },
    };
    static const nmo_lua_integer_entry_t index_build_flags[] = {
        { "class", (lua_Integer)NMO_INDEX_BUILD_CLASS },
        { "name", (lua_Integer)NMO_INDEX_BUILD_NAME },
        { "guid", (lua_Integer)NMO_INDEX_BUILD_GUID },
        { "all", (lua_Integer)NMO_INDEX_BUILD_ALL },
    };
    static const nmo_lua_integer_entry_t query_index_flags[] = {
        { "membership", (lua_Integer)NMO_OBJECT_QUERY_INDEX_MEMBERSHIP },
        { "names", (lua_Integer)NMO_OBJECT_QUERY_INDEX_NAMES },
        { "text", (lua_Integer)NMO_OBJECT_QUERY_INDEX_TEXT },
        { "type_guid", (lua_Integer)NMO_OBJECT_QUERY_INDEX_TYPE_GUID },
        { "all", (lua_Integer)NMO_OBJECT_QUERY_INDEX_ALL },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)(function_count + 2u));
    nmo_lua_set_functions(state, functions, function_count);

    nmo_lua_push_integer_table(
        state, index_build_flags,
        sizeof(index_build_flags) / sizeof(index_build_flags[0]));
    lua_setfield(state, -2, "index_build_flags");

    nmo_lua_push_integer_table(
        state, query_index_flags,
        sizeof(query_index_flags) / sizeof(query_index_flags[0]));
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

