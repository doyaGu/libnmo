/**
 * @file nmo_behavior_graph.h
 * @brief Behavior graph builder for behavior/parameter IO relationships.
 */

#ifndef NMO_BEHAVIOR_GRAPH_H
#define NMO_BEHAVIOR_GRAPH_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_behavior_graph_node {
    nmo_object_id_t id;
    const char *kind;
    const char *name;
    bool owns_name;
    nmo_class_id_t class_id;
    const char *class_name;
} nmo_behavior_graph_node_t;

typedef struct nmo_behavior_graph_edge {
    nmo_object_id_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
    const char *kind;
    const char *field_path;
    nmo_object_id_t in_io_id;
    nmo_object_id_t out_io_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
    bool is_shared;
} nmo_behavior_graph_edge_t;

typedef struct nmo_behavior_graph {
    nmo_object_id_t behavior_id;
    const char *behavior_name;        /* Borrowed from session object */
    nmo_class_id_t behavior_class_id;
    const char *behavior_class_name;  /* Borrowed from type registry */

    nmo_behavior_graph_node_t *nodes;
    size_t node_count;

    nmo_behavior_graph_edge_t *edges;
    size_t edge_count;

    size_t broken_links;
    size_t missing_nodes;
} nmo_behavior_graph_t;

NMO_API bool nmo_behavior_graph_build(nmo_context_t *ctx,
                                      nmo_session_t *session,
                                      nmo_object_id_t behavior_id,
                                      nmo_behavior_graph_t *out_graph);

NMO_API void nmo_behavior_graph_free(nmo_behavior_graph_t *graph);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_GRAPH_H */
