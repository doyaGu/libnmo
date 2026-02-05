/**
 * @file nmo_ref_graph.c
 * @brief Reference graph enumeration implementation
 *
 * Phase 4.1: Uses extensible enumeration registry for all object types.
 * Refactored to use nmo_ref_enumerate.h visitor pattern.
 */

#include "session/nmo_ref_graph.h"
#include "session/nmo_ref_enumerate.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
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
    nmo_session_t *session;
    nmo_ref_enumerator_registry_t *enum_registry;
    
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

nmo_ref_graph_t *nmo_ref_graph_create(nmo_session_t *session, nmo_arena_t *arena) {
    if (!session || !arena) {
        return NULL;
    }
    
    nmo_ref_graph_t *graph = nmo_arena_alloc(arena, sizeof(nmo_ref_graph_t),
                                              _Alignof(nmo_ref_graph_t));
    if (!graph) {
        return NULL;
    }
    
    memset(graph, 0, sizeof(nmo_ref_graph_t));
    graph->arena = arena;
    graph->session = session;
    
    /* Create and populate enumerator registry */
    graph->enum_registry = nmo_ref_enumerator_registry_create(arena);
    if (!graph->enum_registry) {
        return NULL;
    }
    
    /* Register all built-in enumerators */
    if (nmo_ref_enumerator_register_builtins(graph->enum_registry) != NMO_OK) {
        return NULL;
    }
    
    /* Get all objects and enumerate references using the registry */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        return NULL;
    }
    
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        graph->current_object_id = nmo_object_get_id(obj);
        
        /* Use registry-based enumeration */
        nmo_ref_enumerate_object(graph->enum_registry, obj, ref_graph_visitor, graph);
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
        /* Get repository for ID lookups */
        nmo_object_repository_t *repo = nmo_session_get_repository(graph->session);
        if (!repo) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "session has no repository");
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

const char *nmo_ref_kind_name(nmo_ref_kind_t kind) {
    if (kind >= NMO_REF_MAX) {
        return "unknown";
    }
    return ref_kind_names[kind];
}
