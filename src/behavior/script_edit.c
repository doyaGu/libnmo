#include "behavior/nmo_script_edit.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_registry.h"
#include "../runtime/runtime_internal.h"
#include "runtime/nmo_workspace.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_value_writer.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"

#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

static nmo_status_t script_edit_note_changed_id(
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

static nmo_status_t script_edit_note_created_id(
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

static nmo_status_t script_edit_append_deferred_destroy(
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
    return NMO_OK;
}

static nmo_status_t script_edit_append_removed_io_ref(
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

static nmo_status_t script_edit_track_created(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_workspace_edit_track_created_object(tx->edit, object_id);
    if (rc == NMO_OK) {
        rc = script_edit_note_created_id(tx, object_id);
    }
    return rc;
}

static nmo_behavior_state_t *script_edit_find_behavior_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || behavior_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_behavior_state_t *script_edit_find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    return script_edit_find_behavior_state_in_repo(
        session ? nmo_session_get_repository(session) : NULL,
        behavior_id,
        out_object);
}

static nmo_behaviorio_state_t *script_edit_find_io_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t io_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || io_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, io_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORIO) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorio_state_t *)nmo_object_get_state(object);
}

static nmo_behaviorlink_state_t *script_edit_find_link_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t link_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || link_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, link_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorlink_state_t *)nmo_object_get_state(object);
}

static nmo_parameterin_state_t *script_edit_find_parameterin_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || parameter_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETERIN) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameterin_state_t *)nmo_object_get_state(object);
}

static nmo_parameterout_state_t *script_edit_find_parameterout_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || parameter_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROUT) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameterout_state_t *)nmo_object_get_state(object);
}

static nmo_parameteroperation_state_t *script_edit_find_operation_state_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id,
    nmo_object_t **out_object)
{
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!repo || operation_id == 0) {
        return NULL;
    }

    object = repo ? nmo_object_repository_find_by_id(repo, operation_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameteroperation_state_t *)nmo_object_get_state(object);
}

static void script_edit_update_behavior_save_flags(
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

static nmo_status_t script_edit_create_runtime_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_object_id)
{
    nmo_runtime_request_t request;
    nmo_runtime_report_t report;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t object_id = 0;

    if (!tx || !tx->session || !out_object_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(&request, 0, sizeof(request));
    memset(&report, 0, sizeof(report));
    request.kind = NMO_RUNTIME_OP_CREATE;
    request.payload.create.class_id = class_id;
    request.payload.create.name = name;
    request.payload.create.type_guid = type_guid;
    request.payload.create.out_created_id = &object_id;

    rc = nmo_workspace_internal_execute_runtime_request(
        tx->workspace,
        &request,
        &report);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_track_created(tx, object_id);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_object_id = object_id;
    return NMO_OK;
}

static nmo_status_t script_edit_create_io_object(
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

static bool script_edit_is_symbolic_manager_default_type(nmo_guid_t type_guid)
{
    return nmo_guid_equals(type_guid, CKPGUID_MESSAGE) ||
           nmo_guid_equals(type_guid, CKPGUID_ATTRIBUTE);
}

static nmo_status_t script_edit_find_message_manager_value(
    nmo_script_edit_tx_t *tx,
    const char *name,
    uint32_t *out_value)
{
    const nmo_file_state_t *file_state = NULL;
    if (!tx || !tx->session || !name || !out_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    file_state = nmo_session_get_file_state(tx->session);
    if (!file_state || !file_state->manager_data) {
        return NMO_ERR_NOT_FOUND;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            !manager->chunk) {
            continue;
        }

        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(tx->session));
        if (!chunk) {
            return NMO_ERR_NOMEM;
        }
        if (nmo_chunk_start_read(chunk) != NMO_OK ||
            nmo_chunk_seek_identifier(chunk, 0x53u) != NMO_OK) {
            continue;
        }

        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count) != NMO_OK || count < 0) {
            continue;
        }
        for (int32_t index = 0; index < count; ++index) {
            char *entry_name = NULL;
            (void)nmo_chunk_read_string(chunk, &entry_name);
            if (entry_name && strcmp(entry_name, name) == 0) {
                *out_value = (uint32_t)index;
                return NMO_OK;
            }
        }
    }

    return NMO_ERR_NOT_FOUND;
}

static nmo_status_t script_edit_find_attribute_manager_value(
    nmo_script_edit_tx_t *tx,
    const char *name,
    uint32_t *out_value)
{
    const nmo_file_state_t *file_state = NULL;
    if (!tx || !tx->session || !name || !out_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    file_state = nmo_session_get_file_state(tx->session);
    if (!file_state || !file_state->manager_data) {
        return NMO_ERR_NOT_FOUND;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE) ||
            !manager->chunk) {
            continue;
        }

        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(tx->session));
        if (!chunk) {
            return NMO_ERR_NOMEM;
        }
        if (nmo_chunk_start_read(chunk) != NMO_OK ||
            nmo_chunk_seek_identifier(chunk, 0x52u) != NMO_OK) {
            continue;
        }

        int32_t category_count = 0;
        int32_t attribute_count = 0;
        if (nmo_chunk_read_int(chunk, &category_count) != NMO_OK ||
            nmo_chunk_read_int(chunk, &attribute_count) != NMO_OK ||
            category_count < 0 || attribute_count < 0) {
            continue;
        }

        for (int32_t category = 0; category < category_count; ++category) {
            int32_t present = 0;
            if (nmo_chunk_read_int(chunk, &present) != NMO_OK) {
                return NMO_ERR_INVALID_STATE;
            }
            if (present) {
                char *category_name = NULL;
                uint32_t flags = 0;
                (void)nmo_chunk_read_string(chunk, &category_name);
                if (nmo_chunk_read_dword(chunk, &flags) != NMO_OK) {
                    return NMO_ERR_INVALID_STATE;
                }
            }
        }

        for (int32_t attr = 0; attr < attribute_count; ++attr) {
            int32_t present = 0;
            if (nmo_chunk_read_int(chunk, &present) != NMO_OK) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!present) {
                continue;
            }

            char *attr_name = NULL;
            nmo_guid_t parameter_type_guid = NMO_GUID_NULL;
            int32_t category_index = 0;
            int32_t compatible_class_id = 0;
            uint32_t flags = 0;
            (void)nmo_chunk_read_string(chunk, &attr_name);
            if (nmo_chunk_read_guid(chunk, &parameter_type_guid) != NMO_OK ||
                nmo_chunk_read_int(chunk, &category_index) != NMO_OK ||
                nmo_chunk_read_int(chunk, &compatible_class_id) != NMO_OK ||
                nmo_chunk_read_dword(chunk, &flags) != NMO_OK) {
                return NMO_ERR_INVALID_STATE;
            }
            if (attr_name && strcmp(attr_name, name) == 0) {
                *out_value = (uint32_t)attr;
                return NMO_OK;
            }
        }
    }

    return NMO_ERR_NOT_FOUND;
}

static nmo_status_t script_edit_resolve_symbolic_manager_default(
    nmo_script_edit_tx_t *tx,
    nmo_guid_t type_guid,
    const char *default_value,
    nmo_guid_t *out_manager_guid,
    uint32_t *out_manager_value)
{
    if (!default_value || default_value[0] == '\0' ||
        !out_manager_guid || !out_manager_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_guid_equals(type_guid, CKPGUID_MESSAGE)) {
        *out_manager_guid = NMO_MANAGER_GUID_MESSAGE;
        return script_edit_find_message_manager_value(
            tx, default_value, out_manager_value);
    }
    if (nmo_guid_equals(type_guid, CKPGUID_ATTRIBUTE)) {
        *out_manager_guid = NMO_MANAGER_GUID_ATTRIBUTE;
        return script_edit_find_attribute_manager_value(
            tx, default_value, out_manager_value);
    }

    return NMO_ERR_INVALID_ARGUMENT;
}

static nmo_status_t script_edit_apply_symbolic_manager_default(
    nmo_script_edit_tx_t *tx,
    nmo_object_t *parameter_obj,
    nmo_guid_t type_guid,
    const char *default_value,
    const nmo_manager_entry_options_t *manager_entry)
{
    nmo_parameter_state_t *state = parameter_obj
        ? nmo_parameter_get_mutable_state(parameter_obj)
        : NULL;
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_guid_t manager_guid = NMO_GUID_NULL;
    uint32_t manager_value = 0u;
    nmo_manager_entry_options_t effective =
        manager_entry != NULL ? *manager_entry
                              : nmo_manager_entry_options_default();
    if (effective.schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE ||
        (!nmo_guid_is_null(effective.manager_guid) &&
         !nmo_guid_equals(effective.manager_guid, NMO_MANAGER_GUID_MESSAGE))) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    const char *entry_key =
        effective.key != NULL && effective.key[0] != '\0'
            ? effective.key
            : default_value;
    nmo_status_t rc = script_edit_resolve_symbolic_manager_default(
        tx, type_guid, entry_key, &manager_guid, &manager_value);
    if (rc != NMO_OK) {
        if (effective.policy != NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING ||
            !nmo_guid_equals(type_guid, CKPGUID_MESSAGE)) {
            return rc;
        }
        manager_guid = NMO_MANAGER_GUID_MESSAGE;
        rc = nmo_object_edit_ensure_message_manager_entry(
            tx->edit, entry_key, &manager_value);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(script_edit_note_changed_id(
            tx, NMO_OBJECT_ID_INVALID));
    }

    nmo_array_dispose(&state->buffer_data);
    state->mode = CKPARAM_MODE_MANAGER;
    state->manager_guid = manager_guid;
    state->manager_value = manager_value;
    state->has_state = true;
    return NMO_OK;
}

