#include "lua_bindings_internal.h"

#include "document/nmo_document.h"
#include "object/nmo_object_iter.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_ref_graph.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_pipeline.h"

#include "lauxlib.h"

#include <string.h>

typedef struct nmo_lua_edge_collect_ctx {
    lua_State *state;
    int table_index;
    size_t next_index;
} nmo_lua_edge_collect_ctx_t;

static void nmo_lua_push_ref_edge(lua_State *state, const nmo_ref_edge_t *edge)
{
    lua_createtable(state, 0, 5);

    nmo_lua_set_integer_field(state, "from", (lua_Integer)edge->from);
    nmo_lua_set_integer_field(state, "to", (lua_Integer)edge->to);
    nmo_lua_set_integer_field(state, "kind", (lua_Integer)edge->kind);
    nmo_lua_set_optional_string_field(state, "field_path", edge->field_path);
    nmo_lua_set_integer_field(state, "index", (lua_Integer)edge->index);
}

static bool nmo_lua_collect_ref_edge(const nmo_ref_edge_t *edge, void *user_data)
{
    nmo_lua_edge_collect_ctx_t *ctx = (nmo_lua_edge_collect_ctx_t *)user_data;
    lua_State *state = ctx->state;

    nmo_lua_push_ref_edge(state, edge);
    lua_seti(state, ctx->table_index, (lua_Integer)ctx->next_index);
    ctx->next_index += 1u;
    return true;
}

static bool nmo_lua_collect_object_refs_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    return edge != NULL && edge->edge != NULL
        ? nmo_lua_collect_ref_edge(edge->edge, user_data)
        : true;
}

static nmo_status_t nmo_lua_object_make_query_context(
    nmo_document_t *document,
    nmo_object_query_context_t *out_query_ctx)
{
    nmo_context_t *context = NULL;

    if (document == NULL || out_query_ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_query_ctx, 0, sizeof(*out_query_ctx));
    context = nmo_document_get_context(document);
    out_query_ctx->repository = nmo_document_get_repository(document);
    out_query_ctx->registry =
        context != NULL ? nmo_context_get_type_registry(context) : NULL;
    return NMO_OK;
}

static nmo_status_t nmo_lua_push_query_objects(lua_State *state,
                                               nmo_document_t *document,
                                               nmo_lua_handle_scope_t *document_scope,
                                               nmo_object_t *const *objects,
                                               size_t count)
{
    size_t i = 0u;

    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        nmo_status_t status = nmo_lua_push_object_handle(
            state,
            document,
            document_scope,
            nmo_object_get_id(objects[i]));
        if (status != NMO_OK) {
            lua_pop(state, 1);
            return status;
        }
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }

    return NMO_OK;
}

static void nmo_lua_push_copy_result(lua_State *state,
                                     const nmo_runtime_report_t *result)
{
    lua_createtable(state, 0, 4);
    nmo_lua_set_integer_field(
        state, "copied_count", (lua_Integer)result->copied_objects);
    nmo_lua_set_integer_field(
        state, "affected_count", (lua_Integer)result->affected_objects);
    nmo_lua_set_integer_field(
        state,
        "manager_event_errors",
        (lua_Integer)result->manager_event_errors);
    nmo_lua_set_integer_field(
        state, "object_hook_errors", (lua_Integer)result->object_hook_errors);
}

static void nmo_lua_push_destroy_result(lua_State *state,
                                        const nmo_runtime_report_t *result)
{
    lua_createtable(state, 0, 4);
    nmo_lua_set_integer_field(
        state, "deleted_count", (lua_Integer)result->deleted_objects);
    nmo_lua_set_integer_field(
        state, "affected_count", (lua_Integer)result->affected_objects);
    nmo_lua_set_integer_field(
        state,
        "manager_event_errors",
        (lua_Integer)result->manager_event_errors);
    nmo_lua_set_integer_field(
        state, "object_hook_errors", (lua_Integer)result->object_hook_errors);
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

    if (nmo_object_get_name(object) != NULL) {
        lua_pushstring(state, nmo_object_get_name(object));
    } else {
        lua_pushnil(state);
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

static int nmo_lua_object_matches(lua_State *state)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_object_query_t query = {0};
    const nmo_type_registry_t *registry = NULL;
    bool matches = false;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_lua_parse_object_query(state, 2, &query);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object query");
    }

    if (handle != NULL && handle->document != NULL) {
        nmo_context_t *context = nmo_document_get_context(handle->document);
        if (context != NULL) {
            registry = nmo_context_get_type_registry(context);
        }
    }

    status = nmo_object_query_matches(object, &query, registry, &matches);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to match object query");
    }

    lua_pushboolean(state, matches ? 1 : 0);
    return 1;
}

