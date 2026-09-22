/**
 * @file script_edit_link.c
 * @brief Script edit primitives for behavior links.
 */

#include "script_edit_internal.h"

#include "behavior/nmo_behavior_edit.h"

static bool script_edit_io_can_source_control(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id)
{
    const nmo_port_owner_t *owner = NULL;

    if (!script_edit_find_parent_graph_io_owner(session, index,
                                                parent_behavior_id, io_id,
                                                &owner)) {
        return false;
    }

    if (owner->owner_id == parent_behavior_id) {
        return owner->kind == NMO_PORT_IO_IN;
    }
    return owner->kind == NMO_PORT_IO_OUT;
}

static bool script_edit_io_can_target_control(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id)
{
    const nmo_port_owner_t *owner = NULL;

    if (!script_edit_find_parent_graph_io_owner(session, index,
                                                parent_behavior_id, io_id,
                                                &owner)) {
        return false;
    }

    if (owner->owner_id == parent_behavior_id) {
        return owner->kind == NMO_PORT_IO_OUT;
    }
    return owner->kind == NMO_PORT_IO_IN;
}

NMO_API nmo_status_t nmo_script_edit_add_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay,
    nmo_object_id_t *out_link_id)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t link_id = 0;
    nmo_status_t rc = NMO_OK;

    if (out_link_id) {
        *out_link_id = 0;
    }
    if (!tx || !tx->edit || parent_behavior_id == 0 ||
        from_io_id == 0 || to_io_id == 0 ||
        activation_delay > (uint32_t)INT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!script_edit_find_behavior_state_in_repo(
            nmo_workspace_internal_type_registry(tx->workspace),
            nmo_workspace_internal_repository(tx->workspace),
            parent_behavior_id,
            NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_io_can_source_control(tx->session, index,
                                           parent_behavior_id, from_io_id) ||
        !script_edit_io_can_target_control(tx->session, index,
                                           parent_behavior_id, to_io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    rc = nmo_behavior_edit_add_link(
        tx->edit,
        parent_behavior_id,
        to_io_id,
        from_io_id,
        (int16_t)activation_delay,
        &link_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, parent_behavior_id);
    rc = script_edit_note_created_id(tx, link_id);
    if (rc != NMO_OK) {
        return rc;
    }
    if (out_link_id) {
        *out_link_id = link_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rewire_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behaviorlink_state_t *link_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || link_id == 0 ||
        (from_io_id == 0 && to_io_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, link_id);
    if (!owner || owner->kind != NMO_PORT_SUB_LINK) {
        return NMO_ERR_NOT_FOUND;
    }
    if ((from_io_id != 0 &&
         !script_edit_io_can_source_control(tx->session, index,
                                            owner->owner_id, from_io_id)) ||
        (to_io_id != 0 &&
         !script_edit_io_can_target_control(tx->session, index,
                                            owner->owner_id, to_io_id))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    link_state = script_edit_find_link_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        link_id,
        NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
     * and link out_io_id is the target IO.
     */
    if (from_io_id != 0) {
        nmo_behaviorlink_set_in_io_id(link_state, from_io_id);
    }
    if (to_io_id != 0) {
        nmo_behaviorlink_set_out_io_id(link_state, to_io_id);
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    tx->report.moved_links++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_behavior_link_delay(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    uint32_t activation_delay)
{
    nmo_behaviorlink_state_t *link_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || link_id == 0 ||
        activation_delay > (uint32_t)INT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    link_state = script_edit_find_link_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        link_id,
        NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    link_state->activation_delay = (int16_t)activation_delay;
    link_state->initial_activation_delay = (int16_t)activation_delay;
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    nmo_behavior_state_t *parent = NULL;
    size_t link_index = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parent_behavior_id == 0 || link_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    if (!parent) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!nmo_behavior_ref_array_find(
            &parent->sub_behavior_links, link_id, &link_index)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, parent);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_array_remove(&parent->sub_behavior_links, link_index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent);

    rc = script_edit_append_deferred_destroy(tx, link_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}
