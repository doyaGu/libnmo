/**
 * @file nmo_ref_graph.c
 * @brief Reference graph enumeration implementation
 *
 * Phase 4.1: Uses extensible enumeration registry for all object types.
 * Refactored to use nmo_ref_enumerate.h visitor pattern.
 */

#include "object/nmo_ref_graph.h"
#include "object/nmo_ref_enumerate.h"
#include "object/nmo_object_repository.h"
#include "object_scan_internal.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_type_query.h"
#include "format/nmo_object.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Reference kind names */
static const char *ref_kind_names[] = {
    [NMO_REF_KIND_UNKNOWN] = "unknown",
    [NMO_REF_KIND_HIERARCHY] = "hierarchy",
    [NMO_REF_KIND_MESH] = "mesh",
    [NMO_REF_KIND_MATERIAL] = "material",
    [NMO_REF_KIND_TEXTURE] = "texture",
    [NMO_REF_KIND_OWNER] = "owner",
    [NMO_REF_KIND_BEHAVIOR_LINK] = "behavior_link",
    [NMO_REF_KIND_PARAMETER] = "parameter",
    [NMO_REF_KIND_TARGET] = "target",
    [NMO_REF_KIND_GROUP_MEMBER] = "group_member",
    [NMO_REF_KIND_SCENE] = "scene",
    [NMO_REF_KIND_ANIMATION] = "animation",
    [NMO_REF_KIND_PLACE] = "place",
    [NMO_REF_KIND_SKIN_BONE] = "skin_bone",
    [NMO_REF_KIND_DATA_ARRAY] = "data_array",
    [NMO_REF_KIND_SCRIPT] = "script"
};

typedef struct nmo_ref_adjacency_entry {
    nmo_object_id_t object_id;
    size_t edge_index;
} nmo_ref_adjacency_entry_t;

typedef struct nmo_ref_edge_cursor {
    const nmo_ref_graph_t *graph;
    const nmo_ref_adjacency_entry_t *entries;
    size_t count;
    size_t pos;
    nmo_object_id_t object_id;
    nmo_ref_direction_t direction;
    size_t scan_pos;
    bool indexed;
} nmo_ref_edge_cursor_t;

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

    /* Optional adjacency indexes over edge storage. */
    nmo_ref_adjacency_entry_t *outgoing_entries;
    nmo_ref_adjacency_entry_t *incoming_entries;
    bool adjacency_built;
    
    /* Current object being enumerated (for visitor context) */
    nmo_object_id_t current_object_id;
    nmo_status_t build_status;
    
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
    
    if (graph->edge_capacity > SIZE_MAX / 2u) {
        return false;
    }
    size_t new_cap = graph->edge_capacity == 0 ? 256 : graph->edge_capacity * 2;
    if (new_cap > SIZE_MAX / sizeof(nmo_ref_edge_t)) {
        return false;
    }
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
    if (kind < NMO_REF_KIND_MAX) {
        graph->stats.edge_counts[kind]++;
    }
    
    if (from == to) {
        graph->stats.self_refs++;
    }
    
    return true;
}

static bool ref_graph_edge_matches(
    const nmo_ref_edge_t *edge,
    nmo_object_id_t object_id,
    nmo_ref_direction_t direction)
{
    if (edge == NULL) {
        return false;
    }
    if (direction == NMO_REF_DIR_OUTGOING) {
        return edge->from == object_id;
    }
    if (direction == NMO_REF_DIR_INCOMING) {
        return edge->to == object_id;
    }
    return false;
}

static int compare_adjacency_entry(const void *a, const void *b)
{
    const nmo_ref_adjacency_entry_t *ea = (const nmo_ref_adjacency_entry_t *)a;
    const nmo_ref_adjacency_entry_t *eb = (const nmo_ref_adjacency_entry_t *)b;
    if (ea->object_id < eb->object_id) return -1;
    if (ea->object_id > eb->object_id) return 1;
    if (ea->edge_index < eb->edge_index) return -1;
    if (ea->edge_index > eb->edge_index) return 1;
    return 0;
}

