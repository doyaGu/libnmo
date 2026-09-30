/**
 * @file script_edit.c
 * @brief Script edit transaction lifecycle, shared object lookups, and validation.
 */

#include "script_edit_internal.h"

#include "object/nmo_statesave_ids.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_behaviorio_schemas.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void script_edit_tx_destroy(nmo_script_edit_tx_t *tx)
{
    if (tx) {
        if (tx->workspace) {
            nmo_workspace_destroy(tx->workspace);
        }
        if (tx->document) {
            nmo_document_destroy(tx->document);
        }
        free(tx->deferred_destroy_ids);
        free(tx->created_object_ids);
        free(tx->changed_object_ids);
        free(tx->baseline_broken_refs);
        free(tx->removed_io_refs);
    }
    free(tx);
}

static bool script_edit_ref_edge_equals(const nmo_ref_edge_t *lhs,
                                        const nmo_ref_edge_t *rhs)
{
    const char *lhs_field = NULL;
    const char *rhs_field = NULL;

    if (!lhs || !rhs) {
        return lhs == rhs;
    }

    lhs_field = lhs->field_path ? lhs->field_path : "";
    rhs_field = rhs->field_path ? rhs->field_path : "";
    return lhs->from == rhs->from &&
           lhs->to == rhs->to &&
           lhs->kind == rhs->kind &&
           lhs->index == rhs->index &&
           strcmp(lhs_field, rhs_field) == 0;
}

static bool script_edit_broken_ref_set_matches(const nmo_ref_edge_t *current_edges,
                                               size_t current_count,
                                               const nmo_ref_edge_t *baseline_edges,
                                               size_t baseline_count)
{
    bool *matched = NULL;
    bool same = true;

    if (current_count != baseline_count) {
        return false;
    }
    if (current_count == 0u) {
        return true;
    }

    matched = (bool *)calloc(current_count, sizeof(*matched));
    if (!matched) {
        return false;
    }

    for (size_t i = 0; i < baseline_count && same; ++i) {
        bool found = false;
        for (size_t j = 0; j < current_count; ++j) {
            if (matched[j]) {
                continue;
            }
            if (script_edit_ref_edge_equals(&baseline_edges[i], &current_edges[j])) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            same = false;
        }
    }

    free(matched);
    return same;
}

static nmo_status_t script_edit_capture_broken_ref_baseline(nmo_script_edit_tx_t *tx)
{
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->session) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    ref_graph = nmo_workspace_internal_ref_graph(tx->workspace);
    if (!ref_graph) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_count);
    if (rc != NMO_OK && rc != NMO_ERR_VALIDATION_FAILED) {
        return rc;
    }

    if (broken_count == 0u) {
        return NMO_OK;
    }

    tx->baseline_broken_refs = (nmo_ref_edge_t *)calloc(
        broken_count, sizeof(*tx->baseline_broken_refs));
    if (!tx->baseline_broken_refs) {
        return NMO_ERR_NOMEM;
    }
    memcpy(tx->baseline_broken_refs,
           broken_edges,
           broken_count * sizeof(*tx->baseline_broken_refs));
    tx->baseline_broken_ref_count = broken_count;
    return NMO_OK;
}

static bool script_edit_interface_diag_matches(
    const nmo_session_behavior_interface_diagnostics_t *lhs,
    const nmo_session_behavior_interface_diagnostics_t *rhs)
{
    if (!lhs || !rhs) {
        return lhs == rhs;
    }

    return lhs->attempted == rhs->attempted &&
           lhs->available == rhs->available &&
           lhs->status == rhs->status &&
           lhs->attempted_count == rhs->attempted_count &&
           lhs->parsed_count == rhs->parsed_count &&
           lhs->failed_count == rhs->failed_count &&
           lhs->skipped_no_arena_count == rhs->skipped_no_arena_count &&
           lhs->allocation_failure_count == rhs->allocation_failure_count &&
           lhs->first_error_object_id == rhs->first_error_object_id &&
           lhs->first_error_file_id == rhs->first_error_file_id &&
           lhs->first_error_chunk_version == rhs->first_error_chunk_version &&
           lhs->first_error_data_version == rhs->first_error_data_version &&
           lhs->first_error_reader_offset == rhs->first_error_reader_offset &&
           lhs->first_error_chunk_dwords == rhs->first_error_chunk_dwords;
}

