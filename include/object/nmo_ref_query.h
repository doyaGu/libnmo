#ifndef NMO_REF_QUERY_H
#define NMO_REF_QUERY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * Stable reference-query facade over the session-owned reference graph cache.
 * Callers query counts/flags directly and do not hold nmo_ref_graph_t objects.
 */
#define NMO_REF_QUERY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_REF_QUERY_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef enum nmo_ref_query_direction {
    NMO_REF_QUERY_OUTGOING = 0,
    NMO_REF_QUERY_INCOMING = 1
} nmo_ref_query_direction_t;

typedef struct nmo_ref_query_edge {
    nmo_object_id_t from;
    nmo_object_id_t to;
    uint32_t kind;
    const char *field_path;
    uint32_t index;
} nmo_ref_query_edge_t;

typedef bool (*nmo_ref_query_edge_visitor_fn)(
    const nmo_ref_query_edge_t *edge,
    void *user_data);

/**
 * @brief Count all reference edges currently visible through the session cache.
 */
NMO_API nmo_status_t nmo_ref_query_count_total_edges(
    nmo_session_t *session,
    size_t *out_count);

/**
 * @brief Count incoming or outgoing edges for one object.
 */
NMO_API nmo_status_t nmo_ref_query_count_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    size_t *out_count);

/**
 * @brief Return whether an object currently has any incoming/outgoing edges.
 */
NMO_API nmo_status_t nmo_ref_query_has_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    bool *out_has_edges);

/**
 * @brief Count broken reference edges in the current session graph.
 */
NMO_API nmo_status_t nmo_ref_query_count_broken_edges(
    nmo_session_t *session,
    size_t *out_count);

/**
 * @brief Visit all reference edges currently visible through the session cache.
 *
 * This stable facade hides direct nmo_ref_graph_t ownership and graph-owned
 * edge-array lifetimes from consumers.
 */
NMO_API nmo_status_t nmo_ref_query_visit_all_edges(
    nmo_session_t *session,
    nmo_ref_query_edge_visitor_fn visitor,
    void *user_data,
    size_t *out_count);

/**
 * @brief Visit incoming or outgoing edges for one object.
 */
NMO_API nmo_status_t nmo_ref_query_visit_object_edges(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    nmo_ref_query_direction_t direction,
    nmo_ref_query_edge_visitor_fn visitor,
    void *user_data,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REF_QUERY_H */