static int nmo_lua_object_edge_count(lua_State *state,
                                     nmo_object_refs_direction_t direction)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_object_refs_result_t result = {0};
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_object_refs_iterate(handle->document,
                                     nmo_object_get_id(object),
                                     direction,
                                     NULL,
                                     NULL,
                                     &result);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }

    lua_pushinteger(
        state,
        (lua_Integer)(((direction & NMO_OBJECT_REFS_INCOMING) != 0u)
            ? result.incoming
            : result.outgoing));
    return 1;
}

static int nmo_lua_object_outgoing_edge_count(lua_State *state)
{
    return nmo_lua_object_edge_count(state, NMO_OBJECT_REFS_OUTGOING);
}

static int nmo_lua_object_incoming_edge_count(lua_State *state)
{
    return nmo_lua_object_edge_count(state, NMO_OBJECT_REFS_INCOMING);
}

static int nmo_lua_object_count(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    repository = nmo_document_get_repository(document);
    lua_pushinteger(state, (lua_Integer)nmo_object_iter_count(repository));
    return 1;
}

static int nmo_lua_object_at(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_object_t *object = NULL;
    lua_Integer lua_index = 0;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    lua_index = luaL_checkinteger(state, 2);
    if (lua_index < 1) {
        return luaL_error(state, "object index must be 1-based");
    }

    repository = nmo_document_get_repository(document);
    object = nmo_object_iter_at(repository, (size_t)(lua_index - 1));
    if (object == NULL) {
        lua_pushnil(state);
        return 1;
    }

    status = nmo_lua_push_object_handle(state,
                                        document,
                                        document_scope,
                                        nmo_object_get_id(object));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_object_count_class(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_class_id_t class_id = 0;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    repository = nmo_document_get_repository(document);
    lua_pushinteger(state,
                    (lua_Integer)nmo_object_iter_count_class(repository, class_id));
    return 1;
}

static int nmo_lua_object_at_class(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_class_id_t class_id = 0;
    nmo_object_t *object = NULL;
    lua_Integer lua_index = 0;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    lua_index = luaL_checkinteger(state, 3);
    if (lua_index < 1) {
        return luaL_error(state, "class object index must be 1-based");
    }

    repository = nmo_document_get_repository(document);
    object = nmo_object_iter_at_class(repository, class_id, (size_t)(lua_index - 1));
    if (object == NULL) {
        lua_pushnil(state);
        return 1;
    }

    status = nmo_lua_push_object_handle(state,
                                        document,
                                        document_scope,
                                        nmo_object_get_id(object));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_object_query_first(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_query_t query = {0};
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_lua_parse_object_query(state, 2, &query);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object query");
    }

    status = nmo_object_query_find_first(document, &query, &object, NULL);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK || object == NULL) {
        return nmo_lua_raise_last_error(state, status, "Failed to query document objects");
    }

    status = nmo_lua_push_object_handle(
        state, document, document_scope, nmo_object_get_id(object));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_object_query_collect(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_query_context_t query_ctx = {0};
    nmo_object_query_t query = {0};
    nmo_arena_t *arena = NULL;
    nmo_object_t **objects = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_lua_parse_object_query(state, 2, &query);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object query");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate query arena");
    }

    status = nmo_lua_object_make_query_context(document, &query_ctx);
    if (status == NMO_OK) {
        status = nmo_object_query_collect(
            &query_ctx, &query, arena, &objects, &count, NULL);
    }
    if (status == NMO_OK) {
        status = nmo_lua_push_query_objects(
            state, document, document_scope, objects, count);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to query document objects");
    }
    return 1;
}

