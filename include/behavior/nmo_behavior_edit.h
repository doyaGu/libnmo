#ifndef NMO_BEHAVIOR_EDIT_H
#define NMO_BEHAVIOR_EDIT_H

#include "runtime/nmo_workspace.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_semantic_validator.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMO_BEHAVIOR_EDIT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_EDIT_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_behavior_replace_bb_desc {
    nmo_object_id_t behavior_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_links;
    bool preserve_params;
} nmo_behavior_replace_bb_desc_t;

typedef struct nmo_behavior_replace_report {
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
    nmo_behavior_semantic_risk_t *semantic_risks;
    size_t semantic_risk_count;
} nmo_behavior_replace_report_t;

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

typedef enum nmo_behavior_fold_interface_mode {
    NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE = 0,
    NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE = 1,
    NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE = 2,
} nmo_behavior_fold_interface_mode_t;

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
    const nmo_behavior_fold_map_t *input_maps;
    size_t input_map_count;
    const nmo_behavior_fold_map_t *output_maps;
    size_t output_map_count;
    const nmo_behavior_fold_map_t *parameter_maps;
    size_t parameter_map_count;
    nmo_behavior_fold_interface_mode_t interface_mode;
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
    nmo_behavior_fold_map_t *input_maps;
    size_t input_map_count;
    nmo_behavior_fold_map_t *output_maps;
    size_t output_map_count;
    nmo_behavior_fold_map_t *parameter_maps;
    size_t parameter_map_count;
    nmo_behavior_fold_interface_mode_t interface_mode;
    nmo_object_id_t *selected_nodes;
    size_t selected_node_count;
    nmo_object_id_t *nodes_to_delete;
    size_t nodes_to_delete_count;
    nmo_behavior_boundary_t boundary;
    nmo_behavior_boundary_control_edge_t *control_links_to_delete;
    size_t control_links_to_delete_count;
    nmo_behavior_fold_write_blocker_t *write_blockers;
    size_t write_blocker_count;
    nmo_behavior_semantic_risk_t *semantic_risks;
    size_t semantic_risk_count;
    const char *diagnostic_code;
    const char *diagnostic_message;
} nmo_behavior_fold_report_t;

NMO_API nmo_status_t nmo_behavior_edit_add_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);

NMO_API nmo_status_t nmo_behavior_edit_remove_link(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

NMO_API nmo_status_t nmo_behavior_edit_mark_interface(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t behavior_id);

NMO_API nmo_status_t nmo_behavior_edit_replace_bb(
    nmo_workspace_t *workspace,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_replace_bb_in_edit(
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_fold_analyze(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_fold_apply(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_fold(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_fold_in_script_tx(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

NMO_API nmo_status_t nmo_behavior_edit_collect_semantic_risks(
    nmo_workspace_t *workspace,
    const nmo_behavior_boundary_t *boundary,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count);

NMO_API void nmo_behavior_edit_semantic_risks_free(
    nmo_behavior_semantic_risk_t *risks);

NMO_API void nmo_behavior_edit_fold_report_free(
    nmo_behavior_fold_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EDIT_H */
