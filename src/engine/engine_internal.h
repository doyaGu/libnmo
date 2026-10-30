/**
 * @file engine_internal.h
 * @brief State of the behavior engine, shared by its source files.
 */

#ifndef NMO_ENGINE_INTERNAL_H
#define NMO_ENGINE_INTERNAL_H

#include "behavior/nmo_script_model.h"
#include "engine/nmo_engine.h"
#include "engine/nmo_engine_block.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_document.h"
#include "session/nmo_session.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The script flags asking for a change at the next frame (CKScene::ActivateScript) */
#define ENG_NEXT_FRAME_FLAGS                                                                \
    (CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_DEACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME)

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
    bool destroyed;                    /* nmo_engine_destroy_object() */
    int32_t next_frame;                /* adds less removes asked for the next frame */
    bool next_frame_asked;             /* asked at all: added unless next_frame < 0 */
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

/* A texture's bitmap slots as CKBitmapData has them: a loaded movie's frames, or its own. */
typedef struct eng_texture {
    nmo_object_id_t id;
    int32_t slot_count;
    int32_t current_slot;
    bool movie;
} eng_texture_t;

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
    bool cursor_hidden; /* CKInputManager::ShowCursor(FALSE); the cursor starts shown */
} eng_input_t;

/* An attribute type of the engine, from a document's Attribute Manager */
typedef struct eng_attribute_type {
    bool present;
    uint32_t flags; /* CK_ATTRIBUT_FLAGS */
    char *name;
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
    bool sorted;            /* m_Order: sorted ascending on sort_column, and not changed since */
    int32_t sort_column;    /* m_ColumnIndex */
} eng_array_t;

struct nmo_engine_block {
    nmo_engine_t *engine;
    int32_t node;
};

/*
 * A document the engine runs: the one it was made for, then each file loaded
 * since. Its objects have engine IDs offset + 1 to limit (its IDs plus the
 * offset); its message and attribute types map to the engine's.
 */
/*
 * A document the engine runs, or part of one: the objects of its session
 * whose IDs are in [first_local, last_local]. An engine ID is the document
 * ID plus `offset` (wrapping, so a window far into a session can start right
 * after the engine's last ID); `limit` is the engine ID of last_local. The
 * copies Object Copy makes in a loaded file's session are a module of their
 * own that shares that session.
 */
typedef struct eng_module {
    const struct nmo_engine *engine;
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
    nmo_script_model_t *model;
    nmo_object_id_t offset;
    nmo_object_id_t limit;
    nmo_object_id_t first_local, last_local;
    int32_t *message_types;
    uint32_t message_type_count;
    int32_t *attribute_types;
    uint32_t attribute_type_count;
    char *name;              /* a loaded file: the name it was loaded by */
    nmo_session_t *session;  /* what the engine made (and frees): a loaded file's, the created objects' */
    nmo_document_t *document;
    bool created;            /* the objects blocks create */
} eng_module_t;

/* Part of a module to build or load: its objects with IDs in [first, last] */
typedef struct eng_range {
    int32_t module;
    nmo_object_id_t first, last;
} eng_range_t;

/* A VxMatrix: rows 0 to 2 the X, Y and Z axes, row 3 the position. */
typedef struct eng_matrix {
    float m[4][4];
} eng_matrix_t;

typedef struct eng_vector {
    float x, y, z;
} eng_vector_t;

/*
 * A CK3dEntity's matrices and place in the hierarchy: its parent and its
 * children, in order (indexes into the engine's entities, -1 for none).
 */
typedef struct eng_entity {
    nmo_object_id_t id;
    eng_matrix_t world;
    eng_matrix_t local;
    uint32_t moveable_flags;
    int32_t parent;
    int32_t first_child, last_child;
    int32_t next_sibling, prev_sibling;
    bool destroyed;
} eng_entity_t;

