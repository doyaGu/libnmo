/**
 * @file behavior_acceleration.c
 * @brief The behavior acceleration of a session: the behavior index it caches,
 *        the parsed behavior interfaces, and the interface views built on them
 *
 * The session (src/session/session_internal.h) keeps the cached index and the
 * dirty flags; this file builds the index with the behavior layer and leaves the
 * session the function that releases it, so the layers below never call into
 * the behavior layer.
 */

#include "behavior_internal.h"
#include "behavior/nmo_behavior_analyze.h"
#include "core/nmo_logger.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <string.h>

static int nmo_session_build_behavior_index(nmo_session_t *session);
static int nmo_session_ensure_behavior_index(nmo_session_t *session);

nmo_behavior_index_t *nmo_document_internal_behavior_index(
    nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_get_behavior_index(session) : NULL;
}

nmo_status_t nmo_document_internal_ensure_behavior_acceleration(
    nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_ensure_behavior_acceleration(session)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_interface_view_from_behavior(
    nmo_document_t *document,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_interface_view_from_behavior(session, owner_behavior_id, out_view)
        : NMO_ERR_INVALID_STATE;
}

nmo_behavior_index_t *nmo_workspace_internal_behavior_index(
    nmo_workspace_t *workspace)
{
    return workspace != NULL
        ? nmo_document_internal_behavior_index(nmo_workspace_get_document(workspace))
        : NULL;
}

nmo_status_t nmo_workspace_internal_ensure_behavior_acceleration(
    nmo_workspace_t *workspace)
{
    return workspace != NULL
        ? nmo_document_internal_ensure_behavior_acceleration(nmo_workspace_get_document(workspace))
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_interface_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    return workspace != NULL
        ? nmo_document_internal_interface_view_from_behavior(
              nmo_workspace_get_document(workspace),
              owner_behavior_id,
              out_view)
        : NMO_ERR_INVALID_STATE;
}

nmo_behavior_index_t *nmo_session_get_behavior_index(nmo_session_t *session) {
    if (session == NULL) return NULL;
    int index_result = nmo_session_ensure_behavior_index(session);
    if (index_result != NMO_OK) {
        return NULL;
    }
    return session->behavior_index;
}

static int nmo_session_build_behavior_index(nmo_session_t *session) {
    if (session == NULL || session->context == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (session->behavior_index != NULL) {
        return NMO_OK;
    }

    session->behavior_index = nmo_behavior_index_create(session->arena);
    session->behavior_index_destroy = nmo_behavior_index_destroy;
    if (session->behavior_index != NULL) {
        nmo_document_t *document = NULL;
        nmo_workspace_t *workspace = NULL;
        int build_result = NMO_OK;

        build_result = nmo_session_borrow_document(session, &document);
        if (build_result != NMO_OK) {
            nmo_behavior_index_destroy(session->behavior_index);
            session->behavior_index = NULL;
            return build_result;
        }
        build_result = nmo_workspace_create(session->context, document, &workspace);
        if (build_result == NMO_OK) {
            build_result = nmo_behavior_index_build(session->behavior_index, workspace);
        }
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        if (build_result != NMO_OK) {
            nmo_behavior_index_destroy(session->behavior_index);
            session->behavior_index = NULL;
            return build_result;
        }
        return NMO_OK;
    }
    return NMO_ERR_NOMEM;
}

static int nmo_session_ensure_behavior_index(nmo_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->behavior_accel_dirty && session->behavior_index != NULL) {
        nmo_behavior_index_destroy(session->behavior_index);
        session->behavior_index = NULL;
    }

    if (session->behavior_accel_dirty || session->behavior_index == NULL) {
        int build_result = nmo_session_build_behavior_index(session);
        if (build_result != NMO_OK) {
            session->behavior_accel_built = 0;
            session->behavior_accel_dirty = 1;
            return build_result;
        }
        session->behavior_accel_dirty = 0;
    }

    return NMO_OK;
}

nmo_status_t nmo_session_ensure_behavior_acceleration(nmo_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->behavior_accel_built &&
        !session->behavior_accel_dirty &&
        !session->behavior_interface_dirty) {
        return NMO_OK;
    }

    int index_result = nmo_session_ensure_behavior_index(session);
    if (index_result != NMO_OK) {
        return index_result;
    }

    if ((session->behavior_interface_dirty || !session->behavior_accel_built) &&
        session->repository != NULL) {
        nmo_logger_t *logger = session->context
            ? nmo_context_get_logger(session->context)
            : NULL;
        nmo_behavior_interface_parse_stats_t stats;
        memset(&stats, 0, sizeof(stats));
        nmo_status_t parse_result = nmo_behavior_parse_all_interfaces_ex(
            session->repository, logger, &stats);
        session->behavior_interface_parse_stats = stats;
        session->behavior_interface_parse_attempted = 1;
        if (parse_result != NMO_OK) {
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Behavior interface parsing reported errors; first status=%d object=%u file_id=%u offset=%zu/%zu",
                        parse_result,
                        stats.first_error_object_id,
                        stats.first_error_file_id,
                        stats.first_error_reader_offset,
                        stats.first_error_chunk_dwords);
            }
        }
        session->behavior_interface_dirty = 0;
    }

    session->behavior_accel_built = 1;
    session->behavior_accel_dirty = 0;
    return NMO_OK;
}
