#include "session/nmo_runtime_graph.h"

#include "core/nmo_arena.h"
#include "object/nmo_ref_graph.h"
#include <string.h>

struct nmo_runtime_graph {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *type_registry;
    nmo_arena_t *arena;
    nmo_ref_graph_t *ref_graph;
};

static int runtime_graph_copy_edges(
    nmo_runtime_graph_t *graph,
    nmo_ref_edge_t *ref_edges,
    size_t ref_count,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count)
{
    if (graph == NULL || out_edges == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_edges = NULL;
    *out_count = 0;

    if (ref_edges == NULL || ref_count == 0) {
        return NMO_OK;
    }

    nmo_runtime_edge_t *edges = (nmo_runtime_edge_t *)nmo_arena_alloc(
        graph->arena,
        ref_count * sizeof(nmo_runtime_edge_t),
        _Alignof(nmo_runtime_edge_t));
    if (edges == NULL) {
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < ref_count; i++) {
        edges[i].from = ref_edges[i].from;
        edges[i].to = ref_edges[i].to;
        edges[i].kind = (uint32_t)ref_edges[i].kind;
        edges[i].field_path = ref_edges[i].field_path;
        edges[i].index = ref_edges[i].index;
    }

    *out_edges = edges;
    *out_count = ref_count;
    return NMO_OK;
}

nmo_runtime_graph_t *nmo_runtime_graph_create(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *type_registry,
    nmo_arena_t *arena)
{
    if (repo == NULL || type_registry == NULL || arena == NULL) {
        return NULL;
    }

    nmo_runtime_graph_t *graph = (nmo_runtime_graph_t *)nmo_arena_alloc(
        arena,
        sizeof(nmo_runtime_graph_t),
        _Alignof(nmo_runtime_graph_t));
    if (graph == NULL) {
        return NULL;
    }

    memset(graph, 0, sizeof(*graph));
    graph->repo = repo;
    graph->type_registry = type_registry;
    graph->arena = arena;
    graph->ref_graph = nmo_ref_graph_create(repo, type_registry, arena);

    if (graph->ref_graph == NULL) {
        return NULL;
    }

    return graph;
}

void nmo_runtime_graph_destroy(nmo_runtime_graph_t *graph)
{
    if (graph == NULL) {
        return;
    }
    if (graph->ref_graph != NULL) {
        nmo_ref_graph_destroy(graph->ref_graph);
        graph->ref_graph = NULL;
    }
}

int nmo_runtime_graph_rebuild(nmo_runtime_graph_t *graph)
{
    if (graph == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (graph->ref_graph != NULL) {
        nmo_ref_graph_destroy(graph->ref_graph);
        graph->ref_graph = NULL;
    }

    graph->ref_graph = nmo_ref_graph_create(graph->repo, graph->type_registry, graph->arena);
    if (graph->ref_graph == NULL) {
        return NMO_ERR_NOMEM;
    }

    return NMO_OK;
}

int nmo_runtime_graph_get_edges(
    nmo_runtime_graph_t *graph,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count)
{
    if (graph == NULL || out_edges == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_edge_t *ref_edges = NULL;
    size_t ref_count = 0;
    int result = nmo_ref_graph_get_edges(graph->ref_graph, &ref_edges, &ref_count);
    if (result != NMO_OK) {
        return result;
    }

    return runtime_graph_copy_edges(graph, ref_edges, ref_count, out_edges, out_count);
}

int nmo_runtime_graph_get_object_edges(
    nmo_runtime_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_runtime_graph_direction_t direction,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count)
{
    if (graph == NULL || out_edges == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_direction_t ref_dir =
        (direction == NMO_RUNTIME_GRAPH_INBOUND) ? NMO_REF_DIR_INCOMING : NMO_REF_DIR_OUTGOING;

    nmo_ref_edge_t *ref_edges = NULL;
    size_t ref_count = 0;
    int result = nmo_ref_graph_get_object_edges(
        graph->ref_graph,
        object_id,
        ref_dir,
        &ref_edges,
        &ref_count);
    if (result != NMO_OK) {
        return result;
    }

    return runtime_graph_copy_edges(graph, ref_edges, ref_count, out_edges, out_count);
}

int nmo_runtime_graph_get_stats(
    nmo_runtime_graph_t *graph,
    nmo_runtime_graph_stats_t *out_stats)
{
    if (graph == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_stats_t ref_stats;
    int result = nmo_ref_graph_get_stats(graph->ref_graph, &ref_stats);
    if (result != NMO_OK) {
        return result;
    }

    out_stats->total_edges = ref_stats.total_edges;
    out_stats->broken_edges = ref_stats.broken_refs;
    out_stats->self_edges = ref_stats.self_refs;
    return NMO_OK;
}