/* A CKGroup: its objects now, by engine ID. */
typedef struct eng_group {
    nmo_object_id_t id;
    nmo_object_id_t *objects;
    size_t count;
    size_t capacity;
} eng_group_t;

/* An operation function the engine runs itself (engine_operations.c). */
typedef nmo_status_t (*eng_builtin_op_fn)(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out);

/* The function an operation found for these types, kept (neither for none). */
typedef struct eng_op_lookup {
    nmo_guid_t guid;
    const nmo_type_descriptor_t *t1, *t2, *tr;
    const nmo_operation_tree_cell_t *cell;
    eng_builtin_op_fn builtin;
    bool swap;
} eng_op_lookup_t;

/* A registry value: `name` of key `section`, as text. */
typedef struct eng_registry_value {
    char *section;
    char *name;
    char *value;
} eng_registry_value_t;

/* A physical object (Physicalize): its parameters and the provider's handle */
typedef struct eng_physics_object {
    nmo_engine_physics_object_t desc; /* its arrays and strings are the fields below */
    nmo_object_id_t *meshes;          /* the convex, then the concave */
    char *surface;
    char *group;
    void *handle;
} eng_physics_object_t;

typedef struct eng_physics_constraint {
    nmo_engine_physics_constraint_t desc;
    void *handle;
    bool alive; /* not removed by Shutdown */
} eng_physics_constraint_t;

/* A call kept until its objects are physical: a constraint kind, or -1 for a wake-up */
typedef struct eng_physics_call {
    int kind;
    int32_t node;
} eng_physics_call_t;

/* CKIpionManager */
typedef struct eng_physics {
    ENG_VEC(eng_physics_object_t) objects;
    ENG_VEC(eng_physics_constraint_t) constraints;
    ENG_VEC(eng_physics_call_t) calls;
    ENG_VEC(char *) surfaces; /* the polygon shapes made, by name */
    nmo_engine_physics_provider_t provider;
    bool has_provider;
    float gravity[3];
    double time_factor;   /* m_PhysicsTimeFactor: the step per ms of frame */
    double delta;         /* m_DeltaTime: the frame time, smoothed */
    double physics_delta; /* m_PhysicsDeltaTime: the last step */
} eng_physics_t;

struct nmo_engine {
    nmo_workspace_t *workspace;
    ENG_VEC(eng_module_t) modules; /* the first is the document the engine was made for */
    nmo_type_registry_t *types;
    nmo_operation_registry_t *operations_registry;
    ENG_VEC(eng_op_lookup_t) op_lookups;
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
    ENG_VEC(eng_group_t) groups;
    ENG_VEC(eng_entity_t) entities;
    eng_map_t entity_map;

    eng_map_t node_map, io_map, param_map, operation_map, object_map, array_map, group_map;
    /* The objects in the level scene (CKBeObject::IsInScene); every object without one */
    eng_map_t scene_objects;
    eng_map_t destroyed; /* the objects nmo_engine_destroy_object() deleted */
    eng_map_t video_textures; /* the textures TT_PreloadTextures put in video memory */
    bool has_level_scene;

    bool started;
    uint64_t frame;
    double time;
    float delta;
    uint64_t random_state;

    eng_input_t input;
    ENG_VEC(eng_sound_t) sounds;
    ENG_VEC(eng_texture_t) textures;
    ENG_VEC(eng_font_t) fonts;
    ENG_VEC(eng_attribute_type_t) attribute_types;
    ENG_VEC(eng_attribute_t) attributes;