static bool ref_graph_build_adjacency(nmo_ref_graph_t *graph)
{
    if (graph == NULL) {
        return false;
    }

    graph->adjacency_built = false;
    graph->outgoing_entries = NULL;
    graph->incoming_entries = NULL;

    if (graph->edge_count == 0) {
        graph->adjacency_built = true;
        return true;
    }
    if (graph->edge_count > SIZE_MAX / sizeof(nmo_ref_adjacency_entry_t)) {
        return false;
    }

    size_t bytes = graph->edge_count * sizeof(nmo_ref_adjacency_entry_t);
    graph->outgoing_entries = (nmo_ref_adjacency_entry_t *)nmo_arena_alloc(
        graph->arena, bytes, _Alignof(nmo_ref_adjacency_entry_t));
    graph->incoming_entries = (nmo_ref_adjacency_entry_t *)nmo_arena_alloc(
        graph->arena, bytes, _Alignof(nmo_ref_adjacency_entry_t));
    if (graph->outgoing_entries == NULL || graph->incoming_entries == NULL) {
        graph->outgoing_entries = NULL;
        graph->incoming_entries = NULL;
        return false;
    }

    for (size_t i = 0; i < graph->edge_count; ++i) {
        graph->outgoing_entries[i] = (nmo_ref_adjacency_entry_t){
            .object_id = graph->edges[i].from,
            .edge_index = i
        };
        graph->incoming_entries[i] = (nmo_ref_adjacency_entry_t){
            .object_id = graph->edges[i].to,
            .edge_index = i
        };
    }

    qsort(
        graph->outgoing_entries,
        graph->edge_count,
        sizeof(nmo_ref_adjacency_entry_t),
        compare_adjacency_entry);
    qsort(
        graph->incoming_entries,
        graph->edge_count,
        sizeof(nmo_ref_adjacency_entry_t),
        compare_adjacency_entry);
    graph->adjacency_built = true;
    return true;
}

