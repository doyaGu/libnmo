/**
 * @file nmo_ref_graph.c
 * @brief Reference graph enumeration implementation
 *
 * Phase 4.1: Uses extensible enumeration registry for all object types.
 * Refactored to use nmo_ref_enumerate.h visitor pattern.
 */

#include "session/nmo_ref_graph.h"
#include "session/nmo_ref_enumerate.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <string.h>
#include <stdlib.h>

/* Reference kind names */
static const char *ref_kind_names[] = {
    [NMO_REF_UNKNOWN] = "unknown",
    [NMO_REF_HIERARCHY] = "hierarchy",
    [NMO_REF_MESH] = "mesh",
    [NMO_REF_MATERIAL] = "material",
    [NMO_REF_TEXTURE] = "texture",
    [NMO_REF_OWNER] = "owner",
    [NMO_REF_BEHAVIOR_LINK] = "behavior_link",
    [NMO_REF_PARAMETER] = "parameter",
    [NMO_REF_TARGET] = "target",
    [NMO_REF_GROUP_MEMBER] = "group_member",
    [NMO_REF_SCENE] = "scene",
    [NMO_REF_ANIMATION] = "animation",
    [NMO_REF_PLACE] = "place",
    [NMO_REF_SKIN_BONE] = "skin_bone",
    [NMO_REF_DATA_ARRAY] = "data_array",
    [NMO_REF_SCRIPT] = "script"
};

/**
 * @brief Reference graph structure
 */
struct nmo_ref_graph {
    nmo_arena_t *arena;
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *type_registry;
    
    /* Edge storage */
    nmo_ref_edge_t *edges;
    size_t edge_count;
    size_t edge_capacity;
    
    /* Current object being enumerated (for visitor context) */
    nmo_object_id_t current_object_id;
    
    /* Statistics */
    nmo_ref_graph_stats_t stats;
    
    /* Validation state */
    bool validated;
    nmo_ref_edge_t *broken_edges;
    size_t broken_count;
};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * @brief Grow edge array if needed
 */
static bool grow_edges(nmo_ref_graph_t *graph) {
    if (graph->edge_count < graph->edge_capacity) {
        return true;
    }
    
    size_t new_cap = graph->edge_capacity == 0 ? 256 : graph->edge_capacity * 2;
    nmo_ref_edge_t *new_edges = nmo_arena_alloc(graph->arena, 
                                                 new_cap * sizeof(nmo_ref_edge_t),
                                                 _Alignof(nmo_ref_edge_t));
    if (!new_edges) {
        return false;
    }
    
    if (graph->edges && graph->edge_count > 0) {
        memcpy(new_edges, graph->edges, graph->edge_count * sizeof(nmo_ref_edge_t));
    }
    
    graph->edges = new_edges;
    graph->edge_capacity = new_cap;
    return true;
}

/**
 * @brief Add an edge to the graph
 */
static bool add_edge(nmo_ref_graph_t *graph, nmo_object_id_t from,
                     nmo_object_id_t to, nmo_ref_kind_t kind,
                     const char *field_path, uint32_t index) {
    if (to == 0) {
        return true; /* NULL reference, skip */
    }
    
    if (!grow_edges(graph)) {
        return false;
    }
    
    nmo_ref_edge_t *edge = &graph->edges[graph->edge_count++];
    edge->from = from;
    edge->to = to;
    edge->kind = kind;
    edge->field_path = field_path;
    edge->index = index;
    
    graph->stats.total_edges++;
    if (kind < NMO_REF_MAX) {
        graph->stats.edge_counts[kind]++;
    }
    
    if (from == to) {
        graph->stats.self_refs++;
    }
    
    return true;
}

/* ============================================================================
 * Visitor Callback for Enumerator Integration
 * ============================================================================ */

/**
 * @brief Visitor callback that adds edges to the graph
 */
static bool ref_graph_visitor(
    void *user_data,
    nmo_object_id_t target_id,
    nmo_ref_kind_t kind,
    const char *field_path,
    uint32_t index)
{
    nmo_ref_graph_t *graph = (nmo_ref_graph_t *)user_data;
    add_edge(graph, graph->current_object_id, target_id, kind, field_path, index);
    return true; /* Continue enumeration */
}

/* ============================================================================
 * Public API
 * ============================================================================ */