    nmo_engine_trace_fn trace;
    void *trace_user_data;
    nmo_engine_resolve_file_fn resolve_file;
    void *resolve_file_user_data;
    float tt_time;         /* TT Time Manager: TT_Timer's time, and whether it runs */
    bool tt_timing;
    char *database_file;   /* TT Database Manager: TT Set Database Properties */
    bool database_crypted;
    ENG_VEC(eng_registry_value_t) registry; /* the TT blocks' registry, from the host */
    eng_physics_t physics;
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

/* engine_module.c */
/*
 * Add a document: its ID range, script model, and message and attribute
 * types. `session` and `document` are the engine's to free with it (a loaded
 * file), or NULL.
 */
nmo_status_t eng_module_open(nmo_engine_t *engine, nmo_workspace_t *workspace,
                             nmo_session_t *session, nmo_document_t *document,
                             int32_t *out_module);
void eng_free_modules(nmo_engine_t *engine);
/* The document of engine ID `id`, and its ID there; NULL when none has it. */
const eng_module_t *eng_module_of(const nmo_engine_t *engine, nmo_object_id_t id,
                                  nmo_object_id_t *out_local);
/* The objects of class `class_id` (exactly), not deleted, by engine ID (their creation order) */
size_t eng_objects_of_class(nmo_engine_t *engine, uint32_t class_id, nmo_object_id_t **out_ids);
nmo_object_t *eng_find_object(const nmo_engine_t *engine, nmo_object_id_t id);
/* The first object named `name` (of class `class_id` or one derived, unless 0), in document order */
nmo_object_id_t eng_object_named(const nmo_engine_t *engine, const char *name, uint32_t class_id);
nmo_object_id_t eng_create_object(nmo_engine_t *engine, uint32_t class_id, const char *name);

/* engine_transform.c */
void eng_matrix_identity(eng_matrix_t *m);
void eng_matrix_multiply(eng_matrix_t *result, const eng_matrix_t *a, const eng_matrix_t *b);
void eng_matrix_inverse(eng_matrix_t *result, const eng_matrix_t *a);
void eng_vector_transform(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v);
void eng_vector_rotate(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v);
float eng_vector_magnitude(const eng_vector_t *v);
void eng_vector_normalize(eng_vector_t *v);
eng_vector_t eng_vector_cross(const eng_vector_t *a, const eng_vector_t *b);
void eng_matrix_from_rotation(eng_matrix_t *result, const eng_vector_t *axis, float angle);
void eng_matrix_from_euler(eng_matrix_t *result, float ax, float ay, float az);
void eng_matrix_to_euler(const eng_matrix_t *m, float *ax, float *ay, float *az);
nmo_status_t eng_load_entities(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_entities(nmo_engine_t *engine);
nmo_status_t eng_add_entity(nmo_engine_t *engine, nmo_object_id_t id);
int32_t eng_entity_index(const nmo_engine_t *engine, nmo_object_id_t id);
void eng_entity_set_world_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children);
void eng_entity_set_local_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children);
void eng_entity_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                          const eng_vector_t *src, int32_t ref);
void eng_entity_inverse_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                  const eng_vector_t *src, int32_t ref);
void eng_entity_transform_vector(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                 const eng_vector_t *src, int32_t ref, bool inverse_transform);
void eng_entity_set_position(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                             int32_t ref, bool keep_children);
eng_vector_t eng_entity_position(nmo_engine_t *engine, int32_t index, int32_t ref);
void eng_entity_translate(nmo_engine_t *engine, int32_t index, const eng_vector_t *vector,
                          int32_t ref, bool keep_children);
void eng_entity_rotate(nmo_engine_t *engine, int32_t index, const eng_vector_t *axis, float angle,
                       int32_t ref, bool keep_children);
eng_vector_t eng_entity_scale(nmo_engine_t *engine, int32_t index, bool local);
void eng_entity_set_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local);
void eng_entity_add_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local);
void eng_entity_set_orientation(nmo_engine_t *engine, int32_t index, const eng_vector_t *dir,
                                const eng_vector_t *up, const eng_vector_t *right, int32_t ref,
                                bool keep_children);
void eng_entity_orientation(nmo_engine_t *engine, int32_t index, eng_vector_t *dir,
                            eng_vector_t *up, eng_vector_t *right, int32_t ref);
void eng_entity_look_at(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                        int32_t ref, bool keep_children);
bool eng_entity_set_parent(nmo_engine_t *engine, int32_t index, int32_t parent,
                           bool keep_world_position);
