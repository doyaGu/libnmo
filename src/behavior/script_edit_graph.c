#include "behavior/nmo_script_edit_graph.h"

#include "behavior/nmo_behavior_analyze.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_query.h"

#include "../runtime/runtime_internal.h"
#include "behavior_internal.h"

#include <stdlib.h>
#include <string.h>

struct nmo_script_edit_graph {
    nmo_object_id_t root_behavior_id;
    bool edit_ready;
    bool owner_index_available;

    nmo_status_t reference_validation_status;
    size_t broken_reference_count;

    nmo_object_repository_t *repo;
    const nmo_type_registry_t *type_registry;
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

static const nmo_behavior_state_t *get_behavior_state(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t behavior_id)
{
    nmo_object_t *object = NULL;

    if (!graph || !graph->repo || !graph->type_registry || behavior_id == 0u) {
        return NULL;
    }

    object = nmo_object_repository_find_by_id(graph->repo, behavior_id);
    if (!object) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            graph->type_registry, object, CKPGUID_BEHAVIOR);
}

static nmo_guid_t get_parameter_type_guid(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t parameter_id)
{
    nmo_object_t *object = NULL;

    if (!graph || !graph->repo || !graph->type_registry || parameter_id == 0u) {
        return (nmo_guid_t){0u, 0u};
    }

    object = nmo_object_repository_find_by_id(graph->repo, parameter_id);
    if (!object) {
        return (nmo_guid_t){0u, 0u};
    }

    if (nmo_type_query_object_is_derived_from_class(
            graph->type_registry, object, NMO_CID_PARAMETERIN)) {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    graph->type_registry, object, CKPGUID_PARAMETERIN);
        return state ? state->type_guid : (nmo_guid_t){0u, 0u};
    }
    if (nmo_type_query_object_is_derived_from_class(
            graph->type_registry, object, NMO_CID_PARAMETER)) {
        const nmo_parameter_state_t *state =
            (const nmo_parameter_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    graph->type_registry, object, CKPGUID_PARAMETER);
        return state ? state->type_guid : (nmo_guid_t){0u, 0u};
    }
    return (nmo_guid_t){0u, 0u};
}

