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
    int32_t target;                        /* the target parameter; -1 when none */
    uint32_t first_child, child_count;     /* nodes, by priority */
    uint32_t first_link, link_count;       /* links of the graph */

    /* Graph run state; the storage is in engine->indexes. */
    uint32_t first_delayed, delayed_count; /* links waiting (BehaviorGraphData::m_Links) */
    uint32_t first_stack, stack_count;     /* sub-behaviors to execute this frame */
    bool in_stack;                         /* CKBEHAVIOR_RESERVED0 */

    uint64_t executions;
    void *state;            /* nmo_engine_block_state() */
    size_t state_size;
} eng_node_t;

typedef struct eng_object {
    nmo_object_id_t id;
    int32_t priority;
    uint32_t order;                    /* position in the document, for a stable sort */
    uint32_t first_script, script_count; /* nodes, in engine->indexes */
    bool listed;                       /* in the behavior manager's list */
    bool active;                       /* active in the scene (CK_SCENEOBJECT_ACTIVE) */
    int32_t next_frame;                /* > 0 add, < 0 remove at the next frame */
    int32_t waiting;                   /* CKBeObject::SetAsWaitingForMessages count */
} eng_object_t;

/* CK_MESSAGE_SENDINGTYPE */
#define ENG_MESSAGE_BROADCAST 1
#define ENG_MESSAGE_SINGLE 2
#define ENG_MESSAGE_GROUP 3

typedef struct eng_message {
    int32_t type;
    int32_t sending;              /* ENG_MESSAGE_* */
    nmo_object_id_t sender;
    nmo_object_id_t recipient;    /* single and group messages */
    nmo_class_id_t broadcast_cid; /* broadcast messages */
    int32_t sent_by;              /* the node that sent it, -1 for the host */
} eng_message_t;

/* A behavior waiting for a message (CKWaitingObject). */
typedef struct eng_wait {
    int32_t type;
    int32_t node;
    int32_t output;            /* io activated on arrival; -1 for none */
    nmo_object_id_t object;    /* the object the message has to reach */
} eng_wait_t;

/* A message an object received in the last dispatch (CKBeObject last frame messages). */
typedef struct eng_received {
    nmo_object_id_t object;
    int32_t type;
    nmo_object_id_t sender;
    uint32_t message; /* position among the messages delivered, to add a message once */
} eng_received_t;

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

#define ENG_KEY_COUNT 256

/* A key that went down or up since the previous frame (the input manager's key buffer). */
typedef struct eng_key_event {
    uint32_t key;
    bool pressed;
} eng_key_event_t;

/* A sound as the engine plays it: no audio, only its position in time. */
typedef struct eng_sound {
    nmo_object_id_t id;
    float length;    /* ms, as the file records it; 0 when it does not */
    float position;  /* ms since it started */
    float fade_left; /* ms of fade out before it stops; 0 when not stopping */
    bool playing;
    bool paused;
    bool loop;
} eng_sound_t;

/*
 * What the input manager reports during a frame. The host sets the `next`
 * state; at the start of a frame it becomes the frame's state and the key
 * buffer and mouse movement follow from the difference.
 */
typedef struct eng_input {
    uint8_t keys[ENG_KEY_COUNT];
    uint8_t next_keys[ENG_KEY_COUNT];
    eng_key_event_t buffer[ENG_KEY_COUNT];
    uint32_t buffer_count;
    float mouse[2], next_mouse[2];
    float relative[3];       /* x, y and the wheel */
    float next_wheel;
    bool buttons[3], next_buttons[3]; /* CK_MOUSE_BUTTON: left, right, middle */
    nmo_object_id_t under_mouse, next_under_mouse;
} eng_input_t;

/* An attribute type of the file's Attribute Manager */
typedef struct eng_attribute_type {
    bool present;
    nmo_guid_t parameter_type; /* null for an attribute without a value */
    int32_t compatible_class_id;
} eng_attribute_type_t;

/* An attribute an object has at run time, and its parameter's value */
typedef struct eng_attribute {
    nmo_object_id_t object;
    int32_t type;
    bool has_value;
    eng_param_t value;
} eng_attribute_t;

/* A CKTextureFont: only what tells fonts apart, as nothing draws them. */
typedef struct eng_font {
    char *name;
    nmo_object_id_t texture; /* the texture it reads, 0 for a system font's own */
} eng_font_t;

/* A CKDataArray cell: INT, FLOAT and OBJECT hold 32 bits, STRING text, PARAMETER bytes. */
typedef struct eng_cell {
    uint32_t value;
    char *text;     /* STRING; NULL for no string */
    uint8_t *data;  /* PARAMETER */
    uint32_t size;
} eng_cell_t;

typedef struct eng_column {
    int32_t type;           /* CK_ARRAYTYPE */
    nmo_guid_t param_type;  /* PARAMETER columns */
} eng_column_t;

