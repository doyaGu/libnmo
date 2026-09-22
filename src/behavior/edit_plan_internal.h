/**
 * @file edit_plan_internal.h
 * @brief Internal declarations shared by the edit plan translation units.
 */

#ifndef NMO_EDIT_PLAN_INTERNAL_H
#define NMO_EDIT_PLAN_INTERNAL_H

#include "behavior/nmo_edit_plan.h"

#include "edit_op_kind_internal.h"

#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_interface_chunk.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_guids.h"

#include "../runtime/runtime_internal.h"

struct nmo_edit_plan {
    nmo_edit_op_t *ops;
    size_t count;
    size_t capacity;
    bool has_probe_selector_analysis;
    nmo_probe_selector_result_t probe_selector_analysis;
};

typedef struct edit_plan_manager_snapshot {
    bool has_message_manager;
    const char **message_names;
    uint32_t message_name_count;
    struct edit_plan_attribute_entry {
        const char *name;
        const char *category;
        nmo_guid_t type_guid;
        uint32_t compatible_class_id;
        uint32_t flags;
    } *attribute_entries;
    uint32_t attribute_entry_count;
} edit_plan_manager_snapshot_t;

typedef nmo_edit_handle_ref_t edit_plan_handle_ref_t;

typedef struct edit_plan_handle_ref_clone_slot {
    edit_plan_handle_ref_t *dst;
    const edit_plan_handle_ref_t *src;
} edit_plan_handle_ref_clone_slot_t;

char *edit_plan_strdup(const char *text);

nmo_status_t edit_plan_probe_analysis_copy(
    nmo_probe_selector_result_t *dst,
    const nmo_probe_selector_result_t *src);

nmo_status_t edit_plan_add_node_options_clone(
    nmo_add_node_options_t *dst,
    const nmo_add_node_options_t *src);

void edit_plan_manager_snapshot_dispose(
    edit_plan_manager_snapshot_t *snapshot);

nmo_status_t edit_plan_read_manager_snapshot(
    nmo_workspace_t *workspace,
    edit_plan_manager_snapshot_t *snapshot);

bool edit_plan_find_created_message_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    uint32_t *out_index);

bool edit_plan_find_created_attribute_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    const char **out_category,
    nmo_guid_t *out_type_guid,
    uint32_t *out_index,
    uint32_t *out_compatible_class_id,
    uint32_t *out_flags);

const nmo_manager_entry_options_t *edit_plan_op_manager_entry(
    const nmo_edit_op_t *op);

void *edit_plan_get_typed_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid);

void *edit_plan_get_object_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid);

uint32_t edit_plan_get_parameter_manager_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id);

nmo_status_t edit_plan_dup_object_ids(
    const nmo_object_id_t *ids,
    size_t count,
    nmo_object_id_t **out_ids);

nmo_status_t edit_plan_dup_fold_maps(
    const nmo_behavior_fold_map_t *maps,
    size_t count,
    nmo_behavior_fold_map_t **out_maps);

nmo_status_t edit_plan_copy_parameter_write_options(
    nmo_parameter_write_options_t *out_options,
    bool *out_has_options,
    const nmo_parameter_write_options_t *options);

nmo_status_t edit_plan_copy_bytes(
    const uint8_t *bytes,
    size_t byte_count,
    const uint8_t **out_bytes);

void edit_op_dispose(nmo_edit_op_t *op);

nmo_status_t edit_op_clone_handle_ref_slots_or_dispose(
    nmo_edit_op_t *op,
    const edit_plan_handle_ref_clone_slot_t *slots,
    size_t slot_count);

nmo_status_t edit_op_copy(
    nmo_edit_op_t *dst,
    const nmo_edit_op_t *src);

nmo_status_t edit_report_prepare(nmo_edit_report_t *report,
                                        const nmo_edit_plan_t *plan,
                                        bool dry_run);

nmo_edit_object_impact_t *edit_report_find_impact(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role);

void edit_report_set_control_link_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay);

void edit_report_set_control_link_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay);

void edit_report_set_parameter_edge_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id);

void edit_report_set_parameter_edge_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id);

void edit_report_set_operation_slot_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state);

void edit_report_set_operation_slot_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state);

const nmo_behavior_state_t *edit_plan_get_behavior_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id);

const nmo_dataarray_cell_t *edit_plan_get_data_cell(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    uint32_t *out_type);

void edit_report_set_interface_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state);

void edit_report_set_interface_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state);

void edit_report_set_data_cell_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell);

void edit_report_set_data_cell_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell);

void edit_report_set_manager_entry_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_guid_t manager_guid,
    nmo_manager_entry_schema_t schema,
    const char *key,
    const char *category,
    nmo_guid_t type_guid,
    uint32_t entry_index,
    uint32_t entry_value,
    uint32_t compatible_class_id,
    uint32_t flags,
    bool created,
    bool manager_chunk_changed);

nmo_status_t edit_report_note_manager_entry_after(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    const char *key,
    uint32_t entry_index,
    bool created,
    bool manager_chunk_changed);

nmo_status_t edit_report_set_operation_diagnostic(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *code,
    const char *message);

nmo_status_t edit_report_note_created_objects(
    nmo_edit_report_t *report,
    const nmo_object_id_t *ids,
    size_t count,
    nmo_edit_op_kind_t cause,
    const char *role);

nmo_status_t edit_report_note_operation_slot_parameters(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id);

nmo_status_t edit_report_note_control_link_endpoints(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id);

nmo_status_t edit_report_note_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t io_id);

nmo_status_t edit_report_note_parameter_edge_source(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t source_parameter_id);

nmo_object_id_t edit_plan_get_parameterin_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id);

void edit_plan_get_behavior_link_endpoints(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t *out_from_io_id,
    nmo_object_id_t *out_to_io_id,
    uint32_t *out_activation_delay);

void edit_plan_get_parameter_operation_slots(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    nmo_object_id_t *out_in1_parameter_id,
    nmo_object_id_t *out_in2_parameter_id,
    nmo_object_id_t *out_out_parameter_id);

const nmo_parameteroperation_state_t *edit_plan_get_operation_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id);

nmo_class_id_t edit_plan_get_parameter_connection_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    void **out_state);

nmo_status_t edit_report_note_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t parameter_id);

nmo_status_t edit_report_note_operation_slot_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    const nmo_parameteroperation_state_t *operation);

nmo_status_t edit_report_note_behavior_owned_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id);

nmo_status_t edit_report_note_behavior_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id);

nmo_status_t edit_report_note_behavior_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id);

nmo_status_t edit_report_note_fold_impact(
    nmo_edit_report_t *report,
    const nmo_behavior_fold_report_t *fold_report,
    nmo_object_id_t parent_id);

nmo_status_t edit_report_add_node_child_handles(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_id_t node_id);

nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id);

#endif /* NMO_EDIT_PLAN_INTERNAL_H */