static bool ref_graph_adjacency_range(
    const nmo_ref_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_ref_direction_t direction,
    const nmo_ref_adjacency_entry_t **out_entries,
    size_t *out_count)
{
    if (out_entries != NULL) {
        *out_entries = NULL;
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (graph == NULL || !graph->adjacency_built) {
        return false;
    }

    const nmo_ref_adjacency_entry_t *entries = NULL;
    if (direction == NMO_REF_DIR_OUTGOING) {
        entries = graph->outgoing_entries;
    } else if (direction == NMO_REF_DIR_INCOMING) {
        entries = graph->incoming_entries;
    } else {
        return true;
    }
    if (graph->edge_count == 0 || entries == NULL) {
        return true;
    }

    size_t lo = 0;
    size_t hi = graph->edge_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entries[mid].object_id < object_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    size_t start = lo;
    while (lo < graph->edge_count && entries[lo].object_id == object_id) {
        lo++;
    }

    if (out_entries != NULL && lo > start) {
        *out_entries = entries + start;
    }
    if (out_count != NULL) {
        *out_count = lo - start;
    }
    return true;
}

static void ref_graph_edge_cursor_init(
    const nmo_ref_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_ref_direction_t direction,
    nmo_ref_edge_cursor_t *cursor)
{
    memset(cursor, 0, sizeof(*cursor));
    cursor->graph = graph;
    cursor->object_id = object_id;
    cursor->direction = direction;
    cursor->indexed = ref_graph_adjacency_range(
        graph, object_id, direction, &cursor->entries, &cursor->count);
}

static const nmo_ref_edge_t *ref_graph_edge_cursor_next(
    nmo_ref_edge_cursor_t *cursor)
{
    if (cursor == NULL || cursor->graph == NULL) {
        return NULL;
    }

    const nmo_ref_graph_t *graph = cursor->graph;
    if (cursor->indexed) {
        if (cursor->pos >= cursor->count || cursor->entries == NULL) {
            return NULL;
        }
        size_t edge_index = cursor->entries[cursor->pos++].edge_index;
        return edge_index < graph->edge_count ? &graph->edges[edge_index] : NULL;
    }

    while (cursor->scan_pos < graph->edge_count) {
        const nmo_ref_edge_t *edge = &graph->edges[cursor->scan_pos++];
        if (ref_graph_edge_matches(edge, cursor->object_id, cursor->direction)) {
            return edge;
        }
    }
    return NULL;
}

static size_t ref_graph_count_object_edges(
    const nmo_ref_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_ref_direction_t direction)
{
    size_t count = 0;
    if (ref_graph_adjacency_range(graph, object_id, direction, NULL, &count)) {
        return count;
    }

    if (graph == NULL) {
        return 0;
    }
    for (size_t i = 0; i < graph->edge_count; ++i) {
        if (ref_graph_edge_matches(&graph->edges[i], object_id, direction)) {
            count++;
        }
    }
    return count;
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
    if (graph == NULL || graph->build_status != NMO_OK) {
        return false;
    }
    if (!add_edge(
            graph, graph->current_object_id, target_id, kind, field_path, index)) {
        graph->build_status = NMO_ERR_NOMEM;
        return false;
    }
    return true;
}

static nmo_status_t ref_graph_build_object(
    size_t object_index,
    nmo_object_t *object,
    void *user_data)
{
    (void)object_index;
    nmo_ref_graph_t *graph = (nmo_ref_graph_t *)user_data;
    if (graph->build_status != NMO_OK) {
        return graph->build_status;
    }
    graph->current_object_id = nmo_object_get_id(object);
    nmo_status_t status = nmo_ref_enumerate_object(
        graph->type_registry, object, ref_graph_visitor, graph);
    if (status != NMO_OK) {
        graph->build_status = status;
        return status;
    }
    return graph->build_status;
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
    graph->build_status = NMO_OK;
    
    /* Enumerate all repository objects using the registry. */
    nmo_status_t build_result = nmo_object_scan_repository(
        repo, ref_graph_build_object, graph, NULL);
    if (build_result != NMO_OK || graph->build_status != NMO_OK) {
        return NULL;
    }
    if (!ref_graph_build_adjacency(graph)) {
        graph->build_status = NMO_ERR_NOMEM;
        return NULL;
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
    
    size_t match_count = ref_graph_count_object_edges(graph, object_id, direction);
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
    
    nmo_ref_edge_cursor_t cursor;
    ref_graph_edge_cursor_init(graph, object_id, direction, &cursor);
    size_t idx = 0;
    const nmo_ref_edge_t *edge = NULL;
    while (idx < match_count &&
           (edge = ref_graph_edge_cursor_next(&cursor)) != NULL) {
        result[idx++] = *edge;
    }
    
    *edges = result;
    *count = idx;
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
 * @return 1 if inserted, 0 if already present, -1 on allocation failure
 */
static int id_set_insert(nmo_object_id_t **arr, size_t *count,
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
            return 0; /* already present */
        }
    }

    /* Grow if needed */
    if (*count >= *capacity) {
        size_t new_cap = *capacity == 0 ? 64 : *capacity * 2;
        nmo_object_id_t *new_arr = nmo_arena_alloc(
            arena, new_cap * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t));
        if (!new_arr) return -1;
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
    return 1;
}

static int id_queue_append(
    nmo_object_id_t **arr,
    size_t *count,
    size_t *capacity,
    nmo_arena_t *arena,
    nmo_object_id_t id)
{
    if (*count >= *capacity) {
        size_t new_cap = *capacity == 0 ? 64 : *capacity * 2;
        nmo_object_id_t *new_arr = nmo_arena_alloc(
            arena, new_cap * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t));
        if (!new_arr) return -1;
        if (*arr && *count > 0) {
            memcpy(new_arr, *arr, *count * sizeof(nmo_object_id_t));
        }
        *arr = new_arr;
        *capacity = new_cap;
    }

    (*arr)[(*count)++] = id;
    return 0;
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
    nmo_object_id_t *queue = NULL;
    size_t queue_count = 0;
    size_t queue_cap = 0;

    for (size_t i = 0; i < root_count; ++i) {
        if (root_ids[i] == NMO_OBJECT_ID_NONE) continue;
        int irc = id_set_insert(&marked, &marked_count, &marked_cap, arena, root_ids[i]);
        if (irc < 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "allocation failure in mark_reachable");
        }
        if (irc == 1 &&
            id_queue_append(&queue, &queue_count, &queue_cap, arena, root_ids[i]) != 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "allocation failure in mark_reachable");
        }
    }

    for (size_t queue_pos = 0; queue_pos < queue_count; ++queue_pos) {
        nmo_ref_edge_cursor_t cursor;
        ref_graph_edge_cursor_init(
            graph, queue[queue_pos], NMO_REF_DIR_OUTGOING, &cursor);
        const nmo_ref_edge_t *edge = NULL;
        while ((edge = ref_graph_edge_cursor_next(&cursor)) != NULL) {
            nmo_object_id_t to = edge->to;
            if (to == NMO_OBJECT_ID_NONE) continue;
            int irc = id_set_insert(&marked, &marked_count, &marked_cap,
                                    arena, to);
            if (irc < 0) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "allocation failure in mark_reachable");
            }
            if (irc == 1 &&
                id_queue_append(&queue, &queue_count, &queue_cap, arena, to) != 0) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "allocation failure in mark_reachable");
            }
        }
    }

    /* Filter out IDs not present in the repository (broken edge targets) */
    size_t filtered = 0;
    for (size_t i = 0; i < marked_count; ++i) {
        if (nmo_object_repository_find_by_id(graph->repo, marked[i])) {
            marked[filtered++] = marked[i];
        }
    }
    marked_count = filtered;

    *out_reachable_ids = marked;
    *out_reachable_count = marked_count;
    NMO_RETURN_OK();
}

