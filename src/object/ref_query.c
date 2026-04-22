#include "object/nmo_object_refs.h"
#include "object/nmo_ref_query.h"

#include "object/nmo_object_repository.h"
#include "session/nmo_session.h"
#include "object/nmo_ref_graph.h"

static nmo_ref_direction_t nmo_ref_query_to_graph_direction(
    nmo_ref_query_direction_t direction)
{
    return direction == NMO_REF_QUERY_INCOMING ? NMO_REF_DIR_INCOMING : NMO_REF_DIR_OUTGOING;
}

static nmo_status_t nmo_ref_query_visit_edges(
    const nmo_ref_edge_t *edges,
    size_t count,
    nmo_ref_query_edge_visitor_fn visitor,
    void *user_data,
    size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = count;
    }

    if (visitor == NULL) {
        return NMO_OK;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_ref_query_edge_t view = {
            .from = edges[i].from,
            .to = edges[i].to,
            .kind = (uint32_t)edges[i].kind,
            .field_path = edges[i].field_path,
            .index = edges[i].index
        };
        if (!visitor(&view, user_data)) {
            break;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_ref_query_count_total_edges(
    nmo_session_t *session,
    size_t *out_count)
{
    if (session == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_ref_graph_stats_t stats = {0};
    nmo_status_t rc = nmo_ref_graph_get_stats(graph, &stats);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_count = stats.total_edges;
    return NMO_OK;
}

nmo_status_t nmo_ref_query_count_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    size_t *out_count)
{
    if (session == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t count = 0;
    nmo_status_t rc = nmo_ref_graph_get_object_edges(
        graph,
        object_id,
        nmo_ref_query_to_graph_direction(direction),
        &edges,
        &count);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_count = count;
    return NMO_OK;
}

nmo_status_t nmo_ref_query_has_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    bool *out_has_edges)
{
    if (session == NULL || out_has_edges == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = 0;
    nmo_status_t rc = nmo_ref_query_count_object_edges(session, object_id, direction, &count);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_has_edges = count != 0;
    return NMO_OK;
}

nmo_status_t nmo_ref_query_count_broken_edges(
    nmo_session_t *session,
    size_t *out_count)
{
    if (session == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_status_t rc = nmo_ref_graph_validate(graph, &broken_edges, &broken_count);
    if (rc != NMO_OK && rc != NMO_ERR_VALIDATION_FAILED) {
        return rc;
    }

    (void)broken_edges;
    *out_count = broken_count;
    return NMO_OK;
}

nmo_status_t nmo_ref_query_visit_all_edges(
    nmo_session_t *session,
    nmo_ref_query_edge_visitor_fn visitor,
    void *user_data,
    size_t *out_count)
{
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t count = 0;
    nmo_status_t rc = nmo_ref_graph_get_edges(graph, &edges, &count);
    if (rc != NMO_OK) {
        return rc;
    }

    return nmo_ref_query_visit_edges(edges, count, visitor, user_data, out_count);
}

nmo_status_t nmo_ref_query_visit_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    nmo_ref_query_edge_visitor_fn visitor,
    void *user_data,
    size_t *out_count)
{
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t count = 0;
    nmo_status_t rc = nmo_ref_graph_get_object_edges(
        graph,
        object_id,
        nmo_ref_query_to_graph_direction(direction),
        &edges,
        &count);
    if (rc != NMO_OK) {
        return rc;
    }

    return nmo_ref_query_visit_edges(edges, count, visitor, user_data, out_count);
}

nmo_status_t nmo_object_refs_iterate(
    nmo_document_t *document,
    nmo_object_id_t object_id,
    nmo_object_refs_direction_t direction,
    nmo_object_refs_edge_visitor_fn visitor,
    void *user_data,
    nmo_object_refs_result_t *out_result)
{
    if (document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_session_t *session = (nmo_session_t *)document;
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    nmo_object_repository_t *repository = nmo_session_get_repository(session);
    if (graph == NULL || repository == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_refs_result_t result = {0};

    if ((direction & NMO_OBJECT_REFS_OUTGOING) != 0u) {
        nmo_ref_edge_t *edges = NULL;
        size_t count = 0;
        nmo_status_t rc = nmo_ref_graph_get_object_edges(
            graph,
            object_id,
            NMO_REF_DIR_OUTGOING,
            &edges,
            &count);
        if (rc != NMO_OK) {
            return rc;
        }
        result.outgoing = count;
        for (size_t i = 0; i < count && visitor != NULL; ++i) {
            nmo_object_refs_edge_t edge = {
                .edge = &edges[i],
                .is_incoming = false,
                .peer = nmo_object_repository_find_by_id(repository, edges[i].to)
            };
            if (!visitor(&edge, user_data)) {
                break;
            }
        }
    }

    if ((direction & NMO_OBJECT_REFS_INCOMING) != 0u) {
        nmo_ref_edge_t *edges = NULL;
        size_t count = 0;
        nmo_status_t rc = nmo_ref_graph_get_object_edges(
            graph,
            object_id,
            NMO_REF_DIR_INCOMING,
            &edges,
            &count);
        if (rc != NMO_OK) {
            return rc;
        }
        result.incoming = count;
        for (size_t i = 0; i < count && visitor != NULL; ++i) {
            nmo_object_refs_edge_t edge = {
                .edge = &edges[i],
                .is_incoming = true,
                .peer = nmo_object_repository_find_by_id(repository, edges[i].from)
            };
            if (!visitor(&edge, user_data)) {
                break;
            }
        }
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    return NMO_OK;
}