/* A runtime copy of a CKDataArray; cells row by row. */
typedef struct eng_array {
    nmo_object_id_t id;
    uint32_t column_count;
    eng_column_t *columns;
    eng_cell_t *cells;
    size_t row_count;
    size_t row_capacity;
    int32_t key_column;     /* -1 when none */
} eng_array_t;

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
    ENG_VEC(eng_message_t) messages; /* sent this frame */
    ENG_VEC(eng_wait_t) waits;
    ENG_VEC(eng_received_t) received;
    ENG_VEC(char *) message_names;   /* the file's message types, by type */
    ENG_VEC(eng_array_t) arrays;

    eng_map_t node_map, io_map, param_map, operation_map, object_map, array_map;

    bool started;
    uint64_t frame;
    double time;
    float delta;
    uint64_t random_state;

    eng_input_t input;
    ENG_VEC(eng_sound_t) sounds;
    ENG_VEC(eng_font_t) fonts;
    ENG_VEC(eng_attribute_type_t) attribute_types;
    ENG_VEC(eng_attribute_t) attributes;

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
typedef struct nmo_parameter_state nmo_parameter_state_t;
/*
 * The bytes a CKParameter holds in memory, from its saved state, into a new
 * heap block; zeroes of `fallback_size` when the state holds no flat value.
 */
bool eng_parameter_state_bytes(const nmo_parameter_state_t *state, size_t fallback_size,
                               uint8_t **out_data, uint32_t *out_size);

/* engine_array.c */
nmo_status_t eng_load_arrays(nmo_engine_t *engine);
void eng_free_arrays(nmo_engine_t *engine);
eng_array_t *eng_find_array(const nmo_engine_t *engine, nmo_object_id_t id);
size_t eng_type_size(const nmo_engine_t *engine, nmo_guid_t type);

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

/* engine_message.c */
nmo_status_t eng_load_message_names(nmo_engine_t *engine);
void eng_free_messages(nmo_engine_t *engine);
nmo_status_t eng_send(nmo_engine_t *engine, const eng_message_t *message);
nmo_status_t eng_register_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos,
                               nmo_object_id_t object);
void eng_unregister_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos);
/* CKMessageManager::PostProcess: deliver this frame's messages. */
void eng_dispatch_messages(nmo_engine_t *engine);
/* CKMessageManager::OnCKReset: no waits and no messages. */
void eng_reset_messages(nmo_engine_t *engine);
/* The class of object `id` in the document; 0 when there is none. */
nmo_class_id_t eng_object_class(nmo_engine_t *engine, nmo_object_id_t id);
/* CKIsChildClassOf for class ids. */
bool eng_is_child_class(nmo_engine_t *engine, nmo_class_id_t class_id, nmo_class_id_t base);

/* engine_input.c */
/* InputManager::PreProcess: the host's state becomes the frame's. */
void eng_input_begin_frame(nmo_engine_t *engine);

/* engine_logics.c */
nmo_status_t eng_register_logics(nmo_engine_t *engine);

/* engine_narratives.c */
nmo_status_t eng_register_narratives(nmo_engine_t *engine);

/* engine_effects.c */
nmo_status_t eng_register_effects(nmo_engine_t *engine);

/* engine_controllers.c */
nmo_status_t eng_register_controllers(nmo_engine_t *engine);

/* engine_curve.c */
/* A 2D curve parameter's value (CK2dCurve::Read): a sorted curve, or (0,0)-(1,1) when none is saved. */
bool eng_curve_value(const nmo_parameter_state_t *state, uint8_t **out_data, uint32_t *out_size);

/* engine_attributes.c */
nmo_status_t eng_load_attributes(nmo_engine_t *engine);
void eng_free_attributes(nmo_engine_t *engine);
nmo_status_t eng_register_attributes(nmo_engine_t *engine);

/* engine_interface.c */
nmo_status_t eng_register_interface(nmo_engine_t *engine);
void eng_fonts_clear(nmo_engine_t *engine);

/* engine_strings.c */
nmo_status_t eng_register_strings(nmo_engine_t *engine);
/* CKParameter::GetStringValue: a new heap string, NULL when the type has no text. */
char *eng_param_text(nmo_engine_t *engine, const eng_param_t *param);
/* CKParameter::SetStringValue: the value read from text, written as an output writes it. */
nmo_status_t eng_param_set_text(nmo_engine_t *engine, eng_param_t *param, const char *text);

/* engine_media.c */
nmo_status_t eng_register_media(nmo_engine_t *engine);
/* The sounds move on by the frame time (the sound manager's frame). */
void eng_sounds_begin_frame(nmo_engine_t *engine);

#endif /* NMO_ENGINE_INTERNAL_H */
