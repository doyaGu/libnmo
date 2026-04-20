/**
 * @file nmo_behavior_rewrite.h
 * @brief Safe behavior graph rewrite operations.
 */

#ifndef NMO_BEHAVIOR_REWRITE_H
#define NMO_BEHAVIOR_REWRITE_H

#include "behavior/nmo_behavior_boundary.h"
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

typedef struct nmo_behavior_replace_bb_desc {
    nmo_object_id_t behavior_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_links;
    bool preserve_params;
} nmo_behavior_replace_bb_desc_t;

typedef struct nmo_behavior_rewrite_report {
    bool changed;
    bool eligible_leaf;
    nmo_object_id_t behavior_id;
    uint32_t before_flags;
    uint32_t after_flags;
    nmo_guid_t before_guid;
    nmo_guid_t after_guid;
    size_t sub_behavior_count;
    size_t sub_behavior_link_count;
    size_t operation_count;
    size_t preserved_inputs;
    size_t preserved_outputs;
    size_t preserved_in_parameters;
    size_t preserved_out_parameters;
    size_t preserved_local_parameters;
    size_t preserved_control_in;
    size_t preserved_control_out;
    size_t preserved_parameter_in;
    size_t preserved_parameter_out;
    const char *diagnostic_code;
    const char *diagnostic_message;
    size_t diagnostics_count;
} nmo_behavior_rewrite_report_t;

typedef enum nmo_behavior_fold_map_kind {
    NMO_BEHAVIOR_FOLD_MAP_INPUT = 0,
    NMO_BEHAVIOR_FOLD_MAP_OUTPUT = 1,
    NMO_BEHAVIOR_FOLD_MAP_PARAMETER = 2,
} nmo_behavior_fold_map_kind_t;

typedef struct nmo_behavior_fold_map {
    nmo_behavior_fold_map_kind_t kind;
    uint32_t old_index;
    uint32_t new_index;
    nmo_object_id_t old_id;
    nmo_object_id_t new_id;
    const char *label;
} nmo_behavior_fold_map_t;

typedef struct nmo_behavior_fold_desc {
    nmo_object_id_t parent_id;
    const nmo_object_id_t *node_ids;
    size_t node_count;
    nmo_object_id_t anchor_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_boundary;
    bool preserve_links;
    bool preserve_params;
    const nmo_behavior_fold_map_t *output_maps;
    size_t output_map_count;
} nmo_behavior_fold_desc_t;

typedef struct nmo_behavior_fold_write_blocker {
    const char *code;
    const char *message;
} nmo_behavior_fold_write_blocker_t;

typedef struct nmo_behavior_fold_report {
    bool analysis_only;
    bool rejected;
    bool can_write;
    nmo_object_id_t parent_id;
    nmo_object_id_t anchor_id;
    nmo_object_id_t representative_id;
    nmo_guid_t target_guid;
    const char *target_name;
    uint32_t target_version;
    bool preserve_boundary;
    bool preserve_links;
    bool preserve_params;
    nmo_behavior_fold_map_t *output_maps;
    size_t output_map_count;

    nmo_object_id_t *selected_nodes;
    size_t selected_node_count;
    nmo_object_id_t *nodes_to_delete;
    size_t nodes_to_delete_count;

    nmo_behavior_boundary_t boundary;
    nmo_behavior_boundary_control_edge_t *control_links_to_delete;
    size_t control_links_to_delete_count;

    nmo_behavior_fold_write_blocker_t *write_blockers;
    size_t write_blocker_count;

    const char *diagnostic_code;
    const char *diagnostic_message;
} nmo_behavior_fold_report_t;

NMO_API nmo_status_t nmo_behavior_replace_bb(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_rewrite_report_t *report);

NMO_API nmo_status_t nmo_behavior_fold_analyze(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_fold(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API void nmo_behavior_fold_report_free(
    nmo_behavior_fold_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_REWRITE_H */