const char *nmo_ref_kind_name(nmo_ref_kind_t kind) {
    if (kind >= NMO_REF_KIND_MAX) {
        return "unknown";
    }
    return ref_kind_names[kind];
}

/* ============================================================================
 * Orphan Detection
 * ============================================================================ */

nmo_status_t nmo_ref_graph_find_orphans(
    nmo_ref_graph_t *graph,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    nmo_object_id_t **out_orphans,
    size_t *out_count)
{
    if (!graph || !repo || !registry || !arena || !out_orphans || !out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to find_orphans");
    }

    *out_orphans = NULL;
    *out_count = 0;

    size_t object_count = nmo_object_repository_get_count(repo);
    if (object_count == 0) {
        NMO_RETURN_OK();
    }

    /* Allocate root ID array (worst case: all objects are roots) */
    nmo_object_id_t *root_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, object_count * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    if (!root_ids) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "allocation failed in find_orphans");
    }

    size_t root_count = 0;

    /* Tier 1: CKLevel / CKScene */
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        if (nmo_type_query_object_is_derived_from_class(
                registry, object, NMO_CID_LEVEL) ||
            nmo_type_query_object_is_derived_from_class(
                registry, object, NMO_CID_SCENE)) {
            root_ids[root_count++] = nmo_object_get_id(object);
        }
    }

    /* Tier 2: CKGroup */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            if (object == NULL) {
                continue;
            }
            if (nmo_type_query_object_is_derived_from_class(
                    registry, object, NMO_CID_GROUP)) {
                root_ids[root_count++] = nmo_object_get_id(object);
            }
        }
    }

    /* Tier 3: CK3dEntity / CK3dObject */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            if (object == NULL) {
                continue;
            }
            if (nmo_type_query_object_is_derived_from_class(
                    registry, object, NMO_CID_3DENTITY)) {
                root_ids[root_count++] = nmo_object_get_id(object);
            }
        }
    }

    /* Tier 4: all objects with zero incoming references */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            if (object == NULL) {
                continue;
            }
            nmo_object_id_t oid = nmo_object_get_id(object);
            size_t ecount = ref_graph_count_object_edges(
                graph, oid, NMO_REF_DIR_INCOMING);
            if (ecount == 0) {
                root_ids[root_count++] = oid;
            }
        }
    }

    /* Mark reachable set */
    nmo_object_id_t *reachable_ids = NULL;
    size_t reachable_count = 0;
    nmo_status_t ms = nmo_ref_graph_mark_reachable(
        graph, root_ids, root_count, arena,
        &reachable_ids, &reachable_count);
    if (ms != NMO_OK) {
        return ms;
    }

    /* Collect orphans: objects NOT in the reachable set */
    nmo_object_id_t *orphans = (nmo_object_id_t *)nmo_arena_alloc(
        arena, object_count * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    if (!orphans) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "allocation failed in find_orphans");
    }

    size_t orphan_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        nmo_object_id_t oid = nmo_object_get_id(object);
        if (!id_in_sorted(reachable_ids, reachable_count, oid)) {
            orphans[orphan_count++] = oid;
        }
    }

    *out_orphans = orphans;
    *out_count = orphan_count;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Cycle Detection (iterative DFS with rotation-normalized dedup)
 * ============================================================================ */

