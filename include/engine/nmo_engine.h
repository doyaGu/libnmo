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
 * and Narratives blocks unless told not to, and functions for the blocks that
 * only change the scene, materials, sounds, lights or cameras: those report
 * an effect event and continue the flow. A building block without a function
 * follows nmo_engine_options_t.unknown_block.
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
 * The keyboard and mouse the input manager reports. The host sets what is
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
 * of `ids[i]`, 0 for one not copied (an object of the engine's own
 * document). Copies join the level scene with their originals, active with
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

/* ---- Fonts ----
 *
 * The fonts the font manager holds, made by Create Font and Create System
 * Font. A Font parameter holds a font's position from 1; 0 is no font.
 */

NMO_API int32_t nmo_engine_font_count(const nmo_engine_t *engine);
/** The name of font `font`; NULL when there is no such font. */
NMO_API const char *nmo_engine_font_name(const nmo_engine_t *engine, int32_t font);

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
    float gain;            /* the sound's gain, times its fade in or out; an instance's own */
    float pitch;           /* the playback rate: 1 for the file's */
    float pan;             /* -1 left to 1 right */
    bool loop;
    bool paused;
} nmo_engine_voice_t;

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

/* ---- Physics ----
 *
 * The physics blocks (physics_RT: Physicalize, Set Physics Hinge, Slider,
 * Spring, Ball Joint and Globals, SetPhysicsForce, Physics Impulse, Physics
 * WakeUp, PhysicsCollDetection, PhysicsContinuousContact) keep what
 * CKIpionManager keeps: the physical objects with their parameters, the
 * constraints between them, the gravity and the time factor. The engine
 * does not simulate: a host's provider gets these and moves the entities
 * (nmo_engine_set_entity_world_matrix()) each frame; without one, nothing
 * moves. Positions and directions are in world space.
 */

typedef enum nmo_engine_physics_shape {
    NMO_ENGINE_PHYSICS_BALL = 0,    /* a ball of the radius (no surface name, or ball shapes) */
    NMO_ENGINE_PHYSICS_POLYGON = 1  /* the convex and concave meshes, by surface name */
} nmo_engine_physics_shape_t;

typedef struct nmo_engine_physics_object {
    nmo_object_id_t entity;
    nmo_engine_physics_shape_t shape;
    float ball_radius;
    const nmo_object_id_t *convex;  /* the meshes, in Physicalize's order (0 for none) */
    int32_t convex_count;
    const nmo_object_id_t *concave;
    int32_t concave_count;
    const char *collision_surface;  /* the name a polygon shape is shared by */
    const char *collision_group;
    float scale[3];                 /* the entity's local scale, which polygon shapes take */
    bool fixed;
    bool start_frozen;
    bool enable_collision;          /* false for a ball without a surface name, as in CK */
    bool automatic_mass_center;
    float shift_mass_center[3];     /* with automatic_mass_center false */
    float friction;
    float elasticity;
    float mass;
    float linear_damping;
    float rotation_damping;
} nmo_engine_physics_object_t;

typedef enum nmo_engine_physics_constraint_kind {
    NMO_ENGINE_PHYSICS_HINGE = 0,
    NMO_ENGINE_PHYSICS_SLIDER = 1,
    NMO_ENGINE_PHYSICS_SPRING = 2,
    NMO_ENGINE_PHYSICS_BALL_JOINT = 3,
    NMO_ENGINE_PHYSICS_FORCE = 4    /* SetPhysicsForce: a constant force on object1 alone */
} nmo_engine_physics_constraint_kind_t;

typedef struct nmo_engine_physics_constraint {
    nmo_engine_physics_constraint_kind_t kind;
    nmo_object_id_t object1;        /* the block's target */
    nmo_object_id_t object2;
    float anchor[3];                /* hinge, slider, ball joint: the joint; spring: on object1;
                                       force: where it pushes, in object1's own space */
    float anchor2[3];               /* spring: on object2 */
    float axis[3];                  /* hinge: the rotation axis; slider: second - first point;
                                       force: the force */
    bool limited;
    float lower, upper;             /* hinge: radians; slider: along the axis */
    float length, constant, damping, global_damping; /* spring */
} nmo_engine_physics_constraint_t;