static bool add_owned_io_nodes(nmo_script_edit_graph_t *graph,
                               const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->node_count; ++i) {
        const nmo_behavior_graph_node_t *behavior_node = &behavior_graph->nodes[i];
        const nmo_behavior_state_t *state = NULL;
        const nmo_array_t *array = NULL;
        size_t count = 0;
        size_t slot = 0;

        if (!behavior_node->kind ||
            strcmp(behavior_node->kind, "behavior") != 0) {
            continue;
        }

        state = get_behavior_state(graph, behavior_node->id);
        if (!state) {
            continue;
        }

        array = &state->inputs;
        count = state->inputs.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = nmo_behavior_ref_array_get_id(array, slot),
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

        array = &state->outputs;
        count = state->outputs.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = nmo_behavior_ref_array_get_id(array, slot),
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

        array = &state->sub_behavior_links;
        count = state->sub_behavior_links.count;
        for (slot = 0; slot < count; ++slot) {
            nmo_script_edit_node_t node = {
                .object_id = nmo_behavior_ref_array_get_id(array, slot),
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

static bool add_owned_parameter_nodes(nmo_script_edit_graph_t *graph,
                                      const nmo_behavior_graph_t *behavior_graph)
{
    size_t i = 0;

    for (i = 0; i < behavior_graph->node_count; ++i) {
        const nmo_behavior_graph_node_t *behavior_node = &behavior_graph->nodes[i];
        const nmo_behavior_state_t *state = NULL;
        const nmo_array_t *array = NULL;
        size_t count = 0;
        size_t slot = 0;
        nmo_port_kind_t kind = NMO_PORT_PARAM_IN;

        if (!behavior_node->kind ||
            strcmp(behavior_node->kind, "behavior") != 0) {
            continue;
        }

        state = get_behavior_state(graph, behavior_node->id);
        if (!state) {
            continue;
        }

        for (kind = NMO_PORT_PARAM_IN; kind <= NMO_PORT_PARAM_LOCAL; ++kind) {
            switch (kind) {
            case NMO_PORT_PARAM_IN:
                array = &state->in_parameters;
                count = state->in_parameters.count;
                break;
            case NMO_PORT_PARAM_OUT:
                array = &state->out_parameters;
                count = state->out_parameters.count;
                break;
            case NMO_PORT_PARAM_LOCAL:
                array = &state->local_parameters;
                count = state->local_parameters.count;
                break;
            default:
                array = NULL;
                count = 0u;
                break;
            }

            for (slot = 0; slot < count; ++slot) {
                nmo_object_t *object = NULL;
                nmo_script_edit_node_t node = {
                    .object_id = nmo_behavior_ref_array_get_id(array, slot),
                    .kind = NMO_SCRIPT_EDIT_NODE_PARAMETER,
                    .depth = behavior_node->depth + 1u,
                    .parent_behavior_id = behavior_node->id,
                    .owner_behavior_id = behavior_node->id,
                    .owner_slot_index = (int32_t)slot,
                    .owner_slot_kind = (uint32_t)kind,
                };

                object = node.object_id != 0u
                    ? nmo_object_repository_find_by_id(graph->repo, node.object_id)
                    : NULL;
                if (object) {
                    node.name = nmo_object_get_name(object);
                    node.class_id = nmo_object_get_class_id(object);
                }

                if (!add_or_update_node(graph, &node)) {
                    return false;
                }
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
        edge.type_guid = get_parameter_type_guid(graph, src->to_id);

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

static bool add_runtime_data_edges(nmo_script_edit_graph_t *graph)
{
    size_t i = 0;

    if (!graph) {
        return false;
    }

    for (i = 0; i < graph->node_count; ++i) {
        const nmo_script_edit_node_t *node = &graph->nodes[i];
        nmo_object_t *object = NULL;

        if (node->kind != NMO_SCRIPT_EDIT_NODE_PARAMETER || node->object_id == 0u) {
            continue;
        }

        object = nmo_object_repository_find_by_id(graph->repo, node->object_id);
        if (!object) {
            continue;
        }

        if (nmo_type_query_object_is_derived_from_class(
                graph->type_registry, object, NMO_CID_PARAMETERIN)) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)
                    nmo_type_query_object_get_ancestor_state_by_guid(
                        graph->type_registry, object, CKPGUID_PARAMETERIN);
            nmo_script_edit_data_edge_t edge = {0};
            nmo_script_edit_endpoint_t owner = {0};

            if (!state) {
                continue;
            }
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(state);
            if (source_id == 0u) {
                continue;
            }

            edge.source_parameter_id = source_id;
            edge.target_parameter_id = node->object_id;
            edge.shared = state->is_shared != 0u;
            edge.type_guid = state->type_guid;

            if (populate_owner_from_index(graph->behavior_index,
                                          edge.source_parameter_id, &owner)) {
                edge.source_owner_id = owner.owner_behavior_id;
                apply_owner_to_node(graph, edge.source_parameter_id, &owner);
            } else if (!apply_parameter_owner_fallback(graph,
                                                       edge.source_parameter_id,
                                                       &edge.source_owner_id)) {
                graph->edit_ready = false;
            }

            if (populate_owner_from_index(graph->behavior_index,
                                          edge.target_parameter_id, &owner)) {
                edge.target_owner_id = owner.owner_behavior_id;
                apply_owner_to_node(graph, edge.target_parameter_id, &owner);
            } else if (!apply_parameter_owner_fallback(graph,
                                                       edge.target_parameter_id,
                                                       &edge.target_owner_id)) {
                graph->edit_ready = false;
            }

            if (!add_or_merge_data_edge(graph, &edge)) {
                return false;
            }
        } else if (nmo_type_query_object_is_derived_from_class(
                       graph->type_registry, object, NMO_CID_PARAMETEROUT)) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)
                    nmo_type_query_object_get_ancestor_state_by_guid(
                        graph->type_registry, object, CKPGUID_PARAMETEROUT);

            if (!state || !state->destination_ids) {
                continue;
            }

            for (uint32_t d = 0; d < state->destination_count; ++d) {
                nmo_script_edit_data_edge_t edge = {0};
                nmo_script_edit_endpoint_t owner = {0};

                const nmo_object_id_t destination_id =
                    nmo_parameterout_destination_id(state, d);
                if (destination_id == 0u) {
                    continue;
                }

                edge.source_parameter_id = node->object_id;
                edge.target_parameter_id = destination_id;
                edge.type_guid = get_parameter_type_guid(
                    graph, edge.target_parameter_id);
                edge.shared = false;

                if (populate_owner_from_index(graph->behavior_index,
                                              edge.source_parameter_id, &owner)) {
                    edge.source_owner_id = owner.owner_behavior_id;
                    apply_owner_to_node(graph, edge.source_parameter_id, &owner);
                } else if (!apply_parameter_owner_fallback(graph,
                                                           edge.source_parameter_id,
                                                           &edge.source_owner_id)) {
                    graph->edit_ready = false;
                }

                if (populate_owner_from_index(graph->behavior_index,
                                              edge.target_parameter_id, &owner)) {
                    edge.target_owner_id = owner.owner_behavior_id;
                    apply_owner_to_node(graph, edge.target_parameter_id, &owner);
                } else if (!apply_parameter_owner_fallback(graph,
                                                           edge.target_parameter_id,
                                                           &edge.target_owner_id)) {
                    graph->edit_ready = false;
                }

                if (!add_or_merge_data_edge(graph, &edge)) {
                    return false;
                }
            }
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

NMO_API nmo_status_t nmo_script_edit_graph_build(nmo_workspace_t *workspace,
                                                 nmo_object_id_t root_behavior_id,
                                                 uint32_t max_depth,
                                                 nmo_script_edit_graph_t **out_graph)
{
    nmo_behavior_graph_t behavior_graph = {0};
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_arena_t *arena = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    const nmo_type_registry_t *type_registry = NULL;
    size_t broken_edge_count = 0;
    nmo_status_t ref_status = NMO_OK;

    if (!workspace || root_behavior_id == 0u || !out_graph) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid script edit graph arguments");
    }

    *out_graph = NULL;

    if (nmo_workspace_internal_ensure_behavior_acceleration(workspace) != NMO_OK) {
        return nmo_last_error_code();
    }

    repo = nmo_workspace_internal_repository(workspace);
    type_registry = nmo_workspace_internal_type_registry(workspace);
    if (!repo || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "workspace behavior graph state unavailable");
    }

    graph = (nmo_script_edit_graph_t *)calloc(1u, sizeof(*graph));
    if (!graph) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate script edit graph");
    }

    graph->root_behavior_id = root_behavior_id;
    graph->repo = repo;
    graph->type_registry = type_registry;
    graph->behavior_index = nmo_workspace_internal_behavior_index(workspace);
    graph->owner_index_available = graph->behavior_index != NULL;
    graph->edit_ready = graph->owner_index_available;

    if (!nmo_behavior_graph_build(workspace, root_behavior_id,
                                  max_depth, &behavior_graph)) {
        free(graph);
        return (nmo_status_t)nmo_last_error_code();
    }

    if (!copy_behavior_graph_nodes(graph, &behavior_graph) ||
        !add_owned_io_nodes(graph, &behavior_graph) ||
        !add_owned_parameter_nodes(graph, &behavior_graph) ||
        !copy_control_edges(graph, &behavior_graph)) {
        nmo_behavior_graph_free(&behavior_graph);
        nmo_script_edit_graph_destroy(graph);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to populate script edit graph");
    }

    derive_parameter_owners_from_behavior_edges(graph, &behavior_graph);

    if (!copy_data_edges(graph, &behavior_graph) ||
        !add_runtime_data_edges(graph)) {
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

    ref_graph = nmo_ref_graph_create(repo, type_registry, arena);
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

/* ---------------------------------------------------------------------------
 * Alias and query handles
 *
 * An alias is the name of a node. A query is a list of key=value terms
 * separated by spaces, all of which a node must match; a value with spaces is
 * written in double quotes, with \" and \\ inside. Keys: id, kind (behavior,
 * io, parameter, operation, link), name, class (a class name or id), parent,
 * owner, slot, slot_kind (a port kind name such as io_in, or its number) and
 * depth. Either handle resolves only when exactly one node matches.
 * ------------------------------------------------------------------------- */

typedef enum graph_query_key {
    GRAPH_QUERY_ID,
    GRAPH_QUERY_KIND,
    GRAPH_QUERY_NAME,
    GRAPH_QUERY_CLASS,
    GRAPH_QUERY_PARENT,
    GRAPH_QUERY_OWNER,
    GRAPH_QUERY_SLOT,
    GRAPH_QUERY_SLOT_KIND,
    GRAPH_QUERY_DEPTH,
} graph_query_key_t;

typedef struct graph_query_term {
    graph_query_key_t key;
    const char *text; /**< The value as written, unescaped */
    char *owned;      /**< text when the term allocated it */
    bool is_number;
    uint64_t number;
} graph_query_term_t;

typedef struct graph_query_name {
    const char *name;
    uint64_t value;
} graph_query_name_t;

static const graph_query_name_t GRAPH_QUERY_KEYS[] = {
    {"id", GRAPH_QUERY_ID},
    {"kind", GRAPH_QUERY_KIND},
    {"name", GRAPH_QUERY_NAME},
    {"class", GRAPH_QUERY_CLASS},
    {"parent", GRAPH_QUERY_PARENT},
    {"owner", GRAPH_QUERY_OWNER},
    {"slot", GRAPH_QUERY_SLOT},
    {"slot_kind", GRAPH_QUERY_SLOT_KIND},
    {"depth", GRAPH_QUERY_DEPTH},
    {NULL, 0},
};

static const graph_query_name_t GRAPH_QUERY_NODE_KINDS[] = {
    {"behavior", NMO_SCRIPT_EDIT_NODE_BEHAVIOR},
    {"io", NMO_SCRIPT_EDIT_NODE_IO},
    {"parameter", NMO_SCRIPT_EDIT_NODE_PARAMETER},
    {"operation", NMO_SCRIPT_EDIT_NODE_OPERATION},
    {"link", NMO_SCRIPT_EDIT_NODE_LINK},
    {NULL, 0},
};

static const graph_query_name_t GRAPH_QUERY_PORT_KINDS[] = {
    {"io_in", NMO_PORT_IO_IN},
    {"io_out", NMO_PORT_IO_OUT},
    {"param_in", NMO_PORT_PARAM_IN},
    {"param_out", NMO_PORT_PARAM_OUT},
    {"param_local", NMO_PORT_PARAM_LOCAL},
    {"param_target", NMO_PORT_PARAM_TARGET},
    {"operation", NMO_PORT_OPERATION},
    {"sub_behavior", NMO_PORT_SUB_BEHAVIOR},
    {"sub_link", NMO_PORT_SUB_LINK},
    {NULL, 0},
};

static bool graph_query_lookup(const graph_query_name_t *names,
                               const char *text,
                               uint64_t *out_value)
{
    for (const graph_query_name_t *entry = names; entry->name != NULL; ++entry) {
        if (strcmp(entry->name, text) == 0) {
            *out_value = entry->value;
            return true;
        }
    }
    return false;
}

static bool graph_query_parse_number(const char *text, uint64_t *out_value)
{
    if (text[0] < '0' || text[0] > '9') {
        return false;
    }
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == NULL || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint64_t)value;
    return true;
}

static void graph_query_free(graph_query_term_t *terms, size_t count)
{
    for (size_t i = 0; terms != NULL && i < count; ++i) {
        free(terms[i].owned);
    }
    free(terms);
}

/* Read one value at *cursor: a run up to the next space, or a quoted string. */
static nmo_status_t graph_query_read_value(const char **cursor, char **out_text)
{
    const char *p = *cursor;
    size_t length = 0;
    char *text = NULL;
    if (*p == '"') {
        const char *start = ++p;
        while (*p != '\0' && *p != '"') {
            if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                ++p;
            }
            ++p;
            ++length;
        }
        if (*p != '"') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "unterminated quoted value in graph query");
        }
        text = (char *)malloc(length + 1u);
        if (text == NULL) {
            return NMO_ERR_NOMEM;
        }
        size_t out = 0;
        for (const char *q = start; q < p; ++q) {
            if (*q == '\\' && (q[1] == '"' || q[1] == '\\')) {
                ++q;
            }
            text[out++] = *q;
        }
        text[out] = '\0';
        *cursor = p + 1;
    } else {
        const char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
        length = (size_t)(p - start);
        if (length == 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "empty value in graph query");
        }
        text = (char *)malloc(length + 1u);
        if (text == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(text, start, length);
        text[length] = '\0';
        *cursor = p;
    }
    *out_text = text;
    return NMO_OK;
}

/* Turn the value of a term into what its key compares with. */
static nmo_status_t graph_query_check_value(graph_query_term_t *term, const char *key)
{
    switch (term->key) {
    case GRAPH_QUERY_NAME:
        return NMO_OK;
    case GRAPH_QUERY_CLASS:
        term->is_number = graph_query_parse_number(term->text, &term->number);
        return NMO_OK;
    case GRAPH_QUERY_KIND:
        term->is_number = graph_query_lookup(GRAPH_QUERY_NODE_KINDS, term->text, &term->number);
        break;
    case GRAPH_QUERY_SLOT_KIND:
        term->is_number = graph_query_lookup(GRAPH_QUERY_PORT_KINDS, term->text, &term->number) ||
                          graph_query_parse_number(term->text, &term->number);
        break;
    default:
        term->is_number = graph_query_parse_number(term->text, &term->number);
        break;
    }
    if (!term->is_number) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "invalid %s '%s' in graph query", key, term->text);
    }
    return NMO_OK;
}

