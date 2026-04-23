/**
 * @file nmo_behavior_analyze.h
 * @brief Advanced behavior graph, boundary, ownership, and traversal analysis.
 */

#ifndef NMO_BEHAVIOR_ANALYZE_H
#define NMO_BEHAVIOR_ANALYZE_H

#include "nmo_types.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * This header is the single advanced owner for behavior graph, boundary,
 * ownership-index, and traversal analysis. Canonical binding-facing consumers
 * should prefer nmo_behavior_query_*() and nmo_behavior_view_*() where
 * possible.
 */
#define NMO_BEHAVIOR_ANALYZE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_ANALYZE_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_workspace nmo_workspace_t;
typedef struct nmo_behavior_state nmo_behavior_state_t;

typedef struct nmo_behavior_graph_node {
    nmo_object_id_t id;
    const char *kind;
    const char *name;
    bool owns_name;
    nmo_class_id_t class_id;
    const char *class_name;
    uint32_t depth;
    nmo_object_id_t parent_id;
} nmo_behavior_graph_node_t;

typedef struct nmo_behavior_graph_edge {
    nmo_object_id_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
    const char *kind;
    const char *field_path;
    nmo_object_id_t in_io_id;
    nmo_object_id_t out_io_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
    bool is_shared;
} nmo_behavior_graph_edge_t;

typedef struct nmo_behavior_graph {
    nmo_object_id_t behavior_id;
    const char *behavior_name;
    nmo_class_id_t behavior_class_id;
    const char *behavior_class_name;
    nmo_behavior_graph_node_t *nodes;
    size_t node_count;
    nmo_behavior_graph_edge_t *edges;
    size_t edge_count;
    size_t broken_links;
    size_t missing_nodes;
} nmo_behavior_graph_t;

NMO_API bool nmo_behavior_graph_build(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    uint32_t max_depth,
    nmo_behavior_graph_t *out_graph);

NMO_API void nmo_behavior_graph_free(nmo_behavior_graph_t *graph);

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

NMO_API bool nmo_behavior_boundary_build(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    uint32_t max_depth,
    nmo_behavior_boundary_t *out_boundary);

NMO_API bool nmo_behavior_boundary_build_for_nodes(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_boundary_t *out_boundary);

NMO_API void nmo_behavior_boundary_free(nmo_behavior_boundary_t *boundary);

typedef enum nmo_port_kind {
    NMO_PORT_IO_IN,
    NMO_PORT_IO_OUT,
    NMO_PORT_PARAM_IN,
    NMO_PORT_PARAM_OUT,
    NMO_PORT_PARAM_LOCAL,
    NMO_PORT_PARAM_TARGET,
    NMO_PORT_OPERATION,
    NMO_PORT_SUB_BEHAVIOR,
    NMO_PORT_SUB_LINK,
} nmo_port_kind_t;

typedef struct nmo_port_owner {
    nmo_object_id_t owner_id;
    int32_t index;
    nmo_port_kind_t kind;
} nmo_port_owner_t;

typedef struct nmo_behavior_index nmo_behavior_index_t;

NMO_API nmo_behavior_index_t *nmo_behavior_index_create(nmo_arena_t *arena);
NMO_API void nmo_behavior_index_destroy(nmo_behavior_index_t *index);
NMO_API nmo_status_t nmo_behavior_index_build(
    nmo_behavior_index_t *index,
    nmo_workspace_t *workspace);
NMO_API const nmo_port_owner_t *nmo_behavior_index_find(
    const nmo_behavior_index_t *index,
    nmo_object_id_t id);
NMO_API size_t nmo_behavior_index_count(const nmo_behavior_index_t *index);

typedef bool (*nmo_behavior_walk_visitor_fn)(
    nmo_object_id_t behavior_id,
    const nmo_behavior_state_t *state,
    uint32_t depth,
    bool is_building_block,
    void *user_data);

NMO_API nmo_status_t nmo_behavior_walk(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    nmo_behavior_walk_visitor_fn visitor,
    void *user_data);

typedef enum nmo_behavior_trace_step_type {
    NMO_BEHAVIOR_TRACE_STEP_START,
    NMO_BEHAVIOR_TRACE_STEP_SHARED_SOURCE,
    NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE,
} nmo_behavior_trace_step_type_t;

typedef struct nmo_behavior_trace_step {
    nmo_object_id_t id;
    nmo_behavior_trace_step_type_t type;
    nmo_object_id_t owner_id;
    nmo_class_id_t class_id;
} nmo_behavior_trace_step_t;

NMO_API nmo_status_t nmo_behavior_analyze_trace_param_chain(
    nmo_workspace_t *workspace,
    nmo_object_id_t param_in_id,
    nmo_array_t *out_chain,
    uint32_t max_depth);

NMO_API nmo_status_t nmo_behavior_analyze_dump_text(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_ANALYZE_H */