static nmo_status_t script_edit_create_parameter_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_guid_t type_guid,
    const char *default_value,
    const nmo_manager_entry_options_t *manager_entry,
    nmo_object_id_t *out_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *parameter = NULL;
    nmo_parameterin_state_t *input_state = NULL;
    const nmo_type_descriptor_t *type_desc = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, class_id, name, NMO_GUID_NULL, out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, *out_parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_INVALID_STATE;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }
    type_desc = nmo_type_registry_find_by_guid(registry, type_guid);
    if (!type_desc) {
        return NMO_ERR_NOT_FOUND;
    }

    if (class_id == NMO_CID_PARAMETERIN) {
        input_state = (nmo_parameterin_state_t *)nmo_object_get_state(object);
        if (!input_state) {
            return NMO_ERR_INVALID_STATE;
        }
        input_state->type_guid = type_guid;
        input_state->owner_id = owner_id;
        input_state->source_id = 0u;
        input_state->is_shared = 0u;
        input_state->is_disabled = 0u;
        if (default_value != NULL) {
            nmo_object_id_t source_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETER, owner_id, name, type_guid, NULL,
                manager_entry,
                &source_id);
            if (rc != NMO_OK) {
                return rc;
            }
            if (default_value[0] != '\0') {
                rc = nmo_value_writer_set_parameter_value(
                    tx->edit, source_id, default_value, NULL);
                if (rc != NMO_OK) {
                    if (!script_edit_is_symbolic_manager_default_type(type_guid)) {
                        return rc;
                    }
                    nmo_object_t *source_obj =
                        nmo_object_repository_find_by_id(repo, source_id);
                    rc = script_edit_apply_symbolic_manager_default(
                        tx, source_obj, type_guid, default_value,
                        manager_entry);
                    if (rc != NMO_OK) {
                        return rc;
                    }
                }
            }
            input_state->source_id = source_id;
            input_state->is_shared = 0u;
        }
    } else {
        parameter = nmo_parameter_get_mutable_state(object);
        if (!parameter) {
            return NMO_ERR_INVALID_STATE;
        }

        parameter->type_guid = type_guid;
        parameter->has_state = true;
        parameter->manager_guid = NMO_GUID_NULL;
        parameter->manager_value = 0;
        parameter->subchunk = NULL;
        parameter->object_id = 0;
        nmo_array_dispose(&parameter->buffer_data);

        if ((type_desc->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0u) {
            parameter->mode = CKPARAM_MODE_OBJECT;
        } else {
            size_t buffer_size = type_desc->size;
            if (buffer_size == 0u && nmo_guid_equals(type_guid, CKPGUID_STRING)) {
                buffer_size = 1u;
            }
            if (buffer_size == 0u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            parameter->mode = CKPARAM_MODE_BUFFER;
            rc = nmo_array_alloc(&parameter->buffer_data, sizeof(uint8_t),
                                 buffer_size, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
            memset(parameter->buffer_data.data, 0, buffer_size);
        }
    }

    if (class_id == NMO_CID_PARAMETEROUT) {
        nmo_parameterout_state_t *state =
            (nmo_parameterout_state_t *)nmo_object_get_state(object);
        state->owner_id = owner_id;
        state->destination_ids = NULL;
        state->destination_count = 0u;
    } else if (class_id == NMO_CID_PARAMETERLOCAL) {
        nmo_parameterlocal_state_t *state =
            (nmo_parameterlocal_state_t *)nmo_object_get_state(object);
        state->owner_id = owner_id;
        state->is_myself = 0u;
        state->is_setting = 0u;
    }

    if (class_id != NMO_CID_PARAMETERIN &&
        default_value && default_value[0] != '\0') {
        rc = nmo_value_writer_set_parameter_value(
            tx->edit, *out_parameter_id, default_value, NULL);
        if (rc != NMO_OK) {
            if (!script_edit_is_symbolic_manager_default_type(type_guid)) {
                return rc;
            }
            rc = script_edit_apply_symbolic_manager_default(
                tx, object, type_guid, default_value,
                manager_entry);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_status_t script_edit_require_behavior_index(
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

static bool script_edit_is_pending_destroy(
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

static bool script_edit_io_is_linked_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    nmo_object_id_t io_id)
{
    nmo_behavior_state_t *state = NULL;

    state = script_edit_find_behavior_state_in_repo(repo, behavior_id, NULL);
    if (!state || !state->sub_behavior_links.data) {
        return false;
    }

    for (size_t i = 0; i < state->sub_behavior_links.count; ++i) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
            &state->sub_behavior_links, i);
        if (link_id == 0) continue;
        nmo_object_t *link_obj =
            repo ? nmo_object_repository_find_by_id(repo, link_id) : NULL;
        const nmo_behaviorlink_state_t *link_state =
            link_obj ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                     : NULL;
        if (!link_state) {
            continue;
        }
        if (nmo_behaviorlink_in_io_id(link_state) == io_id ||
            nmo_behaviorlink_out_io_id(link_state) == io_id) {
            return true;
        }
    }
    return false;
}

static bool script_edit_behavior_is_graph_member(
    nmo_session_t *session,
    nmo_object_id_t root_behavior_id,
    nmo_object_id_t behavior_id);

static nmo_status_t script_edit_remove_links_for_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_object_id_t deleted_root_id,
    nmo_object_id_t io_id)
{
    nmo_object_repository_t *repo = NULL;
    typedef struct matched_link {
        nmo_object_id_t parent_behavior_id;
        nmo_object_id_t link_id;
    } matched_link_t;

    matched_link_t *matched_links = NULL;
    size_t matched_count = 0u;
    size_t matched_capacity = 0u;
    size_t object_count = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->workspace || behavior_id == 0u || io_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    (void)behavior_id;

    repo = nmo_workspace_internal_repository(tx->workspace);
    if (repo == NULL) {
        return NMO_OK;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *behavior_state = behavior_obj &&
                nmo_object_get_class_id(behavior_obj) == NMO_CID_BEHAVIOR
            ? (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj)
            : NULL;
        if (behavior_state == NULL ||
            behavior_state->sub_behavior_links.data == NULL) {
            continue;
        }

        for (size_t j = 0; j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &behavior_state->sub_behavior_links, j);
            if (link_id == 0) continue;
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link_state = link_obj
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                : NULL;
            if (!link_state ||
                (nmo_behaviorlink_in_io_id(link_state) != io_id &&
                 nmo_behaviorlink_out_io_id(link_state) != io_id)) {
                continue;
            }
            if (deleted_root_id != 0u &&
                script_edit_behavior_is_graph_member(
                    tx->session,
                    deleted_root_id,
                    nmo_object_get_id(behavior_obj))) {
                continue;
            }

            if (matched_count == matched_capacity) {
                size_t next_capacity =
                    matched_capacity == 0u ? 4u : matched_capacity * 2u;
                matched_link_t *next = (matched_link_t *)realloc(
                    matched_links, next_capacity * sizeof(*next));
                if (next == NULL) {
                    free(matched_links);
                    return NMO_ERR_NOMEM;
                }
                matched_links = next;
                matched_capacity = next_capacity;
            }
            matched_links[matched_count++] = (matched_link_t){
                .parent_behavior_id = nmo_object_get_id(behavior_obj),
                .link_id = link_id,
            };
        }
    }

    for (size_t i = 0; i < matched_count; ++i) {
        rc = nmo_script_edit_remove_behavior_link(
            tx,
            matched_links[i].parent_behavior_id,
            matched_links[i].link_id);
        if (rc != NMO_OK) {
            free(matched_links);
            return rc;
        }
    }

    free(matched_links);
    return NMO_OK;
}

static nmo_status_t script_edit_remove_links_for_behavior_ios(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t deleted_root_id,
    const nmo_behavior_state_t *behavior)
{
    if (!tx || !behavior) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_array_t *io_arrays[] = {
        &behavior->inputs,
        &behavior->outputs,
    };
    for (size_t i = 0; i < sizeof(io_arrays) / sizeof(io_arrays[0]); ++i) {
        const nmo_array_t *array = io_arrays[i];
        for (size_t j = 0; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(script_edit_remove_links_for_io(
                tx,
                parent_behavior_id,
                deleted_root_id,
                id));
        }
    }
    for (size_t i = 0; i < behavior->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &behavior->sub_behaviors, i);
        if (sub_id == 0) continue;
        nmo_behavior_state_t *sub_state = script_edit_find_behavior_state_in_repo(
            nmo_workspace_internal_repository(tx->workspace),
            sub_id,
            NULL);
        if (sub_state == NULL) {
            continue;
        }
        NMO_RETURN_IF_ERROR(script_edit_remove_links_for_behavior_ios(
            tx,
            parent_behavior_id,
            deleted_root_id,
            sub_state));
    }
    return NMO_OK;
}

static nmo_status_t script_edit_append_behavior_owned_destroy_objects(
    nmo_script_edit_tx_t *tx,
    nmo_behavior_state_t *state)
{
    if (!tx || !state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_array_t *owned_arrays[] = {
        &state->inputs,
        &state->outputs,
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
        &state->operations,
        &state->sub_behavior_links,
    };
    for (size_t i = 0; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        for (size_t j = 0; j < owned_arrays[i]->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                owned_arrays[i], j);
            if (id == 0) continue;
            nmo_status_t rc = script_edit_append_deferred_destroy(tx, id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (sub_id == 0) continue;
        nmo_behavior_state_t *sub_state = script_edit_find_behavior_state_in_repo(
            nmo_workspace_internal_repository(tx->workspace),
            sub_id,
            NULL);
        if (sub_state == NULL) {
            continue;
        }
        nmo_status_t rc =
            script_edit_append_behavior_owned_destroy_objects(tx, sub_state);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    return NMO_OK;
}

static bool script_edit_behavior_is_direct_graph_member(
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

static bool script_edit_behavior_is_graph_member(
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

static bool script_edit_find_direct_parent_behavior_in_repo(
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    size_t object_count = 0;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!repo || behavior_id == 0u) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *state = NULL;
        nmo_object_id_t parent_id = 0u;

        if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
            continue;
        }

        state = (nmo_behavior_state_t *)nmo_object_get_state(object);
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

static bool script_edit_find_parent_graph_io_owner(
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

static bool script_edit_is_parameter_owner_kind(nmo_port_kind_t kind)
{
    return kind == NMO_PORT_PARAM_IN ||
           kind == NMO_PORT_PARAM_OUT ||
           kind == NMO_PORT_PARAM_LOCAL;
}

static bool script_edit_find_parameter_owner(
    const nmo_behavior_index_t *index,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!index || parameter_id == 0) {
        return false;
    }

    owner = nmo_behavior_index_find(index, parameter_id);
    if (!owner || !script_edit_is_parameter_owner_kind(owner->kind)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_parameter_belongs_to_parent_graph(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return false;
    }
    if (!script_edit_behavior_is_direct_graph_member(session,
                                                     parent_behavior_id,
                                                     owner->owner_id)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_resolve_common_parent_graph(
    nmo_session_t *session,
    nmo_object_id_t left_owner_id,
    nmo_object_id_t right_owner_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    nmo_object_repository_t *repo = session ? nmo_session_get_repository(session) : NULL;
    nmo_object_id_t left_parent_id = 0u;
    nmo_object_id_t right_parent_id = 0u;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!session || left_owner_id == 0u || right_owner_id == 0u) {
        return false;
    }

    if (left_owner_id == right_owner_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (script_edit_behavior_is_direct_graph_member(session,
                                                    right_owner_id,
                                                    left_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = right_owner_id;
        }
        return true;
    }
    if (script_edit_behavior_is_direct_graph_member(session,
                                                    left_owner_id,
                                                    right_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (!script_edit_find_direct_parent_behavior_in_repo(repo, left_owner_id,
                                                         &left_parent_id) ||
        !script_edit_find_direct_parent_behavior_in_repo(repo, right_owner_id,
                                                         &right_parent_id)) {
        return false;
    }

    if (left_parent_id != 0u && left_parent_id == right_parent_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_parent_id;
        }
        return true;
    }
    return false;
}

static bool script_edit_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static bool script_edit_parameter_class_holds_value(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL;
}

static bool script_edit_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    if (!state || destination_id == 0u) {
        return false;
    }
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids && state->destination_ids[i] == destination_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t script_edit_parameterout_append_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_object_id_t *new_ids = NULL;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (script_edit_parameterout_has_destination(state, destination_id)) {
        return NMO_OK;
    }

    new_ids = (nmo_object_id_t *)nmo_arena_alloc(
        nmo_workspace_internal_document_arena(tx->workspace),
        (size_t)(state->destination_count + 1u) * sizeof(*new_ids),
        _Alignof(nmo_object_id_t));
    if (!new_ids) {
        return NMO_ERR_NOMEM;
    }

    if (state->destination_count > 0u && state->destination_ids) {
        memcpy(new_ids, state->destination_ids,
               (size_t)state->destination_count * sizeof(*new_ids));
    }
    new_ids[state->destination_count] = destination_id;
    state->destination_ids = new_ids;
    state->destination_count += 1u;
    return NMO_OK;
}

static nmo_status_t script_edit_parameterout_remove_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_object_id_t *new_ids = NULL;
    uint32_t kept = 0u;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (state->destination_count == 0u || !state->destination_ids) {
        return NMO_OK;
    }

    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids[i] != destination_id) {
            kept++;
        }
    }
    if (kept == state->destination_count) {
        return NMO_OK;
    }
    if (kept > 0u) {
        uint32_t out_index = 0u;
        new_ids = (nmo_object_id_t *)nmo_arena_alloc(
            nmo_workspace_internal_document_arena(tx->workspace),
            (size_t)kept * sizeof(*new_ids),
            _Alignof(nmo_object_id_t));
        if (!new_ids) {
            return NMO_ERR_NOMEM;
        }
        for (uint32_t i = 0; i < state->destination_count; ++i) {
            if (state->destination_ids[i] != destination_id) {
                new_ids[out_index++] = state->destination_ids[i];
            }
        }
    }

    state->destination_ids = new_ids;
    state->destination_count = kept;
    return NMO_OK;
}

static nmo_status_t validate_behavior_link_owners(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_behaviorlink_state_t *state = NULL;

        if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
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
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t class_id = 0;

        if (!object) {
            continue;
        }

        class_id = nmo_object_get_class_id(object);
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }
        if (class_id == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            nmo_object_t *source = NULL;
            if (!state) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            if (state->source_id != 0) {
                source = nmo_object_repository_find_by_id(repo, state->source_id);
                if (!source) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
                if (state->is_shared) {
                    if (nmo_object_get_class_id(source) != NMO_CID_PARAMETERIN) {
                        return NMO_ERR_VALIDATION_FAILED;
                    }
                } else if (!script_edit_is_parameter_reference_class(
                               nmo_object_get_class_id(source))) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
        } else if (class_id == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(object);
            if (!state) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                nmo_object_id_t destination_id = state->destination_ids
                    ? state->destination_ids[j]
                    : 0;
                nmo_object_t *destination = NULL;
                if (destination_id == 0) {
                    continue;
                }
                destination = nmo_object_repository_find_by_id(repo, destination_id);
                if (!destination ||
                    !script_edit_is_parameter_reference_class(
                        nmo_object_get_class_id(destination))) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
        }
    }

    return NMO_OK;
}

static nmo_status_t validate_parameter_operations(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_parameteroperation_state_t *state = NULL;

        if (!object ||
            nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (state->has_in1) {
            nmo_object_t *param = NULL;
            if (state->in1_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->in1_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_in2) {
            nmo_object_t *param = NULL;
            if (state->in2_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->in2_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_out) {
            nmo_object_t *param = NULL;
            if (state->out_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->out_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
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
    tx->session = seed_session;
    rc = nmo_workspace_internal_borrow_document(workspace, &tx->document);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }
    seed_session = nmo_document_internal_session(tx->document);
    if (!seed_session) {
        script_edit_tx_destroy(tx);
        return NMO_ERR_INVALID_STATE;
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
        script_edit_note_delete(tx);
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

static bool script_edit_interface_owner_matches(const nmo_port_owner_t *owner,
                                                nmo_object_id_t owner_id,
                                                nmo_port_kind_t kind)
{
    return owner && owner->owner_id == owner_id && owner->kind == kind;
}

static bool script_edit_behavior_array_contains(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    const char *field_name,
    nmo_object_id_t object_id)
{
    nmo_behavior_state_t *state = script_edit_find_behavior_state(
        session, behavior_id, NULL);
    if (!state || !field_name || object_id == NMO_OBJECT_ID_NONE) return false;
    const nmo_array_t *array = NULL;
    if (strcmp(field_name, "operations") == 0) {
        array = &state->operations;
    } else if (strcmp(field_name, "sub_behavior_links") == 0) {
        array = &state->sub_behavior_links;
    } else {
        return false;
    }
    return nmo_behavior_ref_array_find(array, object_id, NULL);
}

typedef bool (*script_edit_interface_object_match_fn)(const nmo_object_t *object);

static bool script_edit_interface_object_matches_any(
    const nmo_object_t *object)
{
    return object != NULL;
}

static bool script_edit_interface_object_matches_behavior(
    const nmo_object_t *object)
{
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR;
}

static bool script_edit_interface_object_matches_link(
    const nmo_object_t *object)
{
    return object != NULL &&
           nmo_object_get_class_id(object) == NMO_CID_BEHAVIORLINK;
}

static bool script_edit_interface_object_matches_operation(
    const nmo_object_t *object)
{
    return object != NULL &&
           nmo_object_get_class_id(object) == NMO_CID_PARAMETEROPERATION;
}

static bool script_edit_interface_object_matches_parameter(
    const nmo_object_t *object)
{
    return object != NULL && nmo_parameter_get_state((nmo_object_t *)object) != NULL;
}

static nmo_object_t *script_edit_find_interface_object(
    nmo_session_t *session,
    nmo_object_id_t interface_id,
    bool interface_ids_are_runtime,
    script_edit_interface_object_match_fn matches)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (!session || interface_id == 0u) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    if (!repo) {
        return NULL;
    }

    if (interface_ids_are_runtime) {
        object = nmo_object_repository_find_by_id(repo, interface_id);
        if (matches(object)) {
            return object;
        }
        object = nmo_object_repository_find_by_file_id(repo, interface_id);
        if (matches(object)) {
            return object;
        }
    } else {
        object = nmo_object_repository_find_by_file_id(repo, interface_id);
        if (matches(object)) {
            return object;
        }
        object = nmo_object_repository_find_by_id(repo, interface_id);
        if (matches(object)) {
            return object;
        }
    }

    return NULL;
}

static nmo_object_id_t script_edit_resolve_interface_object_id(
    nmo_session_t *session,
    nmo_object_id_t interface_id,
    bool interface_ids_are_runtime,
    script_edit_interface_object_match_fn matches,
    nmo_object_t **out_object)
{
    nmo_object_t *object = script_edit_find_interface_object(session,
                                                             interface_id,
                                                             interface_ids_are_runtime,
                                                             matches);
    if (out_object) {
        *out_object = object;
    }
    return object ? object->id : 0u;
}

static nmo_behavior_state_t *script_edit_resolve_interface_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t interface_behavior_id,
    bool interface_ids_are_runtime,
    nmo_object_id_t *out_runtime_behavior_id)
{
    nmo_object_t *object = NULL;
    nmo_object_id_t runtime_behavior_id = script_edit_resolve_interface_object_id(
        session,
        interface_behavior_id,
        interface_ids_are_runtime,
        script_edit_interface_object_matches_behavior,
        &object);

    if (out_runtime_behavior_id) {
        *out_runtime_behavior_id = runtime_behavior_id;
    }
    return object != NULL ? (nmo_behavior_state_t *)nmo_object_get_state(object) : NULL;
}

static bool script_edit_interface_endpoint_exists(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    bool interface_ids_are_runtime)
{
    return object_id == 0u ||
           script_edit_find_interface_object(session,
                                             object_id,
                                             interface_ids_are_runtime,
                                             script_edit_interface_object_matches_any) != NULL;
}

static bool script_edit_interface_link_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_link_t *link,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t link_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!link) {
        return false;
    }
    if (link->type == NMO_INTERFACE_LINK_PARAMETER) {
        return script_edit_interface_endpoint_exists(session,
                                                     link->start.id,
                                                     interface_ids_are_runtime) &&
               script_edit_interface_endpoint_exists(session,
                                                     link->end.id,
                                                     interface_ids_are_runtime);
    }
    link_id = script_edit_resolve_interface_object_id(session,
                                                      link->link_id,
                                                      interface_ids_are_runtime,
                                                      script_edit_interface_object_matches_link,
                                                      NULL);
    owner = index ? nmo_behavior_index_find(index, link_id) : NULL;
    if (!script_edit_interface_owner_matches(owner, owner_id, NMO_PORT_SUB_LINK)) {
        return false;
    }
    if (!script_edit_behavior_array_contains(
            session, owner_id, "sub_behavior_links", link_id)) {
        return false;
    }
    return script_edit_interface_endpoint_exists(session,
                                                 link->start.id,
                                                 interface_ids_are_runtime) &&
           script_edit_interface_endpoint_exists(session,
                                                 link->end.id,
                                                 interface_ids_are_runtime) &&
           link_id != 0u;
}

static bool script_edit_interface_operation_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_operation_t *op,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t operation_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!op) {
        return false;
    }
    operation_id = script_edit_resolve_interface_object_id(
        session,
        op->id,
        interface_ids_are_runtime,
        script_edit_interface_object_matches_operation,
        NULL);
    owner = index ? nmo_behavior_index_find(index, operation_id) : NULL;
    if (!script_edit_interface_owner_matches(owner, owner_id, NMO_PORT_OPERATION)) {
        return false;
    }
    return script_edit_behavior_array_contains(
        session, owner_id, "operations", operation_id);
}

static bool script_edit_interface_shared_param_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_param_t *param,
    bool interface_ids_are_runtime)
{
    nmo_object_t *object = NULL;
    nmo_object_id_t source_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!param || param->source_id == 0u) {
        return false;
    }
    source_id = script_edit_resolve_interface_object_id(
        session,
        param->source_id,
        interface_ids_are_runtime,
        script_edit_interface_object_matches_parameter,
        &object);
    owner = index ? nmo_behavior_index_find(index, source_id) : NULL;
    if (!owner || owner->owner_id != owner_id) {
        return false;
    }
    return object != NULL && nmo_parameter_get_state(object) != NULL;
}

static bool script_edit_interface_graph_io_is_valid(
    const nmo_behavior_state_t *owner_state,
    const nmo_interface_graph_io_t *graph_io)
{
    const int32_t *sets[4];
    size_t counts[4];

    if (!owner_state || !graph_io) {
        return false;
    }

    sets[0] = graph_io->inward_inputs;
    sets[1] = graph_io->outward_inputs;
    sets[2] = graph_io->inward_outputs;
    sets[3] = graph_io->outward_outputs;
    counts[0] = graph_io->inward_input_count;
    counts[1] = graph_io->outward_input_count;
    counts[2] = graph_io->inward_output_count;
    counts[3] = graph_io->outward_output_count;

    for (size_t set_index = 0; set_index < 4u; ++set_index) {
        for (size_t i = 0; i < counts[set_index]; ++i) {
            if (sets[set_index][i] < 0) {
                return false;
            }
        }
    }
    return true;
}

static bool script_edit_interface_graph_io_is_owner_relative(
    const nmo_behavior_state_t *owner_state,
    const nmo_interface_graph_io_t *graph_io)
{
    const int32_t *sets[4];
    size_t counts[4];
    size_t limits[4];

    if (!owner_state || !graph_io) {
        return false;
    }

    sets[0] = graph_io->inward_inputs;
    sets[1] = graph_io->outward_inputs;
    sets[2] = graph_io->inward_outputs;
    sets[3] = graph_io->outward_outputs;
    counts[0] = graph_io->inward_input_count;
    counts[1] = graph_io->outward_input_count;
    counts[2] = graph_io->inward_output_count;
    counts[3] = graph_io->outward_output_count;
    limits[0] = owner_state->inputs.count;
    limits[1] = owner_state->inputs.count;
    limits[2] = owner_state->outputs.count;
    limits[3] = owner_state->outputs.count;

    for (size_t set_index = 0; set_index < 4u; ++set_index) {
        for (size_t i = 0; i < counts[set_index]; ++i) {
            if (sets[set_index][i] < 0 ||
                (size_t)sets[set_index][i] >= limits[set_index]) {
                return false;
            }
        }
    }
    return true;
}

static bool script_edit_interface_body_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    nmo_behavior_state_t *owner_state = NULL;

    if (!body || !body->has_body) {
        return true;
    }
    owner_state = script_edit_find_behavior_state(session, owner_id, NULL);
    if (!owner_state) {
        return false;
    }

    for (size_t i = 0; i < body->link_count; ++i) {
        if (!script_edit_interface_link_is_valid(session,
                                                 index,
                                                 owner_id,
                                                 &body->links[i],
                                                 interface_ids_are_runtime)) {
            return false;
        }
    }
    for (size_t i = 0; i < body->operation_count; ++i) {
        if (!script_edit_interface_operation_is_valid(session,
                                                      index,
                                                      owner_id,
                                                      &body->operations[i],
                                                      interface_ids_are_runtime)) {
            return false;
        }
    }
    if (body->has_params) {
        if (body->params.local_count > owner_state->local_parameters.count) {
            return false;
        }
        for (size_t i = 0; i < body->params.shared_count; ++i) {
            if (!script_edit_interface_shared_param_is_valid(session,
                                                             index,
                                                             owner_id,
                                                             &body->params.shared[i],
                                                             interface_ids_are_runtime)) {
                return false;
            }
        }
    }
    if (body->has_graph_io && body->graph_io &&
        !script_edit_interface_graph_io_is_valid(owner_state, body->graph_io)) {
        return false;
    }
    return true;
}

NMO_API nmo_status_t nmo_script_edit_validate_interface_refs(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_behavior_state_t *script_behavior = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t script_behavior_id = 0u;
    bool interface_ids_are_runtime = false;

    if (!tx || !tx->session || behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!behavior->interface_data) {
        return NMO_OK;
    }
    interface_ids_are_runtime = behavior->interface_ids_are_runtime;
    script_behavior_id = behavior->interface_data->script.behavior_id != 0u
        ? behavior->interface_data->script.behavior_id
        : behavior_id;
    index = nmo_workspace_internal_behavior_index(tx->workspace);
    script_behavior = script_edit_resolve_interface_behavior_state(
        tx->session,
        script_behavior_id,
        interface_ids_are_runtime,
        &script_behavior_id);
    if (!script_behavior) {
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                           NMO_SEVERITY_WARNING,
                           "Interface root behavior %u could not resolve in %s ID space",
                           behavior->interface_data->script.behavior_id,
                           interface_ids_are_runtime ? "runtime" : "raw");
        return NMO_ERR_VALIDATION_FAILED;
    }

    for (size_t i = 0; i < behavior->interface_data->sub_count; ++i) {
        const nmo_interface_behavior_t *sub = &behavior->interface_data->subs[i];
        nmo_object_id_t sub_behavior_id = 0u;
        nmo_behavior_state_t *sub_behavior = script_edit_resolve_interface_behavior_state(
            tx->session,
            sub->behavior_id,
            interface_ids_are_runtime,
            &sub_behavior_id);

        if (!script_edit_behavior_is_graph_member(tx->session,
                                                  script_behavior_id,
                                                  sub_behavior_id) ||
            !sub_behavior) {
            NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                               NMO_SEVERITY_WARNING,
                               "Interface sub[%zu] behavior %u resolved=%u is not in root graph %u",
                               i,
                               sub->behavior_id,
                               sub_behavior_id,
                               script_behavior_id);
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (!script_edit_interface_body_is_valid(tx->session,
                                                 index,
                                                 sub_behavior_id,
                                                 &sub->body,
                                                 interface_ids_are_runtime)) {
            NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                               NMO_SEVERITY_WARNING,
                               "Interface sub[%zu] body for behavior %u resolved=%u is invalid",
                               i,
                               sub->behavior_id,
                               sub_behavior_id);
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    if (!script_edit_interface_body_is_valid(tx->session, index,
                                             script_behavior_id,
                                             &behavior->interface_data->script.body,
                                             interface_ids_are_runtime)) {
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                           NMO_SEVERITY_WARNING,
                           "Interface script body for behavior %u resolved=%u is invalid",
                           behavior->interface_data->script.behavior_id,
                           script_behavior_id);
        return NMO_ERR_VALIDATION_FAILED;
    }

    return NMO_OK;
}

