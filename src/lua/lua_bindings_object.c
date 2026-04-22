#include "lua_bindings_internal.h"

#include "object/nmo_object_iter.h"
#include "object/nmo_ref_query.h"

#include "lauxlib.h"

typedef struct nmo_lua_edge_collect_ctx {
    lua_State *state;
    int table_index;
    size_t next_index;
} nmo_lua_edge_collect_ctx_t;

static void nmo_lua_push_edge(lua_State *state, const nmo_ref_query_edge_t *edge)
{
    lua_createtable(state, 0, 5);

    lua_pushinteger(state, (lua_Integer)edge->from);
    lua_setfield(state, -2, "from");
    lua_pushinteger(state, (lua_Integer)edge->to);
    lua_setfield(state, -2, "to");
    lua_pushinteger(state, (lua_Integer)edge->kind);
    lua_setfield(state, -2, "kind");
    if (edge->field_path != NULL) {
        lua_pushstring(state, edge->field_path);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "field_path");
    lua_pushinteger(state, (lua_Integer)edge->index);
    lua_setfield(state, -2, "index");
}

static bool nmo_lua_collect_edge(const nmo_ref_query_edge_t *edge, void *user_data)
{
    nmo_lua_edge_collect_ctx_t *ctx = (nmo_lua_edge_collect_ctx_t *)user_data;
    lua_State *state = ctx->state;

    nmo_lua_push_edge(state, edge);
    lua_seti(state, ctx->table_index, (lua_Integer)ctx->next_index);
    ctx->next_index += 1u;
    return true;
}

static int nmo_lua_object_id(lua_State *state)
{
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, NULL, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    lua_pushinteger(state, (lua_Integer)nmo_object_get_id(object));
    return 1;
}

static int nmo_lua_object_name(lua_State *state)
{
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, NULL, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    const char *name = nmo_object_get_name(object);
    if (name == NULL) {
        lua_pushnil(state);
    } else {
        lua_pushstring(state, name);
    }
    return 1;
}

static int nmo_lua_object_class_id(lua_State *state)
{
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, NULL, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    lua_pushinteger(state, (lua_Integer)nmo_object_get_class_id(object));
    return 1;
}

static int nmo_lua_object_edge_count(lua_State *state,
                                     nmo_ref_query_direction_t direction)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    size_t count = 0;
    status = nmo_ref_query_count_object_edges(handle->session,
                                              nmo_object_get_id(object),
                                              direction,
                                              &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }

    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_object_outgoing_edge_count(lua_State *state)
{
    return nmo_lua_object_edge_count(state, NMO_REF_QUERY_OUTGOING);
}

static int nmo_lua_object_incoming_edge_count(lua_State *state)
{
    return nmo_lua_object_edge_count(state, NMO_REF_QUERY_INCOMING);
}

static int nmo_lua_object_count(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    repository = nmo_session_get_repository(session);
    lua_pushinteger(state, (lua_Integer)nmo_object_iter_count(repository));
    return 1;
}