/** Internal cycle record used during DFS */
typedef struct {
    nmo_object_id_t *ids;
    nmo_ref_kind_t *kinds;
    size_t count;
} cycle_record_t;

/** DFS state for cycle detection */
typedef struct {
    nmo_ref_graph_t *graph;
    uint8_t *color;            /* 0=WHITE, 1=GRAY, 2=BLACK */
    nmo_object_id_t *stack;
    nmo_ref_kind_t *stack_kinds;
    size_t stack_size;

    cycle_record_t *cycles;
    size_t cycle_count;
    size_t cycle_cap;

    nmo_arena_t *arena;
    nmo_object_id_t max_id;
} cycle_dfs_state_t;

static void cycle_dfs_record(cycle_dfs_state_t *st, nmo_object_id_t back_target,
                             nmo_ref_kind_t back_kind) {
    /* Find back_target in the stack to extract the cycle */
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i < st->stack_size; ++i) {
        if (st->stack[i] == back_target) {
            start = i;
            found = true;
            break;
        }
    }
    if (!found) return;

    size_t len = st->stack_size - start;

    /* Normalize: rotate so minimum ID is first (for dedup) */
    size_t min_pos = 0;
    for (size_t j = 1; j < len; ++j) {
        if (st->stack[start + j] < st->stack[start + min_pos])
            min_pos = j;
    }

    /* Deduplicate: check if we already have this cycle */
    for (size_t ci = 0; ci < st->cycle_count; ++ci) {
        if (st->cycles[ci].count == len) {
            bool same = true;
            for (size_t j = 0; j < len; ++j) {
                if (st->cycles[ci].ids[j] != st->stack[start + ((min_pos + j) % len)]) {
                    same = false;
                    break;
                }
            }
            if (same) return;
        }
    }

    /* Grow cycle array if needed */
    if (st->cycle_count >= st->cycle_cap) {
        size_t new_cap = st->cycle_cap ? st->cycle_cap * 2 : 16;
        cycle_record_t *tmp = (cycle_record_t *)realloc(
            st->cycles, new_cap * sizeof(cycle_record_t));
        if (!tmp) return;
        st->cycles = tmp;
        st->cycle_cap = new_cap;
    }

    nmo_object_id_t *ids = (nmo_object_id_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    nmo_ref_kind_t *kinds = (nmo_ref_kind_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_ref_kind_t),
        _Alignof(nmo_ref_kind_t));
    if (!ids || !kinds) return;

    for (size_t j = 0; j < len; ++j) {
        size_t src_j = (min_pos + j) % len;
        size_t next_j = (min_pos + j + 1) % len;
        ids[j] = st->stack[start + src_j];
        kinds[j] = (j + 1 < len) ? st->stack_kinds[start + next_j] : back_kind;
    }

    cycle_record_t *rec = &st->cycles[st->cycle_count++];
    rec->ids = ids;
    rec->kinds = kinds;
    rec->count = len;
}

/* Iterative DFS frame */
typedef struct {
    nmo_object_id_t id;
    nmo_ref_kind_t entry_kind;
    nmo_ref_edge_cursor_t edges;
} cycle_dfs_frame_t;

