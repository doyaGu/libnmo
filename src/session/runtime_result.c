#include "session/nmo_runtime_result.h"

#include "../runtime/runtime_internal.h"
#include "core/nmo_arena.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"

#include <stdlib.h>
#include <string.h>

static void nmo_copy_result_clear(nmo_copy_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

static void nmo_destroy_result_clear(nmo_destroy_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

NMO_API nmo_status_t nmo_session_copy_objects_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_copy_result_t *out_result)
{
    nmo_runtime_report_t report = {0};
    nmo_status_t status = NMO_OK;

    if (session == NULL || ids == NULL || count == 0u || out_result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_copy_result_clear(out_result);
    status = nmo_session_copy_objects(session, ids, count, flags, &report);
    if (status != NMO_OK) {
        return status;
    }

    out_result->copied_count = report.copied_objects;
    out_result->affected_count = report.affected_objects;
    out_result->manager_event_errors = report.manager_event_errors;
    out_result->object_hook_errors = report.object_hook_errors;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_session_destroy_objects_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_destroy_result_t *out_result)
{
    nmo_runtime_report_t report = {0};
    nmo_status_t status = NMO_OK;

    if (session == NULL || ids == NULL || count == 0u || out_result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_destroy_result_clear(out_result);
    status = nmo_session_destroy_objects(session, ids, count, flags, &report);
    if (status != NMO_OK) {
        return status;
    }

    out_result->deleted_count = report.deleted_objects;
    out_result->affected_count = report.affected_objects;
    out_result->manager_event_errors = report.manager_event_errors;
    out_result->object_hook_errors = report.object_hook_errors;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_session_preview_destroy_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_preview_destroy_result_t *out_result)
{
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0u;
    nmo_status_t status = NMO_OK;

    if (session == NULL || ids == NULL || count == 0u || out_result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_preview_destroy_result_destroy(out_result);

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return NMO_ERR_NOMEM;
    }

    status = nmo_session_preview_destroy(session,
                                         ids,
                                         count,
                                         flags,
                                         arena,
                                         &expanded_ids,
                                         &expanded_count);
    if (status == NMO_OK && expanded_count > 0u) {
        out_result->ids = (nmo_object_id_t *)malloc(sizeof(*out_result->ids) * expanded_count);
        if (out_result->ids == NULL) {
            status = NMO_ERR_NOMEM;
        } else {
            memcpy(out_result->ids, expanded_ids, sizeof(*out_result->ids) * expanded_count);
            out_result->count = expanded_count;
        }
    }

    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        nmo_preview_destroy_result_destroy(out_result);
    }
    return status;
}

NMO_API size_t nmo_preview_destroy_result_count(
    const nmo_preview_destroy_result_t *result)
{
    return result != NULL ? result->count : 0u;
}

NMO_API nmo_status_t nmo_preview_destroy_result_at(
    const nmo_preview_destroy_result_t *result,
    size_t index,
    nmo_object_id_t *out_id)
{
    if (result == NULL || out_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (index >= result->count || result->ids == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    *out_id = result->ids[index];
    return NMO_OK;
}

NMO_API void nmo_preview_destroy_result_destroy(
    nmo_preview_destroy_result_t *result)
{
    if (result == NULL) {
        return;
    }

    free(result->ids);
    result->ids = NULL;
    result->count = 0u;
}
