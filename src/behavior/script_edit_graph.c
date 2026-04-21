#include "behavior/nmo_script_edit_graph.h"

#include "behavior/nmo_behavior_graph.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

struct nmo_script_edit_graph {
    nmo_object_id_t root_behavior_id;
    bool edit_ready;
    bool owner_index_available;

    nmo_status_t reference_validation_status;
    size_t broken_reference_count;

    nmo_object_repository_t *repo;
    const nmo_behavior_index_t *behavior_index;

    nmo_script_edit_node_t *nodes;
    size_t node_count;
    size_t node_capacity;

    nmo_script_edit_control_edge_t *control_edges;
    size_t control_edge_count;
    size_t control_edge_capacity;

    nmo_script_edit_data_edge_t *data_edges;
    size_t data_edge_count;
    size_t data_edge_capacity;

    nmo_ref_edge_t *reference_edges;
    size_t reference_edge_count;
};

static char *dup_string(const char *value)
{
    size_t len = 0;
    char *copy = NULL;

    if (!value) {
        return NULL;
    }
    len = strlen(value);
    copy = (char *)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, len + 1u);
    return copy;
}

static bool grow_array(void **items,
                       size_t item_size,
                       size_t *capacity,
                       size_t needed)
{
    void *new_items = NULL;
    size_t new_capacity = 0;

    if (*capacity >= needed) {
        return true;
    }

    new_capacity = (*capacity == 0u) ? 16u : *capacity;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }

    new_items = realloc(*items, new_capacity * item_size);
    if (!new_items) {
        return false;
    }

    *items = new_items;
    *capacity = new_capacity;
    return true;
}

static nmo_script_edit_node_kind_t node_kind_from_behavior_graph(const char *kind)
{
    if (!kind || strcmp(kind, "behavior") == 0) {
        return NMO_SCRIPT_EDIT_NODE_BEHAVIOR;
    }
    if (strcmp(kind, "io") == 0) {
        return NMO_SCRIPT_EDIT_NODE_IO;
    }
    if (strcmp(kind, "parameter") == 0) {
        return NMO_SCRIPT_EDIT_NODE_PARAMETER;
    }
    if (strcmp(kind, "operation") == 0) {
        return NMO_SCRIPT_EDIT_NODE_OPERATION;
    }
    return NMO_SCRIPT_EDIT_NODE_LINK;
}

static nmo_script_edit_node_t *find_node_mut(nmo_script_edit_graph_t *graph,
                                             nmo_object_id_t object_id)
{
    size_t i = 0;

    if (!graph || object_id == 0u) {
        return NULL;
    }

    for (i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i].object_id == object_id) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

static const nmo_script_edit_node_t *find_node(const nmo_script_edit_graph_t *graph,
                                               nmo_object_id_t object_id)
{
    size_t i = 0;

    if (!graph || object_id == 0u) {
        return NULL;
    }

    for (i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i].object_id == object_id) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

static bool add_or_update_node(nmo_script_edit_graph_t *graph,
                               const nmo_script_edit_node_t *node)
{
    nmo_script_edit_node_t *existing = NULL;
    char *name_copy = NULL;

    if (!graph || !node || node->object_id == 0u) {
        return true;
    }

    existing = find_node_mut(graph, node->object_id);
    if (existing) {
        if ((!existing->name || existing->name[0] == '\0') &&
            node->name && node->name[0] != '\0') {
            name_copy = dup_string(node->name);
            if (!name_copy) {
                return false;
            }
            free((void *)existing->name);
            existing->name = name_copy;
        }
        if (existing->class_id == 0u) {
            existing->class_id = node->class_id;
            existing->class_name = node->class_name;
        }
        if (existing->owner_behavior_id == 0u && node->owner_behavior_id != 0u) {
            existing->owner_behavior_id = node->owner_behavior_id;
            existing->owner_slot_index = node->owner_slot_index;
            existing->owner_slot_kind = node->owner_slot_kind;
        }
        if (existing->parent_behavior_id == 0u && node->parent_behavior_id != 0u) {
            existing->parent_behavior_id = node->parent_behavior_id;
        }
        if (node->depth < existing->depth || existing->depth == 0u) {
            existing->depth = node->depth;
        }
        if (existing->kind == NMO_SCRIPT_EDIT_NODE_LINK &&
            node->kind != NMO_SCRIPT_EDIT_NODE_LINK) {
            existing->kind = node->kind;
        }
        return true;
    }

    if (!grow_array((void **)&graph->nodes,
                    sizeof(*graph->nodes),
                    &graph->node_capacity,
                    graph->node_count + 1u)) {
        return false;
    }

    graph->nodes[graph->node_count] = *node;
    if (node->name && node->name[0] != '\0') {
        name_copy = dup_string(node->name);
        if (!name_copy) {
            return false;
        }
        graph->nodes[graph->node_count].name = name_copy;
    } else {
        graph->nodes[graph->node_count].name = NULL;
    }
    ++graph->node_count;
    return true;
}

