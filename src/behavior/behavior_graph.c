#include "behavior/nmo_behavior_analyze.h"
#include "../runtime/runtime_internal.h"
#include "type/nmo_type_query.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Build context 芒鈧€?holds all mutable state for recursive graph construction
 * ============================================================================ */

typedef struct graph_build_ctx {
    nmo_context_t *ctx;
    nmo_type_registry_t *registry;
    nmo_object_repository_t *repo;
    const nmo_behavior_index_t *beh_index;

    nmo_behavior_graph_node_t *nodes;
    size_t node_count;
    size_t node_cap;

    nmo_behavior_graph_edge_t *edges;
    size_t edge_count;
    size_t edge_cap;

    nmo_object_id_t *parameter_ids;
    size_t parameter_count;
    size_t parameter_cap;

    nmo_object_id_t *visited_ids;
    size_t visited_count;
    size_t visited_cap;
    nmo_object_id_t *active_ids;
    size_t active_count;
    size_t active_cap;

    size_t broken_links;
    size_t missing_nodes;
    size_t cycle_count;
    uint32_t max_depth;
} graph_build_ctx_t;

/* ============================================================================
 * Helpers
 * ============================================================================ */

static bool is_behavior_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) return false;
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? true : false;
}

static bool has_id(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id) {
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) return true;
    }
    return false;
}

static bool add_unique_id(nmo_object_id_t **ids, size_t *count, size_t *cap, nmo_object_id_t id) {
    if (id == 0 || !ids || !count || !cap) return true;
    for (size_t i = 0; i < *count; ++i) {
        if ((*ids)[i] == id) return true;
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_object_id_t *new_ids = (nmo_object_id_t *)realloc(*ids, new_cap * sizeof(*new_ids));
        if (!new_ids) return false;
        *ids = new_ids;
        *cap = new_cap;
    }
    (*ids)[(*count)++] = id;
    return true;
}

static bool add_graph_node(nmo_behavior_graph_node_t **nodes,
                           size_t *count, size_t *cap,
                           nmo_object_id_t id, const char *kind,
                           const char *name, bool owns_name,
                           nmo_class_id_t class_id, const char *class_name) {
    if (!nodes || !count || !cap || id == 0) return true;
    for (size_t i = 0; i < *count; ++i) {
        if ((*nodes)[i].id == id) {
            if (kind && (*nodes)[i].kind && strcmp((*nodes)[i].kind, "unknown") == 0)
                (*nodes)[i].kind = kind;
            if (class_id != 0 && (*nodes)[i].class_id == 0) {
                (*nodes)[i].class_id = class_id;
                (*nodes)[i].class_name = class_name;
            }
            if (name && name[0] && (!(*nodes)[i].name || !(*nodes)[i].name[0])) {
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            } else if (owns_name && name && name[0] && (*nodes)[i].owns_name) {
                free((void *)((*nodes)[i].name));
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            }
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_behavior_graph_node_t *new_nodes =
            (nmo_behavior_graph_node_t *)realloc(*nodes, new_cap * sizeof(*new_nodes));
        if (!new_nodes) return false;
        *nodes = new_nodes;
        *cap = new_cap;
    }
    (*nodes)[*count] = (nmo_behavior_graph_node_t){
        .id = id, .kind = kind, .name = name, .owns_name = owns_name,
        .class_id = class_id, .class_name = class_name,
        .depth = 0, .parent_id = 0,
    };
    (*count)++;
    return true;
}

static bool add_graph_edge(nmo_behavior_graph_edge_t **edges,
                           size_t *count, size_t *cap,
                           nmo_object_id_t link_id,
                           nmo_object_id_t from_id, nmo_object_id_t to_id,
                           const char *kind, const char *field_path,
                           nmo_object_id_t in_io_id, nmo_object_id_t out_io_id,
                           int32_t activation_delay, int32_t initial_activation_delay,
                           bool is_shared) {
    if (!edges || !count || !cap || from_id == 0 || to_id == 0) return true;
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_behavior_graph_edge_t *new_edges =
            (nmo_behavior_graph_edge_t *)realloc(*edges, new_cap * sizeof(*new_edges));
        if (!new_edges) return false;
        *edges = new_edges;
        *cap = new_cap;
    }
    (*edges)[*count] = (nmo_behavior_graph_edge_t){
        .link_id = link_id, .from_id = from_id, .to_id = to_id,
        .kind = kind, .field_path = field_path,
        .in_io_id = in_io_id, .out_io_id = out_io_id,
        .activation_delay = activation_delay,
        .initial_activation_delay = initial_activation_delay,
        .is_shared = is_shared,
    };
    (*count)++;
    return true;
}

