/**
 * @file script_edit_internal.h
 * @brief Internal declarations shared by the script edit translation units.
 */

#ifndef NMO_SCRIPT_EDIT_INTERNAL_H
#define NMO_SCRIPT_EDIT_INTERNAL_H

#include "behavior/nmo_script_edit.h"

#include "behavior/nmo_behavior_analyze.h"
#include "../runtime/runtime_internal.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"

typedef struct script_edit_removed_io_ref {
    nmo_object_id_t owner_behavior_id;
    nmo_port_kind_t kind;
    size_t removed_index;
} script_edit_removed_io_ref_t;

struct nmo_script_edit_tx {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_workspace_edit_t *edit;
    nmo_script_edit_report_t report;
    uint32_t workspace_edit_flags;
    nmo_object_id_t *deferred_destroy_ids;
    size_t deferred_destroy_count;
    size_t deferred_destroy_capacity;
    nmo_object_id_t *created_object_ids;
    size_t created_object_id_count;
    size_t created_object_id_capacity;
    nmo_object_id_t *changed_object_ids;
    size_t changed_object_id_count;
    size_t changed_object_id_capacity;
    nmo_ref_edge_t *baseline_broken_refs;
    size_t baseline_broken_ref_count;
    nmo_session_behavior_interface_diagnostics_t baseline_interface_diag;
    script_edit_removed_io_ref_t *removed_io_refs;
    size_t removed_io_ref_count;
    size_t removed_io_ref_capacity;
    bool finished;
};

nmo_status_t script_edit_note_changed_id(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id);

nmo_status_t script_edit_note_created_id(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id);

nmo_status_t script_edit_append_deferred_destroy(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id);

nmo_status_t script_edit_append_removed_io_ref(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_port_kind_t kind,
    size_t removed_index);

void *script_edit_get_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid);

void *script_edit_find_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid,
    nmo_object_t **out_object);

nmo_behavior_state_t *script_edit_find_behavior_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object);

nmo_behavior_state_t *script_edit_find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object);

nmo_behaviorlink_state_t *script_edit_find_link_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t link_id,
    nmo_object_t **out_object);

nmo_parameterin_state_t *script_edit_find_parameterin_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object);

nmo_parameterout_state_t *script_edit_find_parameterout_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object);

nmo_parameteroperation_state_t *script_edit_find_operation_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id,
    nmo_object_t **out_object);

nmo_class_id_t script_edit_get_parameter_connection_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    void **out_state);

void script_edit_update_behavior_save_flags(
    nmo_behavior_state_t *state);

nmo_status_t script_edit_create_runtime_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_object_id);

nmo_status_t script_edit_create_io_object(
    nmo_script_edit_tx_t *tx,
    const char *name,
    nmo_script_edit_io_kind_t kind,
    nmo_object_id_t *out_io_id);

nmo_status_t script_edit_require_behavior_index(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_index_t **out_index);

bool script_edit_is_pending_destroy(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id);

bool script_edit_behavior_is_direct_graph_member(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id);

bool script_edit_behavior_is_graph_member(
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    nmo_object_id_t behavior_id);

bool script_edit_find_direct_parent_behavior(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_id_t *out_parent_behavior_id);

bool script_edit_find_parent_graph_io_owner(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id,
    const nmo_port_owner_t **out_owner);

nmo_status_t script_edit_create_parameter_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_guid_t type_guid,
    const char *default_value,
    const nmo_manager_entry_options_t *manager_entry,
    nmo_object_id_t *out_parameter_id);

bool script_edit_find_parameter_owner(
    const nmo_behavior_index_t *index,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner);

bool script_edit_is_parameter_reference_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object);

nmo_parameter_state_t *script_edit_get_value_parameter_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object);

bool script_edit_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id);

nmo_status_t script_edit_append_operation_slot_destroy_objects(
    nmo_script_edit_tx_t *tx,
    const nmo_parameteroperation_state_t *operation);

nmo_guid_t script_edit_parameter_type_guid_from_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *object);

#endif /* NMO_SCRIPT_EDIT_INTERNAL_H */
