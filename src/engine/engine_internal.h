/**
 * @file engine_internal.h
 * @brief State of the behavior engine, shared by its source files.
 */

#ifndef NMO_ENGINE_INTERNAL_H
#define NMO_ENGINE_INTERNAL_H

#include "engine/nmo_engine.h"
#include "engine/nmo_engine_block.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Every index below is a position in the engine array of that kind; -1 is none. */

typedef enum eng_param_kind {
    ENG_PARAM_INPUT = 0, /* CKParameterIn: reads its source */
    ENG_PARAM_OUTPUT,    /* CKParameterOut */
    ENG_PARAM_LOCAL,     /* CKParameterLocal */
    ENG_PARAM_PLAIN,     /* CKParameter */
} eng_param_kind_t;

typedef struct eng_param {
    nmo_object_id_t id;
    eng_param_kind_t kind;
    nmo_guid_t type;
    const nmo_type_descriptor_t *type_desc;
    uint8_t *data;
    uint32_t size;
    int32_t source;             /* input: the parameter it reads, an input when shared */
    bool shared;
    int32_t operation;          /* the operation whose output this is */
    nmo_object_id_t myself_of;  /* a "myself" local: the behavior whose owner it holds */
    uint32_t first_destination; /* output: the parameters it writes, in engine->indexes */
    uint32_t destination_count;
} eng_param_t;

typedef struct eng_io {
    nmo_object_id_t id;
    int32_t node;
    bool is_output;
    bool active;
    uint32_t first_link; /* the links leaving it, in engine->indexes */
    uint32_t link_count;
} eng_io_t;

typedef struct eng_link {
    nmo_object_id_t id;
    int32_t graph;
    int32_t source; /* io */
    int32_t target; /* io */
    int32_t delay;
    int32_t initial_delay;
    bool in_delayed_list;
} eng_link_t;

typedef struct eng_operation {
    nmo_object_id_t id;
    nmo_guid_t guid;
    int32_t in1, in2, out; /* params */
    bool running;          /* guards an operation that reads its own output */
} eng_operation_t;

typedef struct eng_block_entry {
    nmo_engine_block_desc_t desc;
    char *name;
} eng_block_entry_t;

typedef struct eng_node {
    nmo_object_id_t id;
    int32_t parent;
    uint32_t depth;
    uint32_t flags;         /* CKBEHAVIOR_* */
    int32_t priority;
    uint32_t version;
    nmo_guid_t prototype;
    nmo_object_id_t owner;  /* the object whose script holds it */
    bool is_block;
    bool unknown_reported;
    const eng_block_entry_t *block;
    nmo_engine_block_fn fn;
    nmo_guid_t op_guid;     /* an Op building block's operation */

    /* Ranges of engine->indexes, by position; a missing item is -1. */
    uint32_t first_input, input_count;     /* ios */
    uint32_t first_output, output_count;   /* ios */
    uint32_t first_pin, pin_count;         /* params */
    uint32_t first_pout, pout_count;       /* params */
    uint32_t first_local, local_count;     /* params */
    uint32_t first_child, child_count;     /* nodes, by priority */
    uint32_t first_link, link_count;       /* links of the graph */

    /* Graph run state; the storage is in engine->indexes. */
    uint32_t first_delayed, delayed_count; /* links waiting (BehaviorGraphData::m_Links) */
    uint32_t first_stack, stack_count;     /* sub-behaviors to execute this frame */
    bool in_stack;                         /* CKBEHAVIOR_RESERVED0 */

    uint64_t executions;
} eng_node_t;

typedef struct eng_object {
    nmo_object_id_t id;
    int32_t priority;
    uint32_t order;                    /* position in the document, for a stable sort */
    uint32_t first_script, script_count; /* nodes, in engine->indexes */
    bool listed;                       /* in the behavior manager's list */
    int32_t next_frame;                /* > 0 add, < 0 remove at the next frame */
} eng_object_t;

