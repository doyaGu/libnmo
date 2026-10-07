/**
 * @file script_model.c
 * @brief Read-only semantic model of every behavior graph in a document
 */

#include "behavior/nmo_script_model.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_behavior_view.h"
#include "core/nmo_array.h"
#include "extension/nmo_behavior_registry.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Growable arrays and the id -> index map
 * ============================================================================ */

typedef struct model_vec {
    void *data;
    size_t count;
    size_t capacity;
} model_vec_t;

/* A new zeroed item at the end of `vec`, or NULL on OOM. */
static void *model_vec_push(model_vec_t *vec, size_t item_size)
{
    if (vec->count == vec->capacity) {
        size_t capacity = vec->capacity ? vec->capacity * 2u : 64u;
        void *data = realloc(vec->data, capacity * item_size);
        if (data == NULL) {
            return NULL;
        }
        vec->data = data;
        vec->capacity = capacity;
    }
    void *item = (char *)vec->data + vec->count * item_size;
    memset(item, 0, item_size);
    vec->count++;
    return item;
}

#define MODEL_VEC_AT(vec, type, i) (&((type *)(vec).data)[(i)])

typedef struct model_map_slot {
    nmo_object_id_t key; /* 0 = empty */
    uint32_t value;
} model_map_slot_t;

typedef struct model_map {
    model_map_slot_t *slots;
    size_t capacity;
    size_t count;
} model_map_t;

static size_t model_map_hash(nmo_object_id_t id, size_t capacity)
{
    uint32_t h = (uint32_t)id;
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return h & (capacity - 1u);
}

/* Map `key` to `value`; the first mapping of a key wins. */
static bool model_map_put(model_map_t *map, nmo_object_id_t key, uint32_t value)
{
    if (key == 0) {
        return true;
    }
    if ((map->count + 1u) * 10u > map->capacity * 7u) {
        size_t capacity = map->capacity ? map->capacity * 2u : 1024u;
        model_map_slot_t *slots = (model_map_slot_t *)calloc(capacity, sizeof(*slots));
        if (slots == NULL) {
            return false;
        }
        for (size_t i = 0; i < map->capacity; i++) {
            if (map->slots[i].key == 0) {
                continue;
            }
            size_t h = model_map_hash(map->slots[i].key, capacity);
            while (slots[h].key != 0) {
                h = (h + 1u) & (capacity - 1u);
            }
            slots[h] = map->slots[i];
        }
        free(map->slots);
        map->slots = slots;
        map->capacity = capacity;
    }
    size_t h = model_map_hash(key, map->capacity);
    while (map->slots[h].key != 0) {
        if (map->slots[h].key == key) {
            return true;
        }
        h = (h + 1u) & (map->capacity - 1u);
    }
    map->slots[h].key = key;
    map->slots[h].value = value;
    map->count++;
    return true;
}

/* Remap a key `model_map_put` already holds. */
static void model_map_replace(model_map_t *map, nmo_object_id_t key, uint32_t value)
{
    if (key == 0 || map->count == 0) {
        return;
    }
    size_t h = model_map_hash(key, map->capacity);
    while (map->slots[h].key != 0) {
        if (map->slots[h].key == key) {
            map->slots[h].value = value;
            return;
        }
        h = (h + 1u) & (map->capacity - 1u);
    }
}

static bool model_map_get(const model_map_t *map, nmo_object_id_t key, uint32_t *out_value)
{
    if (key == 0 || map->count == 0) {
        return false;
    }
    size_t h = model_map_hash(key, map->capacity);
    while (map->slots[h].key != 0) {
        if (map->slots[h].key == key) {
            *out_value = map->slots[h].value;
            return true;
        }
        h = (h + 1u) & (map->capacity - 1u);
    }
    return false;
}

/* ============================================================================
 * Model
 * ============================================================================ */

struct nmo_script_model {
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *registry;
    const nmo_behavior_registry_t *bb_registry;

    model_vec_t nodes;      /* nmo_script_node_t */
    model_vec_t roots;      /* nmo_object_id_t */
    model_vec_t children;   /* nmo_object_id_t */
    model_vec_t ios;        /* nmo_script_io_t */
    model_vec_t params;     /* nmo_script_param_t */
    model_vec_t operations; /* nmo_script_operation_t */
    model_vec_t links;      /* nmo_script_link_t */
    model_vec_t data_edges; /* nmo_script_data_edge_t */

    model_map_t node_map;
    model_map_t io_map;
    model_map_t param_map;
    model_map_t operation_map;

    const nmo_script_link_t **links_by_source;      /* sorted by source_io_id */
    const nmo_script_data_edge_t **uses_by_source;  /* sorted by source_id */
};

static const char *model_object_name(nmo_object_t *object)
{
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    return name != NULL ? name : "";
}

