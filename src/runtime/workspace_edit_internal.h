/**
 * @file workspace_edit_internal.h
 * @brief Types and helpers shared by the workspace edit sources.
 */

#ifndef NMO_WORKSPACE_EDIT_INTERNAL_H
#define NMO_WORKSPACE_EDIT_INTERNAL_H

#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "core/nmo_array.h"

typedef struct workspace_edit_checkpoint {
    nmo_workspace_edit_journal_checkpoint_t journal;
    uint32_t flags;
} workspace_edit_checkpoint_t;

struct nmo_workspace_edit {
    nmo_workspace_t *workspace;
    nmo_workspace_edit_journal_t journal;
    char *label;
    uint32_t flags;
    bool finished;
    bool owns_active_edit;
};

typedef struct array_id_action {
    nmo_array_t *array;
    nmo_object_id_t id;
    size_t index;
} array_id_action_t;

typedef struct object_id_action {
    nmo_object_id_t id;
} object_id_action_t;

/* Helpers in workspace_edit_common.c. */
nmo_status_t workspace_edit_push_action(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_phase_t phase,
    nmo_workspace_edit_action_fn fn,
    void *payload);
nmo_status_t workspace_edit_push_rollback(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload);
nmo_status_t workspace_edit_push_commit(
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn fn,
    void *payload);
workspace_edit_checkpoint_t workspace_edit_checkpoint(
    const nmo_workspace_edit_t *edit);
nmo_status_t workspace_edit_checkpoint_mark_arena(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t *checkpoint);
nmo_status_t workspace_edit_checkpoint_release_arena(
    nmo_workspace_edit_t *edit,
    const workspace_edit_checkpoint_t *checkpoint);
void workspace_edit_abort_to(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint);
nmo_status_t workspace_edit_abort_status(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_status_t status);
nmo_status_t workspace_edit_abort_arena_status(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_status_t status);
nmo_status_t workspace_edit_push_rollback_or_abort(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    nmo_workspace_edit_action_fn fn,
    void *payload);
nmo_status_t workspace_edit_push_bytes_snapshot(
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size);
nmo_status_t workspace_edit_push_bytes_snapshot_or_abort(
    nmo_workspace_edit_t *edit,
    workspace_edit_checkpoint_t checkpoint,
    void *target,
    size_t size);
nmo_status_t workspace_edit_rollback_remove_array_id(nmo_workspace_edit_t *edit, void *payload);
nmo_status_t workspace_edit_action_remove_object(nmo_workspace_edit_t *edit, void *payload);
nmo_status_t workspace_edit_make_object_id_action(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t id,
    object_id_action_t **out_action);
nmo_status_t workspace_edit_make_array_id_action(
    nmo_workspace_edit_t *edit,
    nmo_array_t *array,
    nmo_object_id_t id,
    size_t index,
    array_id_action_t **out_action);
const nmo_type_registry_t *workspace_edit_type_registry(
    const nmo_workspace_edit_t *edit);
bool workspace_edit_session_object_derives(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object,
    nmo_class_id_t base_class_id);
void *workspace_edit_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid);
nmo_status_t workspace_edit_seek_message_manager_identifier(
    nmo_chunk_t *chunk,
    size_t *out_section_end);
uint8_t workspace_edit_float_color_channel(float value);
uint32_t workspace_edit_pack_argb(float r, float g, float b, float a);
bool workspace_edit_class_is_entity_target(nmo_class_id_t class_id);
bool workspace_edit_object_is_entity_target(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object);
nmo_status_t workspace_edit_read_message_manager_names(
    nmo_session_t *session,
    const nmo_manager_data_t *manager,
    const char ***out_names,
    uint32_t *out_count);

#endif /* NMO_WORKSPACE_EDIT_INTERNAL_H */
