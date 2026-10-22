/**
 * @file nmo_engine.h
 * @brief Behavior engine: runs the behavior graphs of a document frame by frame.
 */

#ifndef NMO_ENGINE_H
#define NMO_ENGINE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMO_ENGINE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_ENGINE_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * The engine executes behavior graphs the way the Virtools behavior manager
 * does (CKBehaviorManager::Execute, CKBehavior::Execute): scripts of the
 * objects in the behavior manager's list run in priority order, a graph pops
 * its active sub-behaviors from an iteration stack ordered by priority,
 * immediate links push their targets onto that stack in the same frame,
 * delayed links count down at the end of each frame of their graph, and a
 * graph stops after the behavior manager's iteration limit.
 *
 * The engine copies what it needs from the document when it is created and
 * never changes the document: behavior flags, IO activations, link delays
 * and parameter values are engine state. Parameter values are the bytes the
 * file holds (an object parameter holds the object id); an input reads its
 * real source, and reading a parameter operation's output runs the
 * operation first.
 *
 * Building blocks run C functions registered by prototype GUID (see
 * engine/nmo_engine_block.h). The engine registers ports of the common Logics
 * blocks unless told not to. A building block without a function follows
 * nmo_engine_options_t.unknown_block.
 *
 * Times are in milliseconds, as CKBehaviorContext::DeltaTime is.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace nmo_workspace_t;
typedef struct nmo_script_model nmo_script_model_t;
typedef struct nmo_engine nmo_engine_t;

/* CK_BEHAVIOR_RETURN: what a building block function returns. */
#define NMO_ENGINE_BR_OK 0
#define NMO_ENGINE_BR_ACTIVATENEXTFRAME 1
#define NMO_ENGINE_BR_INFINITELOOP 8
#define NMO_ENGINE_BR_BREAK 10
#define NMO_ENGINE_BR_GENERICERROR 0xA000
#define NMO_ENGINE_BR_BEHAVIORERROR 0xA002
#define NMO_ENGINE_BR_OWNERERROR 0xA004
#define NMO_ENGINE_BR_PARAMETERERROR 0xA008

/* The behavior manager's default iteration limit (CKBEHAVIOR_MAXITERATION). */
#define NMO_ENGINE_DEFAULT_MAX_ITERATIONS 8000

/* What a building block that has no function does when it executes. */
typedef enum nmo_engine_unknown_block {
    /* Deactivate it without activating an output (it is reported). */
    NMO_ENGINE_UNKNOWN_BLOCK_STOP = 0,
    /* Deactivate its inputs and activate its first output, to follow the flow past it. */
    NMO_ENGINE_UNKNOWN_BLOCK_PASS,
} nmo_engine_unknown_block_t;

typedef struct nmo_engine_options {
    size_t struct_size;
    /* Iterations a graph may run in one frame; 0 for the default. */
    int32_t max_iterations;
    nmo_engine_unknown_block_t unknown_block;
    /* Do not register the built-in building block functions. */
    bool no_builtin_blocks;
    /* Seed of the engine's random numbers (Random, Random Switch, ...). */
    uint32_t random_seed;
} nmo_engine_options_t;

NMO_API nmo_engine_options_t nmo_engine_options_default(void);

/* Which scripts nmo_engine_start() activates. */
typedef enum nmo_engine_start_mode {
    /* The scripts whose scene activity flags say "activate at start". */
    NMO_ENGINE_START_SCENE_DEFAULT = 0,
    /* Every script, reset. */
    NMO_ENGINE_START_ALL,
    /* None; the caller activates scripts with nmo_engine_activate_script(). */
    NMO_ENGINE_START_NONE,
} nmo_engine_start_mode_t;

typedef enum nmo_engine_event_kind {
    NMO_ENGINE_EVENT_FRAME_BEGIN = 0,  /* frame, time */
    NMO_ENGINE_EVENT_FRAME_END,        /* frame, time */
    NMO_ENGINE_EVENT_ACTIVATE,         /* behavior: a script activated (value 1) or not (0) */
    NMO_ENGINE_EVENT_EXECUTE,          /* behavior executed; result is its return value */
    NMO_ENGINE_EVENT_LINK,             /* object: a link that activated its target now */
    NMO_ENGINE_EVENT_LINK_DELAYED,     /* object: a link that entered its graph's delayed list */
    NMO_ENGINE_EVENT_LINK_ARRIVED,     /* object: a delayed link whose delay ran out */
    NMO_ENGINE_EVENT_OPERATION,        /* object: a parameter operation ran; result is its status */
    NMO_ENGINE_EVENT_UNKNOWN_BLOCK,    /* behavior: a building block without a function executed */
    NMO_ENGINE_EVENT_INFINITE_LOOP,    /* behavior: a graph reached the iteration limit */
} nmo_engine_event_kind_t;

