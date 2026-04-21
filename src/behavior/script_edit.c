#include "behavior/nmo_script_edit.h"

#include "behavior/nmo_behavior_index.h"
#include "behavior/nmo_bb_registry.h"
#include "session/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
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
#include "format/nmo_object.h"
#include "core/nmo_array.h"

#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct nmo_script_edit_tx {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_session_edit_t *edit;
    nmo_script_edit_report_t report;
    uint32_t session_edit_flags;
    nmo_object_id_t *deferred_destroy_ids;
    size_t deferred_destroy_count;
    size_t deferred_destroy_capacity;
    bool finished;
};

static void script_edit_tx_destroy(nmo_script_edit_tx_t *tx)
{
    if (tx) {
        free(tx->deferred_destroy_ids);
    }
    free(tx);
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

static void script_edit_note_create(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.created_objects < SIZE_MAX) {
        tx->report.created_objects++;
    }
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

static nmo_status_t script_edit_track_created(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_session_edit_track_created_object(tx->edit, object_id);
    if (rc == NMO_OK) {
        script_edit_note_create(tx);
    }
    return rc;
}

static nmo_behavior_state_t *script_edit_find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || behavior_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_behaviorio_state_t *script_edit_find_io_state(
    nmo_session_t *session,
    nmo_object_id_t io_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || io_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, io_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORIO) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorio_state_t *)nmo_object_get_state(object);
}

