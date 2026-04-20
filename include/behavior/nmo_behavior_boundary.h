/**
 * @file nmo_behavior_boundary.h
 * @brief Behavior graph boundary analysis.
 */

#ifndef NMO_BEHAVIOR_BOUNDARY_H
#define NMO_BEHAVIOR_BOUNDARY_H

#include "core/nmo_guid.h"
#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_behavior_boundary_control_edge {
    nmo_object_id_t link_id;
    nmo_object_id_t source_owner_id;
    nmo_object_id_t source_io_id;
    nmo_object_id_t target_owner_id;
    nmo_object_id_t target_io_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
} nmo_behavior_boundary_control_edge_t;

typedef struct nmo_behavior_boundary_parameter_edge {
    nmo_object_id_t source_parameter_id;
    nmo_object_id_t target_parameter_id;
    nmo_object_id_t source_owner_id;
    nmo_object_id_t target_owner_id;
    nmo_guid_t type_guid;
    bool shared;
} nmo_behavior_boundary_parameter_edge_t;

typedef struct nmo_behavior_boundary {
    nmo_object_id_t behavior_id;
    nmo_object_id_t *internal_nodes;
    size_t internal_node_count;
    nmo_behavior_boundary_control_edge_t *control_in;
    size_t control_in_count;
    nmo_behavior_boundary_control_edge_t *control_out;
    size_t control_out_count;
    nmo_behavior_boundary_parameter_edge_t *parameter_in;
    size_t parameter_in_count;
    nmo_behavior_boundary_parameter_edge_t *parameter_out;
    size_t parameter_out_count;
    size_t broken_links;
    size_t missing_nodes;
} nmo_behavior_boundary_t;

NMO_API bool nmo_behavior_boundary_build(nmo_context_t *ctx,
                                         nmo_session_t *session,
                                         nmo_object_id_t behavior_id,
                                         uint32_t max_depth,
                                         nmo_behavior_boundary_t *out_boundary);

NMO_API bool nmo_behavior_boundary_build_for_nodes(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_boundary_t *out_boundary);

NMO_API void nmo_behavior_boundary_free(nmo_behavior_boundary_t *boundary);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_BOUNDARY_H */
