#include "lua_bindings_internal.h"

#include "core/nmo_arena.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_runtime_result.h"

#include "lauxlib.h"

static nmo_status_t nmo_lua_runtime_collect_integer_id_array(lua_State *state,
                                                             int index,
                                                             nmo_arena_t *arena,
                                                             nmo_object_id_t **out_ids,
                                                             size_t *out_count)
{
    if (state == NULL || arena == NULL || out_ids == NULL || out_count == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state, arena, and output ids must be non-null");
    }

    int table_index = lua_absindex(state, index);
    if (!lua_istable(state, table_index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected Lua table of object ids");
    }

    size_t count = (size_t)lua_rawlen(state, table_index);
    if (count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected at least one object id");
    }

    nmo_object_id_t *ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(*ids) * count, alignof(nmo_object_id_t));
    if (ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate preview-destroy id list");
    }

    for (size_t i = 0; i < count; ++i) {
        lua_geti(state, table_index, (lua_Integer)(i + 1));
        if (!lua_isinteger(state, -1)) {
            lua_pop(state, 1);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Preview-destroy ids must be integers");
        }
        ids[i] = (nmo_object_id_t)lua_tointeger(state, -1);
        lua_pop(state, 1);
    }

    *out_ids = ids;
    *out_count = count;
    NMO_RETURN_OK();
}

static int nmo_lua_runtime_push_id_table(lua_State *state,
                                         const nmo_object_id_t *ids,
                                         size_t count)
{
    lua_createtable(state, (int)count, 0);
    for (size_t i = 0; i < count; ++i) {
        lua_pushinteger(state, (lua_Integer)ids[i]);
        lua_seti(state, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static void nmo_lua_runtime_push_preview_result(lua_State *state,
                                                const nmo_preview_destroy_result_t *result)
{
    lua_createtable(state, 0, 2);
    nmo_lua_set_integer_field(state, "count", (lua_Integer)result->count);
    nmo_lua_runtime_push_id_table(state, result->ids, result->count);
    lua_setfield(state, -2, "ids");
}

static int nmo_lua_runtime_preview_destroy(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to allocate preview arena");
    }

    nmo_object_id_t *input_ids = NULL;
    size_t input_count = 0;
    uint32_t flags = 0u;
    status = nmo_lua_collect_object_id_array(state,
                                             2,
                                             session,
                                             arena,
                                             &input_ids,
                                             &input_count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        return nmo_lua_raise_last_error(state, status, "Invalid preview-destroy objects");
    }

    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;
    status = nmo_session_preview_destroy(session,
                                         input_ids,
                                         input_count,
                                         flags,
                                         arena,
                                         &expanded_ids,
                                         &expanded_count);
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        return nmo_lua_raise_last_error(state, status, "Preview destroy failed");
    }

    int result = nmo_lua_runtime_push_id_table(state, expanded_ids, expanded_count);
    nmo_arena_destroy(arena);
    return result;
}

static int nmo_lua_runtime_preview_destroy_ids(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to allocate preview arena");
    }

    nmo_object_id_t *input_ids = NULL;
    size_t input_count = 0;
    status = nmo_lua_runtime_collect_integer_id_array(state,
                                                      2,
                                                      arena,
                                                      &input_ids,
                                                      &input_count);
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        return nmo_lua_raise_last_error(state, status, "Invalid preview-destroy ids");
    }

    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;
    status = nmo_session_preview_destroy(session,
                                         input_ids,
                                         input_count,
                                         0u,
                                         arena,
                                         &expanded_ids,
                                         &expanded_count);
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        return nmo_lua_raise_last_error(state, status, "Preview destroy failed");
    }

    int result = nmo_lua_runtime_push_id_table(state, expanded_ids, expanded_count);
    nmo_arena_destroy(arena);
    return result;
}

static int nmo_lua_runtime_preview_destroy_info(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *input_ids = NULL;
    size_t input_count = 0u;
    uint32_t flags = 0u;
    nmo_preview_destroy_result_t result = {0};
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOMEM,
                                        "Failed to allocate preview arena");
    }

    status = nmo_lua_collect_object_id_array(state,
                                             2,
                                             session,
                                             arena,
                                             &input_ids,
                                             &input_count);
    if (status == NMO_OK) {
        status = nmo_lua_check_optional_flags_arg(state, 3, 0u, &flags);
    }
    if (status == NMO_OK) {
        status = nmo_session_preview_destroy_result(session,
                                                    input_ids,
                                                    input_count,
                                                    flags,
                                                    &result);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        nmo_preview_destroy_result_destroy(&result);
        return nmo_lua_raise_last_error(state, status, "Preview destroy failed");
    }

    nmo_lua_runtime_push_preview_result(state, &result);
    nmo_preview_destroy_result_destroy(&result);
    return 1;
}

static int nmo_lua_open_runtime_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "preview_destroy", nmo_lua_runtime_preview_destroy },
        { "preview_destroy_info", nmo_lua_runtime_preview_destroy_info },
        { "preview_destroy_ids", nmo_lua_runtime_preview_destroy_ids },
    };
    static const nmo_lua_integer_entry_t request_flags[] = {
        { "default", (lua_Integer)NMO_RUNTIME_REQUEST_DEFAULT },
        { "strict", (lua_Integer)NMO_RUNTIME_REQUEST_STRICT },
        { "cascade", (lua_Integer)NMO_RUNTIME_REQUEST_CASCADE },
        { "safe_detach", (lua_Integer)NMO_RUNTIME_REQUEST_SAFE_DETACH },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)(function_count + 1u));
    nmo_lua_set_functions(state, functions, function_count);

    nmo_lua_push_integer_table(
        state, request_flags, sizeof(request_flags) / sizeof(request_flags[0]));
    lua_setfield(state, -2, "request_flags");

    return 1;
}

nmo_status_t nmo_lua_register_runtime_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.runtime",
        .open_fn = nmo_lua_open_runtime_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
