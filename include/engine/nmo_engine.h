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
 * engine/nmo_engine_block.h). The engine registers none itself: a block pack
 * registers the blocks of its DLLs, with the managers they keep, as a DLL
 * would (bb/nmo_bb_virtools.h for the stock ones, bb/nmo_bb_physics.h,
 * bb/nmo_bb_terratools.h). A building block without a function follows
 * nmo_engine_options_t.unknown_block.
 *
 * Messages work as the message manager's do: a message sent during a frame
 * is delivered at the end of that frame (CKMessageManager::PostProcess). A
 * behavior waiting for it gets its output activated, which its graph acts on
 * in the next frame, and the objects waiting for messages hold it as a last
 * frame message until the next delivery. A message type is the index of the
 * message in the file's message list.
 *
 * Data arrays (CKDataArray) are copied too: blocks read and change the
 * engine's copy of each array, never the document's.
 *
 * Times are in milliseconds, as CKBehaviorContext::DeltaTime is.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace nmo_workspace_t;
typedef struct nmo_script_model nmo_script_model_t;
typedef struct nmo_script_node nmo_script_node_t;
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

/* Failures that stop execution; combine them in nmo_engine_options_t.fail_on. */
#define NMO_ENGINE_FAIL_UNKNOWN_BLOCK (1u << 0)
#define NMO_ENGINE_FAIL_BLOCK_ERROR (1u << 1)
#define NMO_ENGINE_FAIL_OPERATION_ERROR (1u << 2)
#define NMO_ENGINE_FAIL_INFINITE_LOOP (1u << 3)
#define NMO_ENGINE_FAIL_ALL (NMO_ENGINE_FAIL_UNKNOWN_BLOCK | NMO_ENGINE_FAIL_BLOCK_ERROR | \
                             NMO_ENGINE_FAIL_OPERATION_ERROR | NMO_ENGINE_FAIL_INFINITE_LOOP)

typedef struct nmo_engine_options {
    size_t struct_size;
    /* Iterations a graph may run in one frame; 0 for the default. */
    int32_t max_iterations;
    nmo_engine_unknown_block_t unknown_block;
    /* Seed of the engine's random numbers (Random, Random Switch, ...). */
    uint32_t random_seed;
    /* NMO_ENGINE_FAIL_* flags; 0 keeps reporting failures through the trace only.
       A block error is any return except OK, ACTIVATENEXTFRAME or INFINITELOOP
       (which has its own flag). The first selected failure stops subsequent
       behaviors and frame post-processing. The current C block finishes its
       call; changes already made are not rolled back. */
    uint64_t fail_on;
} nmo_engine_options_t;

NMO_API nmo_engine_options_t nmo_engine_options_default(void);