static int nmo_lua_object_query_collect_info(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_query_context_t query_ctx = {0};
    nmo_object_query_t query = {0};
    nmo_arena_t *arena = NULL;
    nmo_object_t **objects = NULL;
    size_t count = 0u;
    nmo_object_query_result_t result = {0};
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_lua_parse_object_query(state, 2, &query);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object query");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate query arena");
    }

    status = nmo_lua_object_make_query_context(document, &query_ctx);
    if (status == NMO_OK) {
        status = nmo_object_query_collect(
            &query_ctx, &query, arena, &objects, &count, &result);
    }
    if (status == NMO_OK) {
        lua_createtable(state, 0, 5);
        status = nmo_lua_push_query_objects(
            state, document, document_scope, objects, count);
        if (status == NMO_OK) {
            lua_setfield(state, -2, "objects");
            nmo_lua_set_integer_field(state, "total", (lua_Integer)result.total);
            nmo_lua_set_integer_field(state, "matched", (lua_Integer)result.matched);
            nmo_lua_set_integer_field(state, "visited", (lua_Integer)result.visited);
            nmo_lua_set_boolean_field(state, "stopped_early", result.stopped_early);
        }
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to query document objects");
    }
    return 1;
}

static int nmo_lua_object_find_object_by_name(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_object_selector_t selector = {0};
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    selector.name = luaL_checkstring(state, 2);
    status = nmo_object_query_resolve_one(document, &selector, &object, NULL);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK || object == NULL) {
        return nmo_lua_raise_last_error(state, status, "Failed to find object by name");
    }

    status = nmo_lua_push_object_handle(
        state, document, document_scope, nmo_object_get_id(object));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_object_total_edge_count(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    nmo_ref_graph_t *graph = NULL;
    nmo_ref_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    graph = session != NULL ? nmo_session_get_ref_graph(session) : NULL;
    if (graph == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Reference graph is unavailable");
    }

    status = nmo_ref_graph_get_edges(graph, &edges, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }
    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_object_broken_edge_count(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    nmo_ref_graph_t *graph = NULL;
    nmo_ref_edge_t *broken_edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    graph = session != NULL ? nmo_session_get_ref_graph(session) : NULL;
    if (graph == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Reference graph is unavailable");
    }

    status = nmo_ref_graph_validate(graph, &broken_edges, &count);
    if (status == NMO_ERR_VALIDATION_FAILED) {
        status = NMO_OK;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }
    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_object_has_edges(lua_State *state,
                                    nmo_object_refs_direction_t direction)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_object_refs_result_t result = {0};
    bool has_edges = false;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_object_refs_iterate(handle->document,
                                     nmo_object_get_id(object),
                                     direction,
                                     NULL,
                                     NULL,
                                     &result);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Reference query failed");
    }

    has_edges = ((direction & NMO_OBJECT_REFS_OUTGOING) != 0u && result.outgoing > 0u) ||
                ((direction & NMO_OBJECT_REFS_INCOMING) != 0u && result.incoming > 0u);
    lua_pushboolean(state, has_edges ? 1 : 0);
    return 1;
}

static int nmo_lua_object_has_outgoing_edges(lua_State *state)
{
    return nmo_lua_object_has_edges(state, NMO_OBJECT_REFS_OUTGOING);
}

static int nmo_lua_object_has_incoming_edges(lua_State *state)
{
    return nmo_lua_object_has_edges(state, NMO_OBJECT_REFS_INCOMING);
}

static int nmo_lua_object_collect_edge_list(lua_State *state,
                                            nmo_document_t *document,
                                            nmo_object_id_t object_id,
                                            nmo_object_refs_direction_t direction,
                                            bool all_edges)
{
    nmo_lua_edge_collect_ctx_t ctx = {
        .state = state,
        .table_index = 0,
        .next_index = 1u,
    };
    nmo_session_t *session = NULL;
    nmo_ref_graph_t *graph = NULL;
    nmo_ref_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status = NMO_OK;
    size_t i = 0u;

    lua_createtable(state, 0, 0);
    ctx.table_index = lua_absindex(state, -1);
    if (all_edges) {
        session = nmo_document_internal_session(document);
        graph = session != NULL ? nmo_session_get_ref_graph(session) : NULL;
        if (graph == NULL) {
            status = NMO_ERR_INVALID_STATE;
        } else {
            status = nmo_ref_graph_get_edges(graph, &edges, &count);
            if (status == NMO_OK) {
                for (i = 0u; i < count; ++i) {
                    if (!nmo_lua_collect_ref_edge(&edges[i], &ctx)) {
                        break;
                    }
                }
            }
        }
    } else {
        nmo_object_refs_result_t result = {0};
        status = nmo_object_refs_iterate(document,
                                         object_id,
                                         direction,
                                         nmo_lua_collect_object_refs_edge,
                                         &ctx,
                                         &result);
        count = ((direction & NMO_OBJECT_REFS_INCOMING) != 0u)
            ? result.incoming
            : result.outgoing;
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
                                            handle->document,
                                            nmo_object_get_id(object),
                                            NMO_OBJECT_REFS_OUTGOING,
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
                                            handle->document,
                                            nmo_object_get_id(object),
                                            NMO_OBJECT_REFS_INCOMING,
                                            false);
}

