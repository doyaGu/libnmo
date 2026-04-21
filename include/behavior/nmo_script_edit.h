/**
 * @file nmo_script_edit.h
 * @brief Script edit transaction kernel.
 */

#ifndef NMO_SCRIPT_EDIT_H
#define NMO_SCRIPT_EDIT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_session_edit nmo_session_edit_t;
typedef struct nmo_script_edit_tx nmo_script_edit_tx_t;

typedef enum nmo_script_edit_io_kind {
    NMO_SCRIPT_EDIT_IO_INPUT = 0,
    NMO_SCRIPT_EDIT_IO_OUTPUT = 1
} nmo_script_edit_io_kind_t;

typedef enum nmo_script_edit_parameter_kind {
    NMO_SCRIPT_EDIT_PARAM_IN = 0,
    NMO_SCRIPT_EDIT_PARAM_OUT = 1,
    NMO_SCRIPT_EDIT_PARAM_LOCAL = 2,
    NMO_SCRIPT_EDIT_PARAM_SHARED = 3
} nmo_script_edit_parameter_kind_t;

typedef enum nmo_script_edit_operation_slot_flags {
    NMO_SCRIPT_EDIT_OP_SLOT_IN1 = 1u << 0,
    NMO_SCRIPT_EDIT_OP_SLOT_IN2 = 1u << 1,
    NMO_SCRIPT_EDIT_OP_SLOT_OUT = 1u << 2
} nmo_script_edit_operation_slot_flags_t;

typedef enum nmo_script_edit_validation_flags {
    NMO_SCRIPT_EDIT_VALIDATE_REFERENCES      = 1u << 0,
    NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX  = 1u << 1,
    NMO_SCRIPT_EDIT_VALIDATE_INTERFACE       = 1u << 2,
    NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY = 1u << 3
} nmo_script_edit_validation_flags_t;

typedef struct nmo_script_edit_report {
    size_t created_objects;
    size_t deleted_objects;
    size_t changed_objects;
    size_t moved_links;
    size_t rewired_parameters;
    size_t interface_changes;
    size_t warnings;
    size_t errors;
} nmo_script_edit_report_t;

NMO_API nmo_status_t nmo_script_edit_begin(nmo_context_t *ctx,
                                           nmo_session_t *session,
                                           const char *label,
                                           nmo_script_edit_tx_t **out_tx);

NMO_API nmo_session_edit_t *nmo_script_edit_session_edit(
    nmo_script_edit_tx_t *tx);

NMO_API void nmo_script_edit_mark(nmo_script_edit_tx_t *tx,
                                  uint32_t session_edit_flags);

NMO_API const nmo_script_edit_report_t *nmo_script_edit_report(
    const nmo_script_edit_tx_t *tx);

NMO_API nmo_status_t nmo_script_edit_validate(nmo_script_edit_tx_t *tx,
                                              uint32_t validation_flags);

NMO_API nmo_status_t nmo_script_edit_add_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    nmo_object_id_t *out_node_id);

NMO_API nmo_status_t nmo_script_edit_remove_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags);

NMO_API nmo_status_t nmo_script_edit_add_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name,
    nmo_object_id_t *out_io_id);

NMO_API nmo_status_t nmo_script_edit_rename_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    const char *name);

NMO_API nmo_status_t nmo_script_edit_remove_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    bool detach_links);

NMO_API nmo_status_t nmo_script_edit_add_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name,
    nmo_object_id_t *out_parameter_id);

NMO_API nmo_status_t nmo_script_edit_set_parameter_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const char *value_str);

NMO_API nmo_status_t nmo_script_edit_set_parameter_bytes(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count);

NMO_API nmo_status_t nmo_script_edit_connect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id);

NMO_API nmo_status_t nmo_script_edit_disconnect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id);

NMO_API nmo_status_t nmo_script_edit_remove_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    bool detach);

NMO_API nmo_status_t nmo_script_edit_add_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay,
    nmo_object_id_t *out_link_id);

NMO_API nmo_status_t nmo_script_edit_rewire_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id);

NMO_API nmo_status_t nmo_script_edit_set_behavior_link_delay(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    uint32_t activation_delay);

NMO_API nmo_status_t nmo_script_edit_remove_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

NMO_API nmo_status_t nmo_script_edit_add_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id,
    nmo_object_id_t *out_operation_id);

NMO_API nmo_status_t nmo_script_edit_rewire_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id);

NMO_API nmo_status_t nmo_script_edit_remove_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id);

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx);
NMO_API void nmo_script_edit_rollback(nmo_script_edit_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_EDIT_H */
