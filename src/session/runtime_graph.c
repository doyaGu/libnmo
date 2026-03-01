#include "session/nmo_runtime_graph.h"

#include "core/nmo_arena.h"
#include "session/nmo_ref_graph.h"
#include <string.h>

struct nmo_runtime_graph {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *type_registry;
    nmo_arena_t *arena;
    nmo_ref_graph_t *legacy;
};

static int runtime_graph_copy_edges(
    nmo_runtime_graph_t *graph,
    nmo_ref_edge_t *legacy_edges,
    size_t legacy_count,
    nmo_runtime_edge_t **out_edges,
    size_t *out_count)
{
    if (graph == NULL || out_edges == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_edges = NULL;
    *out_count = 0;

    if (legacy_edges == NULL || legacy_count == 0) {
        return NMO_OK;
    }

    nmo_runtime_edge_t *edges = (nmo_runtime_edge_t *)nmo_arena_alloc(
        graph->arena,
        legacy_count * sizeof(nmo_runtime_edge_t),
        _Alignof(nmo_runtime_edge_t));
    if (edges == NULL) {
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < legacy_count; i++) {
        edges[i].from = legacy_edges[i].from;
        edges[i].to = legacy_edges[i].to;
        edges[i].kind = (uint32_t)legacy_edges[i].kind;
        edges[i].field_path = legacy_edges[i].field_path;
        edges[i].index = legacy_edges[i].index;
    }

    *out_edges = edges;
    *out_count = legacy_count;
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
    graph->legacy = nmo_ref_graph_create(repo, type_registry, arena);

    if (graph->legacy == NULL) {
        return NULL;
    }

    return graph;
}

void nmo_runtime_graph_destroy(nmo_runtime_graph_t *graph)
{
    if (graph == NULL) {
        return;
    }
    if (graph->legacy != NULL) {
        nmo_ref_graph_destroy(graph->legacy);
        graph->legacy = NULL;
    }
}

int nmo_runtime_graph_rebuild(nmo_runtime_graph_t *graph)
{
    if (graph == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (graph->legacy != NULL) {
        nmo_ref_graph_destroy(graph->legacy);
        graph->legacy = NULL;
    }

    graph->legacy = nmo_ref_graph_create(graph->repo, graph->type_registry, graph->arena);
    if (graph->legacy == NULL) {
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

    nmo_ref_edge_t *legacy_edges = NULL;
    size_t legacy_count = 0;
    int result = nmo_ref_graph_get_edges(graph->legacy, &legacy_edges, &legacy_count);
    if (result != NMO_OK) {
        return result;
    }

    return runtime_graph_copy_edges(graph, legacy_edges, legacy_count, out_edges, out_count);
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

    nmo_ref_direction_t legacy_dir =
        (direction == NMO_RUNTIME_GRAPH_INBOUND) ? NMO_REF_DIR_INCOMING : NMO_REF_DIR_OUTGOING;

    nmo_ref_edge_t *legacy_edges = NULL;
    size_t legacy_count = 0;
    int result = nmo_ref_graph_get_object_edges(
        graph->legacy,
        object_id,
        legacy_dir,
        &legacy_edges,
        &legacy_count);
    if (result != NMO_OK) {
        return result;
    }

    return runtime_graph_copy_edges(graph, legacy_edges, legacy_count, out_edges, out_count);
}

int nmo_runtime_graph_get_stats(
    nmo_runtime_graph_t *graph,
    nmo_runtime_graph_stats_t *out_stats)
{
    if (graph == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_ref_graph_stats_t legacy_stats;
    int result = nmo_ref_graph_get_stats(graph->legacy, &legacy_stats);
    if (result != NMO_OK) {
        return result;
    }

    out_stats->total_edges = legacy_stats.total_edges;
    out_stats->broken_edges = legacy_stats.broken_refs;
    out_stats->self_edges = legacy_stats.self_refs;
    return NMO_OK;
}
