#include "behavior/nmo_behavior_analyze.h"
#include "core/nmo_error.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

#include <stdlib.h>
#include <string.h>

static bool boundary_id_in_set(const nmo_object_id_t *ids,
                               size_t count,
                               nmo_object_id_t id) {
    if (!ids || id == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static bool boundary_copy_internal_nodes(nmo_behavior_boundary_t *boundary,
                                         const nmo_behavior_graph_t *graph) {
    if (graph->node_count == 0) {
        return true;
    }

    boundary->internal_nodes = (nmo_object_id_t *)malloc(
        graph->node_count * sizeof(*boundary->internal_nodes));
    if (!boundary->internal_nodes) {
        return false;
    }

    for (size_t i = 0; i < graph->node_count; ++i) {
        boundary->internal_nodes[i] = graph->nodes[i].id;
    }
    boundary->internal_node_count = graph->node_count;
    return true;
}

static bool boundary_copy_selected_nodes(nmo_behavior_boundary_t *boundary,
                                         const nmo_object_id_t *node_ids,
                                         size_t node_count) {
    if (node_count == 0) {
        return true;
    }

    boundary->internal_nodes = (nmo_object_id_t *)malloc(
        node_count * sizeof(*boundary->internal_nodes));
    if (!boundary->internal_nodes) {
        return false;
    }

    memcpy(boundary->internal_nodes, node_ids,
           node_count * sizeof(*boundary->internal_nodes));
    boundary->internal_node_count = node_count;
    return true;
}

static bool boundary_graph_has_node(const nmo_behavior_graph_t *graph,
                                    nmo_object_id_t node_id) {
    if (!graph || node_id == 0) {
        return false;
    }
    for (size_t i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i].id == node_id) {
            return true;
        }
    }
    return false;
}

static bool boundary_validate_selected_nodes(
    const nmo_behavior_graph_t *graph,
    const nmo_object_id_t *node_ids,
    size_t node_count) {
    if (!node_ids || node_count == 0) {
        return false;
    }
    for (size_t i = 0; i < node_count; ++i) {
        if (!boundary_graph_has_node(graph, node_ids[i])) {
            return false;
        }
    }
    return true;
}

static bool boundary_add_control_edge(
    nmo_behavior_boundary_control_edge_t **edges,
    size_t *count,
    const nmo_behavior_boundary_control_edge_t *edge) {
    if (!edges || !count || !edge) {
        return false;
    }

    size_t new_count = *count + 1;
    nmo_behavior_boundary_control_edge_t *new_edges =
        (nmo_behavior_boundary_control_edge_t *)realloc(
            *edges, new_count * sizeof(*new_edges));
    if (!new_edges) {
        return false;
    }

    new_edges[*count] = *edge;
    *edges = new_edges;
    *count = new_count;
    return true;
}

static bool boundary_add_parameter_edge(
    nmo_behavior_boundary_parameter_edge_t **edges,
    size_t *count,
    const nmo_behavior_boundary_parameter_edge_t *edge) {
    if (!edges || !count || !edge) {
        return false;
    }

    size_t new_count = *count + 1;
    nmo_behavior_boundary_parameter_edge_t *new_edges =
        (nmo_behavior_boundary_parameter_edge_t *)realloc(
            *edges, new_count * sizeof(*new_edges));
    if (!new_edges) {
        return false;
    }

    new_edges[*count] = *edge;
    *edges = new_edges;
    *count = new_count;
    return true;
}

static nmo_guid_t boundary_parameter_type_guid(nmo_object_repository_t *repo,
                                               nmo_object_id_t parameter_id) {
    if (!repo || parameter_id == 0) {
        return (nmo_guid_t){0, 0};
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, parameter_id);
    if (!obj || !obj->state) {
        return (nmo_guid_t){0, 0};
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    if (class_id == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)obj->state;
        return state->type_guid;
    }
    if (class_id == NMO_CID_PARAMETEROUT ||
        class_id == NMO_CID_PARAMETERLOCAL ||
        class_id == NMO_CID_PARAMETER) {
        const nmo_parameter_state_t *state =
            (const nmo_parameter_state_t *)obj->state;
        return state->type_guid;
    }

    return (nmo_guid_t){0, 0};
}

