/**
 * @file nmo_script_edit_graph.h
 * @brief Script edit graph IR for transaction-safe script editing.
 */

#ifndef NMO_SCRIPT_EDIT_GRAPH_H
#define NMO_SCRIPT_EDIT_GRAPH_H

#include "behavior/nmo_behavior_analyze.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"
#include "object/nmo_ref_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * This graph IR remains public for advanced validation, handle resolution, and
 * power-user tooling around script edits. Ordinary consumers should prefer
 * nmo_script_edit_*() for writes and stable read facades such as
 * nmo_behavior_view_*() where possible instead of depending on this IR as the
 * default behavior contract.
 */
#define NMO_SCRIPT_EDIT_GRAPH_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_EDIT_GRAPH_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_workspace nmo_workspace_t;

typedef enum nmo_script_edit_node_kind {
    NMO_SCRIPT_EDIT_NODE_BEHAVIOR = 0,
    NMO_SCRIPT_EDIT_NODE_IO,
    NMO_SCRIPT_EDIT_NODE_PARAMETER,
    NMO_SCRIPT_EDIT_NODE_OPERATION,
    NMO_SCRIPT_EDIT_NODE_LINK
} nmo_script_edit_node_kind_t;

typedef struct nmo_script_edit_endpoint {
    nmo_object_id_t object_id;
    nmo_object_id_t owner_behavior_id;
    int32_t owner_index;
    uint32_t kind;
} nmo_script_edit_endpoint_t;

typedef struct nmo_script_edit_control_edge {
    nmo_object_id_t link_id;
    nmo_script_edit_endpoint_t source;
    nmo_script_edit_endpoint_t target;
    int32_t activation_delay;
    int32_t initial_activation_delay;
} nmo_script_edit_control_edge_t;

typedef struct nmo_script_edit_data_edge {
    nmo_object_id_t source_parameter_id;
    nmo_object_id_t target_parameter_id;
    nmo_object_id_t source_owner_id;
    nmo_object_id_t target_owner_id;
    nmo_guid_t type_guid;
    bool shared;
} nmo_script_edit_data_edge_t;

typedef enum nmo_script_edit_handle_kind {
    NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID = 0,
    NMO_SCRIPT_EDIT_HANDLE_ALIAS,
    NMO_SCRIPT_EDIT_HANDLE_QUERY,
    NMO_SCRIPT_EDIT_HANDLE_SLOT
} nmo_script_edit_handle_kind_t;

typedef struct nmo_script_edit_handle {
    nmo_script_edit_handle_kind_t kind;
    nmo_object_id_t object_id;
    const char *alias;
    const char *query;
    nmo_object_id_t owner_id;
    int32_t slot_index;
    uint32_t slot_kind;
} nmo_script_edit_handle_t;

typedef enum nmo_script_edit_op_kind {
    NMO_SCRIPT_EDIT_OP_NODE_ADD = 0,
    NMO_SCRIPT_EDIT_OP_NODE_REMOVE,
    NMO_SCRIPT_EDIT_OP_IO_ADD,
    NMO_SCRIPT_EDIT_OP_IO_RENAME,
    NMO_SCRIPT_EDIT_OP_IO_REMOVE,
    NMO_SCRIPT_EDIT_OP_LINK_ADD,
    NMO_SCRIPT_EDIT_OP_LINK_REWIRE,
    NMO_SCRIPT_EDIT_OP_LINK_REMOVE,
    NMO_SCRIPT_EDIT_OP_PARAM_ADD,
    NMO_SCRIPT_EDIT_OP_PARAM_SET,
    NMO_SCRIPT_EDIT_OP_PARAM_CONNECT,
    NMO_SCRIPT_EDIT_OP_PARAM_DISCONNECT,
    NMO_SCRIPT_EDIT_OP_PARAM_REMOVE,
    NMO_SCRIPT_EDIT_OP_OPERATION_ADD,
    NMO_SCRIPT_EDIT_OP_OPERATION_REWIRE,
    NMO_SCRIPT_EDIT_OP_OPERATION_REMOVE,
    NMO_SCRIPT_EDIT_OP_SUBGRAPH_FOLD,
    NMO_SCRIPT_EDIT_OP_VALIDATE
} nmo_script_edit_op_kind_t;