static const nmo_behavior_state_t *model_behavior_state(const nmo_script_model_t *model,
                                                        nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? nmo_object_repository_find_by_id(model->repo, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_guid_t model_param_type_guid(nmo_object_t *object)
{
    const void *state = object != NULL ? nmo_object_get_state(object) : NULL;
    if (state == NULL) {
        return NMO_GUID(0u, 0u);
    }
    switch (nmo_object_get_class_id(object)) {
    case NMO_CID_PARAMETERIN:
        return ((const nmo_parameterin_state_t *)state)->type_guid;
    case NMO_CID_PARAMETEROUT:
    case NMO_CID_PARAMETERLOCAL:
    case NMO_CID_PARAMETER:
        return ((const nmo_parameter_state_t *)state)->type_guid;
    default:
        return NMO_GUID(0u, 0u);
    }
}

static const char *model_type_name(const nmo_script_model_t *model, nmo_guid_t guid)
{
    const char *name = nmo_guid_is_null(guid) ? NULL : nmo_field_type_name(model->registry, guid);
    return name != NULL ? name : "?";
}

static bool model_add_param(nmo_script_model_t *model,
                            nmo_object_id_t id,
                            nmo_object_id_t owner_id,
                            nmo_script_param_role_t role,
                            uint32_t index)
{
    if (id == 0) {
        return true;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(model->repo, id);
    nmo_script_param_t *param =
        (nmo_script_param_t *)model_vec_push(&model->params, sizeof(*param));
    if (param == NULL) {
        return false;
    }
    param->id = id;
    param->owner_id = owner_id;
    param->role = role;
    param->index = index;
    param->name = model_object_name(object);
    param->class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
    param->type_guid = model_param_type_guid(object);
    param->type_name = model_type_name(model, param->type_guid);
    const void *state = object != NULL ? nmo_object_get_state(object) : NULL;
    nmo_object_id_t named_owner = 0;
    if (state != NULL && param->class_id == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)state;
        param->source_id = nmo_parameterin_source_id(pin);
        param->is_shared = pin->is_shared != 0;
        named_owner = nmo_parameterin_owner_id(pin);
    } else if (state != NULL && param->class_id == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameterlocal_state_t *local = (const nmo_parameterlocal_state_t *)state;
        param->is_setting = local->is_setting != 0;
        named_owner = nmo_parameterlocal_owner_id(local);
    } else if (state != NULL && param->class_id == NMO_CID_PARAMETEROUT) {
        named_owner = nmo_parameterout_owner_id((const nmo_parameterout_state_t *)state);
    }

    /* Lookups by id find the item of the owner the parameter names, else the first */
    uint32_t item_index = (uint32_t)(model->params.count - 1u);
    uint32_t existing;
    if (model_map_get(&model->param_map, id, &existing)) {
        if (named_owner == owner_id) {
            model_map_replace(&model->param_map, id, item_index);
        }
        return true;
    }
    return model_map_put(&model->param_map, id, item_index);
}

static bool model_add_param_array(nmo_script_model_t *model,
                                  nmo_object_id_t owner_id,
                                  const nmo_array_t *array,
                                  nmo_script_param_role_t role)
{
    for (size_t i = 0; i < array->count; i++) {
        if (!model_add_param(model, nmo_behavior_ref_array_get_id(array, i), owner_id,
                             role, (uint32_t)i)) {
            return false;
        }
    }
    return true;
}

static bool model_add_ios(nmo_script_model_t *model,
                          nmo_object_id_t node_id,
                          const nmo_array_t *array,
                          bool is_output)
{
    for (size_t i = 0; i < array->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) {
            continue;
        }
        nmo_script_io_t *io = (nmo_script_io_t *)model_vec_push(&model->ios, sizeof(*io));
        if (io == NULL) {
            return false;
        }
        io->id = id;
        io->node_id = node_id;
        io->is_output = is_output;
        io->index = (uint32_t)i;
        io->name = model_object_name(nmo_object_repository_find_by_id(model->repo, id));
        if (!model_map_put(&model->io_map, id, (uint32_t)(model->ios.count - 1u))) {
            return false;
        }
    }
    return true;
}

static bool model_add_operations(nmo_script_model_t *model,
                                 nmo_object_id_t graph_id,
                                 const nmo_array_t *array)
{
    for (size_t i = 0; i < array->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        nmo_object_t *object = id != 0 ? nmo_object_repository_find_by_id(model->repo, id) : NULL;
        const nmo_parameteroperation_state_t *state =
            object != NULL ? (const nmo_parameteroperation_state_t *)nmo_object_get_state(object)
                           : NULL;
        if (state == NULL || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
            continue;
        }
        nmo_script_operation_t *op =
            (nmo_script_operation_t *)model_vec_push(&model->operations, sizeof(*op));
        if (op == NULL) {
            return false;
        }
        size_t op_index = model->operations.count - 1u;
        op->id = id;
        op->graph_id = graph_id;
        op->operation_guid = state->operation_guid;
        const char *name = nmo_type_registry_guid_to_name(model->registry, state->operation_guid);
        op->operation_name = (name != NULL && name[0] != '\0') ? name : "?";
        op->input1_id = state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0;
        op->input2_id = state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0;
        op->output_id = state->has_out ? nmo_parameteroperation_out_id(state) : 0;
        op->first_param = model->params.count;
        nmo_object_id_t in1 = op->input1_id, in2 = op->input2_id, out = op->output_id;
        if (!model_add_param(model, in1, id, NMO_SCRIPT_PARAM_OPERATION_INPUT, 0) ||
            !model_add_param(model, in2, id, NMO_SCRIPT_PARAM_OPERATION_INPUT, 1) ||
            !model_add_param(model, out, id, NMO_SCRIPT_PARAM_OPERATION_OUTPUT, 0) ||
            !model_map_put(&model->operation_map, id, (uint32_t)op_index)) {
            return false;
        }
        op = MODEL_VEC_AT(model->operations, nmo_script_operation_t, op_index);
        op->param_count = model->params.count - op->first_param;
    }
    return true;
}

static bool model_add_links(nmo_script_model_t *model,
                            nmo_object_id_t graph_id,
                            const nmo_array_t *array)
{
    for (size_t i = 0; i < array->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        nmo_object_t *object = id != 0 ? nmo_object_repository_find_by_id(model->repo, id) : NULL;
        const nmo_behaviorlink_state_t *state =
            object != NULL ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(object) : NULL;
        if (state == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        nmo_script_link_t *link = (nmo_script_link_t *)model_vec_push(&model->links,
                                                                       sizeof(*link));
        if (link == NULL) {
            return false;
        }
        link->id = id;
        link->graph_id = graph_id;
        /* in_io is the link's source and out_io its target (the SDK naming is backwards) */
        link->source_io_id = nmo_behaviorlink_in_io_id(state);
        link->target_io_id = nmo_behaviorlink_out_io_id(state);
        link->activation_delay = state->activation_delay;
        link->initial_activation_delay = state->initial_activation_delay;
    }
    return true;
}

static nmo_script_node_kind_t model_node_kind(const nmo_behavior_state_t *state)
{
    if (state->flags & CKBEHAVIOR_SCRIPT) {
        return NMO_SCRIPT_NODE_SCRIPT;
    }
    return (state->flags & CKBEHAVIOR_BUILDINGBLOCK) ? NMO_SCRIPT_NODE_BUILDING_BLOCK
                                                     : NMO_SCRIPT_NODE_GRAPH;
}

/* Add behavior `id` and, depth first, its sub-behaviors. */
static bool model_add_node(nmo_script_model_t *model,
                           nmo_object_id_t id,
                           nmo_object_id_t parent_id,
                           nmo_object_id_t owner_object_id,
                           uint32_t depth)
{
    uint32_t existing;
    const nmo_behavior_state_t *state = model_behavior_state(model, id);
    if (state == NULL || model_map_get(&model->node_map, id, &existing) || depth > 256u) {
        return true;
    }
    nmo_script_node_t *node = (nmo_script_node_t *)model_vec_push(&model->nodes, sizeof(*node));
    if (node == NULL) {
        return false;
    }
    size_t node_index = model->nodes.count - 1u;
    if (!model_map_put(&model->node_map, id, (uint32_t)node_index)) {
        return false;
    }
    node->id = id;
    node->kind = model_node_kind(state);
    node->name = model_object_name(nmo_object_repository_find_by_id(model->repo, id));
    node->parent_id = parent_id;
    node->owner_object_id = owner_object_id;
    node->depth = depth;
    node->flags = state->flags;
    if (node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        node->prototype_guid = state->block_guid;
        node->prototype_name = nmo_guid_is_null(state->block_guid) || model->bb_registry == NULL
            ? NULL
            : nmo_behavior_registry_get_name(model->bb_registry, state->block_guid);
        nmo_guid_t op_guid;
        if (nmo_behavior_op_block_operation_guid(model->workspace, id, &op_guid) == NMO_OK) {
            const char *op_name = nmo_type_registry_guid_to_name(model->registry, op_guid);
            node->operation_guid = op_guid;
            node->operation_name = (op_name != NULL && op_name[0] != '\0') ? op_name : "?";
        }
    }

    size_t first_io = model->ios.count;
    size_t first_param = model->params.count;
    size_t first_operation = model->operations.count;
    size_t first_link = model->links.count;
    size_t first_child = model->children.count;
    nmo_object_id_t target_id = nmo_behavior_target_parameter_id(state);
    bool ok = model_add_ios(model, id, &state->inputs, false) &&
              model_add_ios(model, id, &state->outputs, true) &&
              model_add_param(model, target_id, id, NMO_SCRIPT_PARAM_TARGET, 0) &&
              model_add_param_array(model, id, &state->in_parameters, NMO_SCRIPT_PARAM_INPUT) &&
              model_add_param_array(model, id, &state->out_parameters, NMO_SCRIPT_PARAM_OUTPUT) &&
              model_add_param_array(model, id, &state->local_parameters, NMO_SCRIPT_PARAM_LOCAL) &&
              model_add_operations(model, id, &state->operations) &&
              model_add_links(model, id, &state->sub_behavior_links);
    for (size_t i = 0; ok && i < state->sub_behaviors.count; i++) {
        nmo_object_id_t child_id = nmo_behavior_ref_array_get_id(&state->sub_behaviors, i);
        if (model_behavior_state(model, child_id) == NULL) {
            continue;
        }
        nmo_object_id_t *slot =
            (nmo_object_id_t *)model_vec_push(&model->children, sizeof(*slot));
        ok = slot != NULL;
        if (ok) {
            *slot = child_id;
        }
    }
    if (!ok) {
        return false;
    }

    node = MODEL_VEC_AT(model->nodes, nmo_script_node_t, node_index);
    node->first_io = first_io;
    /* Null IO references are skipped, so count the inputs that were added */
    for (size_t i = first_io; i < model->ios.count; i++) {
        node->input_count += !MODEL_VEC_AT(model->ios, nmo_script_io_t, i)->is_output;
    }
    node->output_count = model->ios.count - first_io - node->input_count;
    node->first_param = first_param;
    node->first_operation = first_operation;
    node->operation_count = model->operations.count - first_operation;
    node->first_link = first_link;
    node->link_count = model->links.count - first_link;
    node->first_child = first_child;
    node->child_count = model->children.count - first_child;
    /* The operations' parameters follow the behavior's own; they are not the node's */
    node->param_count = 0;
    for (size_t i = first_param; i < model->params.count; i++) {
        const nmo_script_param_t *p = MODEL_VEC_AT(model->params, nmo_script_param_t, i);
        if (p->owner_id != id) {
            break;
        }
        node->param_count++;
    }

    size_t child_end = first_child + node->child_count;
    for (size_t i = first_child; i < child_end; i++) {
        nmo_object_id_t child_id = *MODEL_VEC_AT(model->children, nmo_object_id_t, i);
        if (!model_add_node(model, child_id, id, 0, depth + 1u)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Reach and data edges
 * ============================================================================ */

static const nmo_script_node_t *model_node(const nmo_script_model_t *model, nmo_object_id_t id)
{
    uint32_t index;
    return model_map_get(&model->node_map, id, &index)
        ? MODEL_VEC_AT(model->nodes, nmo_script_node_t, index) : NULL;
}

static nmo_object_id_t model_parent_id(const nmo_script_model_t *model, nmo_object_id_t id)
{
    const nmo_script_node_t *node = model_node(model, id);
    return node != NULL ? node->parent_id : 0;
}

bool nmo_script_model_is_ancestor(const nmo_script_model_t *model,
                                  nmo_object_id_t ancestor_id,
                                  nmo_object_id_t node_id)
{
    if (model == NULL || ancestor_id == 0) {
        return false;
    }
    nmo_object_id_t cur = model_parent_id(model, node_id);
    for (uint32_t hops = 0; cur != 0 && hops < 512u; hops++) {
        if (cur == ancestor_id) {
            return true;
        }
        cur = model_parent_id(model, cur);
    }
    return false;
}

/* How graph `graph_id` reaches the parameter item `param` (NULL: an external one). */
static nmo_script_reach_t model_param_reach(const nmo_script_model_t *model,
                                            nmo_object_id_t graph_id,
                                            const nmo_script_param_t *param)
{
    if (param == NULL) {
        return NMO_SCRIPT_REACH_EXTERNAL;
    }
    nmo_object_id_t owner = param->owner_id;
    switch (param->role) {
    case NMO_SCRIPT_PARAM_LOCAL:
        if (owner == graph_id) {
            return NMO_SCRIPT_REACH_LOCAL;
        }
        return nmo_script_model_is_ancestor(model, owner, graph_id)
            ? NMO_SCRIPT_REACH_ANCESTOR_LOCAL : NMO_SCRIPT_REACH_FOREIGN_LOCAL;
    case NMO_SCRIPT_PARAM_INPUT:
        if (owner == graph_id || nmo_script_model_is_ancestor(model, owner, graph_id)) {
            return NMO_SCRIPT_REACH_GRAPH_INPUT;
        }
        return model_parent_id(model, owner) == graph_id
            ? NMO_SCRIPT_REACH_CHILD_INPUT : NMO_SCRIPT_REACH_FOREIGN_INPUT;
    case NMO_SCRIPT_PARAM_OUTPUT:
        if (owner == graph_id) {
            return NMO_SCRIPT_REACH_GRAPH_OUTPUT;
        }
        return model_parent_id(model, owner) == graph_id
            ? NMO_SCRIPT_REACH_CHILD_OUTPUT : NMO_SCRIPT_REACH_FOREIGN_OUTPUT;
    case NMO_SCRIPT_PARAM_TARGET:
        return NMO_SCRIPT_REACH_TARGET;
    case NMO_SCRIPT_PARAM_OPERATION_INPUT:
        return NMO_SCRIPT_REACH_OPERATION_INPUT;
    case NMO_SCRIPT_PARAM_OPERATION_OUTPUT:
        return NMO_SCRIPT_REACH_OPERATION_OUTPUT;
    default:
        return NMO_SCRIPT_REACH_EXTERNAL;
    }
}

nmo_script_reach_t nmo_script_model_reach(const nmo_script_model_t *model,
                                          nmo_object_id_t graph_id,
                                          nmo_object_id_t param_id)
{
    return model_param_reach(model, graph_id, nmo_script_model_find_param(model, param_id));
}

/* The owner of the parameter `id` names, or 0 for an external one. */
static nmo_object_id_t model_param_owner(const nmo_script_model_t *model, nmo_object_id_t id)
{
    const nmo_script_param_t *param = nmo_script_model_find_param(model, id);
    return param != NULL ? param->owner_id : 0;
}

/* The read of input `param` inside graph `graph_id`, if it has a source. */
static bool model_add_read(nmo_script_model_t *model,
                           nmo_object_id_t graph_id,
                           const nmo_script_param_t *param)
{
    if (param->source_id == 0) {
        return true;
    }
    nmo_script_param_t copy = *param; /* pushing may move the params */
    nmo_script_data_edge_t *edge =
        (nmo_script_data_edge_t *)model_vec_push(&model->data_edges, sizeof(*edge));
    if (edge == NULL) {
        return false;
    }
    edge->graph_id = graph_id;
    edge->kind = NMO_SCRIPT_DATA_READ;
    edge->source_id = copy.source_id;
    edge->target_id = copy.id;
    edge->source_owner_id = model_param_owner(model, copy.source_id);
    edge->target_owner_id = copy.owner_id;
    edge->source_reach = nmo_script_model_reach(model, graph_id, copy.source_id);
    edge->target_reach = model_param_reach(model, graph_id, &copy);
    edge->type_guid = copy.type_guid;
    edge->is_shared = copy.is_shared;
    return true;
}

/* The writes of output `param` inside graph `graph_id` into parameters other than inputs. */
static bool model_add_writes(nmo_script_model_t *model,
                             nmo_object_id_t graph_id,
                             const nmo_script_param_t *param)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(model->repo, param->id);
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROUT) {
        return true;
    }
    const nmo_parameterout_state_t *pout =
        (const nmo_parameterout_state_t *)nmo_object_get_state(object);
    nmo_script_param_t copy = *param; /* pushing may move the params */
    for (uint32_t i = 0; pout != NULL && i < pout->destination_count; i++) {
        nmo_object_id_t dest_id = nmo_parameterout_destination_id(pout, i);
        nmo_object_t *dest = dest_id != 0 ? nmo_object_repository_find_by_id(model->repo, dest_id)
                                          : NULL;
        if (dest == NULL || nmo_object_get_class_id(dest) == NMO_CID_PARAMETERIN) {
            continue; /* an input reading this output is listed as its read */
        }
        nmo_script_data_edge_t *edge =
            (nmo_script_data_edge_t *)model_vec_push(&model->data_edges, sizeof(*edge));
        if (edge == NULL) {
            return false;
        }
        edge->graph_id = graph_id;
        edge->kind = NMO_SCRIPT_DATA_WRITE;
        edge->source_id = copy.id;
        edge->target_id = dest_id;
        edge->source_owner_id = copy.owner_id;
        edge->target_owner_id = model_param_owner(model, dest_id);
        edge->source_reach = model_param_reach(model, graph_id, &copy);
        edge->target_reach = nmo_script_model_reach(model, graph_id, dest_id);
        edge->type_guid = copy.type_guid;
    }
    return true;
}

/* The reads and writes of the parameters [first, first + count) inside `graph_id`. */
static bool model_add_param_flows(nmo_script_model_t *model,
                                  nmo_object_id_t graph_id,
                                  size_t first,
                                  size_t count)
{
    for (size_t i = first; i < first + count; i++) {
        const nmo_script_param_t *param = MODEL_VEC_AT(model->params, nmo_script_param_t, i);
        bool ok = true;
        switch (param->role) {
        case NMO_SCRIPT_PARAM_TARGET:
        case NMO_SCRIPT_PARAM_INPUT:
        case NMO_SCRIPT_PARAM_OPERATION_INPUT:
            ok = model_add_read(model, graph_id, param);
            break;
        case NMO_SCRIPT_PARAM_OUTPUT:
        case NMO_SCRIPT_PARAM_OPERATION_OUTPUT:
            ok = model_add_writes(model, graph_id, param);
            break;
        default:
            break;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool model_add_data_edges(nmo_script_model_t *model)
{
    for (size_t ni = 0; ni < model->nodes.count; ni++) {
        nmo_script_node_t node = *MODEL_VEC_AT(model->nodes, nmo_script_node_t, ni);
        size_t first_edge = model->data_edges.count;
        for (size_t ci = node.first_child; ci < node.first_child + node.child_count; ci++) {
            const nmo_script_node_t *child =
                model_node(model, *MODEL_VEC_AT(model->children, nmo_object_id_t, ci));
            if (child != NULL && child->parent_id == node.id &&
                !model_add_param_flows(model, node.id, child->first_param, child->param_count)) {
                return false;
            }
        }
        for (size_t oi = node.first_operation; oi < node.first_operation + node.operation_count;
             oi++) {
            const nmo_script_operation_t *op =
                MODEL_VEC_AT(model->operations, nmo_script_operation_t, oi);
            if (!model_add_param_flows(model, node.id, op->first_param, op->param_count)) {
                return false;
            }
        }
        nmo_script_node_t *stored = MODEL_VEC_AT(model->nodes, nmo_script_node_t, ni);
        stored->first_data_edge = first_edge;
        stored->data_edge_count = model->data_edges.count - first_edge;
    }
    return true;
}

/* ============================================================================
 * Sorted lookups
 * ============================================================================ */

/* By source, then in document order: the items are in one array */
static int model_cmp_link_source(const void *a, const void *b)
{
    const nmo_script_link_t *x = *(const nmo_script_link_t *const *)a;
    const nmo_script_link_t *y = *(const nmo_script_link_t *const *)b;
    if (x->source_io_id != y->source_io_id) {
        return x->source_io_id < y->source_io_id ? -1 : 1;
    }
    return x < y ? -1 : x > y ? 1 : 0;
}

static int model_cmp_edge_source(const void *a, const void *b)
{
    const nmo_script_data_edge_t *x = *(const nmo_script_data_edge_t *const *)a;
    const nmo_script_data_edge_t *y = *(const nmo_script_data_edge_t *const *)b;
    if (x->source_id != y->source_id) {
        return x->source_id < y->source_id ? -1 : 1;
    }
    return x < y ? -1 : x > y ? 1 : 0;
}

static bool model_build_lookups(nmo_script_model_t *model)
{
    size_t link_count = model->links.count;
    size_t edge_count = model->data_edges.count;
    model->links_by_source = (const nmo_script_link_t **)malloc(
        (link_count ? link_count : 1u) * sizeof(*model->links_by_source));
    model->uses_by_source = (const nmo_script_data_edge_t **)malloc(
        (edge_count ? edge_count : 1u) * sizeof(*model->uses_by_source));
    if (model->links_by_source == NULL || model->uses_by_source == NULL) {
        return false;
    }
    for (size_t i = 0; i < link_count; i++) {
        nmo_script_link_t *link = MODEL_VEC_AT(model->links, nmo_script_link_t, i);
        const nmo_script_io_t *source = nmo_script_model_find_io(model, link->source_io_id);
        const nmo_script_io_t *target = nmo_script_model_find_io(model, link->target_io_id);
        link->source_node_id = source != NULL ? source->node_id : 0;
        link->target_node_id = target != NULL ? target->node_id : 0;
        model->links_by_source[i] = link;
    }
    for (size_t i = 0; i < edge_count; i++) {
        model->uses_by_source[i] = MODEL_VEC_AT(model->data_edges, nmo_script_data_edge_t, i);
    }
    qsort(model->links_by_source, link_count, sizeof(*model->links_by_source),
          model_cmp_link_source);
    qsort(model->uses_by_source, edge_count, sizeof(*model->uses_by_source),
          model_cmp_edge_source);
    return true;
}

/* ============================================================================
 * Build
 * ============================================================================ */

/* The owners of the document's scripts, keyed by script id. */
static bool model_collect_script_owners(nmo_workspace_t *workspace, model_map_t *owners)
{
    nmo_array_t scripts;
    if (nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL) != NMO_OK) {
        return false;
    }
    bool ok = true;
    /* Without script owners the roots still come from the behaviors */
    if (nmo_behavior_query_collect_scripts(nmo_workspace_get_document(workspace), &scripts) ==
        NMO_OK) {
        const nmo_behavior_script_view_t *views =
            (const nmo_behavior_script_view_t *)scripts.data;
        for (size_t i = 0; ok && i < scripts.count; i++) {
            ok = model_map_put(owners, views[i].script_id, (uint32_t)views[i].owner_id);
        }
    }
    nmo_array_dispose(&scripts);
    return ok;
}

static bool model_build(nmo_script_model_t *model)
{
    /* Every sub-behavior, to tell the roots from the rest */
    model_map_t held = {0};
    model_map_t owners = {0};
    size_t object_count = nmo_object_repository_get_count(model->repo);
    bool ok = model_collect_script_owners(model->workspace, &owners);
    for (size_t i = 0; ok && i < object_count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(model->repo, i);
        const nmo_behavior_state_t *state =
            object != NULL ? model_behavior_state(model, nmo_object_get_id(object)) : NULL;
        for (size_t c = 0; ok && state != NULL && c < state->sub_behaviors.count; c++) {
            ok = model_map_put(&held, nmo_behavior_ref_array_get_id(&state->sub_behaviors, c), 1u);
        }
    }

    for (size_t i = 0; ok && i < object_count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(model->repo, i);
        nmo_object_id_t id = object != NULL ? nmo_object_get_id(object) : 0;
        uint32_t value;
        if (model_behavior_state(model, id) == NULL || model_map_get(&held, id, &value)) {
            continue;
        }
        uint32_t owner = 0;
        model_map_get(&owners, id, &owner);
        nmo_object_id_t *root = (nmo_object_id_t *)model_vec_push(&model->roots, sizeof(*root));
        ok = root != NULL;
        if (ok) {
            *root = id;
            ok = model_add_node(model, id, 0, (nmo_object_id_t)owner, 0);
        }
    }
    free(held.slots);
    free(owners.slots);
    return ok && model_add_data_edges(model) && model_build_lookups(model);
}

nmo_status_t nmo_script_model_build(nmo_workspace_t *workspace, nmo_script_model_t **out_model)
{
    if (workspace == NULL || out_model == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_model = NULL;
    nmo_script_model_t *model = (nmo_script_model_t *)calloc(1, sizeof(*model));
    if (model == NULL) {
        return NMO_ERR_NOMEM;
    }
    model->workspace = workspace;
    model->repo = nmo_workspace_internal_repository(workspace);
    model->registry = nmo_workspace_internal_type_registry(workspace);
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    model->bb_registry = ctx != NULL ? nmo_context_get_bb_registry(ctx) : NULL;
    if (model->repo == NULL || model->registry == NULL) {
        free(model);
        return NMO_ERR_INVALID_STATE;
    }
    if (!model_build(model)) {
        nmo_script_model_destroy(model);
        return NMO_ERR_NOMEM;
    }
    *out_model = model;
    return NMO_OK;
}

void nmo_script_model_destroy(nmo_script_model_t *model)
{
    if (model == NULL) {
        return;
    }
    free(model->nodes.data);
    free(model->roots.data);
    free(model->children.data);
    free(model->ios.data);
    free(model->params.data);
    free(model->operations.data);
    free(model->links.data);
    free(model->data_edges.data);
    free(model->node_map.slots);
    free(model->io_map.slots);
    free(model->param_map.slots);
    free(model->operation_map.slots);
    free((void *)model->links_by_source);
    free((void *)model->uses_by_source);
    free(model);
}

/* ============================================================================
 * Accessors
 * ============================================================================ */

#define MODEL_ARRAY_ACCESSOR(fn, type, field)                                  \
    const type *fn(const nmo_script_model_t *model, size_t *out_count)       \
    {                                                                          \
        if (out_count != NULL) {                                               \
            *out_count = model != NULL ? model->field.count : 0u;              \
        }                                                                      \
        return model != NULL ? (const type *)model->field.data : NULL;         \
    }

MODEL_ARRAY_ACCESSOR(nmo_script_model_nodes, nmo_script_node_t, nodes)
MODEL_ARRAY_ACCESSOR(nmo_script_model_roots, nmo_object_id_t, roots)
MODEL_ARRAY_ACCESSOR(nmo_script_model_children, nmo_object_id_t, children)
MODEL_ARRAY_ACCESSOR(nmo_script_model_ios, nmo_script_io_t, ios)
MODEL_ARRAY_ACCESSOR(nmo_script_model_params, nmo_script_param_t, params)
MODEL_ARRAY_ACCESSOR(nmo_script_model_operations, nmo_script_operation_t, operations)
MODEL_ARRAY_ACCESSOR(nmo_script_model_links, nmo_script_link_t, links)
MODEL_ARRAY_ACCESSOR(nmo_script_model_data_edges, nmo_script_data_edge_t, data_edges)

const nmo_script_node_t *nmo_script_model_find_node(const nmo_script_model_t *model,
                                                    nmo_object_id_t id)
{
    return model != NULL ? model_node(model, id) : NULL;
}

const nmo_script_io_t *nmo_script_model_find_io(const nmo_script_model_t *model,
                                                nmo_object_id_t id)
{
    uint32_t index;
    return model != NULL && model_map_get(&model->io_map, id, &index)
        ? MODEL_VEC_AT(model->ios, nmo_script_io_t, index) : NULL;
}

const nmo_script_param_t *nmo_script_model_find_param(const nmo_script_model_t *model,
                                                      nmo_object_id_t id)
{
    uint32_t index;
    return model != NULL && model_map_get(&model->param_map, id, &index)
        ? MODEL_VEC_AT(model->params, nmo_script_param_t, index) : NULL;
}

const nmo_script_operation_t *nmo_script_model_find_operation(const nmo_script_model_t *model,
                                                              nmo_object_id_t id)
{
    uint32_t index;
    return model != NULL && model_map_get(&model->operation_map, id, &index)
        ? MODEL_VEC_AT(model->operations, nmo_script_operation_t, index) : NULL;
}

const nmo_script_link_t *const *nmo_script_model_links_from_io(const nmo_script_model_t *model,
                                                               nmo_object_id_t io_id,
                                                               size_t *out_count)
{
    *out_count = 0;
    if (model == NULL || model->links.count == 0) {
        return NULL;
    }
    size_t lo = 0, hi = model->links.count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (model->links_by_source[mid]->source_io_id < io_id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    size_t end = lo;
    while (end < model->links.count && model->links_by_source[end]->source_io_id == io_id) {
        end++;
    }
    *out_count = end - lo;
    return end > lo ? &model->links_by_source[lo] : NULL;
}

const nmo_script_data_edge_t *const *nmo_script_model_param_uses(const nmo_script_model_t *model,
                                                                 nmo_object_id_t param_id,
                                                                 size_t *out_count)
{
    *out_count = 0;
    if (model == NULL || model->data_edges.count == 0) {
        return NULL;
    }
    size_t lo = 0, hi = model->data_edges.count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (model->uses_by_source[mid]->source_id < param_id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    size_t end = lo;
    while (end < model->data_edges.count && model->uses_by_source[end]->source_id == param_id) {
        end++;
    }
    *out_count = end - lo;
    return end > lo ? &model->uses_by_source[lo] : NULL;
}

nmo_status_t nmo_script_model_param_value(const nmo_script_model_t *model,
                                          nmo_object_id_t param_id,
                                          char *buffer,
                                          size_t buffer_size)
{
    if (model == NULL || buffer == NULL || buffer_size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    buffer[0] = '\0';
    nmo_object_t *object = nmo_object_repository_find_by_id(model->repo, param_id);
    nmo_class_id_t cid = object != NULL ? nmo_object_get_class_id(object) : 0;
    if (cid != NMO_CID_PARAMETERLOCAL && cid != NMO_CID_PARAMETEROUT &&
        cid != NMO_CID_PARAMETER) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_parameter_state_t *state =
        (const nmo_parameter_state_t *)nmo_object_get_state(object);
    if (state == NULL || !state->has_state) {
        return NMO_ERR_NOT_FOUND;
    }
    nmo_status_t status = nmo_behavior_param_value_to_string(state, model->registry,
                                                             model->workspace, buffer,
                                                             buffer_size);
    if (status == NMO_OK && buffer[0] == '\0') {
        return NMO_ERR_NOT_FOUND;
    }
    return status;
}

size_t nmo_script_model_label(const nmo_script_model_t *model,
                              nmo_object_id_t id,
                              char *buffer,
                              size_t buffer_size)
{
    int length;
    const nmo_script_node_t *node = nmo_script_model_find_node(model, id);
    const nmo_script_operation_t *op = node == NULL ? nmo_script_model_find_operation(model, id)
                                                    : NULL;
    if (node != NULL) {
        const char *name = node->name[0] != '\0' ? node->name : "(unnamed)";
        length = node->operation_name != NULL
            ? snprintf(buffer, buffer_size, "%s(%s)#%u", name, node->operation_name,
                       (unsigned)id)
            : snprintf(buffer, buffer_size, "%s#%u", name, (unsigned)id);
    } else if (op != NULL) {
        length = snprintf(buffer, buffer_size, "%s#%u", op->operation_name, (unsigned)id);
    } else {
        nmo_object_t *object = model != NULL
            ? nmo_object_repository_find_by_id(model->repo, id) : NULL;
        const char *name = model_object_name(object);
        length = snprintf(buffer, buffer_size, "%s#%u", name[0] != '\0' ? name : "(unnamed)",
                          (unsigned)id);
    }
    return length > 0 ? (size_t)length : 0u;
}

const char *nmo_script_node_kind_name(nmo_script_node_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_NODE_SCRIPT:         return "Script";
    case NMO_SCRIPT_NODE_GRAPH:          return "Graph";
    case NMO_SCRIPT_NODE_BUILDING_BLOCK: return "BB";
    default:                             return "?";
    }
}

const char *nmo_script_param_role_name(nmo_script_param_role_t role)
{
    switch (role) {
    case NMO_SCRIPT_PARAM_TARGET:           return "target";
    case NMO_SCRIPT_PARAM_INPUT:            return "pIn";
    case NMO_SCRIPT_PARAM_OUTPUT:           return "pOut";
    case NMO_SCRIPT_PARAM_LOCAL:            return "local";
    case NMO_SCRIPT_PARAM_OPERATION_INPUT:  return "operation pIn";
    case NMO_SCRIPT_PARAM_OPERATION_OUTPUT: return "operation pOut";
    default:                                return "?";
    }
}

const char *nmo_script_reach_name(nmo_script_reach_t reach)
{
    switch (reach) {
    case NMO_SCRIPT_REACH_LOCAL:            return "local";
    case NMO_SCRIPT_REACH_ANCESTOR_LOCAL:   return "ancestor local";
    case NMO_SCRIPT_REACH_FOREIGN_LOCAL:    return "foreign local";
    case NMO_SCRIPT_REACH_GRAPH_INPUT:      return "graph pIn";
    case NMO_SCRIPT_REACH_GRAPH_OUTPUT:     return "graph pOut";
    case NMO_SCRIPT_REACH_CHILD_INPUT:      return "pIn";
    case NMO_SCRIPT_REACH_CHILD_OUTPUT:     return "pOut";
    case NMO_SCRIPT_REACH_FOREIGN_INPUT:    return "foreign pIn";
    case NMO_SCRIPT_REACH_FOREIGN_OUTPUT:   return "foreign pOut";
    case NMO_SCRIPT_REACH_TARGET:           return "target";
    case NMO_SCRIPT_REACH_OPERATION_INPUT:  return "operation pIn";
    case NMO_SCRIPT_REACH_OPERATION_OUTPUT: return "operation";
    case NMO_SCRIPT_REACH_EXTERNAL:         return "external";
    default:                                return "?";
    }
}
