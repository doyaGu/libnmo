#include "document/nmo_document_compare.h"

#include <stdlib.h>
#include <string.h>

static char *nmo_comparison_view_strdup(const char *value)
{
    size_t len = 0u;
    char *copy = NULL;

    if (value == NULL) {
        return NULL;
    }

    len = strlen(value);
    copy = (char *)malloc(len + 1u);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, len + 1u);
    return copy;
}

static nmo_status_t nmo_comparison_view_copy_string(
    const char *value,
    const char **out_value)
{
    char *copy = NULL;

    if (out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (value == NULL) {
        *out_value = NULL;
        return NMO_OK;
    }

    copy = nmo_comparison_view_strdup(value);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }

    *out_value = copy;
    return NMO_OK;
}

static const char *nmo_comparison_diff_type_name(uint32_t type_code)
{
    switch ((nmo_diff_type_t)type_code) {
        case NMO_DIFF_OBJECT_COUNT: return "object_count";
        case NMO_DIFF_MANAGER_COUNT: return "manager_count";
        case NMO_DIFF_OBJECT_MISSING: return "object_missing";
        case NMO_DIFF_OBJECT_ORDER: return "object_order";
        case NMO_DIFF_OBJECT_ID: return "object_id";
        case NMO_DIFF_OBJECT_NAME: return "object_name";
        case NMO_DIFF_OBJECT_CLASS_ID: return "object_class_id";
        case NMO_DIFF_OBJECT_REFERENCE_FLAG: return "object_reference_flag";
        case NMO_DIFF_OBJECT_CHUNK_SIZE: return "object_chunk_size";
        case NMO_DIFF_OBJECT_CHUNK_DATA: return "object_chunk_data";
        case NMO_DIFF_MANAGER_MISSING: return "manager_missing";
        case NMO_DIFF_MANAGER_GUID: return "manager_guid";
        case NMO_DIFF_MANAGER_CHUNK_SIZE: return "manager_chunk_size";
        case NMO_DIFF_MANAGER_CHUNK_DATA: return "manager_chunk_data";
        case NMO_DIFF_FILE_VERSION: return "file_version";
        case NMO_DIFF_CK_VERSION: return "ck_version";
        case NMO_DIFF_SHADOW_DATA: return "shadow_data";
        case NMO_DIFF_NONE:
        default:
            return "none";
    }
}

static void nmo_comparison_view_clear(nmo_comparison_view_t *view)
{
    size_t i = 0u;

    if (view == NULL) {
        return;
    }

    for (i = 0u; i < view->diff_count; ++i) {
        free((void *)view->diffs[i].type_name);
        free((void *)view->diffs[i].context);
    }
    free(view->diffs);
    memset(view, 0, sizeof(*view));
}

NMO_API nmo_status_t nmo_comparison_build_view(
    const nmo_session_t *session1,
    const nmo_session_t *session2,
    uint32_t flags,
    nmo_comparison_view_t *out_view)
{
    nmo_comparison_result_t result;
    nmo_status_t status = NMO_OK;
    int i = 0;

    if (session1 == NULL || session2 == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    nmo_comparison_result_init(&result);
    status = nmo_session_compare(
        session1, session2, (nmo_compare_flags_t)flags, &result);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_comparison_result_collect_stats(&result, &out_view->stats);
    if (status != NMO_OK) {
        return status;
    }

    out_view->diff_count = (size_t)result.diff_count;
    if (out_view->diff_count == 0u) {
        return NMO_OK;
    }

    out_view->diffs = (nmo_comparison_diff_view_t *)calloc(
        out_view->diff_count, sizeof(*out_view->diffs));
    if (out_view->diffs == NULL) {
        return NMO_ERR_NOMEM;
    }

    for (i = 0; i < result.diff_count; ++i) {
        out_view->diffs[i].type_code = (uint32_t)result.diffs[i].type;
        status = nmo_comparison_view_copy_string(
            nmo_comparison_diff_type_name((uint32_t)result.diffs[i].type),
            &out_view->diffs[i].type_name);
        if (status != NMO_OK) {
            nmo_comparison_view_clear(out_view);
            return status;
        }
        out_view->diffs[i].object_id = result.diffs[i].object_id;
        status = nmo_comparison_view_copy_string(
            result.diffs[i].context[0] != '\0' ? result.diffs[i].context : NULL,
            &out_view->diffs[i].context);
        if (status != NMO_OK) {
            nmo_comparison_view_clear(out_view);
            return status;
        }
    }

    return NMO_OK;
}

NMO_API void nmo_comparison_view_destroy(
    nmo_comparison_view_t *view)
{
    nmo_comparison_view_clear(view);
}