nmo_ref_graph_t *nmo_ref_graph_create(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *type_registry,
    nmo_arena_t *arena)
{
    if (!repo || !type_registry || !arena) {
        return NULL;
    }
    
    nmo_ref_graph_t *graph = nmo_arena_alloc(arena, sizeof(nmo_ref_graph_t),
                                              _Alignof(nmo_ref_graph_t));
    if (!graph) {
        return NULL;
    }
    
    memset(graph, 0, sizeof(nmo_ref_graph_t));
    graph->arena = arena;
    graph->repo = repo;
    graph->type_registry = type_registry;
    
    /* Get all objects and enumerate references using the registry */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    
    objects = nmo_object_repository_get_all(repo, &object_count);
    if (object_count > 0 && objects == NULL) {
        return NULL;
    }
    
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        graph->current_object_id = nmo_object_get_id(obj);
        
        nmo_ref_enumerate_object(graph->type_registry, obj, ref_graph_visitor, graph);
    }
    
    return graph;
}

void nmo_ref_graph_destroy(nmo_ref_graph_t *graph) {
    /* Arena-allocated, no explicit destruction needed */
    (void)graph;
}

nmo_status_t nmo_ref_graph_get_edges(nmo_ref_graph_t *graph,
                                      nmo_ref_edge_t **edges,
                                      size_t *count) {
    if (!graph) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph is NULL");
    }
    
    if (edges) {
        *edges = graph->edges;
    }
    if (count) {
        *count = graph->edge_count;
    }
    
    NMO_RETURN_OK();
}

nmo_status_t nmo_ref_graph_get_object_edges(nmo_ref_graph_t *graph,
                                             nmo_object_id_t object_id,
                                             nmo_ref_direction_t direction,
                                             nmo_ref_edge_t **edges,
                                             size_t *count) {
    if (!graph || !edges || !count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid argument");
    }
    
    /* Count matching edges first */
    size_t match_count = 0;
    for (size_t i = 0; i < graph->edge_count; ++i) {
        if (direction == NMO_REF_DIR_OUTGOING && graph->edges[i].from == object_id) {
            match_count++;
        } else if (direction == NMO_REF_DIR_INCOMING && graph->edges[i].to == object_id) {
            match_count++;
        }
    }
    
    if (match_count == 0) {
        *edges = NULL;
        *count = 0;
        NMO_RETURN_OK();
    }
    
    /* Allocate and collect matching edges */
    nmo_ref_edge_t *result = nmo_arena_alloc(graph->arena, 
                                              match_count * sizeof(nmo_ref_edge_t),
                                              _Alignof(nmo_ref_edge_t));
    if (!result) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate edge array");
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < graph->edge_count; ++i) {
        if (direction == NMO_REF_DIR_OUTGOING && graph->edges[i].from == object_id) {
            result[idx++] = graph->edges[i];
        } else if (direction == NMO_REF_DIR_INCOMING && graph->edges[i].to == object_id) {
            result[idx++] = graph->edges[i];
        }
    }
    
    *edges = result;
    *count = match_count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_ref_graph_get_stats(nmo_ref_graph_t *graph,
                                      nmo_ref_graph_stats_t *stats) {
    if (!graph || !stats) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid argument");
    }
    
    *stats = graph->stats;
    NMO_RETURN_OK();
}

nmo_status_t nmo_ref_graph_validate(nmo_ref_graph_t *graph,
                                     nmo_ref_edge_t **broken_edges,
                                     size_t *broken_count) {
    if (!graph) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph is NULL");
    }
    
    if (!graph->validated) {
        nmo_object_repository_t *repo = graph->repo;
        if (!repo) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "graph has no repository");
        }
        
        /* Check all edges for broken references */
        size_t broken = 0;
        
        for (size_t i = 0; i < graph->edge_count; ++i) {
        nmo_object_t *target = nmo_object_repository_find_by_id(
            repo, graph->edges[i].to);
            if (!target) {
                broken++;
            }
        }
        
        graph->stats.broken_refs = broken;
        
        if (broken > 0 && (broken_edges || broken_count)) {
            /* Collect broken edges */
            nmo_ref_edge_t *broken_arr = nmo_arena_alloc(graph->arena,
                                                          broken * sizeof(nmo_ref_edge_t),
                                                          _Alignof(nmo_ref_edge_t));
            if (broken_arr) {
                size_t idx = 0;
                for (size_t i = 0; i < graph->edge_count; ++i) {
                    nmo_object_t *target = nmo_object_repository_find_by_id(
                        repo, graph->edges[i].to);
                    if (!target) {
                        broken_arr[idx++] = graph->edges[i];
                    }
                }
                graph->broken_edges = broken_arr;
                graph->broken_count = broken;
            }
        }
        
        graph->validated = true;
    }
    
    if (broken_edges) {
        *broken_edges = graph->broken_edges;
    }
    if (broken_count) {
        *broken_count = graph->broken_count;
    }
    
    if (graph->stats.broken_refs > 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "broken references found: %zu", graph->stats.broken_refs);
    }
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Mark-Reachable (fixed-point iteration)
 * ============================================================================ */