static nmo_status_t script_edit_capture_interface_diag_baseline(
    nmo_script_edit_tx_t *tx)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->session) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_workspace_internal_ensure_behavior_acceleration(tx->workspace);
    if (rc != NMO_OK) {
        return rc;
    }

    memset(&tx->baseline_interface_diag, 0, sizeof(tx->baseline_interface_diag));
    nmo_workspace_internal_get_behavior_interface_diagnostics(
        tx->workspace, &tx->baseline_interface_diag);
    return NMO_OK;
}

static void script_edit_note_change(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.changed_objects < SIZE_MAX) {
        tx->report.changed_objects++;
    }
}

static void script_edit_note_error(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.errors < SIZE_MAX) {
        tx->report.errors++;
    }
}

nmo_status_t script_edit_note_changed_id(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    nmo_object_id_t *next_ids = NULL;
    size_t next_capacity = 0u;

    if (!tx || object_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < tx->changed_object_id_count; ++i) {
        if (tx->changed_object_ids[i] == object_id) {
            return NMO_OK;
        }
    }
    if (tx->changed_object_id_count == tx->changed_object_id_capacity) {
        next_capacity = tx->changed_object_id_capacity == 0u
                            ? 8u
                            : tx->changed_object_id_capacity * 2u;
        next_ids = (nmo_object_id_t *)realloc(
            tx->changed_object_ids,
            next_capacity * sizeof(*next_ids));
        if (!next_ids) {
            return NMO_ERR_NOMEM;
        }
        tx->changed_object_ids = next_ids;
        tx->changed_object_id_capacity = next_capacity;
    }

    tx->changed_object_ids[tx->changed_object_id_count++] = object_id;
    tx->report.changed_object_ids = tx->changed_object_ids;
    tx->report.changed_object_id_count = tx->changed_object_id_count;
    return NMO_OK;
}

nmo_status_t script_edit_note_created_id(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    nmo_object_id_t *next_ids = NULL;
    size_t next_capacity = 0u;

    if (!tx || object_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (tx->created_object_id_count == tx->created_object_id_capacity) {
        next_capacity = tx->created_object_id_capacity == 0u
                            ? 8u
                            : tx->created_object_id_capacity * 2u;
        next_ids = (nmo_object_id_t *)realloc(
            tx->created_object_ids,
            next_capacity * sizeof(*next_ids));
        if (!next_ids) {
            return NMO_ERR_NOMEM;
        }
        tx->created_object_ids = next_ids;
        tx->created_object_id_capacity = next_capacity;
    }

    tx->created_object_ids[tx->created_object_id_count++] = object_id;
    tx->report.created_objects = tx->created_object_id_count;
    tx->report.created_object_ids = tx->created_object_ids;
    tx->report.created_object_id_count = tx->created_object_id_count;
    return NMO_OK;
}

static void script_edit_note_delete(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.deleted_objects < SIZE_MAX) {
        tx->report.deleted_objects++;
    }
}

nmo_status_t script_edit_append_deferred_destroy(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    if (!tx || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < tx->deferred_destroy_count; ++i) {
        if (tx->deferred_destroy_ids[i] == object_id) {
            return NMO_OK;
        }
    }

    if (tx->deferred_destroy_count == tx->deferred_destroy_capacity) {
        size_t next_capacity =
            tx->deferred_destroy_capacity == 0u
                ? 8u
                : tx->deferred_destroy_capacity * 2u;
        nmo_object_id_t *next_ids =
            (nmo_object_id_t *)realloc(
                tx->deferred_destroy_ids,
                next_capacity * sizeof(*next_ids));
        if (!next_ids) {
            return NMO_ERR_NOMEM;
        }
        tx->deferred_destroy_ids = next_ids;
        tx->deferred_destroy_capacity = next_capacity;
    }

    tx->deferred_destroy_ids[tx->deferred_destroy_count++] = object_id;
    script_edit_note_delete(tx);
    return NMO_OK;
}