static bool boundary_endpoint_internal(
    const nmo_behavior_boundary_t *boundary,
    nmo_object_id_t owner_id,
    nmo_object_id_t object_id) {
    if (owner_id != 0) {
        return boundary_id_in_set(boundary->internal_nodes,
                                  boundary->internal_node_count,
                                  owner_id);
    }
    return boundary_id_in_set(boundary->internal_nodes,
                              boundary->internal_node_count,
                              object_id);
}

static bool boundary_classify_control_edge(
    nmo_behavior_boundary_t *boundary,
    const nmo_behavior_graph_edge_t *edge) {
    bool source_internal = boundary_endpoint_internal(
        boundary, edge->from_id, edge->from_id);
    bool target_internal = boundary_endpoint_internal(
        boundary, edge->to_id, edge->to_id);

    if (source_internal == target_internal) {
        return true;
    }

    nmo_behavior_boundary_control_edge_t out = {
        .link_id = edge->link_id,
        .source_owner_id = edge->from_id,
        .source_io_id = edge->in_io_id,
        .target_owner_id = edge->to_id,
        .target_io_id = edge->out_io_id,
        .activation_delay = edge->activation_delay,
        .initial_activation_delay = edge->initial_activation_delay,
    };

    if (!source_internal && target_internal) {
        return boundary_add_control_edge(&boundary->control_in,
                                         &boundary->control_in_count,
                                         &out);
    }
    return boundary_add_control_edge(&boundary->control_out,
                                     &boundary->control_out_count,
                                     &out);
}

static bool boundary_classify_parameter_edge(
    nmo_behavior_boundary_t *boundary,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index,
    const nmo_behavior_graph_edge_t *edge) {
    const nmo_port_owner_t *source_owner =
        index ? nmo_behavior_index_find(index, edge->from_id) : NULL;
    const nmo_port_owner_t *target_owner =
        index ? nmo_behavior_index_find(index, edge->to_id) : NULL;

    nmo_object_id_t source_owner_id =
        source_owner ? source_owner->owner_id : 0;
    nmo_object_id_t target_owner_id =
        target_owner ? target_owner->owner_id : 0;

    bool source_internal = boundary_endpoint_internal(
        boundary, source_owner_id, edge->from_id);
    bool target_internal = boundary_endpoint_internal(
        boundary, target_owner_id, edge->to_id);

    if (source_internal == target_internal) {
        return true;
    }

    nmo_guid_t type_guid = boundary_parameter_type_guid(repo, edge->to_id);
    if (nmo_guid_is_null(type_guid)) {
        type_guid = boundary_parameter_type_guid(repo, edge->from_id);
    }

    nmo_behavior_boundary_parameter_edge_t out = {
        .source_parameter_id = edge->from_id,
        .target_parameter_id = edge->to_id,
        .source_owner_id = source_owner_id,
        .target_owner_id = target_owner_id,
        .type_guid = type_guid,
        .shared = edge->is_shared,
    };

    if (!source_internal && target_internal) {
        return boundary_add_parameter_edge(&boundary->parameter_in,
                                           &boundary->parameter_in_count,
                                           &out);
    }
    return boundary_add_parameter_edge(&boundary->parameter_out,
                                       &boundary->parameter_out_count,
                                       &out);
}