static nmo_behaviorlink_state_t *script_edit_find_link_state(
    nmo_session_t *session,
    nmo_object_id_t link_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || link_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, link_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorlink_state_t *)nmo_object_get_state(object);
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

    rc = nmo_runtime_kernel_execute(tx->session, &request, &report);
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

    state = script_edit_find_io_state(tx->session, *out_io_id, NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    state->old_flags =
        (kind == NMO_SCRIPT_EDIT_IO_INPUT ? CK_BEHAVIORIO_IN
                                          : CK_BEHAVIORIO_OUT) |
        CK_BEHAVIORIO_ACTIVE;
    state->has_flags = true;
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_status_t script_edit_create_parameter_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *parameter = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, class_id, name, NMO_GUID_NULL, out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    repo = nmo_session_get_repository(tx->session);
    object = repo ? nmo_object_repository_find_by_id(repo, *out_parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_INVALID_STATE;
    }

    parameter = nmo_parameter_get_mutable_state(object);
    if (!parameter) {
        return NMO_ERR_INVALID_STATE;
    }
    parameter->type_guid = type_guid;

    if (class_id == NMO_CID_PARAMETERIN) {
        ((nmo_parameterin_state_t *)nmo_object_get_state(object))->owner_id =
            owner_id;
    } else if (class_id == NMO_CID_PARAMETEROUT) {
        ((nmo_parameterout_state_t *)nmo_object_get_state(object))->owner_id =
            owner_id;
    } else if (class_id == NMO_CID_PARAMETERLOCAL) {
        ((nmo_parameterlocal_state_t *)nmo_object_get_state(object))->owner_id =
            owner_id;
    }

    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_status_t script_edit_require_behavior_index(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_index_t **out_index)
{
    nmo_status_t rc = NMO_OK;
    const nmo_behavior_index_t *index = NULL;

    if (!tx || !tx->session || !out_index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if ((tx->session_edit_flags &
         (NMO_SESSION_EDIT_REFERENCES |
          NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
          NMO_SESSION_EDIT_NAMES |
          NMO_SESSION_EDIT_RESOURCES)) != 0u) {
        rc = nmo_session_apply_edit_flags(
            tx->session,
            tx->session_edit_flags &
                (NMO_SESSION_EDIT_REFERENCES |
                 NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                 NMO_SESSION_EDIT_NAMES |
                 NMO_SESSION_EDIT_RESOURCES));
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_session_ensure_behavior_acceleration(tx->session);
    if (rc != NMO_OK) {
        return rc;
    }

    index = nmo_session_get_behavior_index(tx->session);
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

static bool script_edit_io_is_linked(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_id_t io_id)
{
    nmo_behavior_state_t *state = NULL;
    nmo_object_id_t *link_ids = NULL;

    state = script_edit_find_behavior_state(session, behavior_id, NULL);
    if (!state || !state->sub_behavior_links.data) {
        return false;
    }

    link_ids = (nmo_object_id_t *)state->sub_behavior_links.data;
    for (size_t i = 0; i < state->sub_behavior_links.count; ++i) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *link_obj =
            repo ? nmo_object_repository_find_by_id(repo, link_ids[i]) : NULL;
        const nmo_behaviorlink_state_t *link_state =
            link_obj ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                     : NULL;
        if (!link_state) {
            continue;
        }
        if (link_state->in_io_id == io_id || link_state->out_io_id == io_id) {
            return true;
        }
    }
    return false;
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
    return nmo_array_find(&parent->sub_behaviors, &behavior_id, NULL) != 0;
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

static bool script_edit_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
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
            !nmo_behavior_index_find(index, state->in_io_id) ||
            !nmo_behavior_index_find(index, state->out_io_id)) {
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

NMO_API nmo_status_t nmo_script_edit_begin(nmo_context_t *ctx,
                                           nmo_session_t *session,
                                           const char *label,
                                           nmo_script_edit_tx_t **out_tx)
{
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    if (!ctx || !session || !out_tx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_tx = NULL;
    tx = (nmo_script_edit_tx_t *)calloc(1u, sizeof(*tx));
    if (!tx) {
        return NMO_ERR_NOMEM;
    }

    tx->ctx = ctx;
    tx->session = session;
    rc = nmo_session_edit_begin(session, label, &tx->edit);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }

    *out_tx = tx;
    return NMO_OK;
}

NMO_API nmo_session_edit_t *nmo_script_edit_session_edit(
    nmo_script_edit_tx_t *tx)
{
    if (!tx || tx->finished) {
        return NULL;
    }
    return tx->edit;
}

NMO_API void nmo_script_edit_mark(nmo_script_edit_tx_t *tx,
                                  uint32_t session_edit_flags)
{
    if (!tx || tx->finished) {
        return;
    }

    tx->session_edit_flags |= session_edit_flags;
    if (tx->edit) {
        nmo_session_edit_mark(tx->edit, session_edit_flags);
    }
    if (session_edit_flags != 0u) {
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
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_REFERENCES |
        NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
        NMO_SESSION_EDIT_NAMES |
        NMO_SESSION_EDIT_RESOURCES;
    nmo_object_repository_t *repo = NULL;
    nmo_behavior_index_t *index = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_session_behavior_interface_diagnostics_t interface_diag;
    size_t broken_ref_count = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->session || !tx->ctx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(&tx->report, 0, sizeof(tx->report));
    repo = nmo_session_get_repository(tx->session);
    if (!repo) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    /* The low-level session edit API tracks its own flags internally, but it
     * does not expose them. Validation therefore uses a conservative cache
     * refresh so mixed direct and helper mutations are checked consistently.
     */
    rc = nmo_session_apply_edit_flags(tx->session,
                                      tx->session_edit_flags |
                                          conservative_refresh_flags);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u &&
        nmo_session_is_partial_load(tx->session)) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_ensure_behavior_acceleration(tx->session);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        ref_graph = nmo_session_get_ref_graph(tx->session);
        if (!ref_graph) {
            script_edit_note_error(tx);
            return NMO_ERR_INVALID_STATE;
        }
        rc = nmo_ref_graph_validate(ref_graph, NULL, &broken_ref_count);
        if (rc != NMO_OK || broken_ref_count != 0u) {
            script_edit_note_error(tx);
            return rc != NMO_OK ? rc : NMO_ERR_VALIDATION_FAILED;
        }
    }

    if ((validation_flags &
         (NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
          NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX)) != 0u) {
        index = nmo_session_get_behavior_index(tx->session);
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
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        memset(&interface_diag, 0, sizeof(interface_diag));
        nmo_session_get_behavior_interface_diagnostics(tx->session,
                                                       &interface_diag);
        if (interface_diag.attempted && interface_diag.status != NMO_OK) {
            script_edit_note_error(tx);
            return interface_diag.status;
        }
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
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_bb_proto_t *proto = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t node_id = 0;

    if (!tx || !tx->edit || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state(tx->session,
                                                   parent_behavior_id,
                                                   NULL);
    if (!parent_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent_state,
                                         sizeof(*parent_state));
    if (rc != NMO_OK) {
        return rc;
    }

    proto = nmo_bb_registry_find(nmo_context_get_bb_registry(tx->ctx), bb_guid);
    rc = script_edit_create_runtime_object(
        tx,
        NMO_CID_BEHAVIOR,
        (name && name[0] != '\0') ? name : (proto ? proto->name : "Behavior"),
        NMO_GUID_NULL,
        &node_id);
    if (rc != NMO_OK) {
        return rc;
    }

    node_state = script_edit_find_behavior_state(tx->session, node_id, NULL);
    if (!node_state) {
        return NMO_ERR_INVALID_STATE;
    }

    node_state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    node_state->flags &= ~CKBEHAVIOR_SCRIPT;
    if (proto) {
        node_state->flags |= proto->behavior_flags;
        node_state->compatible_class_id = proto->compatible_class_id;
        node_state->block_version = proto->version != 0u ? proto->version : 65536u;
    } else {
        node_state->block_version = 65536u;
    }
    node_state->block_guid = bb_guid;
    node_state->priority = 0;
    node_state->owner_id = parent_behavior_id;

    if (proto) {
        for (uint32_t i = 0; i < proto->input_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->inputs[i],
                                              NMO_SCRIPT_EDIT_IO_INPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->inputs, &io_id);
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
            rc = nmo_array_append(&node_state->outputs, &io_id);
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
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->in_parameters, &parameter_id);
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
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->out_parameters, &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->local_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->local_params[i].name,
                proto->local_params[i].type_guid,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->local_parameters, &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    script_edit_update_behavior_save_flags(node_state);
    rc = nmo_array_append(&parent_state->sub_behaviors, &node_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
            NMO_SESSION_EDIT_NAMES);

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

    parent_state = script_edit_find_behavior_state(tx->session,
                                                   parent_behavior_id,
                                                   NULL);
    node_state = script_edit_find_behavior_state(tx->session, node_id, NULL);
    if (!parent_state || !node_state) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_array_find(&parent_state->sub_behaviors, &node_id, &node_index) == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_preview_destroy(tx->session, &node_id, 1, delete_flags,
                                     nmo_session_get_arena(tx->session),
                                     &expanded_ids, &expanded_count);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent_state,
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

    /* Fresh BB nodes also own their direct ports and parameters. */
    nmo_array_t *owned_arrays[] = {
        &node_state->inputs,
        &node_state->outputs,
        &node_state->in_parameters,
        &node_state->out_parameters,
        &node_state->local_parameters,
    };
    for (size_t i = 0; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        nmo_object_id_t *ids =
            owned_arrays[i]->data ? (nmo_object_id_t *)owned_arrays[i]->data : NULL;
        for (size_t j = 0; ids && j < owned_arrays[i]->count; ++j) {
            rc = script_edit_append_deferred_destroy(tx, ids[j]);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
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

    behavior = script_edit_find_behavior_state(tx->session, behavior_id, NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_io_object(tx, name, kind, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    array = kind == NMO_SCRIPT_EDIT_IO_INPUT ? &behavior->inputs
                                             : &behavior->outputs;
    rc = nmo_array_append(array, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    nmo_session_edit_mark_behavior_interface(tx->edit, behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
            NMO_SESSION_EDIT_NAMES);

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

    rc = nmo_session_edit_rename_object(tx->edit, io_id, name);
    if (rc != NMO_OK) {
        return rc;
    }
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
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

    (void)detach_links;

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
    if (!detach_links && script_edit_io_is_linked(tx->session, owner->owner_id, io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    behavior = script_edit_find_behavior_state(tx->session, owner->owner_id, NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    array = owner->kind == NMO_PORT_IO_IN ? &behavior->inputs : &behavior->outputs;
    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_note_delete(tx);
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
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
    if (!script_edit_find_behavior_state(tx->session, parent_behavior_id, NULL)) {
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

    rc = nmo_session_edit_add_behavior_link(
        tx->edit,
        parent_behavior_id,
        to_io_id,
        from_io_id,
        (int16_t)activation_delay,
        &link_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, parent_behavior_id);
    tx->report.created_objects++;
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

    link_state = script_edit_find_link_state(tx->session, link_id, NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
     * and link out_io_id is the target IO.
     */
    if (from_io_id != 0) {
        link_state->in_io_id = from_io_id;
    }
    if (to_io_id != 0) {
        link_state->out_io_id = to_io_id;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
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

    link_state = script_edit_find_link_state(tx->session, link_id, NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    link_state->activation_delay = (int16_t)activation_delay;
    link_state->initial_activation_delay = (int16_t)activation_delay;
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
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

    parent = script_edit_find_behavior_state(tx->session, parent_behavior_id, NULL);
    if (!parent) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_array_find(&parent->sub_behavior_links, &link_id, &link_index) == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent, sizeof(*parent));
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

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
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
        rc = nmo_session_destroy_objects(tx->session,
                                         tx->deferred_destroy_ids,
                                         tx->deferred_destroy_count,
                                         NMO_RUNTIME_REQUEST_STRICT,
                                         NULL);
        if (rc != NMO_OK) {
            nmo_session_edit_rollback(tx->edit);
            tx->edit = NULL;
            tx->finished = true;
            script_edit_tx_destroy(tx);
            return rc;
        }
    }

    rc = nmo_session_edit_commit(tx->edit);
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
        nmo_session_edit_rollback(tx->edit);
        tx->edit = NULL;
        tx->finished = true;
    }
    script_edit_tx_destroy(tx);
}