nmo_status_t script_edit_append_removed_io_ref(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_port_kind_t kind,
    size_t removed_index)
{
    script_edit_removed_io_ref_t *next_refs = NULL;
    size_t next_capacity = 0u;

    if (!tx || owner_behavior_id == 0u ||
        (kind != NMO_PORT_IO_IN && kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (tx->removed_io_ref_count == tx->removed_io_ref_capacity) {
        next_capacity = tx->removed_io_ref_capacity == 0u
                            ? 4u
                            : tx->removed_io_ref_capacity * 2u;
        next_refs = (script_edit_removed_io_ref_t *)realloc(
            tx->removed_io_refs,
            next_capacity * sizeof(*next_refs));
        if (!next_refs) {
            return NMO_ERR_NOMEM;
        }
        tx->removed_io_refs = next_refs;
        tx->removed_io_ref_capacity = next_capacity;
    }

    tx->removed_io_refs[tx->removed_io_ref_count++] =
        (script_edit_removed_io_ref_t){
            .owner_behavior_id = owner_behavior_id,
            .kind = kind,
            .removed_index = removed_index,
        };
    return NMO_OK;
}

void *script_edit_get_object_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid)
{
    if (!registry || !object) {
        return NULL;
    }
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == class_id) {
        return nmo_object_get_state(object);
    }
    return nmo_type_query_object_get_ancestor_state_by_guid(
        registry, object, type_guid);
}

void *script_edit_find_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_guid_t type_guid,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;
    void *state = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!registry || !repo || object_id == 0) {
        return NULL;
    }

    object = nmo_object_repository_find_by_id(repo, object_id);
    if (!object) {
        return NULL;
    }
    state = script_edit_get_object_state(
        registry, object, class_id, type_guid);
    if (!state) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return state;
}

nmo_behavior_state_t *script_edit_find_behavior_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    return (nmo_behavior_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        behavior_id,
        NMO_CID_BEHAVIOR,
        CKPGUID_BEHAVIOR,
        out_object);
}

nmo_behavior_state_t *script_edit_find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    nmo_context_t *ctx = session ? nmo_session_get_context(session) : NULL;
    return script_edit_find_behavior_state_in_repo(
        ctx ? nmo_context_get_type_registry(ctx) : NULL,
        session ? nmo_session_get_repository(session) : NULL,
        behavior_id,
        out_object);
}

static nmo_behaviorio_state_t *script_edit_find_io_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t io_id,
    nmo_object_t **out_object)
{
    return (nmo_behaviorio_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        io_id,
        NMO_CID_BEHAVIORIO,
        CKPGUID_BEHAVIORIO,
        out_object);
}

nmo_behaviorlink_state_t *script_edit_find_link_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t link_id,
    nmo_object_t **out_object)
{
    return (nmo_behaviorlink_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        link_id,
        NMO_CID_BEHAVIORLINK,
        CKPGUID_BEHAVIORLINK,
        out_object);
}

nmo_parameterin_state_t *script_edit_find_parameterin_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    return (nmo_parameterin_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        parameter_id,
        NMO_CID_PARAMETERIN,
        CKPGUID_PARAMETERIN,
        out_object);
}

nmo_parameterout_state_t *script_edit_find_parameterout_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    return (nmo_parameterout_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        parameter_id,
        NMO_CID_PARAMETEROUT,
        CKPGUID_PARAMETEROUT,
        out_object);
}

nmo_parameteroperation_state_t *script_edit_find_operation_state_in_repo(
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id,
    nmo_object_t **out_object)
{
    return (nmo_parameteroperation_state_t *)script_edit_find_state_in_repo(
        registry,
        repo,
        operation_id,
        NMO_CID_PARAMETEROPERATION,
        CKPGUID_PARAMETEROPERATION,
        out_object);
}