static nmo_status_t graph_query_parse(const char *query,
                                      graph_query_term_t **out_terms,
                                      size_t *out_count)
{
    graph_query_term_t *terms = NULL;
    size_t count = 0;
    size_t capacity = 0;
    const char *cursor = query;
    nmo_status_t status = NMO_OK;

    *out_terms = NULL;
    *out_count = 0;
    for (;;) {
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        const char *key_start = cursor;
        while (*cursor != '\0' && *cursor != '=' && *cursor != ' ' && *cursor != '\t') {
            ++cursor;
        }
        if (*cursor != '=') {
            graph_query_free(terms, count);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "graph query term '%.*s' has no '='",
                             (int)(cursor - key_start), key_start);
        }
        const size_t key_length = (size_t)(cursor - key_start);
        char *key = (char *)malloc(key_length + 1u);
        if (key == NULL) {
            graph_query_free(terms, count);
            return NMO_ERR_NOMEM;
        }
        memcpy(key, key_start, key_length);
        key[key_length] = '\0';
        ++cursor;

        uint64_t key_value = 0;
        if (!graph_query_lookup(GRAPH_QUERY_KEYS, key, &key_value)) {
            nmo_last_error_setf(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__, "unknown graph query key '%s'", key);
            free(key);
            graph_query_free(terms, count);
            return NMO_ERR_INVALID_ARGUMENT;
        }
        if (count == capacity) {
            size_t grown_capacity = capacity ? capacity * 2u : 4u;
            graph_query_term_t *grown = (graph_query_term_t *)realloc(
                terms, grown_capacity * sizeof(*grown));
            if (grown == NULL) {
                free(key);
                graph_query_free(terms, count);
                return NMO_ERR_NOMEM;
            }
            terms = grown;
            capacity = grown_capacity;
        }
        graph_query_term_t *term = &terms[count];
        memset(term, 0, sizeof(*term));
        term->key = (graph_query_key_t)key_value;
        status = graph_query_read_value(&cursor, &term->owned);
        term->text = term->owned;
        if (status == NMO_OK) {
            ++count;
            status = graph_query_check_value(term, key);
        }
        free(key);
        if (status != NMO_OK) {
            graph_query_free(terms, count);
            return status;
        }
    }
    if (count == 0) {
        free(terms);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph query has no terms");
    }
    *out_terms = terms;
    *out_count = count;
    return NMO_OK;
}

