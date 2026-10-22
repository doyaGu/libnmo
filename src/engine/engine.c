/**
 * @file engine.c
 * @brief Behavior engine lifecycle, queries and tracing.
 */

#include "engine_internal.h"

#include "behavior/nmo_script_model.h"
#include "object/nmo_context.h"
#include "object/nmo_object_enum_defs.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Containers
 * ============================================================================ */

bool eng_vec_reserve(void **data, size_t *capacity, size_t needed, size_t item_size)
{
    if (needed <= *capacity) {
        return true;
    }
    size_t grown = *capacity ? *capacity * 2u : 16u;
    if (grown < needed) {
        grown = needed;
    }
    void *resized = realloc(*data, grown * item_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = grown;
    return true;
}

static size_t eng_map_hash(nmo_object_id_t key, size_t capacity)
{
    uint32_t h = (uint32_t)key * 2654435761u;
    return (size_t)h & (capacity - 1u);
}

int32_t eng_map_get(const eng_map_t *map, nmo_object_id_t key)
{
    if (map->capacity == 0 || key == 0) {
        return -1;
    }
    for (size_t i = eng_map_hash(key, map->capacity);; i = (i + 1u) & (map->capacity - 1u)) {
        if (map->slots[i].key == key) {
            return map->slots[i].value;
        }
        if (map->slots[i].key == 0) {
            return -1;
        }
    }
}

bool eng_map_put(eng_map_t *map, nmo_object_id_t key, int32_t value)
{
    if (key == 0) {
        return true;
    }
    if ((map->count + 1u) * 2u > map->capacity) {
        size_t capacity = map->capacity ? map->capacity * 2u : 64u;
        eng_map_slot_t *slots = (eng_map_slot_t *)calloc(capacity, sizeof(*slots));
        if (slots == NULL) {
            return false;
        }
        for (size_t i = 0; i < map->capacity; i++) {
            if (map->slots[i].key == 0) {
                continue;
            }
            size_t j = eng_map_hash(map->slots[i].key, capacity);
            while (slots[j].key != 0) {
                j = (j + 1u) & (capacity - 1u);
            }
            slots[j] = map->slots[i];
        }
        free(map->slots);
        map->slots = slots;
        map->capacity = capacity;
    }
    size_t i = eng_map_hash(key, map->capacity);
    while (map->slots[i].key != 0 && map->slots[i].key != key) {
        i = (i + 1u) & (map->capacity - 1u);
    }
    if (map->slots[i].key == 0) {
        map->count++;
    }
    map->slots[i].key = key;
    map->slots[i].value = value;
    return true;
}

void eng_emit(nmo_engine_t *engine, nmo_engine_event_kind_t kind, int32_t node,
              nmo_object_id_t object_id, int32_t result)
{
    if (engine->trace == NULL) {
        return;
    }
    nmo_engine_event_t event = {
        .kind = kind,
        .frame = engine->frame,
        .time = engine->time,
        .behavior_id = node >= 0 ? engine->nodes.data[node].id : 0,
        .object_id = object_id,
        .result = result,
        .depth = node >= 0 ? engine->nodes.data[node].depth : 0,
    };
    engine->trace(&event, engine->trace_user_data);
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

nmo_engine_options_t nmo_engine_options_default(void)
{
    nmo_engine_options_t options;
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.max_iterations = NMO_ENGINE_DEFAULT_MAX_ITERATIONS;
    options.unknown_block = NMO_ENGINE_UNKNOWN_BLOCK_STOP;
    options.random_seed = 1u;
    return options;
}

nmo_status_t nmo_engine_create(nmo_workspace_t *workspace,
                               const nmo_engine_options_t *options,
                               nmo_engine_t **out_engine)
{
    if (workspace == NULL || out_engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_engine = NULL;
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (ctx == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_engine_t *engine = (nmo_engine_t *)calloc(1, sizeof(*engine));
    if (engine == NULL) {
        return NMO_ERR_NOMEM;
    }
    engine->workspace = workspace;
    engine->types = nmo_context_get_type_registry(ctx);
    engine->operations_registry = nmo_context_get_operation_registry(ctx);
    engine->options = nmo_engine_options_default();
    if (options != NULL) {
        size_t size = options->struct_size;
        if (size == 0 || size > sizeof(engine->options)) {
            size = sizeof(engine->options);
        }
        memcpy(&engine->options, options, size);
        engine->options.struct_size = sizeof(engine->options);
    }
    if (engine->options.max_iterations <= 0) {
        engine->options.max_iterations = NMO_ENGINE_DEFAULT_MAX_ITERATIONS;
    }
    engine->random_state = (uint64_t)engine->options.random_seed * 0x9E3779B97F4A7C15ull + 1u;

    nmo_status_t status = nmo_script_model_build(workspace, &engine->model);
    if (status == NMO_OK) {
        status = eng_build(engine);
    }
    if (status != NMO_OK) {
        nmo_engine_destroy(engine);
        return status;
    }
    *out_engine = engine;
    return NMO_OK;
}

void nmo_engine_destroy(nmo_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    for (size_t i = 0; i < engine->params.count; i++) {
        free(engine->params.data[i].data);
    }
    for (size_t i = 0; i < engine->blocks.count; i++) {
        free(engine->blocks.data[i].name);
    }
    free(engine->nodes.data);
    free(engine->ios.data);
    free(engine->links.data);
    free(engine->params.data);
    free(engine->operations.data);
    free(engine->objects.data);
    free(engine->indexes.data);
    free(engine->blocks.data);
    free(engine->listed.data);
    free(engine->scratch_io.data);
    free(engine->node_map.slots);
    free(engine->io_map.slots);
    free(engine->param_map.slots);
    free(engine->operation_map.slots);
    free(engine->object_map.slots);
    nmo_script_model_destroy(engine->model);
    free(engine);
}

const nmo_script_model_t *nmo_engine_script_model(const nmo_engine_t *engine)
{
    return engine != NULL ? engine->model : NULL;
}

void nmo_engine_set_trace(nmo_engine_t *engine, nmo_engine_trace_fn fn, void *user_data)
{
    if (engine != NULL) {
        engine->trace = fn;
        engine->trace_user_data = user_data;
    }
}

uint64_t nmo_engine_frame(const nmo_engine_t *engine)
{
    return engine != NULL ? engine->frame : 0u;
}

double nmo_engine_time(const nmo_engine_t *engine)
{
    return engine != NULL ? engine->time : 0.0;
}

/* ============================================================================
 * Queries
 * ============================================================================ */

static const eng_node_t *eng_find_node(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = engine != NULL ? eng_map_get(&engine->node_map, id) : -1;
    return index >= 0 ? &engine->nodes.data[index] : NULL;
}

uint32_t nmo_engine_behavior_flags(const nmo_engine_t *engine, nmo_object_id_t behavior_id)
{
    const eng_node_t *node = eng_find_node(engine, behavior_id);
    return node != NULL ? node->flags : 0u;
}

bool nmo_engine_behavior_is_active(const nmo_engine_t *engine, nmo_object_id_t behavior_id)
{
    return (nmo_engine_behavior_flags(engine, behavior_id) & CKBEHAVIOR_ACTIVE) != 0;
}

uint64_t nmo_engine_behavior_execution_count(const nmo_engine_t *engine,
                                             nmo_object_id_t behavior_id)
{
    const eng_node_t *node = eng_find_node(engine, behavior_id);
    return node != NULL ? node->executions : 0u;
}

bool nmo_engine_io_is_active(const nmo_engine_t *engine, nmo_object_id_t io_id)
{
    int32_t index = engine != NULL ? eng_map_get(&engine->io_map, io_id) : -1;
    return index >= 0 && engine->ios.data[index].active;
}

nmo_status_t nmo_engine_param_value(const nmo_engine_t *engine,
                                    nmo_object_id_t param_id,
                                    const void **out_data,
                                    size_t *out_size,
                                    nmo_guid_t *out_type)
{
    if (engine == NULL || out_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    if (out_size != NULL) {
        *out_size = 0;
    }
    int32_t index = eng_map_get(&engine->param_map, param_id);
    if (index < 0) {
        return NMO_ERR_NOT_FOUND;
    }
    eng_param_t *param = eng_param_real_source((nmo_engine_t *)engine,
                                               &engine->params.data[index]);
    if (param == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    *out_data = param->data;
    if (out_size != NULL) {
        *out_size = param->size;
    }
    if (out_type != NULL) {
        *out_type = param->type;
    }
    return NMO_OK;
}

nmo_status_t nmo_engine_set_param_value(nmo_engine_t *engine,
                                        nmo_object_id_t param_id,
                                        const void *data,
                                        size_t size)
{
    if (engine == NULL || (data == NULL && size != 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int32_t index = eng_map_get(&engine->param_map, param_id);
    if (index < 0) {
        return NMO_ERR_NOT_FOUND;
    }
    eng_param_t *param = &engine->params.data[index];
    if (param->kind == ENG_PARAM_INPUT) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return eng_param_write(engine, param, data, size);
}

const char *nmo_engine_event_kind_name(nmo_engine_event_kind_t kind)
{
    switch (kind) {
    case NMO_ENGINE_EVENT_FRAME_BEGIN: return "frame";
    case NMO_ENGINE_EVENT_FRAME_END: return "frame end";
    case NMO_ENGINE_EVENT_ACTIVATE: return "activate";
    case NMO_ENGINE_EVENT_EXECUTE: return "execute";
    case NMO_ENGINE_EVENT_LINK: return "link";
    case NMO_ENGINE_EVENT_LINK_DELAYED: return "link delayed";
    case NMO_ENGINE_EVENT_LINK_ARRIVED: return "link arrived";
    case NMO_ENGINE_EVENT_OPERATION: return "operation";
    case NMO_ENGINE_EVENT_UNKNOWN_BLOCK: return "unknown block";
    case NMO_ENGINE_EVENT_INFINITE_LOOP: return "infinite loop";
    }
    return "?";
}