typedef struct nmo_script_edit_op {
    nmo_script_edit_op_kind_t kind;
    nmo_script_edit_handle_t primary;
    nmo_script_edit_handle_t secondary;
    const char *label;
    nmo_guid_t guid;
    uint32_t flags;
} nmo_script_edit_op_t;

typedef struct nmo_script_edit_node {
    nmo_object_id_t object_id;
    nmo_script_edit_node_kind_t kind;
    const char *name;
    nmo_class_id_t class_id;
    const char *class_name;
    uint32_t depth;
    nmo_object_id_t parent_behavior_id;
    nmo_object_id_t owner_behavior_id;
    int32_t owner_slot_index;
    uint32_t owner_slot_kind;
} nmo_script_edit_node_t;

typedef struct nmo_script_edit_graph nmo_script_edit_graph_t;

NMO_API nmo_status_t nmo_script_edit_graph_build(nmo_workspace_t *workspace,
                                                 nmo_object_id_t root_behavior_id,
                                                 uint32_t max_depth,
                                                 nmo_script_edit_graph_t **out_graph);

NMO_API void nmo_script_edit_graph_destroy(nmo_script_edit_graph_t *graph);

NMO_API nmo_object_id_t nmo_script_edit_graph_root_behavior_id(
    const nmo_script_edit_graph_t *graph);
NMO_API bool nmo_script_edit_graph_edit_ready(
    const nmo_script_edit_graph_t *graph);
NMO_API bool nmo_script_edit_graph_owner_index_available(
    const nmo_script_edit_graph_t *graph);
NMO_API size_t nmo_script_edit_graph_node_count(
    const nmo_script_edit_graph_t *graph);
NMO_API const nmo_script_edit_node_t *nmo_script_edit_graph_nodes(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count);
NMO_API const nmo_script_edit_control_edge_t *nmo_script_edit_graph_control_edges(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count);
NMO_API const nmo_script_edit_data_edge_t *nmo_script_edit_graph_data_edges(
    const nmo_script_edit_graph_t *graph,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_reference_validation_status(
    const nmo_script_edit_graph_t *graph,
    size_t *out_broken_count);

NMO_API nmo_status_t nmo_script_edit_graph_find_owner(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_script_edit_endpoint_t *out_owner);
NMO_API nmo_status_t nmo_script_edit_graph_get_incoming_control(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t behavior_id,
    nmo_arena_t *arena,
    const nmo_script_edit_control_edge_t **out_edges,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_get_outgoing_control(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t behavior_id,
    nmo_arena_t *arena,
    const nmo_script_edit_control_edge_t **out_edges,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_get_parameter_sources(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t parameter_id,
    nmo_arena_t *arena,
    const nmo_script_edit_data_edge_t **out_edges,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_get_parameter_destinations(
    const nmo_script_edit_graph_t *graph,
    nmo_object_id_t parameter_id,
    nmo_arena_t *arena,
    const nmo_script_edit_data_edge_t **out_edges,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_get_external_refs(
    const nmo_script_edit_graph_t *graph,
    nmo_arena_t *arena,
    const nmo_ref_edge_t **out_edges,
    size_t *out_count);
NMO_API nmo_status_t nmo_script_edit_graph_resolve_handle(
    const nmo_script_edit_graph_t *graph,
    const nmo_script_edit_handle_t *handle,
    nmo_object_id_t *out_object_id);
NMO_API nmo_status_t nmo_script_edit_graph_validate_operation(
    const nmo_script_edit_graph_t *graph,
    const nmo_script_edit_op_t *op);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_EDIT_GRAPH_H */