void eng_entity_destroy(nmo_engine_t *engine, nmo_object_id_t id);
bool eng_entity_bary_center(nmo_engine_t *engine, int32_t index, eng_vector_t *out);
bool eng_entity_has_mesh(nmo_engine_t *engine, int32_t index);
float eng_entity_radius(nmo_engine_t *engine, int32_t index);
bool eng_entity_world_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min,
                          eng_vector_t *max);
bool eng_ray_box(const eng_vector_t *origin, const eng_vector_t *dir, const eng_vector_t *min,
                 const eng_vector_t *max);
/* A ray's hit on an entity's mesh, in the entity's space */
typedef struct eng_ray_hit {
    eng_vector_t point;
    eng_vector_t normal;     /* the vertex normals interpolated, not normalized */
    int32_t face;
    int32_t vertices[3];     /* the face's vertex indices */
    eng_vector_t corners[3]; /* and their positions */
} eng_ray_hit_t;
int eng_entity_ray_intersection(nmo_engine_t *engine, int32_t index, const eng_vector_t *pos1,
                                const eng_vector_t *pos2, eng_ray_hit_t *hit);

/* engine_copy.c */
#define ENG_DEPENDENCY_CLASSES 128
/* A CKDependencies: CK_DEPENDENCIES_FLAGS, and each class's dependency mask */
typedef struct eng_dependencies {
    uint32_t flags;
    uint32_t classes[ENG_DEPENDENCY_CLASSES];
} eng_dependencies_t;
void eng_default_copy_dependencies(eng_dependencies_t *deps);
void eng_dependencies_from_param(nmo_engine_t *engine, nmo_object_id_t param_id,
                                 eng_dependencies_t *deps);
nmo_status_t eng_copy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids, int32_t count,
                              const eng_dependencies_t *deps, bool activate,
                              nmo_object_id_t *out_copies);
void eng_copy_attributes(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to);
void eng_copy_array(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to);
void eng_copy_group(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to,
                    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);

/* engine_physics.c */
nmo_status_t eng_register_physics(nmo_engine_t *engine);
void eng_physics_init(nmo_engine_t *engine);
void eng_free_physics(nmo_engine_t *engine);
void eng_physics_post_process(nmo_engine_t *engine);

/* engine_3dtrans.c */
nmo_status_t eng_register_3dtrans(nmo_engine_t *engine);
/* The engine ID of an object of a module's session outside its window (engine_module.c) */
nmo_object_id_t eng_module_id_outside(const eng_module_t *module, nmo_object_id_t local);
/* The engine ID of a document's object ID; 0 stays 0. */
static inline nmo_object_id_t eng_module_id(const eng_module_t *module, nmo_object_id_t local)
{
    if (local == 0 || module == NULL) {
        return local;
    }
    if (local >= module->first_local && local <= module->last_local) {
        return local + module->offset;
    }
    return eng_module_id_outside(module, local);
}
static inline bool eng_module_has(const eng_module_t *module, nmo_object_id_t local)
{
    return local >= module->first_local && local <= module->last_local;
}
nmo_object_id_t eng_next_engine_id(const nmo_engine_t *engine);
/* The whole window of a module */
eng_range_t eng_module_range(const nmo_engine_t *engine, int32_t module);
nmo_status_t eng_load_range(nmo_engine_t *engine, int32_t module, nmo_object_id_t first,
                            nmo_object_id_t last);
/* A document's message or attribute type as the engine's */
int32_t eng_module_message_type(const eng_module_t *module, int32_t type);
int32_t eng_module_attribute_type(const eng_module_t *module, int32_t type);
/* The engine message type named `name`, made when there is none (AddMessageType). */
int32_t eng_message_type_add(nmo_engine_t *engine, const char *name);

