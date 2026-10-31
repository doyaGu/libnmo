/**
 * @file engine_internal.h
 * @brief The behavior engine's state, made of its parts' (one header each),
 *        shared by its source files.
 *
 * The runtime (the documents, their behavior graphs and the objects owning
 * them) is the engine itself; each manager the blocks talk to keeps its
 * state in a member of its own.
 */

#ifndef NMO_ENGINE_INTERNAL_H
#define NMO_ENGINE_INTERNAL_H

#include "engine_array.h"
#include "engine_attributes.h"
#include "engine_base.h"
#include "engine_blocks.h"
#include "engine_graph.h"
#include "engine_host.h"
#include "engine_input.h"
#include "engine_manager.h"
#include "engine_media.h"
#include "engine_message.h"
#include "engine_module.h"
#include "engine_render.h"
#include "engine_scene.h"
#include "engine_transform.h"
#include "engine_tt.h"

struct nmo_engine {
    nmo_workspace_t *workspace;
    ENG_VEC(eng_module_t) modules; /* the first is the document the engine was made for */
    nmo_type_registry_t *types;
    nmo_operation_registry_t *operations_registry;
    ENG_VEC(eng_op_lookup_t) op_lookups;
    nmo_engine_options_t options;

    /* The behavior graphs and their owners */
    ENG_VEC(eng_node_t) nodes;
    ENG_VEC(eng_io_t) ios;
    ENG_VEC(eng_link_t) links;
    ENG_VEC(eng_param_t) params;
    ENG_VEC(eng_operation_t) operations;
    ENG_VEC(eng_object_t) objects;
    ENG_VEC(int32_t) indexes;
    ENG_VEC(eng_block_entry_t) blocks;
    ENG_VEC(int32_t) listed;     /* objects in the behavior manager's list, by priority */
    ENG_VEC(int32_t) scratch_io; /* CheckIOsActivation's IO lists */
    eng_map_t node_map, io_map, param_map, operation_map, object_map;

    /* The objects the engine models */
    ENG_VEC(eng_array_t) arrays;
    ENG_VEC(eng_group_t) groups;
    ENG_VEC(eng_entity_t) entities;
    eng_map_t array_map, group_map, entity_map;
    /* The objects in the level scene (CKBeObject::IsInScene); every object without one */
    eng_map_t scene_objects;
    eng_map_t destroyed; /* the objects nmo_engine_destroy_object() deleted */
    bool has_level_scene;
    eng_ics_t ics;
    eng_dynamic_t dynamic;

    bool started;
    uint64_t frame;
    double time;
    float delta;
    uint64_t random_state;
    eng_profile_t profile;
    nmo_engine_time_settings_t time_settings; /* CKTimeManager's */

    nmo_engine_trace_fn trace;
    void *trace_user_data;

    /* The managers */
    eng_messages_t messages;
    eng_attributes_t attributes;
    eng_input_t input;
    eng_media_t media;
    eng_render_t render;
    eng_tt_t tt;
    eng_host_t host;
    /* The managers the block packs added, in the order they were added */
    ENG_VEC(eng_manager_t) managers;
};

static inline int32_t *eng_index(nmo_engine_t *engine, uint32_t at)
{
    return &engine->indexes.data[at];
}

static inline eng_io_t *eng_node_io(nmo_engine_t *engine, const eng_node_t *node,
                                    bool output, int pos)
{
    uint32_t count = output ? node->output_count : node->input_count;
    if (pos < 0 || (uint32_t)pos >= count) {
        return NULL;
    }
    int32_t io = *eng_index(engine, (output ? node->first_output : node->first_input) + (uint32_t)pos);
    return io >= 0 ? &engine->ios.data[io] : NULL;
}

static inline eng_param_t *eng_node_param(nmo_engine_t *engine, uint32_t first, uint32_t count,
                                          int pos)
{
    if (pos < 0 || (uint32_t)pos >= count) {
        return NULL;
    }
    int32_t param = *eng_index(engine, first + (uint32_t)pos);
    return param >= 0 ? &engine->params.data[param] : NULL;
}

#endif /* NMO_ENGINE_INTERNAL_H */