static bool add_control_edge(nmo_script_edit_graph_t *graph,
                             const nmo_script_edit_control_edge_t *edge)
{
    if (!graph || !edge) {
        return false;
    }

    if (!grow_array((void **)&graph->control_edges,
                    sizeof(*graph->control_edges),
                    &graph->control_edge_capacity,
                    graph->control_edge_count + 1u)) {
        return false;
    }

    graph->control_edges[graph->control_edge_count++] = *edge;
    return true;
}

static bool add_or_merge_data_edge(nmo_script_edit_graph_t *graph,
                                   const nmo_script_edit_data_edge_t *edge)
{
    size_t i = 0;

    if (!graph || !edge) {
        return false;
    }

    for (i = 0; i < graph->data_edge_count; ++i) {
        nmo_script_edit_data_edge_t *existing = &graph->data_edges[i];
        if (existing->source_parameter_id == edge->source_parameter_id &&
            existing->target_parameter_id == edge->target_parameter_id) {
            if (nmo_guid_is_null(existing->type_guid)) {
                existing->type_guid = edge->type_guid;
            }
            existing->shared = existing->shared || edge->shared;
            if (existing->source_owner_id == 0u) {
                existing->source_owner_id = edge->source_owner_id;
            }
            if (existing->target_owner_id == 0u) {
                existing->target_owner_id = edge->target_owner_id;
            }
            return true;
        }
    }

    if (!grow_array((void **)&graph->data_edges,
                    sizeof(*graph->data_edges),
                    &graph->data_edge_capacity,
                    graph->data_edge_count + 1u)) {
        return false;
    }

    graph->data_edges[graph->data_edge_count++] = *edge;
    return true;
}

static bool copy_reference_edges(nmo_script_edit_graph_t *graph,
                                 nmo_ref_graph_t *ref_graph)
{
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;

    if (nmo_ref_graph_get_edges(ref_graph, &edges, &edge_count) != NMO_OK) {
        return false;
    }
    if (edge_count == 0u) {
        return true;
    }

    graph->reference_edges = (nmo_ref_edge_t *)malloc(edge_count * sizeof(*graph->reference_edges));
    if (!graph->reference_edges) {
        return false;
    }
    memcpy(graph->reference_edges, edges, edge_count * sizeof(*graph->reference_edges));
    graph->reference_edge_count = edge_count;
    return true;
}

static bool populate_owner_from_index(const nmo_behavior_index_t *index,
                                      nmo_object_id_t object_id,
                                      nmo_script_edit_endpoint_t *endpoint)
{
    const nmo_port_owner_t *owner = NULL;

    if (!endpoint) {
        return false;
    }

    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->object_id = object_id;
    endpoint->owner_index = -1;

    if (!index || object_id == 0u) {
        return false;
    }

    owner = nmo_behavior_index_find(index, object_id);
    if (!owner) {
        return false;
    }

    endpoint->owner_behavior_id = owner->owner_id;
    endpoint->owner_index = owner->index;
    endpoint->kind = (uint32_t)owner->kind;
    return true;
}

static void apply_owner_to_node(nmo_script_edit_graph_t *graph,
                                nmo_object_id_t object_id,
                                const nmo_script_edit_endpoint_t *owner)
{
    nmo_script_edit_node_t *node = find_node_mut(graph, object_id);
    if (!node || !owner) {
        return;
    }
    node->owner_behavior_id = owner->owner_behavior_id;
    node->owner_slot_index = owner->owner_index;
    node->owner_slot_kind = owner->kind;
}

