#include "object/nmo_object_refs.h"

#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "session/nmo_session_bridge.h"
#include "session/nmo_session.h"

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

    nmo_session_t *session = nmo_session_from_document(document);
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    nmo_object_repository_t *repository = nmo_document_get_repository(document);
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

