/**
 * @file nmo_edit_plan.h
 * @brief Unified script edit operation plan and transaction executor.
 */

#ifndef NMO_EDIT_PLAN_H
#define NMO_EDIT_PLAN_H

#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_object_edit.h"
#include "runtime/nmo_workspace.h"
#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_edit_plan nmo_edit_plan_t;

typedef enum nmo_edit_op_kind {
    NMO_EDIT_OP_SET_PARAMETER_VALUE = 1,
    NMO_EDIT_OP_SET_PARAMETER_BYTES = 2,
    NMO_EDIT_OP_ADD_NODE = 3,
    NMO_EDIT_OP_REMOVE_NODE = 4,
    NMO_EDIT_OP_ADD_IO = 5,
    NMO_EDIT_OP_RENAME_IO = 6,
    NMO_EDIT_OP_REMOVE_IO = 7,
    NMO_EDIT_OP_ADD_BEHAVIOR_LINK = 8,
    NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK = 9,
    NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY = 10,
    NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK = 11,
    NMO_EDIT_OP_ADD_PARAMETER = 12,
    NMO_EDIT_OP_CONNECT_PARAMETER = 13,
    NMO_EDIT_OP_DISCONNECT_PARAMETER = 14,
    NMO_EDIT_OP_REMOVE_PARAMETER = 15,
    NMO_EDIT_OP_ADD_OPERATION = 16,
    NMO_EDIT_OP_REWIRE_OPERATION = 17,
    NMO_EDIT_OP_REMOVE_OPERATION = 18,
    NMO_EDIT_OP_INTERFACE_POLICY = 19,
    NMO_EDIT_OP_SET_DATA_CELL = 20,
    NMO_EDIT_OP_FOLD = 21,
    NMO_EDIT_OP_REPLACE_BB = 22
} nmo_edit_op_kind_t;

typedef struct nmo_edit_op {
    nmo_edit_op_kind_t kind;
    nmo_object_id_t primary_id;
    union {
        struct {
            const char *value;
            nmo_parameter_write_options_t options;
            bool has_options;
            size_t parameter_ref_operation_index;
            const char *parameter_ref_handle;
            bool has_parameter_ref;
        } set_value;
        struct {
            const uint8_t *bytes;
            size_t byte_count;
            nmo_parameter_write_options_t options;
            bool has_options;
        } set_bytes;
        struct {
            nmo_object_id_t parent_behavior_id;
            nmo_guid_t bb_guid;
            const char *name;
        } add_node;
        struct {
            nmo_object_id_t parent_behavior_id;
            nmo_object_id_t node_id;
            uint32_t delete_flags;
        } remove_node;
        struct {
            nmo_object_id_t behavior_id;
            nmo_script_edit_io_kind_t kind;
            const char *name;
        } add_io;
        struct {
            nmo_object_id_t io_id;
            const char *name;
        } rename_io;
        struct {
            nmo_object_id_t io_id;
            bool detach_links;
        } remove_io;
        struct {
            nmo_object_id_t parent_behavior_id;
            nmo_object_id_t from_io_id;
            nmo_object_id_t to_io_id;
            uint32_t activation_delay;
            size_t from_io_ref_operation_index;
            const char *from_io_ref_handle;
            bool has_from_io_ref;
            size_t to_io_ref_operation_index;
            const char *to_io_ref_handle;
            bool has_to_io_ref;
        } add_link;
        struct {
            nmo_object_id_t link_id;
            nmo_object_id_t from_io_id;
            nmo_object_id_t to_io_id;
        } rewire_link;
        struct {
            nmo_object_id_t link_id;
            uint32_t activation_delay;
        } set_link_delay;
        struct {
            nmo_object_id_t parent_behavior_id;
            nmo_object_id_t link_id;
        } remove_link;
        struct {
            nmo_object_id_t owner_behavior_id;
            nmo_script_edit_parameter_kind_t kind;
            nmo_guid_t type_guid;
            const char *name;
        } add_parameter;
        struct {
            nmo_object_id_t source_parameter_id;
            nmo_object_id_t target_parameter_id;
        } connect_parameter;
        struct {
            nmo_object_id_t target_parameter_id;
        } disconnect_parameter;
        struct {
            nmo_object_id_t parameter_id;
            bool detach;
        } remove_parameter;
        struct {
            nmo_object_id_t parent_behavior_id;
            nmo_guid_t operation_guid;
            nmo_object_id_t in1_parameter_id;
            nmo_object_id_t in2_parameter_id;
            nmo_object_id_t out_parameter_id;
        } add_operation;
        struct {
            nmo_object_id_t operation_id;
            uint32_t slot_flags;
            nmo_object_id_t in1_parameter_id;
            nmo_object_id_t in2_parameter_id;
            nmo_object_id_t out_parameter_id;
        } rewire_operation;
        struct {
            nmo_object_id_t operation_id;
        } remove_operation;
        struct {
            nmo_object_id_t behavior_id;
            nmo_script_edit_interface_mode_t mode;
        } interface_policy;
        struct {
            nmo_object_id_t dataarray_id;
            uint32_t row;
            uint32_t col;
            const char *value;
        } data_cell;
        struct {
            nmo_behavior_fold_desc_t desc;
            nmo_object_id_t *node_ids;
            nmo_behavior_fold_map_t *input_maps;
            nmo_behavior_fold_map_t *output_maps;
            nmo_behavior_fold_map_t *parameter_maps;
        } fold;
        struct {
            nmo_behavior_replace_bb_desc_t desc;
        } replace_bb;
    } data;
} nmo_edit_op_t;

