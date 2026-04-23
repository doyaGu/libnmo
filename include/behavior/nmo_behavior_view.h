/**
 * @file nmo_behavior_view.h
 * @brief Stable read-only summaries, traces, and parameter presentation.
 */

#ifndef NMO_BEHAVIOR_VIEW_H
#define NMO_BEHAVIOR_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_interface_view.h"
#include "type/nmo_type_system.h"
#include "object/builtin/nmo_parameter_schemas.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace nmo_workspace_t;

#define NMO_BEHAVIOR_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_VIEW_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_behavior_view {
    nmo_object_id_t behavior_id;
    nmo_class_id_t class_id;
    const char *name;
    uint32_t flags;
    bool is_building_block;
    bool has_target_parameter;
    nmo_object_id_t target_parameter_id;
    size_t sub_behavior_count;
    size_t link_count;
    size_t operation_count;
    size_t input_count;
    size_t output_count;
    size_t in_parameter_count;
    size_t out_parameter_count;
    size_t local_parameter_count;
    bool owner_index_available;
    bool edit_ready;
    nmo_status_t edit_graph_status;
    bool has_interface;
    bool interface_available;
    nmo_status_t interface_status;
    nmo_interface_view_t interface_view;
} nmo_behavior_view_t;

typedef struct nmo_behavior_boundary_view {
    nmo_object_id_t behavior_id;
    size_t internal_node_count;
    size_t control_in_count;
    size_t control_out_count;
    size_t parameter_in_count;
    size_t parameter_out_count;
    size_t broken_links;
    size_t missing_nodes;
} nmo_behavior_boundary_view_t;

NMO_API nmo_status_t nmo_behavior_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    nmo_behavior_view_t *out_view);

NMO_API nmo_status_t nmo_behavior_view_describe_boundary(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    uint32_t max_depth,
    nmo_behavior_boundary_view_t *out_view);

typedef enum nmo_behavior_trace_step_kind {
    NMO_BEHAVIOR_TRACE_STEP_KIND_START = 0,
    NMO_BEHAVIOR_TRACE_STEP_KIND_SHARED_SOURCE = 1,
    NMO_BEHAVIOR_TRACE_STEP_KIND_DIRECT_SOURCE = 2,
} nmo_behavior_trace_step_kind_t;

typedef struct nmo_behavior_trace_step_view {
    nmo_object_id_t id;
    nmo_behavior_trace_step_kind_t step_kind;
    nmo_object_id_t owner_id;
    nmo_class_id_t class_id;
} nmo_behavior_trace_step_view_t;

typedef struct nmo_behavior_trace_chain_view {
    nmo_behavior_trace_step_view_t *steps;
    size_t step_count;
} nmo_behavior_trace_chain_view_t;

typedef struct nmo_behavior_tree_node_view {
    nmo_object_id_t behavior_id;
    uint32_t depth;
    bool is_building_block;
    const char *name;
    nmo_class_id_t class_id;
} nmo_behavior_tree_node_view_t;

typedef struct nmo_behavior_tree_view {
    nmo_behavior_tree_node_view_t *nodes;
    size_t node_count;
} nmo_behavior_tree_view_t;

NMO_API nmo_status_t nmo_behavior_trace_parameter_chain(
    nmo_workspace_t *workspace,
    nmo_object_id_t parameter_id,
    uint32_t max_depth,
    nmo_behavior_trace_chain_view_t *out_view);

NMO_API void nmo_behavior_trace_chain_view_destroy(
    nmo_behavior_trace_chain_view_t *view);

NMO_API nmo_status_t nmo_behavior_trace_script_tree(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    uint32_t max_depth,
    nmo_behavior_tree_view_t *out_view);

NMO_API void nmo_behavior_tree_view_destroy(
    nmo_behavior_tree_view_t *view);

NMO_API nmo_status_t nmo_behavior_param_value_to_string(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace,
    char *buffer,
    size_t buffer_size);

NMO_API const char *nmo_behavior_param_type_name(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry);

NMO_API const char *nmo_behavior_param_mode_to_string(
    nmo_parameter_mode_t mode);

NMO_API nmo_status_t nmo_behavior_param_format_summary(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace,
    char *buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_VIEW_H */