static bool script_edit_filter_graph_io_indices(int32_t *items,
                                                size_t *count,
                                                size_t limit)
{
    size_t write_index = 0u;
    bool changed = false;

    if (!count) {
        return false;
    }
    for (size_t read_index = 0; read_index < *count; ++read_index) {
        if (!items || items[read_index] < 0 ||
            (size_t)items[read_index] >= limit) {
            changed = true;
            continue;
        }
        if (items && write_index != read_index) {
            items[write_index] = items[read_index];
        }
        ++write_index;
    }
    *count = write_index;
    return changed;
}

static bool script_edit_rewrite_removed_graph_io_index(
    int32_t *items,
    size_t *count,
    size_t removed_index)
{
    size_t write_index = 0u;
    bool changed = false;

    if (!count) {
        return false;
    }

    for (size_t read_index = 0; read_index < *count; ++read_index) {
        int32_t value = items ? items[read_index] : -1;
        if (!items || value < 0) {
            changed = true;
            continue;
        }
        if ((size_t)value == removed_index) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            changed = true;
        }
        items[write_index++] = value;
    }

    *count = write_index;
    return changed;
}

static bool script_edit_rewrite_interface_body_removed_io(
    nmo_interface_body_t *body,
    nmo_port_kind_t kind,
    size_t removed_index)
{
    bool changed = false;

    if (!body || !body->has_body || !body->has_graph_io || !body->graph_io) {
        return false;
    }

    if (kind == NMO_PORT_IO_IN) {
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->inward_inputs,
            &body->graph_io->inward_input_count,
            removed_index);
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->outward_inputs,
            &body->graph_io->outward_input_count,
            removed_index);
    } else if (kind == NMO_PORT_IO_OUT) {
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->inward_outputs,
            &body->graph_io->inward_output_count,
            removed_index);
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->outward_outputs,
            &body->graph_io->outward_output_count,
            removed_index);
    }

    return changed;
}