/* Which scripts nmo_engine_start() activates. */
typedef enum nmo_engine_start_mode {
    /* The scripts whose scene activity flags say "activate at start". */
    NMO_ENGINE_START_SCENE_DEFAULT = 0,
    /* Every script, reset. */
    NMO_ENGINE_START_ALL,
    /* None, though every script is reset; the caller activates scripts with
       nmo_engine_activate_script(). */
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
    NMO_ENGINE_EVENT_MESSAGE_SENT,     /* behavior: the sender block, if any; object: the
                                          recipient (0 for a broadcast); result: the type */
    NMO_ENGINE_EVENT_MESSAGE_RECEIVED, /* behavior: the block whose wait it ended;
                                          object: the object it reached; result: the type */
    NMO_ENGINE_EVENT_EFFECT,           /* behavior: a block whose effect (on the scene, a
                                          sound, ...) the engine does not simulate;
                                          object: its target */
    NMO_ENGINE_EVENT_FILE_LOADED,      /* behavior: the block that loaded it, if any;
                                          result: the loaded file (nmo_engine_load), or -1
                                          when it could not be loaded */
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
 * outlive it; the document is read once, here. Register the block packs
 * before the engine starts.
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
 * (CKM_BEHAVIORLOAD picks a block's function by version), then reset every
 * script and activate the ones `mode` says, as a level launch does.
 * Building block functions must be registered before this. A second call is
 * NMO_ERR_INVALID_STATE.
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

/**
 * Activate or deactivate a behavioral object in the scene, as
 * CKScene::Activate and DeActivate do for one: an inactive object's scripts
 * do not run (they keep whether they are active), and activating it brings
 * them back from the next frame on. Objects start active unless their
 * activity flags say otherwise.
 */
NMO_API nmo_status_t nmo_engine_activate_object(nmo_engine_t *engine,
                                                nmo_object_id_t object_id,
                                                bool active);
NMO_API bool nmo_engine_object_is_active(const nmo_engine_t *engine, nmo_object_id_t object_id);

/* ---- Loading files ----
 *
 * CKContext::Load into the running engine (Object Load). The engine opens no
 * file by itself: the host's resolver turns the name a document gives
 * ("3D Entities\MenuLevel.nmo") into a path to load, or refuses it. A loaded
 * file's objects get engine IDs of their own, after the IDs in use; its
 * message and attribute types join the engine's by name.
 */

/** The path to load for `name`, valid until the next call; NULL to refuse it. */
typedef const char *(*nmo_engine_resolve_file_fn)(void *user_data, const char *name);

NMO_API void nmo_engine_set_file_resolver(nmo_engine_t *engine,
                                          nmo_engine_resolve_file_fn fn,
                                          void *user_data);

/** Write `size` bytes as the file `name` names (as a resolver gets names); NMO_OK when written. */
typedef nmo_status_t (*nmo_engine_write_file_fn)(void *user_data, const char *name,
                                                 const void *data, size_t size);

/**
 * Where the files the blocks save go (TT Save Database). The engine writes
 * nothing itself: without a writer, saving fails as an unwritable file.
 */
NMO_API void nmo_engine_set_file_writer(nmo_engine_t *engine, nmo_engine_write_file_fn fn,
                                        void *user_data);

/**
 * The host's file system, for the TT file blocks (TT_CopyFile, TT_DeleteFile,
 * TT_CreateDirectory, TT_RemoveDirectory, TT_SetCurrentDirectory, TT_ExistFile,
 * TT_ListDir), with names as the documents give them. Each operation returns
 * whether it was done; a NULL operation fails. The engine touches no file
 * itself: without these, the blocks fail, and TT_ExistFile asks the resolver.
 */
typedef struct nmo_engine_file_system {
    void *user_data;
    /** GetFileAttributes: whether the file or directory is there */
    bool (*exists)(void *user_data, const char *name);
    /** CopyFile, which fails on an existing destination unless `overwrite` */
    bool (*copy)(void *user_data, const char *source, const char *destination, bool overwrite);
    bool (*remove_file)(void *user_data, const char *name);
    bool (*create_directory)(void *user_data, const char *name);
    bool (*remove_directory)(void *user_data, const char *name);
    /** SetCurrentDirectory; the host reports the new one with nmo_engine_set_current_directory() */
    bool (*set_current_directory)(void *user_data, const char *name);
    /** FindFirstFile and FindNextFile: each entry the pattern finds ("." and ".." too) to
        `found`; false when the search cannot start */
    bool (*list)(void *user_data, const char *pattern,
                 void (*found)(void *context, const char *name), void *context);
} nmo_engine_file_system_t;

/** The host's file system (copied); NULL for none. */
NMO_API void nmo_engine_set_file_system(nmo_engine_t *engine, const nmo_engine_file_system_t *files);
/**
 * Load the file `name` names. With `add_to_level`, its objects are added to
 * the level as Object Load adds them: each behavioral object, and each of its
 * scripts, activates or deactivates as its saved activity flags say (an
 * object without them stays inactive). `out_file` (optional) is the loaded
 * file, for nmo_engine_loaded_object(). NMO_ERR_NOT_SUPPORTED without a
 * resolver, NMO_ERR_FILE_NOT_FOUND when it refuses the name.
 */
NMO_API nmo_status_t nmo_engine_load(nmo_engine_t *engine,
                                     const char *name,
                                     bool add_to_level,
                                     int32_t *out_file);
/** The name a loaded file was loaded by; NULL for the engine's own document. */
NMO_API const char *nmo_engine_loaded_file_name(const nmo_engine_t *engine, int32_t file);
/** The objects of a loaded file, in the file's order, by engine ID. */
NMO_API int32_t nmo_engine_loaded_object_count(const nmo_engine_t *engine, int32_t file);
NMO_API nmo_object_id_t nmo_engine_loaded_object(const nmo_engine_t *engine,
                                                 int32_t file,
                                                 int32_t index);
/** The name of an object of any file the engine runs; NULL when it has none. */
NMO_API const char *nmo_engine_object_name(const nmo_engine_t *engine, nmo_object_id_t id);
/**
 * The first object named `name` of class `class_id` (or a subclass; 0 for
 * any) that is not destroyed, in the order the files were loaded
 * (CKContext::GetObjectByNameAndClass); 0 when there is none.
 */
NMO_API nmo_object_id_t nmo_engine_object_named(const nmo_engine_t *engine, const char *name,
                                                nmo_class_id_t class_id);
/**
 * The script model node of behavior `id` in whichever file the engine runs
 * it from (the node's own `id` is the file's); NULL when it is none.
 */
NMO_API const nmo_script_node_t *nmo_engine_script_node(const nmo_engine_t *engine,
                                                        nmo_object_id_t id);
/**
 * A label for `id` as nmo_script_model_label() makes one ("Name#id"), with
 * the engine ID; returns its length, writing at most `buffer_size` bytes.
 */
NMO_API size_t nmo_engine_label(const nmo_engine_t *engine,
                                nmo_object_id_t id,
                                char *buffer,
                                size_t buffer_size);
/** The scripts of every file the engine runs, in engine order. */
NMO_API int32_t nmo_engine_script_count(const nmo_engine_t *engine);
NMO_API nmo_object_id_t nmo_engine_script_id(const nmo_engine_t *engine, int32_t index);
/** CK_CLASSID of an object of any file the engine runs; 0 when there is none. */
NMO_API int32_t nmo_engine_object_class(const nmo_engine_t *engine, nmo_object_id_t id);
/**
 * CKContext::DestroyObjects for the engine: a destroyed behavioral object
 * leaves the behavior manager for good and its scripts stop; a destroyed
 * script stops.
 */
NMO_API void nmo_engine_destroy_object(nmo_engine_t *engine, nmo_object_id_t id);

/**
 * Set a value of the registry the TT blocks read (HKEY_CURRENT_USER): value
 * `name` of key `section` ("Software\\Ballance\\Settings"), as text ("1",
 * "0.5", "Path"). Keys and names compare as the registry compares them,
 * ignoring case and a key's trailing backslashes. NMO_ERR_NOMEM on failure.
 */
NMO_API nmo_status_t nmo_engine_set_registry_value(nmo_engine_t *engine,
                                                   const char *section,
                                                   const char *name,
                                                   const char *value);

/**
 * Set an environment variable the TT blocks read (TT GetEnvironmentVariable)
 * as text. Names compare as Windows compares them, ignoring case. The engine
 * reads nothing of the process's environment itself.
 */
NMO_API nmo_status_t nmo_engine_set_environment_variable(nmo_engine_t *engine,
                                                         const char *name,
                                                         const char *value);
/** The current directory the TT blocks read (TT_GetCurrentDirectory); NULL for none. */
NMO_API nmo_status_t nmo_engine_set_current_directory(nmo_engine_t *engine, const char *path);

/* The path manager's categories (CKPathManager) */
#define NMO_ENGINE_PATH_BITMAP 0
#define NMO_ENGINE_PATH_DATA 1
#define NMO_ENGINE_PATH_SOUND 2

/**
 * Add a path to a category of the path manager (CKPathManager::AddPath);
 * blocks may rename it (TT_ReplacePath). The engine resolves no file by
 * them: a host's resolver reads them with nmo_engine_path().
 */
NMO_API nmo_status_t nmo_engine_add_path(nmo_engine_t *engine, int32_t category, const char *path);
/** Path `index` of a category, as blocks left it; NULL past the last. */
NMO_API const char *nmo_engine_path(const nmo_engine_t *engine, int32_t category, int32_t index);

/**
 * Whether a block asked the player to quit (TT Exit to System): the frame
 * that asked runs to its end, and the host stops stepping.
 */
NMO_API bool nmo_engine_exit_requested(const nmo_engine_t *engine);

/**
 * Run one frame of `delta_ms` milliseconds. A failure selected by `fail_on`
 * returns NMO_ERR_VALIDATION_FAILED and is available through nmo_engine_failure().
 * The failed frame counts toward frame/time; later steps return the same error
 * without advancing. Create another engine to run again after a failure.
 */
NMO_API nmo_status_t nmo_engine_step(nmo_engine_t *engine, float delta_ms);

/** Copy the first selected failure event; false when none (out_event may be NULL). */
NMO_API bool nmo_engine_failure(const nmo_engine_t *engine, nmo_engine_event_t *out_event);

NMO_API uint64_t nmo_engine_frame(const nmo_engine_t *engine);

/** CKTimeManager's settings, as Time Settings leaves them (CK's defaults before). */
typedef struct nmo_engine_time_settings {
    uint32_t limit_options;    /* CK_FRAMERATE_LIMITS: frames 0x10 synchronized to the screen,
                                  0x20 free, 0x40 limited; behaviors 0x1 with the frames,
                                  0x4 limited */
    float frame_rate_limit;    /* frames a second, when limited */
    float behavior_rate_limit; /* behavior frames a second, when limited */
    float time_scale;
    float min_delta, max_delta; /* ms */
} nmo_engine_time_settings_t;

NMO_API void nmo_engine_time_settings(const nmo_engine_t *engine,
                                      nmo_engine_time_settings_t *out_settings);
/**
 * CKTimeManager::PreProcess: the frame time a host steps by after
 * `elapsed_ms` of real time: kept within the minimum and maximum, then scaled.
 */
NMO_API float nmo_engine_frame_delta(const nmo_engine_t *engine, float elapsed_ms);
NMO_API double nmo_engine_time(const nmo_engine_t *engine);

/** CKBEHAVIOR_* flags of a behavior now; 0 when the engine does not hold it. */
NMO_API uint32_t nmo_engine_behavior_flags(const nmo_engine_t *engine, nmo_object_id_t behavior_id);
NMO_API bool nmo_engine_behavior_is_active(const nmo_engine_t *engine,
                                           nmo_object_id_t behavior_id);
/**
 * CKBehavior::IsParentScriptActiveInScene: whether the script holding the
 * behavior is active in the level scene (always, for the level's scripts).
 */
NMO_API bool nmo_engine_parent_script_is_active(const nmo_engine_t *engine,
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

/* ---- Messages ---- */

/** Number of message types the file declares. */
NMO_API int32_t nmo_engine_message_type_count(const nmo_engine_t *engine);
/** Name of message type `type`; NULL when there is none. */
NMO_API const char *nmo_engine_message_type_name(const nmo_engine_t *engine, int32_t type);
/** The message type named `name`, or -1. */
NMO_API int32_t nmo_engine_message_type(const nmo_engine_t *engine, const char *name);

/** Send message `type` to object `recipient` (CKMessageManager::SendMessageSingle). */
NMO_API nmo_status_t nmo_engine_send_message(nmo_engine_t *engine,
                                             int32_t type,
                                             nmo_object_id_t recipient,
                                             nmo_object_id_t sender);
/** Send message `type` to every object of class `class_id` and its subclasses. */
NMO_API nmo_status_t nmo_engine_broadcast_message(nmo_engine_t *engine,
                                                  int32_t type,
                                                  nmo_class_id_t class_id,
                                                  nmo_object_id_t sender);

/* ---- Data arrays and groups ---- */

/*
 * A data array cell. `type` is the column's CK_ARRAYTYPE: INT, FLOAT and
 * OBJECT cells hold `value` (an int32, a float's bits, an object id), STRING
 * cells `text` (NULL for no string) and PARAMETER cells `data` and `size`, a
 * value of type `parameter_type`.
 */
typedef struct nmo_engine_cell {
    int32_t type;
    uint32_t value;
    const char *text;
    const void *data;
    size_t size;
    nmo_guid_t parameter_type;
} nmo_engine_cell_t;

/** Rows of data array `array_id`; -1 when it is not a data array. */
NMO_API int32_t nmo_engine_array_row_count(const nmo_engine_t *engine, nmo_object_id_t array_id);
NMO_API int32_t nmo_engine_array_column_count(const nmo_engine_t *engine,
                                              nmo_object_id_t array_id);
/** The column holding the array's keys, or -1. */
NMO_API int32_t nmo_engine_array_key_column(const nmo_engine_t *engine, nmo_object_id_t array_id);
/** CK_ARRAYTYPE of a column; 0 when there is none. */
/** The column's name; NULL for none (or no such column). */
NMO_API const char *nmo_engine_array_column_name(const nmo_engine_t *engine,
                                                 nmo_object_id_t array_id, int32_t column);
/** Name the column (CKDataArray::SetColumnName); NULL for none. */
NMO_API nmo_status_t nmo_engine_array_set_column_name(nmo_engine_t *engine,
                                                      nmo_object_id_t array_id, int32_t column,
                                                      const char *name);
NMO_API int32_t nmo_engine_array_column_type(const nmo_engine_t *engine,
                                             nmo_object_id_t array_id,
                                             int32_t column);

/**
 * The cell at `row`, `column`. Its text and data stay valid until the cell
 * or the array changes. NMO_ERR_NOT_FOUND outside the array.
 */
NMO_API nmo_status_t nmo_engine_array_get_cell(const nmo_engine_t *engine,
                                               nmo_object_id_t array_id,
                                               int32_t row,
                                               int32_t column,
                                               nmo_engine_cell_t *out_cell);
/** Store the part of `cell` the column's type uses (value, text or data). */
NMO_API nmo_status_t nmo_engine_array_set_cell(nmo_engine_t *engine,
                                               nmo_object_id_t array_id,
                                               int32_t row,
                                               int32_t column,
                                               const nmo_engine_cell_t *cell);
/**
 * Insert an empty row before `row`, or at the end for -1 (CKDataArray::InsertRow);
 * its position goes to `out_row`.
 */
NMO_API nmo_status_t nmo_engine_array_insert_row(nmo_engine_t *engine,
                                                 nmo_object_id_t array_id,
                                                 int32_t row,
                                                 int32_t *out_row);
/**
 * The first row from `start` whose cell in `column` equals `key` (FindRowIndex
 * with CKEQUAL), or -1.
 */
NMO_API int32_t nmo_engine_array_find_row(const nmo_engine_t *engine,
                                          nmo_object_id_t array_id,
                                          int32_t column,
                                          const nmo_engine_cell_t *key,
                                          int32_t start);
/**
 * The first row from `start` whose cell in `column` compares with `key` as
 * `op` (a CK_COMPOPERATOR) says, or -1. Strings compare as strcmp() does, a
 * missing string as ""; parameter values compare byte by byte over the
 * shorter of the two.
 */
NMO_API int32_t nmo_engine_array_find_row_op(const nmo_engine_t *engine,
                                             nmo_object_id_t array_id,
                                             int32_t column,
                                             int32_t op,
                                             const nmo_engine_cell_t *key,
                                             int32_t start);
/** CKDataArray::GetCount: the rows whose cell compares true, as find_row_op compares. */
NMO_API int32_t nmo_engine_array_count(const nmo_engine_t *engine,
                                       nmo_object_id_t array_id,
                                       int32_t column,
                                       int32_t op,
                                       const nmo_engine_cell_t *key);
/** CKDataArray::TestRow: whether the row's cell compares true. */
NMO_API bool nmo_engine_array_test_row(const nmo_engine_t *engine,
                                       nmo_object_id_t array_id,
                                       int32_t row,
                                       int32_t column,
                                       int32_t op,
                                       const nmo_engine_cell_t *key);
/**
 * CKDataArray::Sort: the rows by the column, ascending or not. An array
 * already sorted ascending on that column, and not changed since, is left
 * as it is, as CK leaves it. Equal cells keep their order.
 */
NMO_API void nmo_engine_array_sort(nmo_engine_t *engine,
                                   nmo_object_id_t array_id,
                                   int32_t column,
                                   bool ascending);
/**
 * The row whose cell in `column` is the highest (CKDataArray::GetHighest,
 * the first of equal ones), or -1 for an empty array.
 */
NMO_API int32_t nmo_engine_array_highest_row(const nmo_engine_t *engine,
                                             nmo_object_id_t array_id,
                                             int32_t column);
/** Remove row `row` (CKDataArray::RemoveRow). */
NMO_API nmo_status_t nmo_engine_array_remove_row(nmo_engine_t *engine,
                                                 nmo_object_id_t array_id,
                                                 int32_t row);
/** Remove every row (CKDataArray::Clear). */
NMO_API nmo_status_t nmo_engine_array_clear(nmo_engine_t *engine, nmo_object_id_t array_id);
/** Exchange rows `a` and `b` (CKDataArray::SwapRows). */
NMO_API nmo_status_t nmo_engine_array_swap_rows(nmo_engine_t *engine,
                                                nmo_object_id_t array_id,
                                                int32_t a,
                                                int32_t b);
/**
 * CKDataArray::Sum: the sum of an Integer column, or the bits of the sum of
 * a Float column; 0 for other columns.
 */
NMO_API uint32_t nmo_engine_array_sum(const nmo_engine_t *engine,
                                      nmo_object_id_t array_id,
                                      int32_t column);
/**
 * Insert an empty column of `type` (a CK_ARRAYTYPE; `param_type` for a
 * parameter column) before `column`, or after the last for -1
 * (CKDataArray::InsertColumn). Columns do not keep their names here.
 */
NMO_API nmo_status_t nmo_engine_array_insert_column(nmo_engine_t *engine,
                                                    nmo_object_id_t array_id,
                                                    int32_t column,
                                                    int32_t type,
                                                    nmo_guid_t param_type);
/** Remove column `column` and its cells (CKDataArray::RemoveColumn). */
NMO_API nmo_status_t nmo_engine_array_remove_column(nmo_engine_t *engine,
                                                    nmo_object_id_t array_id,
                                                    int32_t column);

/** Objects in group `group_id`; -1 when it is not a group. */
NMO_API int32_t nmo_engine_group_count(const nmo_engine_t *engine, nmo_object_id_t group_id);
/** The object at `index` in the group; 0 when there is none. */
NMO_API nmo_object_id_t nmo_engine_group_object(const nmo_engine_t *engine,
                                                nmo_object_id_t group_id,
                                                int32_t index);

/* ---- Input ----
 *
 * The keyboard, mouse and joysticks the input manager reports. The host sets what is
 * down and where the mouse is; the state takes effect at the start of the
 * next frame, when the keys that changed since the previous frame form the
 * frame's key buffer and the mouse movement is the difference of positions.
 * Keys are DirectInput scan codes (Keyboard Key values); mouse buttons are
 * CK_MOUSEBUTTON values: 0 left, 1 right, 2 middle.
 */

NMO_API void nmo_engine_set_key(nmo_engine_t *engine, uint32_t key, bool down);
NMO_API void nmo_engine_set_mouse_position(nmo_engine_t *engine, float x, float y);
NMO_API void nmo_engine_set_mouse_button(nmo_engine_t *engine, int32_t button, bool down);
/** Add wheel movement to the next frame. */
NMO_API void nmo_engine_add_mouse_wheel(nmo_engine_t *engine, float delta);

/* CKMAX_JOY: the four native joystick slots, indexed 0 through 3. */
#define NMO_ENGINE_JOYSTICK_COUNT 4

typedef struct nmo_engine_joystick_state {
    bool attached;
    float position[3];
    float rotation[3];
    float sliders[2];
    float point_of_view; /* a negative value means centered */
    uint32_t buttons; /* bit 0 is button 1; all 32 native buttons are retained */
} nmo_engine_joystick_state_t;

/** Copy a host joystick state for the next frame. NULL or !attached disconnects
 * it and clears its axes/buttons. Invalid slot indices are ignored. */
NMO_API void nmo_engine_set_joystick(nmo_engine_t *engine, int32_t slot,
                                      const nmo_engine_joystick_state_t *state);
/** True when the slot is attached in the current frame. Optional `out` receives
 * that frame's state, or zero axes/buttons and a centered POV when unavailable. */
NMO_API bool nmo_engine_joystick_state(const nmo_engine_t *engine, int32_t slot,
                                        nmo_engine_joystick_state_t *out);
/**
 * The 2D entity under the mouse from the next frame on, as the renderer picks
 * it (Pick2D) for the font manager; 0 for none. There is no renderer, so the
 * host says which.
 */
NMO_API void nmo_engine_set_entity_under_mouse(nmo_engine_t *engine, nmo_object_id_t entity);

/** What the renderer picks at a point of the window (CKPICKRESULT). */
typedef struct nmo_engine_pick {
    nmo_object_id_t object; /* the 3D entity hit; 0 for none */
    float point[3];         /* where, in the object's space */
    float normal[3];        /* the surface normal there, in the object's space */
    float uv[2];            /* the texture coordinates there */
    float distance;         /* from the viewpoint */
    int32_t face;           /* the face index */
    nmo_object_id_t sprite; /* the sprite hit; 0 for none */
} nmo_engine_pick_t;

/** The host's picking at window coordinates `x, y`; false when nothing is there. */
typedef bool (*nmo_engine_pick_fn)(void *user_data, int32_t x, int32_t y, nmo_engine_pick_t *out);

/**
 * CKRenderContext::Pick (2D Picking). There is no renderer, so the host
 * picks; without a picker nothing is ever under a point.
 */
NMO_API void nmo_engine_set_picker(nmo_engine_t *engine, nmo_engine_pick_fn pick, void *user_data);

/** CKInputManager::IsKeyDown during the current frame. */
NMO_API bool nmo_engine_is_key_down(const nmo_engine_t *engine, uint32_t key);
/** Keys that changed since the previous frame. */
NMO_API int32_t nmo_engine_key_buffer_count(const nmo_engine_t *engine);
/** The `index`th of them (GetKeyFromBuffer): its key, and whether it went down. */
NMO_API bool nmo_engine_key_buffer_get(const nmo_engine_t *engine,
                                       int32_t index,
                                       uint32_t *out_key,
                                       bool *out_pressed);
NMO_API void nmo_engine_mouse_position(const nmo_engine_t *engine, float *out_x, float *out_y);
/** Movement since the previous frame, and the wheel (GetMouseRelativePosition). */
NMO_API void nmo_engine_mouse_relative(const nmo_engine_t *engine,
                                       float *out_x,
                                       float *out_y,
                                       float *out_wheel);
NMO_API bool nmo_engine_is_mouse_button_down(const nmo_engine_t *engine, int32_t button);
/** CKFontManager::GetEntityUnderMouse during the current frame. */
NMO_API nmo_object_id_t nmo_engine_entity_under_mouse(const nmo_engine_t *engine);
/** CKInputManager::GetCursorVisibility: shown until Show Mouse Cursor hides it. */
NMO_API bool nmo_engine_cursor_is_visible(const nmo_engine_t *engine);

/**
 * CKContext::CopyObjects with the default copy dependencies: copies of the
 * objects and of what their classes copy with them (a behavioral object's
 * scripts and attributes, a 3D entity's children), sharing the rest (a
 * mesh), each in the state its original has. `out_copies[i]` is the copy
 * of `ids[i]`, 0 for one not copied (an unknown object ID). A copy of an
 * object of the initial document leaves the host's document unchanged.
 * Copies join the level scene with their originals, active with
 * `activate` or when their originals are.
 */
NMO_API nmo_status_t nmo_engine_copy_objects(nmo_engine_t *engine,
                                             const nmo_object_id_t *ids,
                                             int32_t count,
                                             bool activate,
                                             nmo_object_id_t *out_copies);

/* ---- 3D entities ----
 *
 * The engine keeps each CK3dEntity's world and local matrices and its
 * parent, as CK does: the transform blocks change them. A matrix is 16
 * floats, VxMatrix's rows: the X, Y and Z axes, then the position.
 */

/** Entity slots include loaded and copied objects; deleted slots return ID 0. */
NMO_API size_t nmo_engine_entity_count(const nmo_engine_t *engine);
NMO_API nmo_object_id_t nmo_engine_entity_id(const nmo_engine_t *engine, size_t index);
/** The entity's world matrix; false when `entity_id` is no 3D entity. */
NMO_API bool nmo_engine_entity_world_matrix(const nmo_engine_t *engine,
                                            nmo_object_id_t entity_id,
                                            float out_matrix[16]);
/** The entity's matrix relative to its parent (its world matrix without one). */
NMO_API bool nmo_engine_entity_local_matrix(const nmo_engine_t *engine,
                                            nmo_object_id_t entity_id,
                                            float out_matrix[16]);
/** The entity's parent; 0 for none. */
NMO_API nmo_object_id_t nmo_engine_entity_parent(const nmo_engine_t *engine,
                                                 nmo_object_id_t entity_id);
/**
 * CK3dEntity::SetWorldMatrix from the host: the children move along, or
 * keep their place with `keep_children`.
 */
NMO_API nmo_status_t nmo_engine_set_entity_world_matrix(nmo_engine_t *engine,
                                                        nmo_object_id_t entity_id,
                                                        const float matrix[16],
                                                        bool keep_children);

/* ---- Attributes ----
 *
 * The attributes of behavioral objects, copied from the document when the
 * engine is made. An attribute type is an index into the file's attribute
 * types, as an Attribute parameter holds it.
 */

NMO_API bool nmo_engine_object_has_attribute(const nmo_engine_t *engine,
                                             nmo_object_id_t object,
                                             int32_t type);
/** The value of an attribute's parameter; NMO_ERR_NOT_FOUND without one. */
NMO_API nmo_status_t nmo_engine_object_attribute_value(const nmo_engine_t *engine,
                                                       nmo_object_id_t object,
                                                       int32_t type,
                                                       const void **out_data,
                                                       size_t *out_size,
                                                       nmo_guid_t *out_type);
/**
 * CKBeObject::SetAttribute: give the object the attribute, with a zeroed
 * parameter of the attribute's type when it has one. False when it has the
 * attribute already, the type is unknown, or its class may not have it.
 */
NMO_API bool nmo_engine_object_set_attribute(nmo_engine_t *engine,
                                             nmo_object_id_t object,
                                             int32_t type);
NMO_API bool nmo_engine_object_remove_attribute(nmo_engine_t *engine,
                                                nmo_object_id_t object,
                                                int32_t type);

/* ---- Sounds ---- */

/**
 * True while sound `sound_id` plays, as the engine follows it: Wave Player
 * starts and stops it, and it ends after the length the file records.
 */
NMO_API bool nmo_engine_sound_is_playing(const nmo_engine_t *engine, nmo_object_id_t sound_id);

/**
 * A sound to hear now: a wave sound that plays, or one of its instances
 * (Play Sound Instance). The engine plays no audio; a host mixes these.
 */
typedef struct nmo_engine_voice {
    uint32_t key;          /* the same while it plays: the sound's ID, or its instance's number */
    nmo_object_id_t sound;
    const char *file;      /* the wave file as the sound names it; NULL for none */
    uint32_t start;        /* changes when it starts again: the host plays it from `position` */
    float position;        /* ms into the file */
    float gain;            /* the sound's gain, times its fade in or out; an instance's own;
                              times the sound manager's global gain */
    float pitch;           /* the playback rate: 1 for the file's */
    float pan;             /* -1 left to 1 right */
    bool loop;
    bool paused;
    /* CKWaveSound's 3D settings, for a host that places its voices: a point or cone sound
       (CK_WAVESOUND_TYPE: 1 background, 2 point, 3 cone) is heard from world_position */
    uint32_t type;
    float priority;
    float cone[3];          /* inside and outside angles in degrees, the outside gain */
    float min_distance, max_distance;
    bool mute_after_max;
    float world_position[3], world_direction[3];
} nmo_engine_voice_t;

/**
 * The sound manager's listener (Set Listener's entity, else the render
 * context's camera) and settings: the distance and Doppler factors and the
 * global gain (Fade In, Fade Out, Sound Manager Setup), which the voices'
 * gains already include. Each output may be NULL.
 */
NMO_API bool nmo_engine_sound_listener(const nmo_engine_t *engine, nmo_object_id_t *out_listener,
                                       float *out_distance_factor, float *out_doppler_factor,
                                       float *out_global_gain);
/** The voices to hear this frame, `*out_count` of them; valid until the next step. */
NMO_API const nmo_engine_voice_t *nmo_engine_voices(nmo_engine_t *engine, size_t *out_count);
/** The whole file a sound names, through the path manager's sound paths; free() it. */
NMO_API unsigned char *nmo_engine_read_sound_file(nmo_engine_t *engine, const char *name,
                                                  size_t *out_size);
/**
 * The slot texture `texture_id` shows: the frame of the movie Movie Load
 * gave it as Movie Player advances it, else the slot the file records.
 */
NMO_API int32_t nmo_engine_texture_current_slot(const nmo_engine_t *engine,
                                                nmo_object_id_t texture_id);

/* ---- Meshes ---- */

/** A mesh's vertices and faces, as loaded (for a provider's shapes); counts are -1 for no mesh. */
NMO_API int32_t nmo_engine_mesh_vertex_count(const nmo_engine_t *engine, nmo_object_id_t mesh);
NMO_API bool nmo_engine_mesh_vertex(const nmo_engine_t *engine, nmo_object_id_t mesh,
                                    int32_t index, float out_position[3]);
NMO_API int32_t nmo_engine_mesh_face_count(const nmo_engine_t *engine, nmo_object_id_t mesh);
NMO_API bool nmo_engine_mesh_face(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                                  int32_t out_vertices[3]);

/** "execute", "link", ... */
NMO_API const char *nmo_engine_event_kind_name(nmo_engine_event_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENGINE_H */