static bool graph_query_matches(const nmo_script_edit_node_t *node,
                                const graph_query_term_t *terms,
                                size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const graph_query_term_t *term = &terms[i];
        bool match = false;
        switch (term->key) {
        case GRAPH_QUERY_ID:
            match = node->object_id == term->number;
            break;
        case GRAPH_QUERY_KIND:
            match = (uint64_t)node->kind == term->number;
            break;
        case GRAPH_QUERY_NAME:
            match = node->name != NULL && strcmp(node->name, term->text) == 0;
            break;
        case GRAPH_QUERY_CLASS:
            match = term->is_number
                ? node->class_id == term->number
                : node->class_name != NULL && strcmp(node->class_name, term->text) == 0;
            break;
        case GRAPH_QUERY_PARENT:
            match = node->parent_behavior_id == term->number;
            break;
        case GRAPH_QUERY_OWNER:
            match = node->owner_behavior_id == term->number;
            break;
        case GRAPH_QUERY_SLOT:
            match = node->owner_behavior_id != 0 && node->owner_slot_index >= 0 &&
                    (uint64_t)node->owner_slot_index == term->number;
            break;
        case GRAPH_QUERY_SLOT_KIND:
            match = node->owner_behavior_id != 0 && node->owner_slot_kind == term->number;
            break;
        case GRAPH_QUERY_DEPTH:
            match = node->depth == term->number;
            break;
        }
        if (!match) {
            return false;
        }
    }
    return true;
}