static bool boundary_classify_graph_edges(
    nmo_session_t *session,
    const nmo_behavior_graph_t *graph,
    nmo_behavior_boundary_t *boundary) {
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const nmo_behavior_index_t *index = nmo_session_get_behavior_index(session);

    for (size_t i = 0; i < graph->edge_count; ++i) {
        const nmo_behavior_graph_edge_t *edge = &graph->edges[i];
        bool ok = true;

        if (edge->kind && strcmp(edge->kind, "behavior_link") == 0) {
            ok = boundary_classify_control_edge(boundary, edge);
        } else if (edge->kind &&
                   (strcmp(edge->kind, "param_source") == 0 ||
                    strcmp(edge->kind, "param_dest") == 0)) {
            ok = boundary_classify_parameter_edge(boundary, repo,
                                                  index, edge);
        }

        if (!ok) {
            return false;
        }
    }
    return true;
}

bool nmo_behavior_boundary_build(nmo_context_t *ctx,
                                 nmo_session_t *session,
                                 nmo_object_id_t behavior_id,
                                 uint32_t max_depth,
                                 nmo_behavior_boundary_t *out_boundary) {
    if (!ctx || !session || behavior_id == 0 || !out_boundary) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                           "Invalid behavior boundary arguments");
        return false;
    }

    memset(out_boundary, 0, sizeof(*out_boundary));

    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                           "Failed to build behavior acceleration");
        return false;
    }

    nmo_behavior_graph_t graph = {0};
    if (!nmo_behavior_graph_build(ctx, session, behavior_id,
                                  max_depth, &graph)) {
        return false;
    }

    out_boundary->behavior_id = behavior_id;
    out_boundary->broken_links = graph.broken_links;
    out_boundary->missing_nodes = graph.missing_nodes;

    if (!boundary_copy_internal_nodes(out_boundary, &graph)) {
        nmo_behavior_graph_free(&graph);
        nmo_behavior_boundary_free(out_boundary);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Out of memory while building behavior boundary");
        return false;
    }

    if (!boundary_classify_graph_edges(session, &graph, out_boundary)) {
        nmo_behavior_graph_free(&graph);
        nmo_behavior_boundary_free(out_boundary);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Out of memory while classifying boundary edge");
        return false;
    }

    nmo_behavior_graph_free(&graph);
    nmo_last_error_clear();
    return true;
}

bool nmo_behavior_boundary_build_for_nodes(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_boundary_t *out_boundary) {
    if (!ctx || !session || parent_behavior_id == 0 || !node_ids ||
        node_count == 0 || !out_boundary) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                           "Invalid selected behavior boundary arguments");
        return false;
    }

    memset(out_boundary, 0, sizeof(*out_boundary));

    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                           "Failed to build behavior acceleration");
        return false;
    }

    nmo_behavior_graph_t graph = {0};
    if (!nmo_behavior_graph_build(ctx, session, parent_behavior_id,
                                  UINT32_MAX, &graph)) {
        return false;
    }

    if (!boundary_validate_selected_nodes(&graph, node_ids, node_count)) {
        nmo_behavior_graph_free(&graph);
        NMO_SET_LAST_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                           "Selected fold node is not in parent graph");
        return false;
    }

    out_boundary->behavior_id = parent_behavior_id;
    out_boundary->broken_links = graph.broken_links;
    out_boundary->missing_nodes = graph.missing_nodes;

    if (!boundary_copy_selected_nodes(out_boundary, node_ids, node_count)) {
        nmo_behavior_graph_free(&graph);
        nmo_behavior_boundary_free(out_boundary);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Out of memory while building selected behavior boundary");
        return false;
    }

    if (!boundary_classify_graph_edges(session, &graph, out_boundary)) {
        nmo_behavior_graph_free(&graph);
        nmo_behavior_boundary_free(out_boundary);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Out of memory while classifying selected boundary edge");
        return false;
    }

    nmo_behavior_graph_free(&graph);
    nmo_last_error_clear();
    return true;
}

void nmo_behavior_boundary_free(nmo_behavior_boundary_t *boundary) {
    if (!boundary) {
        return;
    }
    free(boundary->internal_nodes);
    free(boundary->control_in);
    free(boundary->control_out);
    free(boundary->parameter_in);
    free(boundary->parameter_out);
    memset(boundary, 0, sizeof(*boundary));
}