static bool add_graph_node_from_object(nmo_behavior_graph_node_t **nodes,
                                       size_t *node_count, size_t *node_cap,
                                       nmo_object_repository_t *repo, nmo_context_t *ctx,
                                       nmo_object_id_t id, const char *kind,
                                       const char *missing_prefix, size_t *missing_count) {
    if (!repo || !ctx || id == 0) return true;
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        if (missing_count) (*missing_count)++;
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%s %u", missing_prefix, id);
        size_t label_len = strlen(label_buf);
        char *label_copy = (char *)malloc(label_len + 1);
        if (!label_copy) return false;
        memcpy(label_copy, label_buf, label_len + 1);
        if (!add_graph_node(nodes, node_count, node_cap, id, kind, label_copy, true, 0, NULL)) {
            free(label_copy);
            return false;
        }
        return true;
    }
    const char *name = nmo_object_get_name(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_type_query_class_name_from_id(nmo_context_get_type_registry(ctx), class_id);
    const char *label = (name && name[0]) ? name : class_name;
    return add_graph_node(nodes, node_count, node_cap, id, kind, label, false, class_id, class_name);
}

static bool add_parameter_edge(nmo_object_id_t **param_ids, size_t *param_count, size_t *param_cap,
                               nmo_behavior_graph_node_t **nodes, size_t *node_count, size_t *node_cap,
                               nmo_behavior_graph_edge_t **edges, size_t *edge_count, size_t *edge_cap,
                               nmo_object_repository_t *repo, nmo_context_t *ctx,
                               nmo_object_id_t param_id, nmo_object_id_t from_id, nmo_object_id_t to_id,
                               const char *edge_kind, const char *field_path,
                               size_t *missing_nodes, bool is_shared) {
    if (!add_unique_id(param_ids, param_count, param_cap, param_id)) return false;
    if (!add_graph_node_from_object(nodes, node_count, node_cap,
                                    repo, ctx, param_id, "parameter", "Param", missing_nodes))
        return false;
    if (!add_graph_edge(edges, edge_count, edge_cap,
                        0, from_id, to_id, edge_kind, field_path,
                        0, 0, 0, 0, is_shared))
        return false;
    return true;
}

static void free_graph_nodes(nmo_behavior_graph_node_t *nodes, size_t count) {
    if (!nodes) return;
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].owns_name)
            free((void *)nodes[i].name);
    }
    free(nodes);
}

/* Set depth/parent on a node by ID (reverse scan 芒鈧€?just-added is at end) */
static void set_node_depth(nmo_behavior_graph_node_t *nodes, size_t count,
                           nmo_object_id_t id, uint32_t depth, nmo_object_id_t parent_id) {
    for (size_t j = count; j > 0; --j) {
        if (nodes[j - 1].id == id) {
            nodes[j - 1].depth = depth;
            nodes[j - 1].parent_id = parent_id;
            return;
        }
    }
}

/* ============================================================================
 * Recursive behavior contents builder
 * ============================================================================ */

#define GRAPH_MAX_RECURSION 256

