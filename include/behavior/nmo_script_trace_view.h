/**
 * @file nmo_script_trace_view.h
 * @brief Stable snapshot views for script/behavior tracing
 */

#ifndef NMO_SCRIPT_TRACE_VIEW_H
#define NMO_SCRIPT_TRACE_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

#define NMO_SCRIPT_TRACE_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_TRACE_VIEW_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef enum nmo_script_trace_step_kind {
    NMO_SCRIPT_TRACE_STEP_START = 0,
    NMO_SCRIPT_TRACE_STEP_SHARED_SOURCE = 1,
    NMO_SCRIPT_TRACE_STEP_DIRECT_SOURCE = 2,
} nmo_script_trace_step_kind_t;

typedef struct nmo_script_trace_step_view {
    nmo_object_id_t id;
    nmo_script_trace_step_kind_t step_kind;
    nmo_object_id_t owner_id;
    nmo_class_id_t class_id;
} nmo_script_trace_step_view_t;

typedef struct nmo_script_trace_chain_view {
    nmo_script_trace_step_view_t *steps;
    size_t step_count;
} nmo_script_trace_chain_view_t;

typedef struct nmo_script_tree_node_view {
    nmo_object_id_t behavior_id;
    uint32_t depth;
    bool is_building_block;
    const char *name;
    nmo_class_id_t class_id;
} nmo_script_tree_node_view_t;

typedef struct nmo_script_tree_view {
    nmo_script_tree_node_view_t *nodes;
    size_t node_count;
} nmo_script_tree_view_t;

NMO_API nmo_status_t nmo_script_trace_parameter_chain(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t parameter_id,
    uint32_t max_depth,
    nmo_script_trace_chain_view_t *out_view);

NMO_API void nmo_script_trace_chain_view_destroy(
    nmo_script_trace_chain_view_t *view);

/**
 * @brief Build a stable snapshot of a behavior/script tree.
 *
 * @param ctx Context used for traversal.
 * @param session Session containing the behavior tree.
 * @param root_behavior_id Root behavior to trace.
 * @param max_depth Maximum recursion depth. `0` means no explicit depth cap.
 * @param out_view Output snapshot. Destroy with nmo_script_tree_view_destroy().
 *
 * @return `NMO_OK` on success, `NMO_ERR_NOT_FOUND` when the root behavior does
 *         not exist, or `NMO_ERR_NOMEM` if snapshot allocation fails while
 *         collecting nodes.
 */
NMO_API nmo_status_t nmo_script_trace_script_tree(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    uint32_t max_depth,
    nmo_script_tree_view_t *out_view);

NMO_API void nmo_script_tree_view_destroy(
    nmo_script_tree_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_TRACE_VIEW_H */
