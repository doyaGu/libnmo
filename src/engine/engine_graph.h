/**
 * @file engine_graph.h
 * @brief The behavior graphs as the engine runs them: parameters, IOs,
 *        links, operations, behaviors and their owners, and the functions
 *        that execute them.
 */

#ifndef NMO_ENGINE_GRAPH_H
#define NMO_ENGINE_GRAPH_H

#include "engine_base.h"

#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

/* The script flags asking for a change at the next frame (CKScene::ActivateScript) */
#define ENG_NEXT_FRAME_FLAGS                                                                \
    (CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_DEACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME)

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
    bool myself;
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

struct nmo_engine_block {
    nmo_engine_t *engine;
    int32_t node;
};

/* An operation function the engine runs itself (core/operations.c). */
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

/* CKStats: what a frame executed (counted as CK2 counts it), and its length */
typedef struct eng_stats {
    float frame_time; /* ms */
    int32_t active_objects_executed; /* only while profiling, as CKBeObject counts them */
    int32_t behaviors_executed;
    int32_t building_blocks_executed;
    int32_t links_parsed;
    int32_t links_delayed;
} eng_stats_t;

/* The context's profiling */
typedef struct eng_profile {
    eng_stats_t frame; /* the frame running */
    eng_stats_t last;  /* the last frame's (CKContext::GetProfileStats) */
    bool enabled;      /* CKContext::EnableProfiling */
} eng_profile_t;

void eng_emit(nmo_engine_t *engine, nmo_engine_event_kind_t kind, int32_t node,
              nmo_object_id_t object_id, int32_t result);

/* core/param.c */
/* CKParameterManager::TypeToClassID: the class a parameter type holds objects of, 0 for none */
uint32_t eng_type_class_id(const nmo_type_descriptor_t *type);
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

/* core/operations.c */
/*
 * The function the engine runs `operation` with itself (strings, objects);
 * NULL when the operation registry has to.
 */
eng_builtin_op_fn eng_operation_builtin(nmo_guid_t operation, nmo_guid_t result,
                                        nmo_guid_t input1, nmo_guid_t input2);
eng_builtin_op_fn eng_operation_builtin_text(nmo_guid_t operation, nmo_guid_t result,
                                             nmo_guid_t input1, nmo_guid_t input2);
bool eng_param_resize(eng_param_t *param, size_t size);
/* need_sources: CKParameterOperation::DoOperation's refusal of an input with no source */
nmo_status_t eng_operation_execute(nmo_engine_t *engine, nmo_guid_t guid, eng_param_t *in1,
                                   eng_param_t *in2, eng_param_t *out, bool need_sources);

/* core/param_text.c */
/* CKParameter::GetStringValue: a new heap string, NULL when the type has no text. */
char *eng_param_text(nmo_engine_t *engine, const eng_param_t *param);
/* CKParameter::SetStringValue: the value read from text, written as an output writes it. */
nmo_status_t eng_param_set_text(nmo_engine_t *engine, eng_param_t *param, const char *text);
/* The first `count` floats of a parameter's value, 0 past its end */
void eng_param_read_floats(const eng_param_t *param, float *out, size_t count);
/* A parameter's value as a 32-bit integer, 0 when it is shorter */
int32_t eng_param_read_int(const eng_param_t *param);

/* core/curve.c */
typedef struct nmo_parameter_state nmo_parameter_state_t;
/* A 2D curve parameter's value (CK2dCurve::Read): a sorted curve, or (0,0)-(1,1) when none is saved. */
bool eng_curve_value(const nmo_parameter_state_t *state, uint8_t **out_data, uint32_t *out_size);

/* core/schedule.c */
void eng_behavior_activate(nmo_engine_t *engine, int32_t node, bool active, bool reset);
void eng_behavior_reset(nmo_engine_t *engine, int32_t node);
nmo_status_t eng_manager_execute(nmo_engine_t *engine);
void eng_object_next_frame(nmo_engine_t *engine, int32_t object, int32_t delta);
/* CKBehavior::Execute, outside the behavior manager (Call Script) */
int eng_execute_behavior(nmo_engine_t *engine, int32_t node);
void eng_sort_listed(nmo_engine_t *engine);
int eng_block_call(nmo_engine_t *engine, int32_t node, nmo_engine_block_fn fn);
int eng_block_callback(nmo_engine_t *engine, int32_t node, uint32_t message);

/* core/block.c */
/* Find the function of every building block from node `first_node` on */
void eng_bind_blocks(nmo_engine_t *engine, size_t first_node);
nmo_guid_t eng_block_operation_guid(const nmo_engine_block_t *block);
/* Run `guid` over the block's first two inputs into its first output (the Op block). */
nmo_status_t eng_block_run_operation(nmo_engine_block_t *block, nmo_guid_t guid);

#endif /* NMO_ENGINE_GRAPH_H */