/* The one node the terms match; what describes the handle goes in messages. */
static nmo_status_t graph_find_unique_node(const nmo_script_edit_graph_t *graph,
                                           const graph_query_term_t *terms,
                                           size_t count,
                                           const char *what,
                                           const char *text,
                                           nmo_object_id_t *out_object_id)
{
    const nmo_script_edit_node_t *found = NULL;
    size_t matches = 0;
    for (size_t i = 0; i < graph->node_count; ++i) {
        if (graph_query_matches(&graph->nodes[i], terms, count)) {
            if (matches == 0) {
                found = &graph->nodes[i];
            }
            ++matches;
        }
    }
    if (matches == 0) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "no graph node matches %s '%s'", what, text);
    }
    if (matches > 1) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "%s '%s' matches %zu graph nodes", what, text, matches);
    }
    *out_object_id = found->object_id;
    NMO_RETURN_OK();
}

static nmo_status_t graph_resolve_alias(const nmo_script_edit_graph_t *graph,
                                        const char *alias,
                                        nmo_object_id_t *out_object_id)
{
    if (alias == NULL || alias[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "alias handle must contain a name");
    }
    graph_query_term_t term = {
        .key = GRAPH_QUERY_NAME,
        .text = alias,
    };
    return graph_find_unique_node(graph, &term, 1u, "alias", alias, out_object_id);
}