static const nmo_behavior_state_t *get_behavior_state(nmo_object_repository_t *repo,
                                                      nmo_object_id_t behavior_id)
{
    nmo_object_t *object = NULL;

    if (!repo || behavior_id == 0u) {
        return NULL;
    }

    object = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!object) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_guid_t get_parameter_type_guid(nmo_object_repository_t *repo,
                                          nmo_object_id_t parameter_id)
{
    nmo_object_t *object = NULL;

    if (!repo || parameter_id == 0u) {
        return (nmo_guid_t){0u, 0u};
    }

    object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (!object) {
        return (nmo_guid_t){0u, 0u};
    }

    switch (nmo_object_get_class_id(object)) {
    case NMO_CID_PARAMETERIN:
        return ((const nmo_parameterin_state_t *)nmo_object_get_state(object))->type_guid;
    case NMO_CID_PARAMETEROUT:
    case NMO_CID_PARAMETERLOCAL:
    case NMO_CID_PARAMETER:
        return ((const nmo_parameter_state_t *)nmo_object_get_state(object))->type_guid;
    default:
        return (nmo_guid_t){0u, 0u};
    }
}

static bool add_owned_io_nodes(nmo_script_edit_graph_t *graph,
                               const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->node_count; ++i) {
        const nmo_behavior_graph_node_t *behavior_node = &behavior_graph->nodes[i];
        const nmo_behavior_state_t *state = NULL;
        const nmo_object_id_t *ids = NULL;
        size_t count = 0;
        size_t slot = 0;

        if (!behavior_node->kind ||
            strcmp(behavior_node->kind, "behavior") != 0) {
            continue;
        }

        state = get_behavior_state(graph->repo, behavior_node->id);
        if (!state) {
            continue;
        }

        ids = (const nmo_object_id_t *)state->inputs.data;
        count = state->inputs.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = ids[slot],
                .kind = NMO_SCRIPT_EDIT_NODE_IO,
                .depth = behavior_node->depth + 1u,
                .parent_behavior_id = behavior_node->id,
                .owner_behavior_id = behavior_node->id,
                .owner_slot_index = (int32_t)slot,
                .owner_slot_kind = (uint32_t)NMO_PORT_IO_IN,
            };
            if (!add_or_update_node(graph, &node)) {
                return false;
            }
        }

        ids = (const nmo_object_id_t *)state->outputs.data;
        count = state->outputs.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = ids[slot],
                .kind = NMO_SCRIPT_EDIT_NODE_IO,
                .depth = behavior_node->depth + 1u,
                .parent_behavior_id = behavior_node->id,
                .owner_behavior_id = behavior_node->id,
                .owner_slot_index = (int32_t)slot,
                .owner_slot_kind = (uint32_t)NMO_PORT_IO_OUT,
            };
            if (!add_or_update_node(graph, &node)) {
                return false;
            }
        }

        ids = (const nmo_object_id_t *)state->sub_behavior_links.data;
        count = state->sub_behavior_links.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = ids[slot],
                .kind = NMO_SCRIPT_EDIT_NODE_LINK,
                .depth = behavior_node->depth + 1u,
                .parent_behavior_id = behavior_node->id,
                .owner_behavior_id = behavior_node->id,
                .owner_slot_index = (int32_t)slot,
                .owner_slot_kind = (uint32_t)NMO_PORT_SUB_LINK,
            };
            if (!add_or_update_node(graph, &node)) {
                return false;
            }
        }
    }

    return true;
}

static bool copy_behavior_graph_nodes(nmo_script_edit_graph_t *graph,
                                      const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->node_count; ++i) {
        const nmo_behavior_graph_node_t *src = &behavior_graph->nodes[i];
        nmo_script_edit_node_t node = {
            .object_id = src->id,
            .kind = node_kind_from_behavior_graph(src->kind),
            .name = src->name,
            .class_id = src->class_id,
            .class_name = src->class_name,
            .depth = src->depth,
            .parent_behavior_id = src->parent_id,
            .owner_slot_index = -1,
        };
        nmo_script_edit_endpoint_t owner = {0};

        if (!add_or_update_node(graph, &node)) {
            return false;
        }

        if (populate_owner_from_index(graph->behavior_index, src->id, &owner)) {
            apply_owner_to_node(graph, src->id, &owner);
        } else if (node.kind == NMO_SCRIPT_EDIT_NODE_BEHAVIOR &&
                   src->parent_id != 0u) {
            nmo_script_edit_node_t *existing = find_node_mut(graph, src->id);
            if (existing) {
                existing->owner_behavior_id = src->parent_id;
                existing->owner_slot_index = -1;
                existing->owner_slot_kind = (uint32_t)NMO_PORT_SUB_BEHAVIOR;
            }
        }
    }

    return true;
}

