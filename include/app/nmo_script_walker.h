/**
 * @file nmo_script_walker.h
 * @brief Scene-level script traversal API
 *
 * Provides high-level traversal from CKScene/CKLevel down through scripts,
 * behavior trees, and parameters. Builds on the existing behavior_graph and
 * param_value modules.
 */

#ifndef NMO_SCRIPT_WALKER_H
#define NMO_SCRIPT_WALKER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_behavior_state nmo_behavior_state_t;
typedef struct nmo_json_stream nmo_json_stream_t;

/* ============================================================================
 * Script discovery
 * ============================================================================ */

/**
 * @brief Entry describing a script and its owner object.
 */
typedef struct nmo_script_entry {
    nmo_object_id_t script_id;   /**< CKBehavior script object ID */
    nmo_object_id_t owner_id;    /**< CKBeObject that owns this script */
    const char *script_name;     /**< Script name (session-owned, may be NULL) */
    const char *owner_name;      /**< Owner name (session-owned, may be NULL) */
    nmo_class_id_t owner_class;  /**< Owner class ID */
} nmo_script_entry_t;

/**
 * @brief Find all script behaviors in the file.
 *
 * Iterates all CKBeObject-derived objects, collects their script_ids.
 * Results are appended to out_scripts (caller must init the array).
 *
 * @param ctx       Context
 * @param session   Session with loaded file
 * @param out_scripts  Output array of nmo_script_entry_t (caller-inited)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_script_walker_find_scripts(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_array_t *out_scripts);

/* ============================================================================
 * Behavior tree walking
 * ============================================================================ */

/**
 * @brief Visitor callback for behavior tree traversal.
 *
 * @param behavior_id  Current behavior object ID
 * @param state        Behavior state (may be NULL if object has no state)
 * @param depth        Nesting depth (0 = root script)
 * @param is_building_block  True if this is a leaf building block
 * @param user_data    User context
 * @return true to continue traversal, false to stop
 */
typedef bool (*nmo_behavior_visitor_fn)(
    nmo_object_id_t behavior_id,
    const nmo_behavior_state_t *state,
    uint32_t depth,
    bool is_building_block,
    void *user_data);

/**
 * @brief Recursively walk a behavior tree (depth-first).
 *
 * Starting from root_behavior_id, visits each sub-behavior recursively.
 * The visitor is called for the root first, then each sub-behavior.
 *
 * @param ctx              Context
 * @param session          Session
 * @param root_behavior_id Root behavior (typically a script)
 * @param visitor          Callback function
 * @param user_data        User context passed to visitor
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_script_walker_walk(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    nmo_behavior_visitor_fn visitor,
    void *user_data);

/* ============================================================================
 * Parameter source tracing
 * ============================================================================ */

/**
 * @brief Trace a ParameterIn back to its data source.
 *
 * Follows the source_id chain: ParameterIn -> ParameterOut/ParameterLocal,
 * possibly through ParameterOperations. Returns the chain of object IDs
 * from the input to the ultimate source.
 *
 * @param ctx            Context
 * @param session        Session
 * @param param_in_id    Starting ParameterIn object ID
 * @param out_chain      Output array of nmo_object_id_t (caller-inited)
 * @param max_depth      Maximum chain depth (0 = unlimited, 32 recommended)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_script_walker_trace_param_source(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t param_in_id,
    nmo_array_t *out_chain,
    uint32_t max_depth);

/* ============================================================================
 * Script dump (structured output)
 * ============================================================================ */

/**
 * @brief Dump a complete behavior tree to FILE in text format.
 *
 * Prints an indented tree showing behaviors, sub-behaviors, parameters
 * with decoded values, and links.
 *
 * @param ctx              Context
 * @param session          Session
 * @param root_behavior_id Root behavior
 * @param out              Output file
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_script_walker_dump_text(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_WALKER_H */