/**
 * @brief Check if an ID is present in a sorted array (binary search)
 */
static bool id_in_sorted(const nmo_object_id_t *arr, size_t count,
                          nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) {
            lo = mid + 1;
        } else if (arr[mid] > id) {
            hi = mid;
        } else {
            return true;
        }
    }
    return false;
}

/**
 * @brief Insert an ID into a sorted array if not already present
 * @return true if inserted, false if already present or allocation failed
 */
static bool id_set_insert(nmo_object_id_t **arr, size_t *count,
                           size_t *capacity, nmo_arena_t *arena,
                           nmo_object_id_t id) {
    /* Binary search for insertion point */
    size_t lo = 0, hi = *count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((*arr)[mid] < id) {
            lo = mid + 1;
        } else if ((*arr)[mid] > id) {
            hi = mid;
        } else {
            return false; /* already present */
        }
    }

    /* Grow if needed */
    if (*count >= *capacity) {
        size_t new_cap = *capacity == 0 ? 64 : *capacity * 2;
        nmo_object_id_t *new_arr = nmo_arena_alloc(
            arena, new_cap * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t));
        if (!new_arr) return false;
        if (*arr && *count > 0) {
            memcpy(new_arr, *arr, *count * sizeof(nmo_object_id_t));
        }
        *arr = new_arr;
        *capacity = new_cap;
    }

    /* Shift elements to make room at position lo */
    if (lo < *count) {
        memmove(&(*arr)[lo + 1], &(*arr)[lo],
                (*count - lo) * sizeof(nmo_object_id_t));
    }
    (*arr)[lo] = id;
    (*count)++;
    return true;
}

nmo_status_t nmo_ref_graph_mark_reachable(
    nmo_ref_graph_t *graph,
    const nmo_object_id_t *root_ids,
    size_t root_count,
    nmo_arena_t *arena,
    nmo_object_id_t **out_reachable_ids,
    size_t *out_reachable_count)
{
    if (!graph || !arena || !out_reachable_ids || !out_reachable_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to mark_reachable");
    }

    /* Empty root set */
    if (root_count == 0 || !root_ids) {
        *out_reachable_ids = NULL;
        *out_reachable_count = 0;
        NMO_RETURN_OK();
    }

    /* Build marked set, seeded with deduplicated roots */
    nmo_object_id_t *marked = NULL;
    size_t marked_count = 0;
    size_t marked_cap = 0;

    for (size_t i = 0; i < root_count; ++i) {
        if (root_ids[i] == NMO_OBJECT_ID_NONE) continue;
        id_set_insert(&marked, &marked_count, &marked_cap, arena, root_ids[i]);
    }

    /* Fixed-point iteration over all edges */
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < graph->edge_count; ++i) {
            nmo_object_id_t from = graph->edges[i].from;
            nmo_object_id_t to = graph->edges[i].to;
            if (to == NMO_OBJECT_ID_NONE) continue;
            if (id_in_sorted(marked, marked_count, from) &&
                !id_in_sorted(marked, marked_count, to)) {
                if (id_set_insert(&marked, &marked_count, &marked_cap,
                                  arena, to)) {
                    changed = true;
                }
            }
        }
    }

    *out_reachable_ids = marked;
    *out_reachable_count = marked_count;
    NMO_RETURN_OK();
}

const char *nmo_ref_kind_name(nmo_ref_kind_t kind) {
    if (kind >= NMO_REF_MAX) {
        return "unknown";
    }
    return ref_kind_names[kind];
}
