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
#include "object/nmo_class_ids.h"
#include "type/nmo_type_system.h"
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
        int irc = id_set_insert(&marked, &marked_count, &marked_cap, arena, root_ids[i]);
        if (irc < 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "allocation failure in mark_reachable");
        }
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
                int irc = id_set_insert(&marked, &marked_count, &marked_cap,
                                        arena, to);
                if (irc < 0) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "allocation failure in mark_reachable");
                }
                if (irc == 1) {
                    changed = true;
                }
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
    if (kind >= NMO_REF_MAX) {
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

    /* Get all objects */
    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
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
        nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
        if (cid == NMO_CID_LEVEL || cid == NMO_CID_SCENE ||
            nmo_type_registry_is_class_derived_from(registry, cid, NMO_CID_LEVEL) ||
            nmo_type_registry_is_class_derived_from(registry, cid, NMO_CID_SCENE)) {
            root_ids[root_count++] = nmo_object_get_id(objects[i]);
        }
    }

    /* Tier 2: CKGroup */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
            if (cid == NMO_CID_GROUP ||
                nmo_type_registry_is_class_derived_from(registry, cid, NMO_CID_GROUP)) {
                root_ids[root_count++] = nmo_object_get_id(objects[i]);
            }
        }
    }

    /* Tier 3: CK3dEntity / CK3dObject */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
            if (cid == NMO_CID_3DENTITY || cid == NMO_CID_3DOBJECT ||
                nmo_type_registry_is_class_derived_from(registry, cid, NMO_CID_3DENTITY)) {
                root_ids[root_count++] = nmo_object_get_id(objects[i]);
            }
        }
    }

    /* Tier 4: all objects with zero incoming references */
    if (root_count == 0) {
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_id_t oid = nmo_object_get_id(objects[i]);
            nmo_ref_edge_t *edges = NULL;
            size_t ecount = 0;
            nmo_ref_graph_get_object_edges(graph, oid, NMO_REF_DIR_INCOMING,
                                           &edges, &ecount);
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
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
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
    nmo_ref_edge_t *edges;
    size_t ecount;
    size_t edge_idx;
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

    nmo_ref_edge_t *edges = NULL;
    size_t ecount = 0;
    nmo_ref_graph_get_object_edges(st->graph, start_id, NMO_REF_DIR_OUTGOING,
                                   &edges, &ecount);
    frames[frame_top].id = start_id;
    frames[frame_top].entry_kind = start_kind;
    frames[frame_top].edges = edges;
    frames[frame_top].ecount = ecount;
    frames[frame_top].edge_idx = 0;
    frame_top++;

    while (frame_top > 0) {
        cycle_dfs_frame_t *f = &frames[frame_top - 1];

        if (f->edge_idx >= f->ecount) {
            st->stack_size--;
            st->color[f->id] = 2; /* BLACK */
            frame_top--;
            continue;
        }

        nmo_ref_edge_t *edge = &f->edges[f->edge_idx++];
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

            nmo_ref_edge_t *tedges = NULL;
            size_t tecount = 0;
            nmo_ref_graph_get_object_edges(st->graph, target, NMO_REF_DIR_OUTGOING,
                                           &tedges, &tecount);
            frames[frame_top].id = target;
            frames[frame_top].entry_kind = edge->kind;
            frames[frame_top].edges = tedges;
            frames[frame_top].ecount = tecount;
            frames[frame_top].edge_idx = 0;
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

    /* Get all objects to find max_id */
    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    if (object_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_object_id_t max_id = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
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
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
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