nmo_class_id_t script_edit_get_parameter_connection_state(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    void **out_state)
{
    if (out_state) {
        *out_state = NULL;
    }
    if (!registry || !object || !out_state) {
        return 0;
    }

    if (nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        if (class_id == NMO_CID_PARAMETERIN ||
            class_id == NMO_CID_PARAMETEROUT ||
            class_id == NMO_CID_PARAMETEROPERATION) {
            *out_state = nmo_object_get_state(object);
            return *out_state != NULL ? class_id : 0;
        }
    }

    *out_state = script_edit_get_object_state(
        registry, object, NMO_CID_PARAMETERIN, CKPGUID_PARAMETERIN);
    if (*out_state) {
        return NMO_CID_PARAMETERIN;
    }
    *out_state = script_edit_get_object_state(
        registry, object, NMO_CID_PARAMETEROUT, CKPGUID_PARAMETEROUT);
    if (*out_state) {
        return NMO_CID_PARAMETEROUT;
    }
    *out_state = script_edit_get_object_state(
        registry,
        object,
        NMO_CID_PARAMETEROPERATION,
        CKPGUID_PARAMETEROPERATION);
    return *out_state != NULL ? NMO_CID_PARAMETEROPERATION : 0;
}

void script_edit_update_behavior_save_flags(
    nmo_behavior_state_t *state)
{
    if (!state) {
        return;
    }

    state->has_save_flags = true;
    if (state->sub_behaviors.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBBEHAV;
    }
    if (state->sub_behavior_links.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
    }
    if (state->operations.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROPERATIONS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROPERATIONS;
    }
    if (state->in_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORINPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORINPARAMS;
    }
    if (state->out_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROUTPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROUTPARAMS;
    }
    if (state->local_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORLOCALPARAMS;
    }
    if (state->inputs.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORINPUTS;
    }
    if (state->outputs.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROUTPUTS;
    }
}

