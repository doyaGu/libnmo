/**
 * @file engine_build.c
 * @brief Build the engine state of a document from its script model.
 *
 * The engine's nodes, IOs, links and operations of a document follow its
 * script model's, after the ones the engine has already; IDs taken from the
 * document become engine IDs (eng_module_id).
 */

#include "engine_internal.h"

#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_script_model.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

/* The flags the engine keeps from a file: runtime bits start cleared, as after a load. */
#define ENG_RUNTIME_FLAGS                                                                   \
    (CKBEHAVIOR_ACTIVE | CKBEHAVIOR_WAITSFORMESSAGE | CKBEHAVIOR_RESERVED0 |                 \
     CKBEHAVIOR_EXECUTEDLASTFRAME | CKBEHAVIOR_DEACTIVATENEXTFRAME |                        \
     CKBEHAVIOR_RESETNEXTFRAME | CKBEHAVIOR_ACTIVATENEXTFRAME)

typedef struct eng_builder {
    nmo_engine_t *engine;
    const eng_module_t *module;
    nmo_object_repository_t *repo;
    /* The engine's counts before the document: its first node, IO, link, operation, param */
    size_t node_base, io_base, link_base, op_base, param_base;
} eng_builder_t;

/* The engine ID of an ID in the document being built */
#define B_ID(b, id) eng_module_id((b)->module, (id))

/* Reserve `count` index slots set to -1; returns the first, or UINT32_MAX on OOM. */
static uint32_t eng_alloc_indexes(nmo_engine_t *engine, size_t count)
{
    size_t first = engine->indexes.count;
    if (!eng_vec_reserve((void **)&engine->indexes.data, &engine->indexes.capacity,
                         first + count, sizeof(int32_t))) {
        return UINT32_MAX;
    }
    for (size_t i = 0; i < count; i++) {
        engine->indexes.data[first + i] = -1;
    }
    engine->indexes.count += count;
    return (uint32_t)first;
}

/* ============================================================================
 * Parameters
 * ============================================================================ */

bool eng_parameter_state_bytes(const eng_module_t *module, const nmo_parameter_state_t *state,
                               size_t fallback_size, uint8_t **out_data, uint32_t *out_size)
{
    const void *source = NULL;
    size_t size = fallback_size;
    uint32_t value = 0;
    switch (state->mode) {
    case CKPARAM_MODE_BUFFER:
        if (state->buffer_data.count > 0) {
            source = state->buffer_data.data;
            size = state->buffer_data.count;
        }
        break;
    case CKPARAM_MODE_OBJECT:
        value = (uint32_t)eng_module_id(module, nmo_parameter_object_id(state));
        source = &value;
        size = sizeof(value);
        break;
    case CKPARAM_MODE_MANAGER:
        value = state->manager_value;
        if (nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_MESSAGE)) {
            value = (uint32_t)eng_module_message_type(module, (int32_t)value);
        } else if (nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
            value = (uint32_t)eng_module_attribute_type(module, (int32_t)value);
        }
        source = &value;
        size = sizeof(value);
        break;
    default:
        break; /* no flat value saved: zeroes, so blocks can write it */
    }
    uint8_t *data = NULL;
    if (size > 0) {
        data = (uint8_t *)calloc(1, size);
        if (data == NULL) {
            return false;
        }
        if (source != NULL) {
            memcpy(data, source, size);
        }
    }
    *out_data = data;
    *out_size = (uint32_t)size;
    return true;
}

/* The value bytes a CKParameter holds in memory, from its saved state. */
static bool eng_param_load_value(const eng_module_t *module, eng_param_t *param,
                                 const nmo_parameter_state_t *state)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    /* A 2D curve holds a CK2dCurve, read from its sub-chunk */
    bool loaded = nmo_guid_equals(param->type, CKPGUID_2DCURVE)
                      ? eng_curve_value(state, &data, &size)
                      : eng_parameter_state_bytes(
                            module, state, param->type_desc != NULL ? param->type_desc->size : 0u,
                            &data, &size);
    if (!loaded) {
        return false;
    }
    free(param->data);
    param->data = data;
    param->size = size;
    return true;
}