static bool script_edit_apply_removed_io_refs_to_behavior(
    nmo_script_edit_tx_t *tx,
    nmo_interface_data_t *idata,
    nmo_object_id_t root_behavior_id)
{
    bool changed = false;

    if (!tx || !idata || root_behavior_id == 0u) {
        return false;
    }

    for (size_t i = 0; i < tx->removed_io_ref_count; ++i) {
        const script_edit_removed_io_ref_t *ref = &tx->removed_io_refs[i];
        nmo_interface_body_t *body = NULL;

        if (idata->script.behavior_id == ref->owner_behavior_id ||
            root_behavior_id == ref->owner_behavior_id) {
            body = &idata->script.body;
        } else {
            for (size_t j = 0; j < idata->sub_count; ++j) {
                if (idata->subs[j].behavior_id == ref->owner_behavior_id) {
                    if (script_edit_rewrite_interface_body_removed_io(
                            &idata->subs[j].body,
                            ref->kind,
                            ref->removed_index)) {
                        changed = true;
                    }
                }
            }
        }

        if (body != NULL &&
            script_edit_rewrite_interface_body_removed_io(body,
                                                          ref->kind,
                                                          ref->removed_index)) {
            changed = true;
        }
    }

    return changed;
}

static bool script_edit_rewrite_interface_endpoint_to_runtime(
    nmo_session_t *session,
    nmo_interface_endpoint_t *endpoint,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t runtime_id = 0u;

    if (!endpoint || endpoint->id == 0u) {
        return false;
    }

    runtime_id = script_edit_resolve_interface_object_id(
        session,
        endpoint->id,
        interface_ids_are_runtime,
        script_edit_interface_object_matches_any,
        NULL);
    if (runtime_id == 0u || runtime_id == endpoint->id) {
        return false;
    }

    endpoint->id = runtime_id;
    return true;
}