static void apply_owner_to_node_if_missing(nmo_script_edit_graph_t *graph,
                                           nmo_object_id_t object_id,
                                           nmo_object_id_t owner_behavior_id,
                                           uint32_t owner_slot_kind)
{
    nmo_script_edit_node_t *node = find_node_mut(graph, object_id);
    if (!node || node->owner_behavior_id != 0u) {
        return;
    }
    node->owner_behavior_id = owner_behavior_id;
    node->owner_slot_index = -1;
    node->owner_slot_kind = owner_slot_kind;
}

static void derive_parameter_owners_from_behavior_edges(
    nmo_script_edit_graph_t *graph,
    const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->edge_count; ++i) {
        const nmo_behavior_graph_edge_t *edge = &behavior_graph->edges[i];
        const nmo_script_edit_node_t *operation_node = NULL;

        if (!edge->kind) {
            continue;
        }

        if (strcmp(edge->kind, "param_in") == 0 ||
            strcmp(edge->kind, "param_out") == 0 ||
            strcmp(edge->kind, "param_local") == 0) {
            apply_owner_to_node_if_missing(graph, edge->to_id, edge->from_id,
                                           (uint32_t)NMO_PORT_PARAM_LOCAL);
            continue;
        }

        if (strcmp(edge->kind, "op_in1") == 0 ||
            strcmp(edge->kind, "op_in2") == 0) {
            operation_node = find_node(graph, edge->to_id);
            if (operation_node && operation_node->owner_behavior_id != 0u) {
                apply_owner_to_node_if_missing(graph, edge->from_id,
                                               operation_node->owner_behavior_id,
                                               (uint32_t)NMO_PORT_OPERATION);
            }
            continue;
        }

        if (strcmp(edge->kind, "op_out") == 0) {
            operation_node = find_node(graph, edge->from_id);
            if (operation_node && operation_node->owner_behavior_id != 0u) {
                apply_owner_to_node_if_missing(graph, edge->to_id,
                                               operation_node->owner_behavior_id,
                                               (uint32_t)NMO_PORT_OPERATION);
            }
        }
    }
}

static bool copy_control_edges(nmo_script_edit_graph_t *graph,
                               const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->edge_count; ++i) {
        const nmo_behavior_graph_edge_t *src = &behavior_graph->edges[i];
        nmo_script_edit_control_edge_t edge = {0};
        bool have_source = false;
        bool have_target = false;
        nmo_script_edit_node_t link_node = {0};

        if (!src->kind || strcmp(src->kind, "behavior_link") != 0) {
            continue;
        }

        have_source = populate_owner_from_index(graph->behavior_index,
                                                src->in_io_id,
                                                &edge.source);
        have_target = populate_owner_from_index(graph->behavior_index,
                                                src->out_io_id,
                                                &edge.target);
        edge.link_id = src->link_id;
        edge.activation_delay = src->activation_delay;
        edge.initial_activation_delay = src->initial_activation_delay;

        if (!have_source || !have_target) {
            graph->edit_ready = false;
        }

        if (!add_control_edge(graph, &edge)) {
            return false;
        }

        apply_owner_to_node(graph, edge.source.object_id, &edge.source);
        apply_owner_to_node(graph, edge.target.object_id, &edge.target);

        link_node.object_id = src->link_id;
        link_node.kind = NMO_SCRIPT_EDIT_NODE_LINK;
        link_node.depth = 0u;
        link_node.parent_behavior_id = edge.source.owner_behavior_id;
        link_node.owner_behavior_id = edge.source.owner_behavior_id;
        link_node.owner_slot_index = -1;
        link_node.owner_slot_kind = (uint32_t)NMO_PORT_SUB_LINK;
        if (!add_or_update_node(graph, &link_node)) {
            return false;
        }
    }

    return true;
}

static bool apply_parameter_owner_fallback(const nmo_script_edit_graph_t *graph,
                                           nmo_object_id_t parameter_id,
                                           nmo_object_id_t *out_owner_id)
{
    const nmo_script_edit_node_t *node = find_node(graph, parameter_id);
    if (!node || node->owner_behavior_id == 0u) {
        return false;
    }
    if (out_owner_id) {
        *out_owner_id = node->owner_behavior_id;
    }
    return true;
}

