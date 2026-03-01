#ifndef NMO_SESSION_RUNTIME_GRAPH_H
#define NMO_SESSION_RUNTIME_GRAPH_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_runtime_graph nmo_runtime_graph_t;

typedef enum nmo_runtime_graph_direction {
    NMO_RUNTIME_GRAPH_OUTBOUND = 0,
    NMO_RUNTIME_GRAPH_INBOUND = 1
} nmo_runtime_graph_direction_t;

typedef struct nmo_runtime_edge {
    nmo_object_id_t from;
    nmo_object_id_t to;
    uint32_t kind;
    const char *field_path;
    uint32_t index;
} nmo_runtime_edge_t;

typedef struct nmo_runtime_graph_stats {
    size_t total_edges;
    size_t broken_edges;
    size_t self_edges;
} nmo_runtime_graph_stats_t;

NMO_API nmo_runtime_graph_t *nmo_runtime_graph_create(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *type_registry,
    nmo_arena_t *arena);

NMO_API void nmo_runtime_graph_destroy(nmo_runtime_graph_t *graph);

NMO_API int nmo_runtime_graph_rebuild(nmo_runtime_graph_t *graph);

NMO_API int nmo_runtime_graph_get_edges(
    nmo_runtime_graph_t *graph,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count);

NMO_API int nmo_runtime_graph_get_object_edges(
    nmo_runtime_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_runtime_graph_direction_t direction,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count);

NMO_API int nmo_runtime_graph_get_stats(
    nmo_runtime_graph_t *graph,
    nmo_runtime_graph_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_RUNTIME_GRAPH_H */