typedef struct eng_map_slot {
    nmo_object_id_t key;
    int32_t value;
} eng_map_slot_t;

typedef struct eng_map {
    eng_map_slot_t *slots;
    size_t capacity;
    size_t count;
} eng_map_t;

#define ENG_VEC(type) struct { type *data; size_t count; size_t capacity; }

struct nmo_engine_block {
    nmo_engine_t *engine;
    int32_t node;
};

struct nmo_engine {
    nmo_workspace_t *workspace;
    nmo_script_model_t *model;
    nmo_type_registry_t *types;
    nmo_operation_registry_t *operations_registry;
    nmo_engine_options_t options;

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

    bool started;
    uint64_t frame;
    double time;
    float delta;
    uint64_t random_state;

    nmo_engine_trace_fn trace;
    void *trace_user_data;
};

/* engine.c */
bool eng_vec_reserve(void **data, size_t *capacity, size_t needed, size_t item_size);
#define ENG_VEC_PUSH(vec, out_item)                                                         \
    (eng_vec_reserve((void **)&(vec).data, &(vec).capacity, (vec).count + 1u,               \
                     sizeof(*(vec).data))                                                   \
         ? ((out_item) = &(vec).data[(vec).count++], memset((out_item), 0, sizeof(*(out_item))), true) \
         : false)
int32_t eng_map_get(const eng_map_t *map, nmo_object_id_t key);
bool eng_map_put(eng_map_t *map, nmo_object_id_t key, int32_t value);
void eng_emit(nmo_engine_t *engine, nmo_engine_event_kind_t kind, int32_t node,
              nmo_object_id_t object_id, int32_t result);

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

/* engine_build.c */
nmo_status_t eng_build(nmo_engine_t *engine);

/* engine_param.c */
/* The parameter holding the value an input reads (GetRealSource); NULL when none. */
eng_param_t *eng_param_real_source(nmo_engine_t *engine, eng_param_t *param);
/* Run the operation `param` is the output of, if any (CKParameterOut::Update). */
void eng_param_update(nmo_engine_t *engine, eng_param_t *param);
/* Store `size` bytes as `param`'s value, then copy it to its destinations (DataChanged). */
nmo_status_t eng_param_write(nmo_engine_t *engine, eng_param_t *param, const void *data,
                             size_t size);
/* CKParameter::CopyValue: `dest` takes `source`'s value when the types are compatible. */
nmo_status_t eng_param_copy(nmo_engine_t *engine, eng_param_t *dest, eng_param_t *source,
                            bool update_source);
/* Run operation `op` (CKParameterOperation::DoOperation). */
nmo_status_t eng_operation_run(nmo_engine_t *engine, eng_operation_t *op);
nmo_status_t eng_operation_execute(nmo_engine_t *engine, nmo_guid_t guid, eng_param_t *in1,
                                   eng_param_t *in2, eng_param_t *out);

/* engine_schedule.c */
void eng_behavior_activate(nmo_engine_t *engine, int32_t node, bool active, bool reset);
void eng_behavior_reset(nmo_engine_t *engine, int32_t node);
nmo_status_t eng_manager_execute(nmo_engine_t *engine);
void eng_object_next_frame(nmo_engine_t *engine, int32_t object, int32_t delta);
void eng_sort_listed(nmo_engine_t *engine);
int eng_block_call(nmo_engine_t *engine, int32_t node, nmo_engine_block_fn fn);
int eng_block_callback(nmo_engine_t *engine, int32_t node, uint32_t message);

/* engine_block.c */
void eng_bind_blocks(nmo_engine_t *engine);
nmo_guid_t eng_block_operation_guid(const nmo_engine_block_t *block);
/* Run `guid` over the block's first two inputs into its first output (the Op block). */
nmo_status_t eng_block_run_operation(nmo_engine_block_t *block, nmo_guid_t guid);

/* engine_logics.c */
nmo_status_t eng_register_logics(nmo_engine_t *engine);

#endif /* NMO_ENGINE_INTERNAL_H */