static bool copy_data_edges(nmo_script_edit_graph_t *graph,
                            const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->edge_count; ++i) {
        const nmo_behavior_graph_edge_t *src = &behavior_graph->edges[i];
        nmo_script_edit_data_edge_t edge = {0};
        nmo_script_edit_endpoint_t owner = {0};

        if (!src->kind ||
            (strcmp(src->kind, "param_source") != 0 &&
             strcmp(src->kind, "param_dest") != 0)) {
            continue;
        }

        edge.source_parameter_id = src->from_id;
        edge.target_parameter_id = src->to_id;
        edge.shared = src->is_shared;
        edge.type_guid = get_parameter_type_guid(graph->repo, src->to_id);

        if (populate_owner_from_index(graph->behavior_index,
                                      edge.source_parameter_id, &owner)) {
            edge.source_owner_id = owner.owner_behavior_id;
            apply_owner_to_node(graph, edge.source_parameter_id, &owner);
        } else if (!apply_parameter_owner_fallback(graph,
                                                   edge.source_parameter_id,
                                                   &edge.source_owner_id)) {
            graph->edit_ready = false;
        } else {
            apply_owner_to_node_if_missing(graph, edge.source_parameter_id,
                                           edge.source_owner_id,
                                           (uint32_t)NMO_PORT_OPERATION);
        }

        if (populate_owner_from_index(graph->behavior_index,
                                      edge.target_parameter_id, &owner)) {
            edge.target_owner_id = owner.owner_behavior_id;
            apply_owner_to_node(graph, edge.target_parameter_id, &owner);
        } else if (!apply_parameter_owner_fallback(graph,
                                                   edge.target_parameter_id,
                                                   &edge.target_owner_id)) {
            graph->edit_ready = false;
        } else {
            apply_owner_to_node_if_missing(graph, edge.target_parameter_id,
                                           edge.target_owner_id,
                                           (uint32_t)NMO_PORT_OPERATION);
        }

        if (!add_or_merge_data_edge(graph, &edge)) {
            return false;
        }
    }

    return true;
}

static nmo_status_t copy_filtered_control_edges(const nmo_script_edit_graph_t *graph,
                                                nmo_object_id_t behavior_id,
                                                bool incoming,
                                                nmo_arena_t *arena,
                                                const nmo_script_edit_control_edge_t **out_edges,
                                                size_t *out_count)
{
    nmo_script_edit_control_edge_t *edges = NULL;
    size_t count = 0;
    size_t i = 0;

    if (!graph || !arena || !out_edges || !out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid control edge query");
    }

    for (i = 0; i < graph->control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &graph->control_edges[i];
        bool match = incoming
            ? edge->target.owner_behavior_id == behavior_id
            : edge->source.owner_behavior_id == behavior_id;
        if (match) {
            ++count;
        }
    }

    if (count == 0u) {
        *out_edges = NULL;
        *out_count = 0u;
        NMO_RETURN_OK();
    }

    edges = (nmo_script_edit_control_edge_t *)nmo_arena_alloc(
        arena, count * sizeof(*edges), _Alignof(nmo_script_edit_control_edge_t));
    if (!edges) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate control edge query result");
    }

    count = 0;
    for (i = 0; i < graph->control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &graph->control_edges[i];
        bool match = incoming
            ? edge->target.owner_behavior_id == behavior_id
            : edge->source.owner_behavior_id == behavior_id;
        if (match) {
            edges[count++] = *edge;
        }
    }

    *out_edges = edges;
    *out_count = count;
    NMO_RETURN_OK();
}

static nmo_status_t copy_filtered_data_edges(const nmo_script_edit_graph_t *graph,
                                             nmo_object_id_t parameter_id,
                                             bool sources,
                                             nmo_arena_t *arena,
                                             const nmo_script_edit_data_edge_t **out_edges,
                                             size_t *out_count)
{
    nmo_script_edit_data_edge_t *edges = NULL;
    size_t count = 0;
    size_t i = 0;

    if (!graph || !arena || !out_edges || !out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid data edge query");
    }

    for (i = 0; i < graph->data_edge_count; ++i) {
        const nmo_script_edit_data_edge_t *edge = &graph->data_edges[i];
        bool match = sources
            ? edge->target_parameter_id == parameter_id
            : edge->source_parameter_id == parameter_id;
        if (match) {
            ++count;
        }
    }

    if (count == 0u) {
        *out_edges = NULL;
        *out_count = 0u;
        NMO_RETURN_OK();
    }

    edges = (nmo_script_edit_data_edge_t *)nmo_arena_alloc(
        arena, count * sizeof(*edges), _Alignof(nmo_script_edit_data_edge_t));
    if (!edges) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate data edge query result");
    }

    count = 0u;
    for (i = 0; i < graph->data_edge_count; ++i) {
        const nmo_script_edit_data_edge_t *edge = &graph->data_edges[i];
        bool match = sources
            ? edge->target_parameter_id == parameter_id
            : edge->source_parameter_id == parameter_id;
        if (match) {
            edges[count++] = *edge;
        }
    }

    *out_edges = edges;
    *out_count = count;
    NMO_RETURN_OK();
}