static void cycle_dfs_visit(cycle_dfs_state_t *st, nmo_object_id_t start_id,
                            nmo_ref_kind_t start_kind) {
    size_t frame_cap = st->max_id < 4096 ? 4096 : (size_t)(st->max_id + 1);
    cycle_dfs_frame_t *frames = (cycle_dfs_frame_t *)malloc(
        frame_cap * sizeof(cycle_dfs_frame_t));
    if (!frames) return;
    size_t frame_top = 0;

    if (start_id > st->max_id) { free(frames); return; }
    st->color[start_id] = 1; /* GRAY */
    st->stack[st->stack_size] = start_id;
    st->stack_kinds[st->stack_size] = start_kind;
    st->stack_size++;

    frames[frame_top].id = start_id;
    frames[frame_top].entry_kind = start_kind;
    ref_graph_edge_cursor_init(
        st->graph, start_id, NMO_REF_DIR_OUTGOING, &frames[frame_top].edges);
    frame_top++;

    while (frame_top > 0) {
        cycle_dfs_frame_t *f = &frames[frame_top - 1];
        const nmo_ref_edge_t *edge = ref_graph_edge_cursor_next(&f->edges);

        if (edge == NULL) {
            st->stack_size--;
            st->color[f->id] = 2; /* BLACK */
            frame_top--;
            continue;
        }

        nmo_object_id_t target = edge->to;
        if (target > st->max_id) continue;

        if (st->color[target] == 1) {
            /* Back edge -> cycle */
            cycle_dfs_record(st, target, edge->kind);
        } else if (st->color[target] == 0) {
            if (frame_top >= frame_cap || st->stack_size >= frame_cap) continue;

            st->color[target] = 1; /* GRAY */
            st->stack[st->stack_size] = target;
            st->stack_kinds[st->stack_size] = edge->kind;
            st->stack_size++;

            frames[frame_top].id = target;
            frames[frame_top].entry_kind = edge->kind;
            ref_graph_edge_cursor_init(
                st->graph, target, NMO_REF_DIR_OUTGOING, &frames[frame_top].edges);
            frame_top++;
        }
    }

    free(frames);
}

nmo_status_t nmo_ref_graph_find_cycles(
    nmo_ref_graph_t *graph,
    nmo_object_repository_t *repo,
    nmo_arena_t *arena,
    nmo_ref_cycle_t **out_cycles,
    size_t *out_count)
{
    if (!graph || !repo || !arena || !out_cycles || !out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to find_cycles");
    }

    *out_cycles = NULL;
    *out_count = 0;

    /* Find max_id across all objects. */
    size_t object_count = nmo_object_repository_get_count(repo);
    if (object_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_object_id_t max_id = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        nmo_object_id_t oid = nmo_object_get_id(object);
        if (oid > max_id) max_id = oid;
    }

    /* Allocate DFS state */
    size_t color_size = (size_t)(max_id + 1);
    uint8_t *color = (uint8_t *)calloc(color_size, sizeof(uint8_t));
    nmo_object_id_t *stack = (nmo_object_id_t *)malloc(
        object_count * sizeof(nmo_object_id_t));
    nmo_ref_kind_t *stack_kinds = (nmo_ref_kind_t *)malloc(
        object_count * sizeof(nmo_ref_kind_t));

    if (!color || !stack || !stack_kinds) {
        free(color);
        free(stack);
        free(stack_kinds);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "allocation failed in find_cycles");
    }

    cycle_dfs_state_t st;
    memset(&st, 0, sizeof(st));
    st.graph = graph;
    st.color = color;
    st.stack = stack;
    st.stack_kinds = stack_kinds;
    st.stack_size = 0;
    st.cycles = NULL;
    st.cycle_count = 0;
    st.cycle_cap = 0;
    st.arena = arena;
    st.max_id = max_id;

    /* Run DFS from each unvisited object */
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        nmo_object_id_t oid = nmo_object_get_id(object);
        if (oid <= max_id && color[oid] == 0) {
            cycle_dfs_visit(&st, oid, NMO_REF_KIND_UNKNOWN);
        }
    }

    free(color);
    free(stack);
    free(stack_kinds);

    /* Copy results to arena-allocated output */
    if (st.cycle_count > 0) {
        nmo_ref_cycle_t *result = (nmo_ref_cycle_t *)nmo_arena_alloc(
            arena, st.cycle_count * sizeof(nmo_ref_cycle_t),
            _Alignof(nmo_ref_cycle_t));
        if (!result) {
            free(st.cycles);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "allocation failed in find_cycles");
        }

        for (size_t i = 0; i < st.cycle_count; ++i) {
            result[i].ids = st.cycles[i].ids;
            result[i].kinds = st.cycles[i].kinds;
            result[i].count = st.cycles[i].count;
        }

        *out_cycles = result;
        *out_count = st.cycle_count;
    }

    free(st.cycles);
    NMO_RETURN_OK();
}