typedef struct nmo_engine_event {
    nmo_engine_event_kind_t kind;
    uint64_t frame;
    double time;                    /* engine time at the start of the frame, in ms */
    nmo_object_id_t behavior_id;    /* the behavior involved, or the graph of a link */
    nmo_object_id_t object_id;      /* the link or operation */
    int32_t result;
    uint32_t depth;                 /* nesting of the executing behavior, 0 for a script */
} nmo_engine_event_t;

typedef void (*nmo_engine_trace_fn)(const nmo_engine_event_t *event, void *user_data);

/**
 * Create an engine for the workspace's document. The engine keeps the
 * workspace pointer for the operation and type registries and must not
 * outlive it; the document is read once, here.
 */
NMO_API nmo_status_t nmo_engine_create(nmo_workspace_t *workspace,
                                       const nmo_engine_options_t *options,
                                       nmo_engine_t **out_engine);
NMO_API void nmo_engine_destroy(nmo_engine_t *engine);

/** The script model the engine was built from (labels, names, topology). */
NMO_API const nmo_script_model_t *nmo_engine_script_model(const nmo_engine_t *engine);

/** Report what the engine does to `fn`; NULL stops reporting. */
NMO_API void nmo_engine_set_trace(nmo_engine_t *engine, nmo_engine_trace_fn fn, void *user_data);

/**
 * Prepare to run: send the load and reset messages to every building block
 * (CKM_BEHAVIORLOAD picks a block's function by version), then activate
 * scripts as `mode` says. Building block functions must be registered
 * before this. A second call is NMO_ERR_INVALID_STATE.
 */
NMO_API nmo_status_t nmo_engine_start(nmo_engine_t *engine, nmo_engine_start_mode_t mode);

/**
 * Activate or deactivate script `script_id` from the next frame on, as
 * CKScene::ActivateScript does; a reset clears its graph and activates its
 * first input. NMO_ERR_NOT_FOUND when it is not a script.
 */
NMO_API nmo_status_t nmo_engine_activate_script(nmo_engine_t *engine,
                                                nmo_object_id_t script_id,
                                                bool active,
                                                bool reset);

/** Run one frame of `delta_ms` milliseconds. */
NMO_API nmo_status_t nmo_engine_step(nmo_engine_t *engine, float delta_ms);

NMO_API uint64_t nmo_engine_frame(const nmo_engine_t *engine);
NMO_API double nmo_engine_time(const nmo_engine_t *engine);

/** CKBEHAVIOR_* flags of a behavior now; 0 when the engine does not hold it. */
NMO_API uint32_t nmo_engine_behavior_flags(const nmo_engine_t *engine, nmo_object_id_t behavior_id);
NMO_API bool nmo_engine_behavior_is_active(const nmo_engine_t *engine,
                                           nmo_object_id_t behavior_id);
NMO_API bool nmo_engine_io_is_active(const nmo_engine_t *engine, nmo_object_id_t io_id);
/** Executions of a behavior since start. */
NMO_API uint64_t nmo_engine_behavior_execution_count(const nmo_engine_t *engine,
                                                     nmo_object_id_t behavior_id);

/**
 * The value parameter `param_id` holds now: its bytes (valid until the next
 * call into the engine) and type. An input returns its real source's value,
 * without running an operation. NMO_ERR_NOT_FOUND when the engine does not
 * hold the parameter or an input reads nothing.
 */
NMO_API nmo_status_t nmo_engine_param_value(const nmo_engine_t *engine,
                                            nmo_object_id_t param_id,
                                            const void **out_data,
                                            size_t *out_size,
                                            nmo_guid_t *out_type);

/**
 * Set the value of a parameter that holds one (not an input), as an output
 * writes it: the parameters it is connected to receive the value too.
 */
NMO_API nmo_status_t nmo_engine_set_param_value(nmo_engine_t *engine,
                                                nmo_object_id_t param_id,
                                                const void *data,
                                                size_t size);

/** "execute", "link", ... */
NMO_API const char *nmo_engine_event_kind_name(nmo_engine_event_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENGINE_H */