/* The engine index of parameter `id` of the document, loading it (and what it reads or writes) once. */
static int32_t eng_load_param(eng_builder_t *b, nmo_object_id_t id)
{
    nmo_engine_t *engine = b->engine;
    if (id == 0) {
        return -1;
    }
    int32_t existing = eng_map_get(&engine->param_map, B_ID(b, id));
    if (existing >= 0) {
        return existing;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(b->repo, id);
    const void *state = object != NULL ? nmo_object_get_state(object) : NULL;
    if (state == NULL) {
        return -1;
    }
    nmo_class_id_t class_id = nmo_object_get_class_id(object);
    if (class_id != NMO_CID_PARAMETERIN && class_id != NMO_CID_PARAMETEROUT &&
        class_id != NMO_CID_PARAMETERLOCAL && class_id != NMO_CID_PARAMETER) {
        return -1;
    }

    eng_param_t *param = NULL;
    if (!ENG_VEC_PUSH(engine->params, param)) {
        return -2;
    }
    int32_t index = (int32_t)(engine->params.count - 1u);
    param->id = B_ID(b, id);
    param->source = -1;
    param->operation = -1;
    if (!eng_map_put(&engine->param_map, B_ID(b, id), index)) {
        return -2;
    }

    if (class_id == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)state;
        param->kind = ENG_PARAM_INPUT;
        param->type = pin->type_guid;
        param->type_desc = nmo_type_registry_find_by_guid(engine->types, param->type);
        param->shared = pin->is_shared != 0;
        int32_t source = eng_load_param(b, nmo_parameterin_source_id(pin));
        if (source == -2) {
            return -2;
        }
        engine->params.data[index].source = source;
        return index;
    }

    const nmo_parameter_state_t *base = (const nmo_parameter_state_t *)state;
    param->kind = class_id == NMO_CID_PARAMETEROUT     ? ENG_PARAM_OUTPUT
                  : class_id == NMO_CID_PARAMETERLOCAL ? ENG_PARAM_LOCAL
                                                       : ENG_PARAM_PLAIN;
    param->type = base->type_guid;
    param->type_desc = nmo_type_registry_find_by_guid(engine->types, param->type);
    if (!eng_param_load_value(b->module, param, base)) {
        return -2;
    }
    if (class_id == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameterlocal_state_t *local = (const nmo_parameterlocal_state_t *)state;
        if (local->is_myself) {
            param->myself_of = B_ID(b, nmo_parameterlocal_owner_id(local));
        }
    }
    if (class_id != NMO_CID_PARAMETEROUT) {
        return index;
    }

    /* An output writes its destinations: load them, then record their indexes */
    const nmo_parameterout_state_t *pout = (const nmo_parameterout_state_t *)state;
    for (uint32_t i = 0; i < pout->destination_count; i++) {
        if (eng_load_param(b, nmo_parameterout_destination_id(pout, i)) == -2) {
            return -2;
        }
    }
    uint32_t first = eng_alloc_indexes(engine, pout->destination_count);
    if (first == UINT32_MAX) {
        return -2;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < pout->destination_count; i++) {
        int32_t dest =
            eng_map_get(&engine->param_map, B_ID(b, nmo_parameterout_destination_id(pout, i)));
        if (dest >= 0 && dest != index) {
            engine->indexes.data[first + count++] = dest;
        }
    }
    param = &engine->params.data[index];
    param->first_destination = first;
    param->destination_count = count;
    return index;
}

/* Load the parameters of a reference array into index slots; false on OOM. */
static bool eng_load_param_array(eng_builder_t *b, const nmo_array_t *array, uint32_t *out_first,
                                 uint32_t *out_count)
{
    nmo_engine_t *engine = b->engine;
    size_t count = array != NULL ? array->count : 0u;
    for (size_t i = 0; i < count; i++) {
        if (eng_load_param(b, nmo_behavior_ref_array_get_id(array, i)) == -2) {
            return false;
        }
    }
    uint32_t first = eng_alloc_indexes(engine, count);
    if (first == UINT32_MAX) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        engine->indexes.data[first + i] =
            eng_map_get(&engine->param_map, B_ID(b, nmo_behavior_ref_array_get_id(array, i)));
    }
    *out_first = first;
    *out_count = (uint32_t)count;
    return true;
}

/* ============================================================================
 * Behaviors
 * ============================================================================ */

