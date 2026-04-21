/**
 * @file nmo_behavior_view.h
 * @brief Stable read-only summaries for behavior objects and boundaries
 */

#ifndef NMO_BEHAVIOR_VIEW_H
#define NMO_BEHAVIOR_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "format/nmo_interface_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * Stable inspect facade over behavior state and graph-boundary analysis.
 * This header is intended for binding-facing consumers and should not expose
 * raw behavior_state layout or graph-owned arrays as the default contract.
 */
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
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_behavior_view_t *out_view);

NMO_API nmo_status_t nmo_behavior_view_describe_boundary(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    uint32_t max_depth,
    nmo_behavior_boundary_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_VIEW_H */
