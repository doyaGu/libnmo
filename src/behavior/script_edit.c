#include "behavior/nmo_script_edit.h"

#include "behavior/nmo_behavior_index.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "format/nmo_object.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct nmo_script_edit_tx {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_session_edit_t *edit;
    nmo_script_edit_report_t report;
    uint32_t session_edit_flags;
    bool finished;
};

static void script_edit_tx_destroy(nmo_script_edit_tx_t *tx)
{
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

static nmo_status_t validate_behavior_link_owners(
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
        if (class_id == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            if (!state) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            if (state->source_id != 0 &&
                (!nmo_object_repository_find_by_id(repo, state->source_id) ||
                 !nmo_behavior_index_find(index, state->source_id))) {
                return NMO_ERR_VALIDATION_FAILED;
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
                if (destination_id == 0) {
                    continue;
                }
                if (!nmo_object_repository_find_by_id(repo, destination_id) ||
                    !nmo_behavior_index_find(index, destination_id)) {
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

        rc = validate_behavior_link_owners(repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }

        rc = validate_parameter_links(repo, index);
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

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->edit) {
        return NMO_ERR_INVALID_ARGUMENT;
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