static bool node_is_in_graph(const nmo_script_edit_graph_t *graph,
                             nmo_object_id_t object_id)
{
    return find_node(graph, object_id) != NULL;
}

NMO_API nmo_status_t nmo_script_edit_graph_build(nmo_context_t *ctx,
                                                 nmo_session_t *session,
                                                 nmo_object_id_t root_behavior_id,
                                                 uint32_t max_depth,
                                                 nmo_script_edit_graph_t **out_graph)
{
    nmo_behavior_graph_t behavior_graph = {0};
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_arena_t *arena = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    size_t broken_edge_count = 0;
    nmo_status_t ref_status = NMO_OK;

    if (!ctx || !session || root_behavior_id == 0u || !out_graph) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid script edit graph arguments");
    }

    *out_graph = NULL;

    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        return nmo_last_error_code();
    }

    repo = nmo_session_get_repository(session);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "session repository unavailable");
    }

    graph = (nmo_script_edit_graph_t *)calloc(1u, sizeof(*graph));
    if (!graph) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate script edit graph");
    }

    graph->root_behavior_id = root_behavior_id;
    graph->repo = repo;
    graph->behavior_index = nmo_session_get_behavior_index(session);
    graph->owner_index_available = graph->behavior_index != NULL;
    graph->edit_ready = graph->owner_index_available;

    if (!nmo_behavior_graph_build(ctx, session, root_behavior_id,
                                  max_depth, &behavior_graph)) {
        free(graph);
        return (nmo_status_t)nmo_last_error_code();
    }

    if (!copy_behavior_graph_nodes(graph, &behavior_graph) ||
        !add_owned_io_nodes(graph, &behavior_graph) ||
        !copy_control_edges(graph, &behavior_graph)) {
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to populate script edit graph");
    }

    derive_parameter_owners_from_behavior_edges(graph, &behavior_graph);

    if (!copy_data_edges(graph, &behavior_graph)) {
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to populate script edit graph");
    }

    arena = nmo_arena_create(NULL, 64u * 1024u);
    if (!arena) {
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate reference graph arena");
    }

    ref_graph = nmo_ref_graph_create(repo, nmo_context_get_type_registry(ctx), arena);
    if (!ref_graph) {
        nmo_arena_destroy(arena);
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to create reference graph");
    }

    if (!copy_reference_edges(graph, ref_graph)) {
        nmo_arena_destroy(arena);
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy reference graph edges");
    }

    ref_status = nmo_ref_graph_validate(ref_graph, NULL, &broken_edge_count);
    graph->reference_validation_status = ref_status;
    graph->broken_reference_count = broken_edge_count;
    if (ref_status != NMO_OK) {
        graph->edit_ready = false;
    }

    nmo_arena_destroy(arena);
    nmo_behavior_graph_free(&behavior_graph);
    *out_graph = graph;
    NMO_RETURN_OK();
}

NMO_API void nmo_script_edit_graph_destroy(nmo_script_edit_graph_t *graph)
{
    size_t i = 0;

    if (!graph) {
        return;
    }

    for (i = 0; i < graph->node_count; ++i) {
        free((void *)graph->nodes[i].name);
    }
    free(graph->nodes);
    free(graph->control_edges);
    free(graph->data_edges);
    free(graph->reference_edges);
    free(graph);
}

NMO_API nmo_object_id_t nmo_script_edit_graph_root_behavior_id(
    const nmo_script_edit_graph_t *graph)
{
    return graph ? graph->root_behavior_id : 0u;
}

NMO_API bool nmo_script_edit_graph_edit_ready(
    const nmo_script_edit_graph_t *graph)
{
    return graph ? graph->edit_ready : false;
}