static bool script_edit_rewrite_interface_body_to_runtime(
    nmo_session_t *session,
    nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    bool changed = false;

    if (!body || !body->has_body) {
        return false;
    }

    for (size_t i = 0; i < body->link_count; ++i) {
        nmo_object_id_t runtime_link_id = script_edit_resolve_interface_object_id(
            session,
            body->links[i].link_id,
            interface_ids_are_runtime,
            script_edit_interface_object_matches_link,
            NULL);
        if (runtime_link_id != 0u && runtime_link_id != body->links[i].link_id) {
            body->links[i].link_id = runtime_link_id;
            changed = true;
        }
        changed |= script_edit_rewrite_interface_endpoint_to_runtime(
            session,
            &body->links[i].start,
            interface_ids_are_runtime);
        changed |= script_edit_rewrite_interface_endpoint_to_runtime(
            session,
            &body->links[i].end,
            interface_ids_are_runtime);
    }

    for (size_t i = 0; i < body->operation_count; ++i) {
        nmo_object_id_t runtime_operation_id = script_edit_resolve_interface_object_id(
            session,
            body->operations[i].id,
            interface_ids_are_runtime,
            script_edit_interface_object_matches_operation,
            NULL);
        if (runtime_operation_id != 0u &&
            runtime_operation_id != body->operations[i].id) {
            body->operations[i].id = runtime_operation_id;
            changed = true;
        }
    }

    if (body->has_params) {
        for (size_t i = 0; i < body->params.shared_count; ++i) {
            nmo_object_id_t runtime_source_id = script_edit_resolve_interface_object_id(
                session,
                body->params.shared[i].source_id,
                interface_ids_are_runtime,
                script_edit_interface_object_matches_parameter,
                NULL);
            if (runtime_source_id != 0u &&
                runtime_source_id != body->params.shared[i].source_id) {
                body->params.shared[i].source_id = runtime_source_id;
                changed = true;
            }
        }
    }

    return changed;
}

static bool script_edit_rewrite_interface_data_to_runtime(
    nmo_session_t *session,
    nmo_interface_data_t *idata,
    bool interface_ids_are_runtime)
{
    bool changed = false;

    if (!session || !idata) {
        return false;
    }

    if (idata->script.behavior_id != 0u) {
        nmo_object_id_t runtime_script_id = script_edit_resolve_interface_object_id(
            session,
            idata->script.behavior_id,
            interface_ids_are_runtime,
            script_edit_interface_object_matches_behavior,
            NULL);
        if (runtime_script_id != 0u && runtime_script_id != idata->script.behavior_id) {
            idata->script.behavior_id = runtime_script_id;
            changed = true;
        }
    }
    changed |= script_edit_rewrite_interface_body_to_runtime(session,
                                                             &idata->script.body,
                                                             interface_ids_are_runtime);

    for (size_t i = 0; i < idata->sub_count; ++i) {
        nmo_object_id_t runtime_behavior_id = script_edit_resolve_interface_object_id(
            session,
            idata->subs[i].behavior_id,
            interface_ids_are_runtime,
            script_edit_interface_object_matches_behavior,
            NULL);
        if (runtime_behavior_id != 0u &&
            runtime_behavior_id != idata->subs[i].behavior_id) {
            idata->subs[i].behavior_id = runtime_behavior_id;
            changed = true;
        }
        changed |= script_edit_rewrite_interface_body_to_runtime(session,
                                                                 &idata->subs[i].body,
                                                                 interface_ids_are_runtime);
    }

    return changed;
}

static bool script_edit_canonicalize_interface_body(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    nmo_behavior_state_t *owner_state = NULL;
    size_t write_index = 0u;
    bool changed = false;

    if (!body || !body->has_body) {
        return false;
    }
    owner_state = script_edit_find_behavior_state(session, owner_id, NULL);
    if (!owner_state) {
        return false;
    }

    for (size_t read_index = 0; read_index < body->link_count; ++read_index) {
        if (!script_edit_interface_link_is_valid(session,
                                                 index,
                                                 owner_id,
                                                 &body->links[read_index],
                                                 interface_ids_are_runtime)) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            body->links[write_index] = body->links[read_index];
        }
        ++write_index;
    }
    body->link_count = write_index;

    write_index = 0u;
    for (size_t read_index = 0; read_index < body->operation_count; ++read_index) {
        if (!script_edit_interface_operation_is_valid(session,
                                                      index,
                                                      owner_id,
                                                      &body->operations[read_index],
                                                      interface_ids_are_runtime)) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            body->operations[write_index] = body->operations[read_index];
        }
        ++write_index;
    }
    body->operation_count = write_index;

    if (body->has_params) {
        if (body->params.local_count > owner_state->local_parameters.count) {
            body->params.local_count = owner_state->local_parameters.count;
            changed = true;
        }
        write_index = 0u;
        for (size_t read_index = 0; read_index < body->params.shared_count; ++read_index) {
            if (!script_edit_interface_shared_param_is_valid(
                    session,
                    index,
                    owner_id,
                    &body->params.shared[read_index],
                    interface_ids_are_runtime)) {
                changed = true;
                continue;
            }
            if (write_index != read_index) {
                body->params.shared[write_index] = body->params.shared[read_index];
            }
            ++write_index;
        }
        body->params.shared_count = write_index;
    }

    if (body->has_graph_io && body->graph_io &&
        script_edit_interface_graph_io_is_owner_relative(owner_state,
                                                         body->graph_io)) {
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->inward_inputs,
            &body->graph_io->inward_input_count,
            owner_state->inputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->outward_inputs,
            &body->graph_io->outward_input_count,
            owner_state->inputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->inward_outputs,
            &body->graph_io->inward_output_count,
            owner_state->outputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->outward_outputs,
            &body->graph_io->outward_output_count,
            owner_state->outputs.count);
    }

    return changed;
}