static int nmo_lua_object_all_edges(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    return nmo_lua_object_collect_edge_list(state,
                                            document,
                                            0u,
                                            NMO_OBJECT_REFS_OUTGOING,
                                            true);
}

static int nmo_lua_object_create_object(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_lua_handle_scope_t *document_scope = NULL;
    nmo_session_t *session = NULL;
    nmo_class_id_t class_id = 0;
    const char *name = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    nmo_object_id_t created_id = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, &document_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_workspace_internal_session(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Workspace has no backing session");
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

    status = nmo_lua_push_object_handle(state,
                                        nmo_workspace_get_document(workspace),
                                        document_scope,
                                        created_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to push object handle");
    }
    return 1;
}

static int nmo_lua_object_copy_objects(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_workspace_internal_session(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Workspace has no backing session");
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

static int nmo_lua_object_copy_objects_info(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t result = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_workspace_internal_session(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Workspace has no backing session");
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
        status = nmo_session_copy_objects(session, ids, count, flags, &result);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to copy objects");
    }

    nmo_lua_push_copy_result(state, &result);
    return 1;
}

static int nmo_lua_object_destroy_objects(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t report = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_workspace_internal_session(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Workspace has no backing session");
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

static int nmo_lua_object_destroy_objects_info(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *ids = NULL;
    size_t count = 0u;
    uint32_t flags = 0u;
    nmo_runtime_report_t result = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    session = nmo_workspace_internal_session(workspace);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Workspace has no backing session");
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
        status = nmo_session_destroy_objects(session, ids, count, flags, &result);
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to destroy objects");
    }

    nmo_lua_push_destroy_result(state, &result);
    return 1;
}

static int nmo_lua_open_object_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "id", nmo_lua_object_id },
        { "name", nmo_lua_object_name },
        { "class_id", nmo_lua_object_class_id },
        { "matches", nmo_lua_object_matches },
        { "count", nmo_lua_object_count },
        { "at", nmo_lua_object_at },
        { "count_class", nmo_lua_object_count_class },
        { "at_class", nmo_lua_object_at_class },
        { "query_first", nmo_lua_object_query_first },
        { "query_collect", nmo_lua_object_query_collect },
        { "query_collect_info", nmo_lua_object_query_collect_info },
        { "find_object_by_name", nmo_lua_object_find_object_by_name },
        { "create_object", nmo_lua_object_create_object },
        { "copy_objects", nmo_lua_object_copy_objects },
        { "copy_objects_info", nmo_lua_object_copy_objects_info },
        { "destroy_objects", nmo_lua_object_destroy_objects },
        { "destroy_objects_info", nmo_lua_object_destroy_objects_info },
        { "total_edge_count", nmo_lua_object_total_edge_count },
        { "broken_edge_count", nmo_lua_object_broken_edge_count },
        { "outgoing_edge_count", nmo_lua_object_outgoing_edge_count },
        { "incoming_edge_count", nmo_lua_object_incoming_edge_count },
        { "has_outgoing_edges", nmo_lua_object_has_outgoing_edges },
        { "has_incoming_edges", nmo_lua_object_has_incoming_edges },
        { "outgoing_edges", nmo_lua_object_outgoing_edges },
        { "incoming_edges", nmo_lua_object_incoming_edges },
        { "all_edges", nmo_lua_object_all_edges },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)(function_count + 1u));
    nmo_lua_set_functions(state, functions, function_count);
    nmo_lua_push_object_query_name_modes(state);
    lua_setfield(state, -2, "query_name_modes");

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