static int nmo_lua_object_at(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_object_t *object = NULL;
    lua_Integer lua_index = 0;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, &session_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    lua_index = luaL_checkinteger(state, 2);
    if (lua_index < 1) {
        return luaL_error(state, "object index must be 1-based");
    }

    repository = nmo_session_get_repository(session);
    object = nmo_object_iter_at(repository, (size_t)(lua_index - 1));
    if (object == NULL) {
        lua_pushnil(state);
        return 1;
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

static int nmo_lua_object_count_class(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_class_id_t class_id = 0;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    repository = nmo_session_get_repository(session);
    lua_pushinteger(state,
                    (lua_Integer)nmo_object_iter_count_class(repository, class_id));
    return 1;
}

static int nmo_lua_object_at_class(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_class_id_t class_id = 0;
    nmo_object_t *object = NULL;
    lua_Integer lua_index = 0;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, &session_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    lua_index = luaL_checkinteger(state, 3);
    if (lua_index < 1) {
        return luaL_error(state, "class object index must be 1-based");
    }

    repository = nmo_session_get_repository(session);
    object = nmo_object_iter_at_class(repository, class_id, (size_t)(lua_index - 1));
    if (object == NULL) {
        lua_pushnil(state);
        return 1;
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

static int nmo_lua_object_total_edge_count(lua_State *state)
{
    nmo_session_t *session = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_ref_query_count_total_edges(session, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }
    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_object_broken_edge_count(lua_State *state)
{
    nmo_session_t *session = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_ref_query_count_broken_edges(session, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }
    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_object_has_edges(lua_State *state,
                                    nmo_ref_query_direction_t direction)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    bool has_edges = false;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_ref_query_has_object_edges(handle->session,
                                            nmo_object_get_id(object),
                                            direction,
                                            &has_edges);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }

    lua_pushboolean(state, has_edges ? 1 : 0);
    return 1;
}

static int nmo_lua_object_has_outgoing_edges(lua_State *state)
{
    return nmo_lua_object_has_edges(state, NMO_REF_QUERY_OUTGOING);
}

static int nmo_lua_object_has_incoming_edges(lua_State *state)
{
    return nmo_lua_object_has_edges(state, NMO_REF_QUERY_INCOMING);
}

static int nmo_lua_object_collect_edge_list(lua_State *state,
                                            nmo_session_t *session,
                                            nmo_object_id_t object_id,
                                            nmo_ref_query_direction_t direction,
                                            bool all_edges)
{
    nmo_lua_edge_collect_ctx_t ctx = {
        .state = state,
        .table_index = 0,
        .next_index = 1u,
    };
    size_t count = 0u;
    nmo_status_t status = NMO_OK;

    lua_createtable(state, 0, 0);
    ctx.table_index = lua_absindex(state, -1);
    if (all_edges) {
        status = nmo_ref_query_visit_all_edges(session,
                                               nmo_lua_collect_edge,
                                               &ctx,
                                               &count);
    } else {
        status = nmo_ref_query_visit_object_edges(session,
                                                  object_id,
                                                  direction,
                                                  nmo_lua_collect_edge,
                                                  &ctx,
                                                  &count);
    }
    if (status != NMO_OK) {
        lua_pop(state, 1);
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }
    return 1;
}

static int nmo_lua_object_outgoing_edges(lua_State *state)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    return nmo_lua_object_collect_edge_list(state,
                                            handle->session,
                                            nmo_object_get_id(object),
                                            NMO_REF_QUERY_OUTGOING,
                                            false);
}

static int nmo_lua_object_incoming_edges(lua_State *state)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    return nmo_lua_object_collect_edge_list(state,
                                            handle->session,
                                            nmo_object_get_id(object),
                                            NMO_REF_QUERY_INCOMING,
                                            false);
}

static int nmo_lua_object_all_edges(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    return nmo_lua_object_collect_edge_list(state,
                                            session,
                                            0u,
                                            NMO_REF_QUERY_OUTGOING,
                                            true);
}

static int nmo_lua_open_object_module(lua_State *state)
{
    lua_createtable(state, 0, 15);

    lua_pushcfunction(state, nmo_lua_object_id);
    lua_setfield(state, -2, "id");

    lua_pushcfunction(state, nmo_lua_object_name);
    lua_setfield(state, -2, "name");

    lua_pushcfunction(state, nmo_lua_object_class_id);
    lua_setfield(state, -2, "class_id");

    lua_pushcfunction(state, nmo_lua_object_count);
    lua_setfield(state, -2, "count");

    lua_pushcfunction(state, nmo_lua_object_at);
    lua_setfield(state, -2, "at");

    lua_pushcfunction(state, nmo_lua_object_count_class);
    lua_setfield(state, -2, "count_class");

    lua_pushcfunction(state, nmo_lua_object_at_class);
    lua_setfield(state, -2, "at_class");

    lua_pushcfunction(state, nmo_lua_object_total_edge_count);
    lua_setfield(state, -2, "total_edge_count");

    lua_pushcfunction(state, nmo_lua_object_broken_edge_count);
    lua_setfield(state, -2, "broken_edge_count");

    lua_pushcfunction(state, nmo_lua_object_outgoing_edge_count);
    lua_setfield(state, -2, "outgoing_edge_count");

    lua_pushcfunction(state, nmo_lua_object_incoming_edge_count);
    lua_setfield(state, -2, "incoming_edge_count");

    lua_pushcfunction(state, nmo_lua_object_has_outgoing_edges);
    lua_setfield(state, -2, "has_outgoing_edges");

    lua_pushcfunction(state, nmo_lua_object_has_incoming_edges);
    lua_setfield(state, -2, "has_incoming_edges");

    lua_pushcfunction(state, nmo_lua_object_outgoing_edges);
    lua_setfield(state, -2, "outgoing_edges");

    lua_pushcfunction(state, nmo_lua_object_incoming_edges);
    lua_setfield(state, -2, "incoming_edges");

    lua_pushcfunction(state, nmo_lua_object_all_edges);
    lua_setfield(state, -2, "all_edges");

    return 1;
}

nmo_status_t nmo_lua_register_object_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.object",
        .open_fn = nmo_lua_open_object_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