/* engine_build.c */
/* Build the behaviors, parameters and script owners of document `module`. */
nmo_status_t eng_build_module(nmo_engine_t *engine, const eng_range_t *range);
typedef struct nmo_parameter_state nmo_parameter_state_t;
/*
 * The bytes a CKParameter of document `module` holds in memory, from its
 * saved state, into a new heap block; zeroes of `fallback_size` when the
 * state holds no flat value. Object IDs and message and attribute types come
 * out as the engine's.
 */
bool eng_parameter_state_bytes(const eng_module_t *module, const nmo_parameter_state_t *state,
                               size_t fallback_size, uint8_t **out_data, uint32_t *out_size);

/* engine_array.c */
nmo_status_t eng_load_arrays(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_arrays(nmo_engine_t *engine);
eng_array_t *eng_find_array(const nmo_engine_t *engine, nmo_object_id_t id);
nmo_status_t eng_add_array(nmo_engine_t *engine, nmo_object_id_t id);
nmo_status_t eng_load_groups(nmo_engine_t *engine, const eng_range_t *range);
nmo_status_t eng_add_group(nmo_engine_t *engine, nmo_object_id_t id);
void eng_group_join(nmo_engine_t *engine, nmo_object_id_t original, nmo_object_id_t copy);
void eng_forget_object(nmo_engine_t *engine, nmo_object_id_t id);
bool eng_group_add(nmo_engine_t *engine, nmo_object_id_t group_id, nmo_object_id_t object_id);
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

/* engine_operations.c */
/*
 * Run `operation` when it is one the engine runs itself (strings, objects),
 * its status in `out_status`; false when the registry has to.
 */
eng_builtin_op_fn eng_operation_builtin(nmo_guid_t operation, nmo_guid_t result,
                                        nmo_guid_t input1, nmo_guid_t input2);
eng_builtin_op_fn eng_operation_builtin_text(nmo_guid_t operation, nmo_guid_t result,
                                             nmo_guid_t input1, nmo_guid_t input2);
nmo_status_t eng_operation_execute(nmo_engine_t *engine, nmo_guid_t guid, eng_param_t *in1,
                                   eng_param_t *in2, eng_param_t *out);

/* engine_schedule.c */
void eng_behavior_activate(nmo_engine_t *engine, int32_t node, bool active, bool reset);
void eng_behavior_reset(nmo_engine_t *engine, int32_t node);
nmo_status_t eng_manager_execute(nmo_engine_t *engine);
void eng_object_next_frame(nmo_engine_t *engine, int32_t object, int32_t delta);
/* CKLevel::AddObject for every object of document `module` (Object Load) */
void eng_add_module_to_level(nmo_engine_t *engine, int32_t module);
bool eng_in_scene(const nmo_engine_t *engine, nmo_object_id_t id);
void eng_add_to_level_scene(nmo_engine_t *engine, nmo_object_id_t id);
/* CKBehavior::Execute, outside the behavior manager (Call Script) */
int eng_execute_behavior(nmo_engine_t *engine, int32_t node);
void eng_sort_listed(nmo_engine_t *engine);
int eng_block_call(nmo_engine_t *engine, int32_t node, nmo_engine_block_fn fn);
int eng_block_callback(nmo_engine_t *engine, int32_t node, uint32_t message);

/* engine_block.c */
/* Find the function of every building block from node `first_node` on */
void eng_bind_blocks(nmo_engine_t *engine, size_t first_node);
nmo_guid_t eng_block_operation_guid(const nmo_engine_block_t *block);
/* Run `guid` over the block's first two inputs into its first output (the Op block). */
nmo_status_t eng_block_run_operation(nmo_engine_block_t *block, nmo_guid_t guid);

/* engine_message.c */
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
nmo_status_t eng_load_attributes(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_attributes(nmo_engine_t *engine);
nmo_status_t eng_register_attributes(nmo_engine_t *engine);

/* engine_tt.c */
nmo_status_t eng_register_tt(nmo_engine_t *engine);
void eng_free_tt(nmo_engine_t *engine);

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