typedef struct nmo_edit_executor_options {
    bool dry_run;
    uint32_t validation_flags;
} nmo_edit_executor_options_t;

typedef struct nmo_edit_changed_object {
    nmo_object_id_t id;
    nmo_edit_op_kind_t cause;
    const char *role;
} nmo_edit_changed_object_t;

typedef nmo_edit_changed_object_t nmo_edit_object_impact_t;

typedef struct nmo_edit_operation_handle {
    const char *name;
    nmo_object_id_t id;
} nmo_edit_operation_handle_t;

typedef struct nmo_edit_operation_result {
    nmo_edit_op_kind_t kind;
    nmo_object_id_t primary_id;
    nmo_object_id_t result_id;
    nmo_status_t status;
    const char *diagnostic_code;
    const char *diagnostic_message;
    nmo_edit_operation_handle_t *handles;
    size_t handle_count;
} nmo_edit_operation_result_t;

typedef struct nmo_edit_validation_report {
    nmo_status_t final_status;
    nmo_status_t roundtrip_status;
    nmo_status_t reference_status;
    nmo_status_t behavior_index_status;
    nmo_status_t interface_status;
} nmo_edit_validation_report_t;

typedef struct nmo_edit_report {
    bool ok;
    bool dry_run;
    nmo_status_t status;
    char *output_path;
    size_t operation_count;
    nmo_edit_operation_result_t *operations;
    nmo_edit_validation_report_t validation;
    size_t changed_object_count;
    nmo_edit_changed_object_t *changed_objects;
    size_t created_object_count;
    size_t created_object_capacity;
    nmo_edit_object_impact_t *created_objects;
    size_t deleted_object_count;
    size_t deleted_object_capacity;
    nmo_edit_object_impact_t *deleted_objects;
    nmo_behavior_semantic_risk_t *semantic_risks;
    size_t semantic_risk_count;
    size_t semantic_risk_capacity;
} nmo_edit_report_t;

NMO_API nmo_status_t nmo_edit_plan_create(nmo_edit_plan_t **out_plan);
NMO_API nmo_status_t nmo_edit_plan_clone(
    const nmo_edit_plan_t *plan,
    nmo_edit_plan_t **out_plan);
NMO_API void nmo_edit_plan_destroy(nmo_edit_plan_t *plan);
NMO_API size_t nmo_edit_plan_count(const nmo_edit_plan_t *plan);
NMO_API const nmo_edit_op_t *nmo_edit_plan_get(const nmo_edit_plan_t *plan, size_t index);

NMO_API nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_edit_plan_add_set_parameter_value_from_handle(
    nmo_edit_plan_t *plan,
    size_t operation_index,
    const char *handle_name,
    const char *value_str,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_edit_plan_add_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name);

NMO_API nmo_status_t nmo_edit_plan_add_remove_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags);

NMO_API nmo_status_t nmo_edit_plan_add_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name);

NMO_API nmo_status_t nmo_edit_plan_add_rename_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    const char *name);

NMO_API nmo_status_t nmo_edit_plan_add_remove_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    bool detach_links);

NMO_API nmo_status_t nmo_edit_plan_add_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay);

NMO_API nmo_status_t nmo_edit_plan_add_behavior_link_from_handles(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    size_t from_operation_index,
    const char *from_handle_name,
    size_t to_operation_index,
    const char *to_handle_name,
    uint32_t activation_delay);

NMO_API nmo_status_t nmo_edit_plan_add_rewire_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id);

NMO_API nmo_status_t nmo_edit_plan_add_set_behavior_link_delay(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    uint32_t activation_delay);

NMO_API nmo_status_t nmo_edit_plan_add_remove_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

NMO_API nmo_status_t nmo_edit_plan_add_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name);

NMO_API nmo_status_t nmo_edit_plan_add_connect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id);

NMO_API nmo_status_t nmo_edit_plan_add_disconnect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t target_parameter_id);

NMO_API nmo_status_t nmo_edit_plan_add_remove_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    bool detach);

NMO_API nmo_status_t nmo_edit_plan_add_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id);

NMO_API nmo_status_t nmo_edit_plan_add_rewire_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id);

NMO_API nmo_status_t nmo_edit_plan_add_remove_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id);

NMO_API nmo_status_t nmo_edit_plan_add_interface_policy(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_interface_mode_t mode);

NMO_API nmo_status_t nmo_edit_plan_add_data_cell(
    nmo_edit_plan_t *plan,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value);

NMO_API nmo_status_t nmo_edit_plan_add_fold(
    nmo_edit_plan_t *plan,
    const nmo_behavior_fold_desc_t *desc);

NMO_API nmo_status_t nmo_edit_plan_add_replace_bb(
    nmo_edit_plan_t *plan,
    const nmo_behavior_replace_bb_desc_t *desc);

NMO_API nmo_edit_executor_options_t nmo_edit_executor_options_default(void);

NMO_API nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report);
NMO_API void nmo_edit_report_dispose(nmo_edit_report_t *report);

NMO_API nmo_status_t nmo_edit_report_set_output_path(
    nmo_edit_report_t *report,
    const char *output_path);

NMO_API nmo_status_t nmo_edit_report_add_operation_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *name,
    nmo_object_id_t id);

NMO_API nmo_status_t nmo_edit_report_add_created_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role);

NMO_API nmo_status_t nmo_edit_report_add_deleted_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role);

NMO_API nmo_status_t nmo_edit_report_add_changed_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role);

NMO_API nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report);

NMO_API nmo_status_t nmo_edit_executor_execute_transaction(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EDIT_PLAN_H */