static const nmo_behavior_state_t *eng_behavior_state(eng_builder_t *b, nmo_object_id_t id)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(b->repo, id);
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_status_t eng_build_nodes(eng_builder_t *b)
{
    nmo_engine_t *engine = b->engine;
    size_t node_count = 0, io_count = 0, link_count = 0, op_count = 0, child_count = 0;
    const nmo_script_model_t *model = b->module->model;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(model, &node_count);
    const nmo_script_io_t *ios = nmo_script_model_ios(model, &io_count);
    const nmo_script_link_t *links = nmo_script_model_links(model, &link_count);
    const nmo_script_operation_t *ops = nmo_script_model_operations(model, &op_count);
    const nmo_object_id_t *children = nmo_script_model_children(model, &child_count);

    /* Nodes, IOs, links and operations keep the model's order, after the engine's */
    for (size_t i = 0; i < node_count; i++) {
        eng_node_t *node = NULL;
        if (!ENG_VEC_PUSH(engine->nodes, node) ||
            !eng_map_put(&engine->node_map, B_ID(b, nodes[i].id), (int32_t)(b->node_base + i))) {
            return NMO_ERR_NOMEM;
        }
        node->id = B_ID(b, nodes[i].id);
        node->depth = nodes[i].depth;
        node->parent = -1;
        node->is_block = nodes[i].kind == NMO_SCRIPT_NODE_BUILDING_BLOCK;
        node->prototype = nodes[i].prototype_guid;
        node->op_guid = nodes[i].operation_guid;
    }
    for (size_t i = 0; i < io_count; i++) {
        eng_io_t *io = NULL;
        if (!ENG_VEC_PUSH(engine->ios, io) ||
            !eng_map_put(&engine->io_map, B_ID(b, ios[i].id), (int32_t)(b->io_base + i))) {
            return NMO_ERR_NOMEM;
        }
        io->id = B_ID(b, ios[i].id);
        io->node = eng_map_get(&engine->node_map, B_ID(b, ios[i].node_id));
        io->is_output = ios[i].is_output;
    }
    for (size_t i = 0; i < link_count; i++) {
        eng_link_t *link = NULL;
        if (!ENG_VEC_PUSH(engine->links, link)) {
            return NMO_ERR_NOMEM;
        }
        link->id = B_ID(b, links[i].id);
        link->graph = eng_map_get(&engine->node_map, B_ID(b, links[i].graph_id));
        link->source = eng_map_get(&engine->io_map, B_ID(b, links[i].source_io_id));
        link->target = eng_map_get(&engine->io_map, B_ID(b, links[i].target_io_id));
        link->delay = links[i].activation_delay;
        link->initial_delay = links[i].initial_activation_delay;
    }
    for (size_t i = 0; i < op_count; i++) {
        eng_operation_t *op = NULL;
        if (!ENG_VEC_PUSH(engine->operations, op) ||
            !eng_map_put(&engine->operation_map, B_ID(b, ops[i].id), (int32_t)(b->op_base + i))) {
            return NMO_ERR_NOMEM;
        }
        op->id = B_ID(b, ops[i].id);
        op->guid = ops[i].operation_guid;
        nmo_object_id_t in1 = ops[i].input1_id, in2 = ops[i].input2_id, out = ops[i].output_id;
        int32_t p1 = eng_load_param(b, in1), p2 = eng_load_param(b, in2);
        int32_t po = eng_load_param(b, out);
        if (p1 == -2 || p2 == -2 || po == -2) {
            return NMO_ERR_NOMEM;
        }
        op = &engine->operations.data[b->op_base + i];
        op->in1 = p1;
        op->in2 = p2;
        op->out = po;
        if (po >= 0) {
            engine->params.data[po].operation = (int32_t)(b->op_base + i);
        }
    }

    for (size_t i = 0; i < node_count; i++) {
        const nmo_script_node_t *mnode = &nodes[i];
        const nmo_behavior_state_t *state = eng_behavior_state(b, mnode->id);
        if (state == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        eng_node_t *node = &engine->nodes.data[b->node_base + i];
        node->flags = state->flags & ~(uint32_t)ENG_RUNTIME_FLAGS;
        node->priority = state->priority;
        node->version = state->block_version;
        node->parent = eng_map_get(&engine->node_map, B_ID(b, mnode->parent_id));
        /* The model lists a graph before its sub-behaviors, so the parent's owner is set */
        node->owner = node->parent >= 0 ? engine->nodes.data[node->parent].owner
                                        : B_ID(b, mnode->owner_object_id);

        uint32_t first_input = eng_alloc_indexes(engine, state->inputs.count);
        uint32_t first_output = eng_alloc_indexes(engine, state->outputs.count);
        uint32_t first_child = eng_alloc_indexes(engine, mnode->child_count);
        uint32_t first_stack = eng_alloc_indexes(engine, mnode->child_count);
        uint32_t first_delayed = eng_alloc_indexes(engine, mnode->link_count);
        if (first_input == UINT32_MAX || first_output == UINT32_MAX ||
            first_child == UINT32_MAX || first_stack == UINT32_MAX ||
            first_delayed == UINT32_MAX) {
            return NMO_ERR_NOMEM;
        }
        uint32_t first_pin = 0, pin_count = 0, first_pout = 0, pout_count = 0;
        uint32_t first_local = 0, local_count = 0;
        if (!eng_load_param_array(b, &state->in_parameters, &first_pin, &pin_count) ||
            !eng_load_param_array(b, &state->out_parameters, &first_pout, &pout_count) ||
            !eng_load_param_array(b, &state->local_parameters, &first_local, &local_count) ||
            eng_load_param(b, nmo_behavior_target_parameter_id(state)) == -2) {
            return NMO_ERR_NOMEM;
        }

        node = &engine->nodes.data[b->node_base + i];
        node->first_input = first_input;
        node->input_count = (uint32_t)state->inputs.count;
        node->first_output = first_output;
        node->output_count = (uint32_t)state->outputs.count;
        for (size_t k = mnode->first_io; k < mnode->first_io + mnode->input_count +
                                                 mnode->output_count; k++) {
            const nmo_script_io_t *mio = &ios[k];
            uint32_t base = mio->is_output ? first_output : first_input;
            uint32_t limit = mio->is_output ? node->output_count : node->input_count;
            if (mio->index < limit) {
                engine->indexes.data[base + mio->index] = (int32_t)(b->io_base + k);
            }
        }
        node->first_pin = first_pin;
        node->pin_count = pin_count;
        node->first_pout = first_pout;
        node->pout_count = pout_count;
        node->first_local = first_local;
        node->local_count = local_count;
        nmo_object_id_t target_id = nmo_behavior_target_parameter_id(state);
        node->target = target_id != 0 ? eng_map_get(&engine->param_map, B_ID(b, target_id)) : -1;
        node->first_child = first_child;
        node->child_count = (uint32_t)mnode->child_count;
        for (size_t k = 0; k < mnode->child_count; k++) {
            engine->indexes.data[first_child + k] =
                eng_map_get(&engine->node_map, B_ID(b, children[mnode->first_child + k]));
        }
        node->first_stack = first_stack;
        node->first_link = (uint32_t)(b->link_base + mnode->first_link);
        node->link_count = (uint32_t)mnode->link_count;
        node->first_delayed = first_delayed;
    }
    return NMO_OK;
}

/* Sub-behaviors in CKBehavior::SortSubs order: higher priority first, stable otherwise. */
static void eng_sort_children(nmo_engine_t *engine, size_t first_node)
{
    for (size_t i = first_node; i < engine->nodes.count; i++) {
        eng_node_t *node = &engine->nodes.data[i];
        int32_t *children = &engine->indexes.data[node->first_child];
        uint32_t kept = 0;
        for (uint32_t k = 0; k < node->child_count; k++) {
            if (children[k] >= 0) {
                children[kept++] = children[k];
            }
        }
        node->child_count = kept;
        for (uint32_t k = 1; k < kept; k++) {
            int32_t moving = children[k];
            int32_t priority = engine->nodes.data[moving].priority;
            uint32_t j = k;
            while (j > 0 && engine->nodes.data[children[j - 1]].priority < priority) {
                children[j] = children[j - 1];
                j--;
            }
            children[j] = moving;
        }
    }
}

/* The links leaving each new IO, in the order of their graphs' link lists. */
static nmo_status_t eng_build_io_links(nmo_engine_t *engine, size_t first_io, size_t first_link)
{
    for (size_t i = first_link; i < engine->links.count; i++) {
        int32_t source = engine->links.data[i].source;
        if (source >= 0) {
            engine->ios.data[source].link_count++;
        }
    }
    for (size_t i = first_io; i < engine->ios.count; i++) {
        eng_io_t *io = &engine->ios.data[i];
        uint32_t first = eng_alloc_indexes(engine, io->link_count);
        if (first == UINT32_MAX) {
            return NMO_ERR_NOMEM;
        }
        io->first_link = first;
        io->link_count = 0;
    }
    for (size_t i = first_link; i < engine->links.count; i++) {
        int32_t source = engine->links.data[i].source;
        if (source >= 0) {
            eng_io_t *io = &engine->ios.data[source];
            engine->indexes.data[io->first_link + io->link_count++] = (int32_t)i;
        }
    }
    return NMO_OK;
}

/* "Myself" locals hold the object whose script their behavior is in. */
static void eng_resolve_myself(nmo_engine_t *engine, size_t first_param)
{
    for (size_t i = first_param; i < engine->params.count; i++) {
        eng_param_t *param = &engine->params.data[i];
        int32_t node = param->myself_of != 0 ? eng_map_get(&engine->node_map, param->myself_of)
                                             : -1;
        if (node < 0 || param->size < sizeof(uint32_t)) {
            continue;
        }
        uint32_t owner = (uint32_t)engine->nodes.data[node].owner;
        memcpy(param->data, &owner, sizeof(owner));
    }
}

/* ============================================================================
 * Script owners: the objects the behavior manager executes
 * ============================================================================ */

static nmo_status_t eng_build_objects(eng_builder_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_array_t views;
    nmo_status_t status = nmo_array_init(&views, sizeof(nmo_behavior_script_view_t), 32, NULL);
    if (status != NMO_OK) {
        return status;
    }
    if (nmo_behavior_query_collect_scripts(nmo_workspace_get_document(b->module->workspace),
                                           &views) != NMO_OK) {
        nmo_array_dispose(&views);
        return NMO_OK; /* no script owners: nothing runs, the model is still queryable */
    }
    const nmo_behavior_script_view_t *list = (const nmo_behavior_script_view_t *)views.data;

    /* Objects in the order they first own a script; their scripts follow in order */
    size_t first_object = engine->objects.count;
    for (size_t i = 0; i < views.count; i++) {
        if (eng_map_get(&engine->object_map, B_ID(b, list[i].owner_id)) >= 0) {
            continue;
        }
        eng_object_t *object = NULL;
        if (!ENG_VEC_PUSH(engine->objects, object) ||
            !eng_map_put(&engine->object_map, B_ID(b, list[i].owner_id),
                         (int32_t)(engine->objects.count - 1u))) {
            nmo_array_dispose(&views);
            return NMO_ERR_NOMEM;
        }
        object->id = B_ID(b, list[i].owner_id);
        object->order = (uint32_t)(engine->objects.count - 1u);
        object->active = true;
        nmo_object_t *owner = nmo_object_repository_find_by_id(b->repo, list[i].owner_id);
        const nmo_beobject_state_t *be =
            owner != NULL ? (const nmo_beobject_state_t *)
                                nmo_type_query_object_get_ancestor_state_by_guid(
                                    engine->types, owner, CKPGUID_BEOBJECT)
                          : NULL;
        object->priority = be != NULL ? be->priority : 0;
    }
    for (size_t o = first_object; o < engine->objects.count; o++) {
        eng_object_t *object = &engine->objects.data[o];
        uint32_t count = 0;
        for (size_t i = 0; i < views.count; i++) {
            count += B_ID(b, list[i].owner_id) == object->id;
        }
        uint32_t first = eng_alloc_indexes(engine, count);
        if (first == UINT32_MAX) {
            nmo_array_dispose(&views);
            return NMO_ERR_NOMEM;
        }
        object = &engine->objects.data[o];
        object->first_script = first;
        for (size_t i = 0; i < views.count; i++) {
            int32_t node = eng_map_get(&engine->node_map, B_ID(b, list[i].script_id));
            if (B_ID(b, list[i].owner_id) != object->id || node < 0) {
                continue;
            }
            engine->indexes.data[first + object->script_count++] = node;
            engine->nodes.data[node].owner = object->id;
        }
    }
    nmo_array_dispose(&views);

    /* A script's sub-behaviors share its owner */
    for (size_t i = b->node_base; i < engine->nodes.count; i++) {
        eng_node_t *node = &engine->nodes.data[i];
        if (node->parent >= 0) {
            node->owner = engine->nodes.data[node->parent].owner;
        }
    }
    return NMO_OK;
}

nmo_status_t eng_build_module(nmo_engine_t *engine, int32_t module)
{
    eng_builder_t b = {
        .engine = engine,
        .module = &engine->modules.data[module],
        .repo = engine->modules.data[module].repo,
        .node_base = engine->nodes.count,
        .io_base = engine->ios.count,
        .link_base = engine->links.count,
        .op_base = engine->operations.count,
        .param_base = engine->params.count,
    };
    if (b.repo == NULL || engine->types == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_status_t status = eng_build_nodes(&b);
    if (status == NMO_OK) {
        status = eng_build_io_links(engine, b.io_base, b.link_base);
    }
    if (status == NMO_OK) {
        status = eng_build_objects(&b);
    }
    if (status == NMO_OK) {
        eng_sort_children(engine, b.node_base);
        eng_resolve_myself(engine, b.param_base);
    }
    return status;
}
