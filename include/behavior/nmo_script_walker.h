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

/*
 * Traversal callbacks, nmo_array_t collection, and FILE* dump helpers remain
 * advanced C inspection surfaces. Binding-facing consumers should prefer
 * stable summaries such as nmo_script_view_*() and nmo_behavior_view_*()
 * instead of inheriting these traversal/result-buffer contracts by default.
 */
#define NMO_SCRIPT_WALKER_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_WALKER_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_behavior_state nmo_behavior_state_t;

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
 * Parameter source tracing (typed chain)
 * ============================================================================ */

/**
 * @brief Step type in a parameter source chain.
 */
typedef enum nmo_param_chain_step_type {
    NMO_CHAIN_STEP_START,            /**< Starting ParameterIn */
    NMO_CHAIN_STEP_SHARED_SOURCE,    /**< Hop via SharedSource (pIn -> pIn) */
    NMO_CHAIN_STEP_DIRECT_SOURCE,    /**< Hop via DirectSource (pIn -> pOut/pLocal) */
} nmo_param_chain_step_type_t;

/**
 * @brief One step in a parameter source chain.
 *
 * For ParameterIn nodes: type reflects how this node's own source is
 * resolved (is_shared => SHARED_SOURCE, !is_shared => DIRECT_SOURCE).
 * The first step is always START regardless of is_shared.
 * For terminal nodes (ParameterOut/ParameterLocal): type is DIRECT_SOURCE.
 */
typedef struct nmo_param_chain_step {
    nmo_object_id_t id;              /**< Object ID at this step */
    nmo_param_chain_step_type_t type; /**< Source resolution type of this node */
    nmo_object_id_t owner_id;        /**< Owning behavior ID (0 if unknown) */
    nmo_class_id_t class_id;         /**< Object class (ParameterIn/Out/Local) */
} nmo_param_chain_step_t;

/**
 * @brief Trace a ParameterIn back to its data source with typed steps.
 *
 * Each step reports whether it was a SharedSource or DirectSource hop,
 * plus the owning behavior ID (via behavior_index if available).
 *
 * @param ctx            Context
 * @param session        Session
 * @param param_in_id    Starting ParameterIn object ID
 * @param out_chain      Output array of nmo_param_chain_step_t (caller-inited)
 * @param max_depth      Maximum chain depth (0 = default 32)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_script_walker_trace_param_chain(
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