static nmo_status_t graph_resolve_query(const nmo_script_edit_graph_t *graph,
                                        const char *query,
                                        nmo_object_id_t *out_object_id)
{
    if (query == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "query handle must contain a query");
    }
    graph_query_term_t *terms = NULL;
    size_t count = 0;
    NMO_RETURN_IF_ERROR(graph_query_parse(query, &terms, &count));
    nmo_status_t status = graph_find_unique_node(graph, terms, count, "query", query,
                                                 out_object_id);
    graph_query_free(terms, count);
    return status;
}

NMO_API nmo_status_t nmo_script_edit_graph_resolve_handle(
    const nmo_script_edit_graph_t *graph,
    const nmo_script_edit_handle_t *handle,
    nmo_object_id_t *out_object_id)
{
    const nmo_behavior_state_t *behavior = NULL;
    const nmo_array_t *array = NULL;
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
        return graph_resolve_alias(graph, handle->alias, out_object_id);
    case NMO_SCRIPT_EDIT_HANDLE_QUERY:
        return graph_resolve_query(graph, handle->query, out_object_id);
    default:
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unknown handle kind");
    }

    behavior = get_behavior_state(graph, handle->owner_id);
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
        array = &behavior->inputs;
        count = behavior->inputs.count;
        break;
    case NMO_PORT_IO_OUT:
        array = &behavior->outputs;
        count = behavior->outputs.count;
        break;
    case NMO_PORT_PARAM_IN:
        array = &behavior->in_parameters;
        count = behavior->in_parameters.count;
        break;
    case NMO_PORT_PARAM_OUT:
        array = &behavior->out_parameters;
        count = behavior->out_parameters.count;
        break;
    case NMO_PORT_PARAM_LOCAL:
        array = &behavior->local_parameters;
        count = behavior->local_parameters.count;
        break;
    case NMO_PORT_OPERATION:
        array = &behavior->operations;
        count = behavior->operations.count;
        break;
    case NMO_PORT_SUB_BEHAVIOR:
        array = &behavior->sub_behaviors;
        count = behavior->sub_behaviors.count;
        break;
    case NMO_PORT_SUB_LINK:
        array = &behavior->sub_behavior_links;
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

    *out_object_id = nmo_behavior_ref_array_get_id(
        array, (size_t)handle->slot_index);
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