NMO_API nmo_status_t nmo_script_edit_apply_interface_policy(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_interface_mode_t mode)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_interface_data_t *idata = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t script_behavior_id = 0u;
    size_t write_index = 0u;
    nmo_status_t rc = NMO_OK;
    bool interface_ids_are_runtime = false;

    if (!tx || !tx->edit || behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (mode == NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        return nmo_script_edit_validate_interface_refs(tx, behavior_id);
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    if (mode == NMO_SCRIPT_EDIT_INTERFACE_REMOVE) {
        if (!behavior->has_interface &&
            !behavior->interface_chunk &&
            !behavior->interface_data) {
            return NMO_OK;
        }
        behavior->has_interface = false;
        behavior->interface_chunk = NULL;
        behavior->interface_data = NULL;
        behavior->interface_ids_are_runtime = false;
        (void)nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
        tx->report.interface_changes++;
        nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        return NMO_OK;
    }

    idata = behavior->interface_data;
    if (!idata) {
        return NMO_OK;
    }
    interface_ids_are_runtime = behavior->interface_ids_are_runtime;
    if (!interface_ids_are_runtime) {
        if (script_edit_rewrite_interface_data_to_runtime(tx->session,
                                                          idata,
                                                          false)) {
            tx->report.interface_changes++;
        }
        behavior->interface_ids_are_runtime = true;
        interface_ids_are_runtime = true;
    }
    script_behavior_id = idata->script.behavior_id != 0u
        ? idata->script.behavior_id
        : behavior_id;
    index = nmo_workspace_internal_behavior_index(tx->workspace);

    for (size_t read_index = 0; read_index < idata->sub_count; ++read_index) {
        nmo_interface_behavior_t *sub = &idata->subs[read_index];
        if (!script_edit_behavior_is_graph_member(tx->session,
                                                  script_behavior_id,
                                                  sub->behavior_id)) {
            tx->report.interface_changes++;
            continue;
        }
        if (write_index != read_index) {
            idata->subs[write_index] = *sub;
        }
        if (script_edit_canonicalize_interface_body(tx->session,
                                                    index,
                                                    sub->behavior_id,
                                                    &idata->subs[write_index].body,
                                                    interface_ids_are_runtime)) {
            tx->report.interface_changes++;
        }
        ++write_index;
    }
    idata->sub_count = write_index;
    if (script_edit_canonicalize_interface_body(tx->session,
                                                index,
                                                script_behavior_id,
                                                &idata->script.body,
                                                interface_ids_are_runtime)) {
        tx->report.interface_changes++;
    }
    if (script_edit_apply_removed_io_refs_to_behavior(tx, idata, behavior_id)) {
        tx->report.interface_changes++;
    }

    rc = nmo_script_edit_validate_interface_refs(tx, behavior_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (tx->report.interface_changes > 0u) {
        (void)nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
        nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    nmo_object_id_t *out_node_id)
{
    return nmo_script_edit_add_node_ex(
        tx, parent_behavior_id, bb_guid, name, NULL, out_node_id);
}

NMO_API nmo_status_t nmo_script_edit_add_node_ex(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    const nmo_script_edit_add_node_options_t *options,
    nmo_object_id_t *out_node_id)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_behavior_proto_t *proto = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t node_id = 0;
    nmo_manager_entry_options_t manager_entry =
        options != NULL ? options->manager_entry
                        : nmo_manager_entry_options_default();

    if (!tx || !tx->edit || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    if (!parent_state) {
        return NMO_ERR_NOT_FOUND;
    }

    proto = nmo_behavior_registry_find(nmo_context_get_bb_registry(tx->ctx), bb_guid);
    if (!proto) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, parent_state,
                                         sizeof(*parent_state));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_runtime_object(
        tx,
        NMO_CID_BEHAVIOR,
        (name && name[0] != '\0') ? name : proto->name,
        NMO_GUID_NULL,
        &node_id);
    if (rc != NMO_OK) {
        return rc;
    }

    node_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        node_id,
        NULL);
    if (!node_state) {
        return NMO_ERR_INVALID_STATE;
    }

    node_state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    node_state->flags &= ~CKBEHAVIOR_SCRIPT;
    node_state->flags |= proto->behavior_flags;
    node_state->compatible_class_id = proto->compatible_class_id;
    node_state->block_version = proto->version != 0u ? proto->version : 65536u;
    node_state->block_guid = bb_guid;
    node_state->priority = 0;
    node_state->owner_id = parent_behavior_id;

    {
        for (uint32_t i = 0; i < proto->input_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->inputs[i],
                                              NMO_SCRIPT_EDIT_IO_INPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(&node_state->inputs, io_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->outputs[i],
                                              NMO_SCRIPT_EDIT_IO_OUTPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(&node_state->outputs, io_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->input_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERIN, node_id,
                proto->input_params[i].name,
                proto->input_params[i].type_guid,
                proto->input_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->in_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETEROUT, node_id,
                proto->output_params[i].name,
                proto->output_params[i].type_guid,
                proto->output_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->out_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        if ((proto->behavior_flags & CKBEHAVIOR_TARGETABLE) != 0u) {
            nmo_type_registry_t *registry = nmo_context_get_type_registry(tx->ctx);
            nmo_guid_t target_type_guid = NMO_GUID_NULL;
            nmo_object_id_t target_parameter_id = 0;
            uint32_t target_class_id = proto->compatible_class_id != 0
                ? (uint32_t)proto->compatible_class_id
                : (uint32_t)NMO_CID_BEOBJECT;
            if (!registry) {
                return NMO_ERR_INVALID_STATE;
            }
            rc = nmo_type_registry_class_id_to_guid(
                registry,
                target_class_id,
                &target_type_guid);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERIN, node_id, "Target", target_type_guid,
                NULL,
                &manager_entry,
                &target_parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            node_state->target_parameter_id = target_parameter_id;
        }
        for (uint32_t i = 0; i < proto->local_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->local_params[i].name,
                proto->local_params[i].type_guid,
                proto->local_params[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_behavior_ref_array_append(
                &node_state->local_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->setting_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            nmo_object_t *parameter_obj = NULL;
            nmo_parameterlocal_state_t *parameter_state = NULL;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->settings[i].name,
                proto->settings[i].type_guid,
                proto->settings[i].default_value,
                &manager_entry,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            parameter_obj = nmo_object_repository_find_by_id(
                nmo_workspace_internal_repository(tx->workspace),
                parameter_id);
            parameter_state = parameter_obj
                ? (nmo_parameterlocal_state_t *)nmo_object_get_state(parameter_obj)
                : NULL;
            if (!parameter_state) {
                return NMO_ERR_INVALID_STATE;
            }
            parameter_state->is_setting = 1u;
            rc = nmo_behavior_ref_array_append(
                &node_state->local_parameters, parameter_id, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    script_edit_update_behavior_save_flags(node_state);
    rc = nmo_behavior_ref_array_append(
        &parent_state->sub_behaviors, node_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
            NMO_WORKSPACE_EDIT_NAMES);

    if (out_node_id) {
        *out_node_id = node_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;
    size_t node_index = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parent_behavior_id == 0 || node_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    node_state = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        node_id,
        NULL);
    if (!parent_state || !node_state) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!nmo_behavior_ref_array_find(
            &parent_state->sub_behaviors, node_id, &node_index)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = script_edit_remove_links_for_behavior_ios(
        tx, parent_behavior_id, node_id, node_state);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_append_behavior_owned_destroy_objects(tx, node_state);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_internal_preview_destroy(
        tx->workspace,
        &node_id,
        1,
        delete_flags,
        nmo_workspace_internal_document_arena(tx->workspace),
        &expanded_ids,
        &expanded_count);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, parent_state,
                                         sizeof(*parent_state));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&parent_state->sub_behaviors, node_index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    for (size_t i = 0; i < expanded_count; ++i) {
        rc = script_edit_append_deferred_destroy(tx, expanded_ids[i]);
        if (rc != NMO_OK) {
            return rc;
        }
        script_edit_note_delete(tx);
    }

    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name,
    nmo_object_id_t *out_io_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_object_id_t io_id = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || behavior_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_io_object(tx, name, kind, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    array = kind == NMO_SCRIPT_EDIT_IO_INPUT ? &behavior->inputs
                                             : &behavior->outputs;
    rc = nmo_behavior_ref_array_append(array, io_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
            NMO_WORKSPACE_EDIT_NAMES);
    rc = script_edit_note_changed_id(tx, behavior_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (out_io_id) {
        *out_io_id = io_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rename_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    const char *name)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || io_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_object_edit_rename(tx->edit, io_id, name);
    if (rc != NMO_OK) {
        return rc;
    }
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    bool detach_links)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || io_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!detach_links &&
        script_edit_io_is_linked_in_repo(
            nmo_workspace_internal_repository(tx->workspace),
            owner->owner_id,
            io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (detach_links) {
        rc = script_edit_remove_links_for_io(tx, owner->owner_id, 0u, io_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        owner->owner_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    array = owner->kind == NMO_PORT_IO_IN ? &behavior->inputs : &behavior->outputs;
    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_removed_io_ref(tx,
                                           owner->owner_id,
                                           owner->kind,
                                           (size_t)owner->index);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_append_deferred_destroy(tx, io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_note_delete(tx);
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_WORKSPACE_EDIT_OBJECT_STATE |
            NMO_WORKSPACE_EDIT_REFERENCES |
            NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

static nmo_guid_t script_edit_parameter_type_guid_from_object(
    const nmo_object_t *object)
{
    if (!object) {
        return NMO_GUID_NULL;
    }
    if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)nmo_object_get_state(object);
        return state ? state->type_guid : NMO_GUID_NULL;
    }

    {
        const nmo_parameter_state_t *state = nmo_parameter_get_state(object);
        return state ? state->type_guid : NMO_GUID_NULL;
    }
}

static const nmo_type_descriptor_t *script_edit_resolve_parameter_type_desc(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    if (!tx || !tx->ctx || !tx->session || parameter_id == 0u) {
        return NULL;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object) {
        return NULL;
    }

    type_guid = script_edit_parameter_type_guid_from_object(object);
    if (nmo_guid_is_null(type_guid)) {
        return NULL;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    return registry ? nmo_type_registry_find_by_guid(registry, type_guid) : NULL;
}

static bool script_edit_type_matches_operation_guid(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *actual_type,
    nmo_guid_t expected_guid)
{
    nmo_type_id_t actual_id = NMO_TYPE_ID_INVALID;
    nmo_type_id_t expected_id = NMO_TYPE_ID_INVALID;

    if (!actual_type || nmo_guid_is_null(expected_guid)) {
        return false;
    }
    if (nmo_guid_equals(actual_type->guid, expected_guid)) {
        return true;
    }
    if (!registry) {
        return false;
    }

    actual_id = nmo_type_registry_guid_to_type_id(registry, actual_type->guid);
    expected_id = nmo_type_registry_guid_to_type_id(registry, expected_guid);
    if (actual_id == NMO_TYPE_ID_INVALID || expected_id == NMO_TYPE_ID_INVALID) {
        return false;
    }

    return nmo_type_get_derivation_depth((nmo_type_registry_t *)registry,
                                         actual_id, expected_id) >= 0;
}

static bool script_edit_operation_family_matches(
    const nmo_operation_family_t *family,
    const nmo_type_registry_t *type_registry,
    const nmo_type_descriptor_t *in1_type,
    const nmo_type_descriptor_t *in2_type,
    const nmo_type_descriptor_t *out_type)
{
    if (!family) {
        return false;
    }

    for (size_t i = 0; i < family->p1_layers.count; ++i) {
        const nmo_operation_p1_layer_t *p1_layer =
            (const nmo_operation_p1_layer_t *)nmo_arena_array_get(
                (nmo_arena_array_t *)&family->p1_layers, i);
        if (!p1_layer) {
            continue;
        }
        if (in1_type &&
            !script_edit_type_matches_operation_guid(type_registry, in1_type,
                                                     p1_layer->p1_type_guid)) {
            continue;
        }

        for (size_t j = 0; j < p1_layer->p2_layers.count; ++j) {
            const nmo_operation_p2_layer_t *p2_layer =
                (const nmo_operation_p2_layer_t *)nmo_arena_array_get(
                    (nmo_arena_array_t *)&p1_layer->p2_layers, j);
            if (!p2_layer) {
                continue;
            }
            if (in2_type &&
                !script_edit_type_matches_operation_guid(type_registry, in2_type,
                                                         p2_layer->p2_type_guid)) {
                continue;
            }

            for (size_t k = 0; k < p2_layer->cells.count; ++k) {
                const nmo_operation_tree_cell_t *cell =
                    (const nmo_operation_tree_cell_t *)nmo_arena_array_get(
                        (nmo_arena_array_t *)&p2_layer->cells, k);
                if (!cell) {
                    continue;
                }
                if (out_type &&
                    !script_edit_type_matches_operation_guid(type_registry, out_type,
                                                             cell->desc.result_type_guid)) {
                    continue;
                }
                return true;
            }
        }
    }

    return false;
}

static nmo_status_t script_edit_validate_operation_signature(
    nmo_script_edit_tx_t *tx,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    nmo_operation_registry_t *operation_registry = NULL;
    nmo_type_registry_t *type_registry = NULL;
    const nmo_operation_family_t *family = NULL;
    const nmo_type_descriptor_t *in1_type = NULL;
    const nmo_type_descriptor_t *in2_type = NULL;
    const nmo_type_descriptor_t *out_type = NULL;

    if (!tx || !tx->ctx || nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (in1_parameter_id == 0u &&
        in2_parameter_id == 0u &&
        out_parameter_id == 0u) {
        return NMO_OK;
    }

    operation_registry = nmo_context_get_operation_registry(tx->ctx);
    type_registry = nmo_context_get_type_registry(tx->ctx);
    if (!operation_registry || !type_registry) {
        return NMO_ERR_INVALID_STATE;
    }

    family = nmo_operation_registry_get_family(operation_registry, &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u) {
        in1_type = script_edit_resolve_parameter_type_desc(tx, in1_parameter_id);
        if (!in1_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (in2_parameter_id != 0u) {
        in2_type = script_edit_resolve_parameter_type_desc(tx, in2_parameter_id);
        if (!in2_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (out_parameter_id != 0u) {
        out_type = script_edit_resolve_parameter_type_desc(tx, out_parameter_id);
        if (!out_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }

    return script_edit_operation_family_matches(family, type_registry,
                                                in1_type, in2_type, out_type)
        ? NMO_OK
        : NMO_ERR_VALIDATION_FAILED;
}

static nmo_status_t script_edit_disconnect_parameter_internal(
    nmo_script_edit_tx_t *tx,
    nmo_parameterin_state_t *target_state,
    nmo_object_id_t target_parameter_id)
{
    nmo_parameterout_state_t *source_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || !target_state || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (target_state->source_id == 0u) {
        return NMO_OK;
    }

    if (target_state->is_shared == 0u) {
        source_state = script_edit_find_parameterout_state_in_repo(
            nmo_workspace_internal_repository(tx->workspace),
            target_state->source_id,
            NULL);
        if (source_state) {
            rc = nmo_workspace_edit_snapshot_bytes(tx->edit, source_state,
                                                 sizeof(*source_state));
            if (rc != NMO_OK) {
                return rc;
            }
            rc = script_edit_parameterout_remove_destination(
                tx, source_state, target_parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, target_state,
                                         sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    target_state->source_id = 0u;
    target_state->is_shared = 0u;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static bool script_edit_parameter_has_live_connections(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    size_t object_count = 0;

    if (!tx || !tx->session || parameter_id == 0u) {
        return false;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    if (!repo) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t class_id = 0;
        if (!object ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        class_id = nmo_object_get_class_id(object);
        if (class_id == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if (nmo_object_get_id(object) == parameter_id && state->source_id != 0u) {
                return true;
            }
            if (state->source_id == parameter_id) {
                return true;
            }
        } else if (class_id == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if (nmo_object_get_id(object) == parameter_id &&
                state->destination_count > 0u) {
                return true;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                if (state->destination_ids && state->destination_ids[j] == parameter_id) {
                    return true;
                }
            }
        } else if (class_id == NMO_CID_PARAMETEROPERATION) {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if ((state->has_in1 && state->in1_id == parameter_id) ||
                (state->has_in2 && state->in2_id == parameter_id) ||
                (state->has_out && state->out_id == parameter_id)) {
                return true;
            }
        }
    }

    return false;
}

static nmo_status_t script_edit_detach_parameter_references(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_class_id_t class_id = 0;
    size_t object_count = 0;

    if (!tx || !tx->edit || !tx->session || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    class_id = nmo_object_get_class_id(object);

    if (class_id == NMO_CID_PARAMETERIN) {
        nmo_parameterin_state_t *target_state =
            (nmo_parameterin_state_t *)nmo_object_get_state(object);
        nmo_status_t rc = script_edit_disconnect_parameter_internal(
            tx, target_state, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *other = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t other_class = 0;
        nmo_status_t rc = NMO_OK;

        if (!other || nmo_object_get_id(other) == parameter_id ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(other))) {
            continue;
        }

        other_class = nmo_object_get_class_id(other);
        if (other_class == NMO_CID_PARAMETERIN) {
            nmo_parameterin_state_t *state =
                (nmo_parameterin_state_t *)nmo_object_get_state(other);
            if (state && state->source_id == parameter_id) {
                rc = nmo_workspace_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                state->source_id = 0u;
                state->is_shared = 0u;
            }
        } else if (other_class == NMO_CID_PARAMETEROUT) {
            nmo_parameterout_state_t *state =
                (nmo_parameterout_state_t *)nmo_object_get_state(other);
            if (state && script_edit_parameterout_has_destination(state, parameter_id)) {
                rc = nmo_workspace_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                rc = script_edit_parameterout_remove_destination(tx, state,
                                                                 parameter_id);
                if (rc != NMO_OK) {
                    return rc;
                }
            }
        } else if (other_class == NMO_CID_PARAMETEROPERATION) {
            nmo_parameteroperation_state_t *state =
                (nmo_parameteroperation_state_t *)nmo_object_get_state(other);
            if (!state) {
                continue;
            }
            if ((state->has_in1 && state->in1_id == parameter_id) ||
                (state->has_in2 && state->in2_id == parameter_id) ||
                (state->has_out && state->out_id == parameter_id)) {
                rc = nmo_workspace_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                if (state->has_in1 && state->in1_id == parameter_id) {
                    state->has_in1 = 0u;
                    state->in1_id = 0u;
                }
                if (state->has_in2 && state->in2_id == parameter_id) {
                    state->has_in2 = 0u;
                    state->in2_id = 0u;
                }
                if (state->has_out && state->out_id == parameter_id) {
                    state->has_out = 0u;
                    state->out_id = 0u;
                }
            }
        }
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name,
    nmo_object_id_t *out_parameter_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_class_id_t class_id = 0;
    nmo_object_id_t parameter_id = 0;
    nmo_status_t rc = NMO_OK;

    if (out_parameter_id) {
        *out_parameter_id = 0u;
    }
    if (!tx || !tx->edit || owner_behavior_id == 0u ||
        nmo_guid_is_null(type_guid) || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        owner_behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    switch (kind) {
    case NMO_SCRIPT_EDIT_PARAM_IN:
    case NMO_SCRIPT_EDIT_PARAM_SHARED:
        class_id = NMO_CID_PARAMETERIN;
        array = &behavior->in_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_OUT:
        class_id = NMO_CID_PARAMETEROUT;
        array = &behavior->out_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_LOCAL:
        class_id = NMO_CID_PARAMETERLOCAL;
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_parameter_object(tx, class_id, owner_behavior_id, name,
                                             type_guid, NULL, NULL,
                                             &parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (kind == NMO_SCRIPT_EDIT_PARAM_SHARED) {
        nmo_parameterin_state_t *state =
            script_edit_find_parameterin_state_in_repo(
                nmo_workspace_internal_repository(tx->workspace),
                parameter_id,
                NULL);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        state->is_shared = 1u;
    }

    rc = nmo_behavior_ref_array_append(array, parameter_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_behavior_edit_mark_interface(tx->edit, owner_behavior_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                               NMO_WORKSPACE_EDIT_NAMES);

    if (out_parameter_id) {
        *out_parameter_id = parameter_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u || !value_str) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    object = nmo_object_repository_find_by_id(nmo_workspace_internal_repository(tx->workspace),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_parameter_class_holds_value(nmo_object_get_class_id(object))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    state = nmo_parameter_get_mutable_state(object);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_object_edit_set_parameter_value(tx->edit, parameter_id, value_str);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_bytes(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u ||
        (bytes == NULL && byte_count > 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    object = nmo_object_repository_find_by_id(nmo_workspace_internal_repository(tx->workspace),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_parameter_class_holds_value(nmo_object_get_class_id(object))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_object_edit_set_parameter_bytes(tx->edit, parameter_id, bytes,
                                              byte_count);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_ensure_input_parameter_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_in_id,
    nmo_object_id_t *out_source_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *input_object = NULL;
    nmo_parameterin_state_t *input_state = NULL;
    nmo_object_id_t source_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (out_source_parameter_id != NULL) {
        *out_source_parameter_id = 0u;
    }
    if (tx == NULL || tx->edit == NULL || parameter_in_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    input_object = repo ? nmo_object_repository_find_by_id(repo, parameter_in_id) : NULL;
    if (input_object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_object_get_class_id(input_object) != NMO_CID_PARAMETERIN) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    input_state = (nmo_parameterin_state_t *)nmo_object_get_state(input_object);
    if (input_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (input_state->source_id != 0u) {
        if (out_source_parameter_id != NULL) {
            *out_source_parameter_id = input_state->source_id;
        }
        return NMO_OK;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, input_state, sizeof(*input_state));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = script_edit_create_parameter_object(
        tx,
        NMO_CID_PARAMETER,
        input_state->owner_id,
        nmo_object_get_name(input_object),
        input_state->type_guid,
        NULL,
        NULL,
        &source_id);
    if (rc != NMO_OK) {
        return rc;
    }

    input_state->source_id = source_id;
    input_state->is_shared = 0u;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);

    if (out_source_parameter_id != NULL) {
        *out_source_parameter_id = source_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_connect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *source_owner = NULL;
    const nmo_port_owner_t *target_owner = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *source_object = NULL;
    nmo_object_t *target_object = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_parameterout_state_t *source_state = NULL;
    nmo_guid_t source_type = NMO_GUID_NULL;
    nmo_guid_t target_type = NMO_GUID_NULL;
    nmo_object_id_t common_parent_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || source_parameter_id == 0u || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, source_parameter_id, &source_owner) ||
        !script_edit_find_parameter_owner(index, target_parameter_id, &target_owner)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_resolve_common_parent_graph(tx->session,
                                                 source_owner->owner_id,
                                                 target_owner->owner_id,
                                                 &common_parent_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    repo = nmo_workspace_internal_repository(tx->workspace);
    source_object = repo ? nmo_object_repository_find_by_id(repo, source_parameter_id) : NULL;
    target_object = repo ? nmo_object_repository_find_by_id(repo, target_parameter_id) : NULL;
    if (!source_object || !target_object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_is_parameter_reference_class(
            nmo_object_get_class_id(source_object)) ||
        nmo_object_get_class_id(target_object) != NMO_CID_PARAMETERIN) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    source_type = script_edit_parameter_type_guid_from_object(source_object);
    target_type = script_edit_parameter_type_guid_from_object(target_object);
    if (!nmo_guid_equals(source_type, target_type)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    target_state = (nmo_parameterin_state_t *)nmo_object_get_state(target_object);
    if (!target_state) {
        return NMO_ERR_INVALID_STATE;
    }
    if (target_state->source_id != 0u && target_state->source_id != source_parameter_id) {
        rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                       target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, target_state, sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    target_state->source_id = source_parameter_id;
    target_state->is_shared =
        nmo_object_get_class_id(source_object) == NMO_CID_PARAMETERIN ? 1u : 0u;

    source_state = script_edit_find_parameterout_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        source_parameter_id,
        NULL);
    if (source_state) {
        rc = nmo_workspace_edit_snapshot_bytes(tx->edit, source_state,
                                             sizeof(*source_state));
        if (rc != NMO_OK) {
            return rc;
        }
        rc = script_edit_parameterout_append_destination(tx, source_state,
                                                         target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    (void)common_parent_id;
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_disconnect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, target_parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    target_state = script_edit_find_parameterin_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        target_parameter_id,
        NULL);
    if (!target_state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                   target_parameter_id);
    if (rc == NMO_OK) {
        tx->report.rewired_parameters++;
    }
    return rc;
}

NMO_API nmo_status_t nmo_script_edit_remove_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    bool detach)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return NMO_ERR_NOT_FOUND;
    }

    if (!detach && script_edit_parameter_has_live_connections(tx, parameter_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (detach) {
        rc = script_edit_detach_parameter_references(tx, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        owner->owner_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    switch (owner->kind) {
    case NMO_PORT_PARAM_IN:
        array = &behavior->in_parameters;
        break;
    case NMO_PORT_PARAM_OUT:
        array = &behavior->out_parameters;
        break;
    case NMO_PORT_PARAM_LOCAL:
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    script_edit_note_delete(tx);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id,
    nmo_object_id_t *out_operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_operation_family_t *family = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_object_id_t operation_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (out_operation_id) {
        *out_operation_id = 0u;
    }
    if (!tx || !tx->edit || parent_behavior_id == 0u ||
        nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        parent_behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    family = nmo_operation_registry_get_family(
        nmo_context_get_operation_registry(tx->ctx), &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u || in2_parameter_id != 0u || out_parameter_id != 0u) {
        rc = script_edit_require_behavior_index(tx, &index);
        if (rc != NMO_OK) {
            return rc;
        }
        if ((in1_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in1_parameter_id,
                                                            NULL)) ||
            (in2_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in2_parameter_id,
                                                            NULL)) ||
            (out_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            out_parameter_id,
                                                            NULL))) {
            return NMO_ERR_VALIDATION_FAILED;
        }

        rc = script_edit_validate_operation_signature(tx, operation_guid,
                                                      in1_parameter_id,
                                                      in2_parameter_id,
                                                      out_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_runtime_object(tx, NMO_CID_PARAMETEROPERATION,
                                           family->name ? family->name : "Operation",
                                           NMO_GUID_NULL, &operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    state = script_edit_find_operation_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        operation_id,
        NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    state->operation_guid = operation_guid;
    state->owner_id = parent_behavior_id;
    state->has_owner = 1u;
    state->has_in1 = in1_parameter_id != 0u;
    state->in1_id = in1_parameter_id;
    state->has_in2 = in2_parameter_id != 0u;
    state->in2_id = in2_parameter_id;
    state->has_out = out_parameter_id != 0u;
    state->out_id = out_parameter_id;
    state->in1_chunk = NULL;
    state->in2_chunk = NULL;
    state->out_chunk = NULL;

    rc = nmo_behavior_ref_array_append(&behavior->operations, operation_id, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_behavior_edit_mark_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                               NMO_WORKSPACE_EDIT_NAMES);

    if (out_operation_id) {
        *out_operation_id = operation_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rewire_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_object_id_t final_in1_parameter_id = 0u;
    nmo_object_id_t final_in2_parameter_id = 0u;
    nmo_object_id_t final_out_parameter_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u || slot_flags == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    if (((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u &&
         in1_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in1_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u &&
         in2_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in2_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u &&
         out_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        out_parameter_id,
                                                        NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    state = script_edit_find_operation_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        operation_id,
        NULL);
    if (!state) {
        return NMO_ERR_NOT_FOUND;
    }

    final_in1_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u)
            ? in1_parameter_id
            : (state->has_in1 ? state->in1_id : 0u);
    final_in2_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u)
            ? in2_parameter_id
            : (state->has_in2 ? state->in2_id : 0u);
    final_out_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u)
            ? out_parameter_id
            : (state->has_out ? state->out_id : 0u);

    rc = script_edit_validate_operation_signature(tx, state->operation_guid,
                                                  final_in1_parameter_id,
                                                  final_in2_parameter_id,
                                                  final_out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
    if (rc != NMO_OK) {
        return rc;
    }

    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
        state->has_in1 = in1_parameter_id != 0u;
        state->in1_id = in1_parameter_id;
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
        state->has_in2 = in2_parameter_id != 0u;
        state->in2_id = in2_parameter_id;
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
        state->has_out = out_parameter_id != 0u;
        state->out_id = out_parameter_id;
    }

    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_repository(tx->workspace),
        owner->owner_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&behavior->operations, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_behavior_edit_mark_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                               NMO_WORKSPACE_EDIT_REFERENCES |
                               NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH);
    script_edit_note_delete(tx);
    return NMO_OK;
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

    rc = nmo_workspace_edit_snapshot_bytes(tx->edit, parent, sizeof(*parent));
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
    script_edit_note_delete(tx);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->edit) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (tx->deferred_destroy_count > 0u) {
        rc = nmo_workspace_internal_destroy_objects(
            tx->workspace,
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