static bool build_behavior_contents(graph_build_ctx_t *gc,
                                    nmo_object_id_t behavior_id,
                                    const nmo_behavior_state_t *state,
                                    uint32_t depth)
{
    if (depth > GRAPH_MAX_RECURSION) return true; /* guard against corrupt data */
    if (has_id(gc->active_ids, gc->active_count, behavior_id)) {
        gc->cycle_count++;
        return true;
    }
    if (has_id(gc->visited_ids, gc->visited_count, behavior_id)) return true;
    if (!add_unique_id(&gc->visited_ids, &gc->visited_count,
                       &gc->visited_cap, behavior_id) ||
        !add_unique_id(&gc->active_ids, &gc->active_count,
                       &gc->active_cap, behavior_id)) {
        return false;
    }
    /* --- Sub-behaviors as nodes --- */
    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (sub_id == 0) continue;
        if (!add_graph_node_from_object(&gc->nodes, &gc->node_count, &gc->node_cap,
                                        gc->repo, gc->ctx, sub_id, "behavior",
                                        "Behavior", &gc->missing_nodes))
            return false;
        set_node_depth(gc->nodes, gc->node_count, sub_id, depth + 1, behavior_id);
    }

    /* --- Behavior links (control flow) --- */
    for (size_t i = 0; i < state->sub_behavior_links.count; ++i) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
            &state->sub_behavior_links, i);
        if (link_id == 0) continue;

        nmo_object_t *link_obj = nmo_object_repository_find_by_id(gc->repo, link_id);
        const nmo_behaviorlink_state_t *link_state = NULL;
        if (link_obj && nmo_type_query_object_is_derived_from_guid(gc->registry, link_obj, CKPGUID_BEHAVIORLINK)) {
            link_state = (const nmo_behaviorlink_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                gc->registry, link_obj, CKPGUID_BEHAVIORLINK);
        }
        if (!link_state) { gc->broken_links++; continue; }

        const nmo_object_id_t in_io_id =
            nmo_behaviorlink_in_io_id(link_state);
        const nmo_object_id_t out_io_id =
            nmo_behaviorlink_out_io_id(link_state);

        /* O(1) IO owner lookup via behavior_index */
        const nmo_port_owner_t *in_owner = gc->beh_index
            ? nmo_behavior_index_find(gc->beh_index, in_io_id) : NULL;
        const nmo_port_owner_t *out_owner = gc->beh_index
            ? nmo_behavior_index_find(gc->beh_index, out_io_id) : NULL;

        /* Virtools SDK naming: in_io = SOURCE, out_io = TARGET (backwards!)
         * Edge direction: in_owner (source BB) -> out_owner (target BB) */
        if (in_owner && out_owner) {
            if (!add_graph_edge(&gc->edges, &gc->edge_count, &gc->edge_cap, link_id,
                                in_owner->owner_id, out_owner->owner_id,
                                "behavior_link", "sub_behavior_links",
                                in_io_id, out_io_id,
                                link_state->activation_delay,
                                link_state->initial_activation_delay, false))
                return false;
        } else if (in_io_id != 0 && out_io_id != 0) {
            if (!add_graph_node_from_object(&gc->nodes, &gc->node_count, &gc->node_cap,
                                            gc->repo, gc->ctx, in_io_id,
                                            "io", "IO", &gc->missing_nodes))
                return false;
            if (!add_graph_node_from_object(&gc->nodes, &gc->node_count, &gc->node_cap,
                                            gc->repo, gc->ctx, out_io_id,
                                            "io", "IO", &gc->missing_nodes))
                return false;
            if (!add_graph_edge(&gc->edges, &gc->edge_count, &gc->edge_cap, link_id,
                                in_io_id, out_io_id,
                                "io_link", "sub_behavior_links",
                                in_io_id, out_io_id, 0, 0, false))
                return false;
        }
    }

    /* --- Parameters (in/out/local) --- */
    for (size_t i = 0; i < state->in_parameters.count; ++i) {
        nmo_object_id_t parameter_id = nmo_behavior_ref_array_get_id(
            &state->in_parameters, i);
        if (parameter_id == 0) continue;
        if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                &gc->nodes, &gc->node_count, &gc->node_cap,
                                &gc->edges, &gc->edge_count, &gc->edge_cap,
                                gc->repo, gc->ctx, parameter_id, behavior_id, parameter_id,
                                "param_in", "in_parameters", &gc->missing_nodes, false))
            return false;
    }
    for (size_t i = 0; i < state->out_parameters.count; ++i) {
        nmo_object_id_t parameter_id = nmo_behavior_ref_array_get_id(
            &state->out_parameters, i);
        if (parameter_id == 0) continue;
        if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                &gc->nodes, &gc->node_count, &gc->node_cap,
                                &gc->edges, &gc->edge_count, &gc->edge_cap,
                                gc->repo, gc->ctx, parameter_id, behavior_id, parameter_id,
                                "param_out", "out_parameters", &gc->missing_nodes, false))
            return false;
    }
    for (size_t i = 0; i < state->local_parameters.count; ++i) {
        nmo_object_id_t parameter_id = nmo_behavior_ref_array_get_id(
            &state->local_parameters, i);
        if (parameter_id == 0) continue;
        if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                &gc->nodes, &gc->node_count, &gc->node_cap,
                                &gc->edges, &gc->edge_count, &gc->edge_cap,
                                gc->repo, gc->ctx, parameter_id, behavior_id, parameter_id,
                                "param_local", "local_parameters", &gc->missing_nodes, false))
            return false;
    }

    /* --- Operations --- */
    for (size_t i = 0; i < state->operations.count; ++i) {
        nmo_object_id_t op_id = nmo_behavior_ref_array_get_id(&state->operations, i);
        if (op_id == 0) continue;
        if (!add_graph_node_from_object(&gc->nodes, &gc->node_count, &gc->node_cap,
                                        gc->repo, gc->ctx, op_id, "operation",
                                        "Operation", &gc->missing_nodes))
            return false;

        nmo_object_t *op_obj = nmo_object_repository_find_by_id(gc->repo, op_id);
        if (!op_obj || !nmo_type_query_object_is_derived_from_guid(gc->registry, op_obj, CKPGUID_PARAMETEROPERATION))
            continue;

        const nmo_parameteroperation_state_t *op_state =
            (const nmo_parameteroperation_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                gc->registry, op_obj, CKPGUID_PARAMETEROPERATION);
        if (!op_state) continue;

        const nmo_object_id_t in1_id = nmo_parameteroperation_in1_id(op_state);
        const nmo_object_id_t in2_id = nmo_parameteroperation_in2_id(op_state);
        const nmo_object_id_t out_id = nmo_parameteroperation_out_id(op_state);
        if (op_state->has_in1 && in1_id != 0) {
            if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                    &gc->nodes, &gc->node_count, &gc->node_cap,
                                    &gc->edges, &gc->edge_count, &gc->edge_cap,
                                    gc->repo, gc->ctx, in1_id, in1_id, op_id,
                                    "op_in1", "in1_id", &gc->missing_nodes, false))
                return false;
        }
        if (op_state->has_in2 && in2_id != 0) {
            if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                    &gc->nodes, &gc->node_count, &gc->node_cap,
                                    &gc->edges, &gc->edge_count, &gc->edge_cap,
                                    gc->repo, gc->ctx, in2_id, in2_id, op_id,
                                    "op_in2", "in2_id", &gc->missing_nodes, false))
                return false;
        }
        if (op_state->has_out && out_id != 0) {
            if (!add_parameter_edge(&gc->parameter_ids, &gc->parameter_count, &gc->parameter_cap,
                                    &gc->nodes, &gc->node_count, &gc->node_cap,
                                    &gc->edges, &gc->edge_count, &gc->edge_cap,
                                    gc->repo, gc->ctx, out_id, op_id, out_id,
                                    "op_out", "out_id", &gc->missing_nodes, false))
                return false;
        }
    }

    /* --- Recurse into graph-type sub-behaviors --- */
    if (depth < gc->max_depth) {
        for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &state->sub_behaviors, i);
            if (sub_id == 0) continue;

            nmo_object_t *sub_obj = nmo_object_repository_find_by_id(gc->repo, sub_id);
            if (!sub_obj) continue;

            const nmo_behavior_state_t *sub_state =
                (const nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                    gc->registry, sub_obj, CKPGUID_BEHAVIOR);
            if (!sub_state) continue;
            if (sub_state->flags & CKBEHAVIOR_BUILDINGBLOCK) continue;

            if (!build_behavior_contents(gc, sub_id, sub_state, depth + 1))
                return false;
        }
    }

    if (gc->active_count > 0) gc->active_count--;
    return true;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool nmo_behavior_graph_build(nmo_workspace_t *workspace,
                              nmo_object_id_t behavior_id,
                              uint32_t max_depth,
                              nmo_behavior_graph_t *out_graph) {
    nmo_context_t *ctx = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_type_registry_t *registry = NULL;

    if (!workspace || behavior_id == 0 || !out_graph) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                           "Invalid behavior graph arguments");
        return false;
    }

    memset(out_graph, 0, sizeof(*out_graph));

    ctx = nmo_workspace_internal_context(workspace);
    registry = (nmo_type_registry_t *)nmo_workspace_internal_type_registry(workspace);
    repo = nmo_workspace_internal_repository(workspace);
    if (!registry || !repo) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                           "Missing registry or repository");
        return false;
    }

    nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!behavior) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                           "Behavior %u not found", behavior_id);
        return false;
    }
    if (!is_behavior_class(registry, nmo_object_get_class_id(behavior))) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                           "Object %u is not a behavior", behavior_id);
        return false;
    }

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, behavior, CKPGUID_BEHAVIOR);
    if (!state) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                           "Behavior state unavailable");
        return false;
    }

    graph_build_ctx_t gc = {0};
    gc.ctx = ctx;
    gc.registry = registry;
    gc.repo = repo;
    gc.beh_index = nmo_workspace_internal_behavior_index(workspace);
    gc.max_depth = max_depth;

    /* Root behavior node at depth 0 */
    if (!add_graph_node_from_object(&gc.nodes, &gc.node_count, &gc.node_cap,
                                    repo, ctx, behavior_id, "behavior",
                                    "Behavior", &gc.missing_nodes))
        goto fail_nomem;

    if (!build_behavior_contents(&gc, behavior_id, state, 0))
        goto fail_nomem;

    /* --- Parameter source/destination edges (post-pass) --- */
    for (size_t i = 0; i < gc.parameter_count; ++i) {
        nmo_object_id_t param_id = gc.parameter_ids[i];
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
        if (!param_obj) continue;

        if (nmo_type_query_object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETEROUT)) {
            const nmo_parameterout_state_t *out_state =
                (const nmo_parameterout_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETEROUT);
            if (out_state && out_state->destination_ids && out_state->destination_count > 0) {
                for (uint32_t d = 0; d < out_state->destination_count; ++d) {
                    nmo_object_id_t dest_id = out_state->destination_ids[d];
                    if (dest_id == 0) continue;
                    if (!add_parameter_edge(&gc.parameter_ids, &gc.parameter_count, &gc.parameter_cap,
                                            &gc.nodes, &gc.node_count, &gc.node_cap,
                                            &gc.edges, &gc.edge_count, &gc.edge_cap,
                                            repo, ctx, dest_id, param_id, dest_id,
                                            "param_dest", "destination_ids",
                                            &gc.missing_nodes, false))
                        goto fail_nomem;
                }
            }
        }

        if (nmo_type_query_object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETERIN)) {
            const nmo_parameterin_state_t *in_state =
                (const nmo_parameterin_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETERIN);
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(in_state);
            if (source_id != 0) {
                if (!add_parameter_edge(&gc.parameter_ids, &gc.parameter_count, &gc.parameter_cap,
                                        &gc.nodes, &gc.node_count, &gc.node_cap,
                                        &gc.edges, &gc.edge_count, &gc.edge_cap,
                                        repo, ctx, source_id, source_id, param_id,
                                        "param_source", "source_id",
                                        &gc.missing_nodes, in_state->is_shared != 0))
                    goto fail_nomem;
            }
        }
    }

    out_graph->behavior_id = behavior_id;
    out_graph->behavior_name = nmo_object_get_name(behavior);
    out_graph->behavior_class_id = nmo_object_get_class_id(behavior);
    out_graph->behavior_class_name = nmo_type_query_class_name_from_id(gc.registry, out_graph->behavior_class_id);
    out_graph->nodes = gc.nodes;
    out_graph->node_count = gc.node_count;
    out_graph->edges = gc.edges;
    out_graph->edge_count = gc.edge_count;
    out_graph->broken_links = gc.broken_links;
    out_graph->missing_nodes = gc.missing_nodes;
    out_graph->cycle_count = gc.cycle_count;

    free(gc.parameter_ids);
    free(gc.visited_ids);
    free(gc.active_ids);
    nmo_last_error_clear();
    return true;

fail_nomem:
    free(gc.parameter_ids);
    free(gc.visited_ids);
    free(gc.active_ids);
    free(gc.edges);
    free_graph_nodes(gc.nodes, gc.node_count);
    NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                       "Out of memory while building behavior graph");
    return false;
}

void nmo_behavior_graph_free(nmo_behavior_graph_t *graph) {
    if (!graph) return;
    free(graph->edges);
    free_graph_nodes(graph->nodes, graph->node_count);
    memset(graph, 0, sizeof(*graph));
}