NMO_API bool nmo_script_edit_graph_owner_index_available(
    const nmo_script_edit_graph_t *graph)
{
    return graph ? graph->owner_index_available : false;
}

NMO_API size_t nmo_script_edit_graph_node_count(
    const nmo_script_edit_graph_t *graph)
{
    return graph ? graph->node_count : 0u;
}

NMO_API const nmo_script_edit_node_t *nmo_script_edit_graph_nodes(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count)
{
    if (out_count) {
        *out_count = graph ? graph->node_count : 0u;
    }
    return graph ? graph->nodes : NULL;
}

NMO_API const nmo_script_edit_control_edge_t *nmo_script_edit_graph_control_edges(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count)
{
    if (out_count) {
        *out_count = graph ? graph->control_edge_count : 0u;
    }
    return graph ? graph->control_edges : NULL;
}

NMO_API const nmo_script_edit_data_edge_t *nmo_script_edit_graph_data_edges(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count)
{
    if (out_count) {
        *out_count = graph ? graph->data_edge_count : 0u;
    }
    return graph ? graph->data_edges : NULL;
}

NMO_API nmo_status_t nmo_script_edit_graph_reference_validation_status(
    const nmo_script_edit_graph_t *graph,
    size_t *out_broken_count)
{
    if (!graph) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph is NULL");
    }
    if (out_broken_count) {
        *out_broken_count = graph->broken_reference_count;
    }
    return graph->reference_validation_status;
}

NMO_API nmo_status_t nmo_script_edit_graph_find_owner(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_script_edit_endpoint_t *out_owner)
{
    const nmo_script_edit_node_t *node = NULL;
    nmo_script_edit_endpoint_t owner = {0};

    if (!graph || object_id == 0u || !out_owner) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid owner lookup");
    }

    if (populate_owner_from_index(graph->behavior_index, object_id, &owner)) {
        *out_owner = owner;
        NMO_RETURN_OK();
    }

    node = find_node(graph, object_id);
    if (!node) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "object %u not found in script edit graph", object_id);
    }

    if (node->owner_behavior_id == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "object %u has no behavior owner", object_id);
    }

    out_owner->object_id = object_id;
    out_owner->owner_behavior_id = node->owner_behavior_id;
    out_owner->owner_index = node->owner_slot_index;
    out_owner->kind = node->owner_slot_kind;
    NMO_RETURN_OK();
}

NMO_API nmo_status_t nmo_script_edit_graph_get_incoming_control(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t behavior_id,
    nmo_arena_t *arena,
    const nmo_script_edit_control_edge_t **out_edges,
    size_t *out_count)
{
    return copy_filtered_control_edges(graph, behavior_id, true, arena,
                                       out_edges, out_count);
}

NMO_API nmo_status_t nmo_script_edit_graph_get_outgoing_control(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t behavior_id,
    nmo_arena_t *arena,
    const nmo_script_edit_control_edge_t **out_edges,
    size_t *out_count)
{
    return copy_filtered_control_edges(graph, behavior_id, false, arena,
                                       out_edges, out_count);
}

NMO_API nmo_status_t nmo_script_edit_graph_get_parameter_sources(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t parameter_id,
    nmo_arena_t *arena,
    const nmo_script_edit_data_edge_t **out_edges,
    size_t *out_count)
{
    return copy_filtered_data_edges(graph, parameter_id, true, arena,
                                    out_edges, out_count);
}

NMO_API nmo_status_t nmo_script_edit_graph_get_parameter_destinations(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t parameter_id,
    nmo_arena_t *arena,
    const nmo_script_edit_data_edge_t **out_edges,
    size_t *out_count)
{
    return copy_filtered_data_edges(graph, parameter_id, false, arena,
                                    out_edges, out_count);
}