typedef struct nmo_engine_physics_provider {
    void *user_data;
    /* Each may be NULL. A handle is the provider's; the engine passes it back. */
    nmo_status_t (*create_object)(void *user_data, const nmo_engine_physics_object_t *object,
                                  void **out_handle);
    void (*destroy_object)(void *user_data, void *handle);
    nmo_status_t (*create_constraint)(void *user_data,
                                      const nmo_engine_physics_constraint_t *constraint,
                                      void *object1, void *object2, void **out_handle);
    void (*destroy_constraint)(void *user_data, void *handle);
    /* An impulse at a point (Physics Impulse) */
    void (*push)(void *user_data, void *object, const float position[3], const float impulse[3]);
    void (*wake_up)(void *user_data, void *object);
    void (*set_gravity)(void *user_data, const float gravity[3]);
    /* Set Physics Globals' Clean Physics World: the environment made again */
    void (*reset)(void *user_data);
    /* A step of `seconds` (the frame time smoothed, times the time factor) */
    void (*simulate)(void *user_data, nmo_engine_t *engine, float seconds);
    /* Report the object's collisions and contacts (nmo_engine_physics_collision and
     * nmo_engine_physics_contact) from now on, or no more */
    void (*listen)(void *user_data, void *object, bool listen);
    /* The simulation's time in seconds (IVP_Environment::current_time) */
    double (*time)(void *user_data);
    /* DeleteCollisionSurfaces: drop the shapes kept by collision surface name (nothing is
     * physical then) */
    void (*delete_surfaces)(void *user_data);
} nmo_engine_physics_provider_t;

/* A collision of a listened object (IVP_Event_Collision before its impact) */
typedef struct nmo_engine_physics_collision {
    nmo_object_id_t object;  /* the listened object */
    nmo_object_id_t other;
    bool object_first;       /* whether it is the contact's first object */
    float speed[3];          /* the contact's speed: the second object's surface relative to the first */
    float normal[3];         /* the surface normal, from the first object toward the second */
    float point[3];          /* where they touch */
} nmo_engine_physics_collision_t;

/** A provider reports a listened object's collision (PhysicsCollDetection hears it). */
NMO_API void nmo_engine_physics_collision(nmo_engine_t *engine,
                                          const nmo_engine_physics_collision_t *collision);
/**
 * A provider reports a listened object's contact with `other` starting or
 * ending (friction created or deleted; PhysicsContinuousContact hears it).
 */
NMO_API void nmo_engine_physics_contact(nmo_engine_t *engine, nmo_object_id_t object,
                                        nmo_object_id_t other, bool started);

/** A mesh's vertices and faces, as loaded (for a provider's shapes); counts are -1 for no mesh. */
NMO_API int32_t nmo_engine_mesh_vertex_count(const nmo_engine_t *engine, nmo_object_id_t mesh);
NMO_API bool nmo_engine_mesh_vertex(const nmo_engine_t *engine, nmo_object_id_t mesh,
                                    int32_t index, float out_position[3]);
NMO_API int32_t nmo_engine_mesh_face_count(const nmo_engine_t *engine, nmo_object_id_t mesh);
NMO_API bool nmo_engine_mesh_face(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                                  int32_t out_vertices[3]);

/** Give the engine a physics provider (copied); NULL for none. */
NMO_API void nmo_engine_set_physics_provider(nmo_engine_t *engine,
                                             const nmo_engine_physics_provider_t *provider);
/** Whether `entity` is physical; `out` (optional) its parameters, valid until the next step. */
NMO_API bool nmo_engine_physics_object(const nmo_engine_t *engine, nmo_object_id_t entity,
                                       nmo_engine_physics_object_t *out);
/** The physical objects, in the order they became physical. */
NMO_API int32_t nmo_engine_physics_object_count(const nmo_engine_t *engine);
NMO_API bool nmo_engine_physics_object_at(const nmo_engine_t *engine, int32_t index,
                                          nmo_engine_physics_object_t *out);
/** The constraints the physics blocks made, in order. */
NMO_API int32_t nmo_engine_physics_constraint_count(const nmo_engine_t *engine);
NMO_API bool nmo_engine_physics_constraint(const nmo_engine_t *engine, int32_t index,
                                           nmo_engine_physics_constraint_t *out);
/** The gravity Set Physics Globals set: (0, -9.81, 0) until then. */
NMO_API void nmo_engine_physics_gravity(const nmo_engine_t *engine, float out[3]);

/** "execute", "link", ... */
NMO_API const char *nmo_engine_event_kind_name(nmo_engine_event_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENGINE_H */