nmo_status_t script_edit_create_runtime_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_object_id)
{
    nmo_object_id_t object_id = 0;

    if (!tx || !tx->session || !tx->edit || !out_object_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_object_create_desc_t desc = {
        .class_id = class_id,
        .name = name,
        .type_guid = type_guid,
    };
    nmo_status_t rc = nmo_object_edit_create(tx->edit, &desc, &object_id);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_note_created_id(tx, object_id);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_object_id = object_id;
    return NMO_OK;
}

nmo_status_t script_edit_create_io_object(
    nmo_script_edit_tx_t *tx,
    const char *name,
    nmo_script_edit_io_kind_t kind,
    nmo_object_id_t *out_io_id)
{
    nmo_behaviorio_state_t *state = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, NMO_CID_BEHAVIORIO, name, NMO_GUID_NULL, out_io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    state = script_edit_find_io_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        *out_io_id,
        NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    state->old_flags = (kind == NMO_SCRIPT_EDIT_IO_INPUT)
        ? NMO_BEHAVIORIO_OLD_IN
        : NMO_BEHAVIORIO_OLD_OUT;
    state->has_flags = true;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t script_edit_require_behavior_index(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_index_t **out_index)
{
    nmo_status_t rc = NMO_OK;
    const nmo_behavior_index_t *index = NULL;

    if (!tx || !out_index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if ((tx->workspace_edit_flags &
         (NMO_WORKSPACE_EDIT_REFERENCES |
          NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
          NMO_WORKSPACE_EDIT_NAMES |
          NMO_WORKSPACE_EDIT_RESOURCES)) != 0u) {
        rc = nmo_workspace_apply_edit_flags(
            tx->workspace,
            tx->workspace_edit_flags &
                (NMO_WORKSPACE_EDIT_REFERENCES |
                 NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                 NMO_WORKSPACE_EDIT_NAMES |
                 NMO_WORKSPACE_EDIT_RESOURCES));
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_workspace_internal_ensure_behavior_acceleration(tx->workspace);
    if (rc != NMO_OK) {
        return rc;
    }

    index = nmo_workspace_internal_behavior_index(tx->workspace);
    if (!index) {
        return NMO_ERR_INVALID_STATE;
    }
    *out_index = index;
    return NMO_OK;
}

bool script_edit_is_pending_destroy(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    if (!tx || object_id == 0u) {
        return false;
    }

    for (size_t i = 0; i < tx->deferred_destroy_count; ++i) {
        if (tx->deferred_destroy_ids[i] == object_id) {
            return true;
        }
    }
    return false;
}

bool script_edit_behavior_is_direct_graph_member(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id)
{
    nmo_behavior_state_t *parent = NULL;

    if (!session || parent_behavior_id == 0 || behavior_id == 0) {
        return false;
    }
    if (behavior_id == parent_behavior_id) {
        return true;
    }

    parent = script_edit_find_behavior_state(session, parent_behavior_id, NULL);
    if (!parent) {
        return false;
    }
    return nmo_behavior_ref_array_find(
        &parent->sub_behaviors, behavior_id, NULL);
}

bool script_edit_behavior_is_graph_member(
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    nmo_object_id_t behavior_id)
{
    nmo_behavior_state_t *root = NULL;

    if (!session || root_behavior_id == 0u || behavior_id == 0u) {
        return false;
    }
    if (root_behavior_id == behavior_id) {
        return true;
    }

    root = script_edit_find_behavior_state(session, root_behavior_id, NULL);
    if (!root || root->sub_behaviors.count == 0u) {
        return false;
    }

    for (size_t i = 0; i < root->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_behavior_id = nmo_behavior_ref_array_get_id(
            &root->sub_behaviors, i);
        if (sub_behavior_id == 0) continue;
        if (sub_behavior_id == behavior_id ||
            script_edit_behavior_is_graph_member(session,
                                                 sub_behavior_id,
                                                 behavior_id)) {
            return true;
        }
    }

    return false;
}

bool script_edit_find_direct_parent_behavior(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    size_t object_count = 0;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!session || behavior_id == 0u) {
        return false;
    }
    repo = nmo_session_get_repository(session);
    registry = nmo_context_get_type_registry(nmo_session_get_context(session));
    if (!repo || !registry) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *state = NULL;
        nmo_object_id_t parent_id = 0u;

        state = (nmo_behavior_state_t *)script_edit_get_object_state(
            registry,
            object,
            NMO_CID_BEHAVIOR,
            CKPGUID_BEHAVIOR);
        if (!state || state->sub_behaviors.count == 0u) {
            continue;
        }

        parent_id = nmo_object_get_id(object);
        if (nmo_behavior_ref_array_find(
                &state->sub_behaviors, behavior_id, NULL)) {
            if (out_parent_behavior_id) {
                *out_parent_behavior_id = parent_id;
            }
            return true;
        }
    }

    return false;
}

bool script_edit_find_parent_graph_io_owner(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!session || parent_behavior_id == 0 || io_id == 0) {
        return false;
    }

    owner = index ? nmo_behavior_index_find(index, io_id) : NULL;
    if (!owner ||
        (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT) ||
        !script_edit_behavior_is_direct_graph_member(session,
                                                     parent_behavior_id,
                                                     owner->owner_id)) {
        return false;
    }

    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static nmo_status_t validate_behavior_link_owners(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    const nmo_type_registry_t *registry = NULL;
    size_t object_count = 0;

    if (!tx || !repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_behaviorlink_state_t *state = NULL;

        if (!object) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_behaviorlink_state_t *)script_edit_get_object_state(
            registry,
            object,
            NMO_CID_BEHAVIORLINK,
            CKPGUID_BEHAVIORLINK);
        if (!state) {
            nmo_guid_t type_guid = nmo_object_get_type_guid(object);
            if ((nmo_guid_is_null(type_guid) &&
                 nmo_object_get_class_id(object) == NMO_CID_BEHAVIORLINK) ||
                (!nmo_guid_is_null(type_guid) &&
                 nmo_type_query_object_is_derived_from_guid(
                     registry, object, CKPGUID_BEHAVIORLINK))) {
                return NMO_ERR_INVALID_STATE;
            }
            continue;
        }
        if (!nmo_behavior_index_find(index, nmo_object_get_id(object)) ||
            !nmo_behavior_index_find(
                index, nmo_behaviorlink_in_io_id(state)) ||
            !nmo_behavior_index_find(
                index, nmo_behaviorlink_out_io_id(state))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    return NMO_OK;
}

static nmo_status_t validate_parameter_links(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    const nmo_type_registry_t *registry = NULL;
    size_t object_count = 0;

    if (!tx || !repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        void *connection_state = NULL;

        if (!object) {
            continue;
        }

        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }
        switch (script_edit_get_parameter_connection_state(
            registry, object, &connection_state)) {
        case NMO_CID_PARAMETERIN: {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)connection_state;
            nmo_object_t *source = NULL;
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(state);
            if (source_id != 0) {
                source = nmo_object_repository_find_by_id(repo, source_id);
                if (!source) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
                if (state->is_shared) {
                    if (!script_edit_get_object_state(
                            registry,
                            source,
                            NMO_CID_PARAMETERIN,
                            CKPGUID_PARAMETERIN)) {
                        return NMO_ERR_VALIDATION_FAILED;
                    }
                } else if (!script_edit_is_parameter_reference_object(
                               registry, source)) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
            break;
        }
        case NMO_CID_PARAMETEROUT: {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)connection_state;
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                nmo_object_id_t destination_id =
                    nmo_parameterout_destination_id(state, j);
                nmo_object_t *destination = NULL;
                if (destination_id == 0) {
                    continue;
                }
                destination = nmo_object_repository_find_by_id(repo, destination_id);
                if (!destination ||
                    !script_edit_is_parameter_reference_object(
                        registry, destination)) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    return NMO_OK;
}

static nmo_status_t validate_parameter_operations(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    const nmo_type_registry_t *registry = NULL;
    size_t object_count = 0;

    if (!tx || !repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    registry = nmo_workspace_internal_type_registry(tx->workspace);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_parameteroperation_state_t *state = NULL;

        if (!object) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_parameteroperation_state_t *)
            script_edit_get_object_state(
                registry,
                object,
                NMO_CID_PARAMETEROPERATION,
                CKPGUID_PARAMETEROPERATION);
        if (!state) {
            continue;
        }
        if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (state->has_in1 && state->in1.ref.state == NMO_REF_RESOLVED) {
            nmo_object_t *param = NULL;
            const nmo_object_id_t parameter_id =
                nmo_parameteroperation_in1_id(state);
            if (parameter_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, parameter_id);
            if (!param || !script_edit_is_parameter_reference_object(
                              registry, param)) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_in2 && state->in2.ref.state == NMO_REF_RESOLVED) {
            nmo_object_t *param = NULL;
            const nmo_object_id_t parameter_id =
                nmo_parameteroperation_in2_id(state);
            if (parameter_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, parameter_id);
            if (!param || !script_edit_is_parameter_reference_object(
                              registry, param)) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_out && state->out.ref.state == NMO_REF_RESOLVED) {
            nmo_object_t *param = NULL;
            const nmo_object_id_t parameter_id =
                nmo_parameteroperation_out_id(state);
            if (parameter_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, parameter_id);
            if (!param || !script_edit_is_parameter_reference_object(
                              registry, param)) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_begin(nmo_workspace_t *workspace,
                                           const char *label,
                                           nmo_script_edit_tx_t **out_tx)
{
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_session_t *seed_session = NULL;
    nmo_context_t *ctx = NULL;

    if (!workspace || !out_tx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_tx = NULL;
    tx = (nmo_script_edit_tx_t *)calloc(1u, sizeof(*tx));
    if (!tx) {
        return NMO_ERR_NOMEM;
    }

    ctx = nmo_workspace_internal_context(workspace);
    if (!ctx) {
        script_edit_tx_destroy(tx);
        return NMO_ERR_INVALID_STATE;
    }

    tx->ctx = ctx;
    seed_session = nmo_workspace_internal_session(workspace);
    if (!seed_session) {
        script_edit_tx_destroy(tx);
        return NMO_ERR_INVALID_STATE;
    }
    rc = nmo_session_borrow_document(seed_session, &tx->document);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }
    tx->session = seed_session;
    rc = nmo_workspace_create(ctx, tx->document, &tx->workspace);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }
    rc = nmo_workspace_edit_begin(tx->workspace, label, &tx->edit);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }
    rc = script_edit_capture_broken_ref_baseline(tx);
    if (rc != NMO_OK) {
        if (tx->edit) {
            nmo_workspace_edit_rollback(tx->edit);
            tx->edit = NULL;
        }
        script_edit_tx_destroy(tx);
        return rc;
    }
    rc = script_edit_capture_interface_diag_baseline(tx);
    if (rc != NMO_OK) {
        if (tx->edit) {
            nmo_workspace_edit_rollback(tx->edit);
            tx->edit = NULL;
        }
        script_edit_tx_destroy(tx);
        return rc;
    }

    *out_tx = tx;
    return NMO_OK;
}

NMO_API nmo_workspace_edit_t *nmo_script_edit_workspace_edit(
    nmo_script_edit_tx_t *tx)
{
    if (!tx || tx->finished) {
        return NULL;
    }
    return tx->edit;
}

NMO_API nmo_workspace_t *nmo_script_edit_workspace(
    nmo_script_edit_tx_t *tx)
{
    if (!tx || tx->finished) {
        return NULL;
    }
    return tx->workspace;
}

NMO_API nmo_status_t nmo_script_edit_defer_destroy_objects(
    nmo_script_edit_tx_t *tx,
    const nmo_object_id_t *object_ids,
    size_t object_count)
{
    if (!tx || tx->finished || (object_count > 0u && !object_ids)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < object_count; ++i) {
        nmo_status_t rc =
            script_edit_append_deferred_destroy(tx, object_ids[i]);
        if (rc != NMO_OK) {
            return rc;
        }
    }
    if (object_count > 0u) {
        nmo_script_edit_mark(
            tx,
            NMO_WORKSPACE_EDIT_OBJECT_STATE |
                NMO_WORKSPACE_EDIT_REFERENCES |
                NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    }
    return NMO_OK;
}

NMO_API void nmo_script_edit_mark(nmo_script_edit_tx_t *tx,
                                  uint32_t workspace_edit_flags)
{
    if (!tx || tx->finished) {
        return;
    }

    tx->workspace_edit_flags |= workspace_edit_flags;
    if (tx->edit) {
        nmo_workspace_edit_mark(tx->edit, workspace_edit_flags);
    }
    if (workspace_edit_flags != 0u) {
        script_edit_note_change(tx);
    }
}

NMO_API const nmo_script_edit_report_t *nmo_script_edit_report(
    const nmo_script_edit_tx_t *tx)
{
    return tx ? &tx->report : NULL;
}

NMO_API nmo_status_t nmo_script_edit_validate(nmo_script_edit_tx_t *tx,
                                              uint32_t validation_flags)
{
    static const uint32_t conservative_refresh_flags =
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
        NMO_WORKSPACE_EDIT_REFERENCES |
        NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
        NMO_WORKSPACE_EDIT_NAMES |
        NMO_WORKSPACE_EDIT_RESOURCES;
    nmo_object_repository_t *repo = NULL;
    nmo_behavior_index_t *index = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_session_behavior_interface_diagnostics_t interface_diag;
    nmo_script_edit_report_t edit_report = {0};
    size_t broken_ref_count = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->session || !tx->ctx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    edit_report = tx->report;
    memset(&tx->report, 0, sizeof(tx->report));
    tx->report.created_objects = edit_report.created_objects;
    tx->report.created_object_ids = edit_report.created_object_ids;
    tx->report.created_object_id_count = edit_report.created_object_id_count;
    tx->report.deleted_objects = edit_report.deleted_objects;
    tx->report.changed_objects = edit_report.changed_objects;
    tx->report.changed_object_ids = edit_report.changed_object_ids;
    tx->report.changed_object_id_count = edit_report.changed_object_id_count;
    tx->report.moved_links = edit_report.moved_links;
    tx->report.rewired_parameters = edit_report.rewired_parameters;
    tx->report.interface_changes = edit_report.interface_changes;
    repo = nmo_workspace_internal_repository(tx->workspace);
    if (!repo) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    /* The low-level session edit API tracks its own flags internally, but it
     * does not expose them. Validation therefore uses a conservative cache
     * refresh so mixed direct and helper mutations are checked consistently.
     */
    rc = nmo_workspace_apply_edit_flags(tx->workspace,
                                      tx->workspace_edit_flags |
                                          conservative_refresh_flags);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u &&
        nmo_document_internal_is_partial_load(tx->document)) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_internal_ensure_behavior_acceleration(tx->workspace);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        nmo_ref_edge_t *broken_edges = NULL;
        ref_graph = nmo_workspace_internal_ref_graph(tx->workspace);
        if (!ref_graph) {
            script_edit_note_error(tx);
            return NMO_ERR_INVALID_STATE;
        }
        rc = nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_ref_count);
        if (rc != NMO_OK || broken_ref_count != 0u) {
            bool inherited_broken_refs =
                (rc == NMO_ERR_VALIDATION_FAILED || broken_ref_count != 0u) &&
                script_edit_broken_ref_set_matches(
                    broken_edges,
                    broken_ref_count,
                    tx->baseline_broken_refs,
                    tx->baseline_broken_ref_count);
            if (!inherited_broken_refs) {
                script_edit_note_error(tx);
                return rc != NMO_OK ? rc : NMO_ERR_VALIDATION_FAILED;
            }
        }
    }

    if ((validation_flags &
         (NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
          NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX)) != 0u) {
        index = nmo_workspace_internal_behavior_index(tx->workspace);
        if (!index) {
            script_edit_note_error(tx);
            return NMO_ERR_INVALID_STATE;
        }

        rc = validate_behavior_link_owners(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }

        rc = validate_parameter_links(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }

        rc = validate_parameter_operations(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        memset(&interface_diag, 0, sizeof(interface_diag));
        nmo_workspace_internal_get_behavior_interface_diagnostics(
            tx->workspace, &interface_diag);
        if (interface_diag.attempted && interface_diag.status != NMO_OK) {
            if (script_edit_interface_diag_matches(&interface_diag,
                                                   &tx->baseline_interface_diag)) {
                return NMO_OK;
            }
            script_edit_note_error(tx);
            return interface_diag.status;
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->edit) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (tx->deferred_destroy_count > 0u) {
        rc = nmo_workspace_edit_defer_destroy_objects(
            tx->edit,
            tx->deferred_destroy_ids,
            tx->deferred_destroy_count,
            NMO_RUNTIME_REQUEST_STRICT |
                NMO_RUNTIME_REQUEST_SAFE_DETACH);
        if (rc != NMO_OK) {
            nmo_workspace_edit_rollback(tx->edit);
            tx->edit = NULL;
            tx->finished = true;
            script_edit_tx_destroy(tx);
            return rc;
        }
    }

    rc = nmo_workspace_edit_commit(tx->edit);
    tx->edit = NULL;
    tx->finished = true;
    script_edit_tx_destroy(tx);
    return rc;
}

NMO_API void nmo_script_edit_rollback(nmo_script_edit_tx_t *tx)
{
    if (!tx) {
        return;
    }
    if (!tx->finished && tx->edit) {
        nmo_workspace_edit_rollback(tx->edit);
        tx->edit = NULL;
        tx->finished = true;
    }
    script_edit_tx_destroy(tx);
}