NMO_API nmo_status_t nmo_script_edit_graph_get_external_refs(
    const nmo_script_edit_graph_t *graph,
    nmo_arena_t *arena,
    const nmo_ref_edge_t **out_edges,
    size_t *out_count)
{
    nmo_ref_edge_t *edges = NULL;
    size_t count = 0;
    size_t i = 0;

    if (!graph || !arena || !out_edges || !out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid external reference query");
    }

    for (i = 0; i < graph->reference_edge_count; ++i) {
        bool from_in_graph = node_is_in_graph(graph, graph->reference_edges[i].from);
        bool to_in_graph = node_is_in_graph(graph, graph->reference_edges[i].to);
        if (from_in_graph != to_in_graph) {
            ++count;
        }
    }

    if (count == 0u) {
        *out_edges = NULL;
        *out_count = 0u;
        NMO_RETURN_OK();
    }

    edges = (nmo_ref_edge_t *)nmo_arena_alloc(
        arena, count * sizeof(*edges), _Alignof(nmo_ref_edge_t));
    if (!edges) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate external reference result");
    }

    count = 0u;
    for (i = 0; i < graph->reference_edge_count; ++i) {
        bool from_in_graph = node_is_in_graph(graph, graph->reference_edges[i].from);
        bool to_in_graph = node_is_in_graph(graph, graph->reference_edges[i].to);
        if (from_in_graph != to_in_graph) {
            edges[count++] = graph->reference_edges[i];
        }
    }

    *out_edges = edges;
    *out_count = count;
    NMO_RETURN_OK();
}

NMO_API nmo_status_t nmo_script_edit_graph_resolve_handle(
    const nmo_script_edit_graph_t *graph,
    const nmo_script_edit_handle_t *handle,
    nmo_object_id_t *out_object_id)
{
    const nmo_behavior_state_t *behavior = NULL;
    const nmo_object_id_t *ids = NULL;
    size_t count = 0;

    if (!graph || !handle || !out_object_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid handle resolution");
    }

    switch (handle->kind) {
    case NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID:
        if (handle->object_id == 0u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "object handle must contain a non-zero object id");
        }
        *out_object_id = handle->object_id;
        NMO_RETURN_OK();
    case NMO_SCRIPT_EDIT_HANDLE_SLOT:
        break;
    case NMO_SCRIPT_EDIT_HANDLE_ALIAS:
    case NMO_SCRIPT_EDIT_HANDLE_QUERY:
        NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                         "handle kind is reserved for later tasks");
    default:
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unknown handle kind");
    }

    behavior = get_behavior_state(graph->repo, handle->owner_id);
    if (!behavior) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "owner behavior %u not found", handle->owner_id);
    }
    if (handle->slot_index < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "slot handle index must be non-negative");
    }

    switch ((nmo_port_kind_t)handle->slot_kind) {
    case NMO_PORT_IO_IN:
        ids = (const nmo_object_id_t *)behavior->inputs.data;
        count = behavior->inputs.count;
        break;
    case NMO_PORT_IO_OUT:
        ids = (const nmo_object_id_t *)behavior->outputs.data;
        count = behavior->outputs.count;
        break;
    case NMO_PORT_PARAM_IN:
        ids = (const nmo_object_id_t *)behavior->in_parameters.data;
        count = behavior->in_parameters.count;
        break;
    case NMO_PORT_PARAM_OUT:
        ids = (const nmo_object_id_t *)behavior->out_parameters.data;
        count = behavior->out_parameters.count;
        break;
    case NMO_PORT_PARAM_LOCAL:
        ids = (const nmo_object_id_t *)behavior->local_parameters.data;
        count = behavior->local_parameters.count;
        break;
    case NMO_PORT_OPERATION:
        ids = (const nmo_object_id_t *)behavior->operations.data;
        count = behavior->operations.count;
        break;
    case NMO_PORT_SUB_BEHAVIOR:
        ids = (const nmo_object_id_t *)behavior->sub_behaviors.data;
        count = behavior->sub_behaviors.count;
        break;
    case NMO_PORT_SUB_LINK:
        ids = (const nmo_object_id_t *)behavior->sub_behavior_links.data;
        count = behavior->sub_behavior_links.count;
        break;
    default:
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unsupported slot handle kind");
    }

    if ((size_t)handle->slot_index >= count) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "slot index %d is out of range", handle->slot_index);
    }

    *out_object_id = ids[handle->slot_index];
    NMO_RETURN_OK();
}

NMO_API nmo_status_t nmo_script_edit_graph_validate_operation(
    const nmo_script_edit_graph_t *graph,
    const nmo_script_edit_op_t *op)
{
    nmo_object_id_t resolved = 0;

    if (!graph || !op) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid script edit operation");
    }

    switch (op->kind) {
    case NMO_SCRIPT_EDIT_OP_NODE_ADD:
    case NMO_SCRIPT_EDIT_OP_VALIDATE:
        NMO_RETURN_OK();
    default:
        break;
    }

    return nmo_script_edit_graph_resolve_handle(graph, &op->primary, &resolved);
}
