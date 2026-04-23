#include "object/nmo_object_diff.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

static char *nmo_report_view_strdup(const char *value)
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

static nmo_status_t nmo_report_view_copy_string_field(
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

    copy = nmo_report_view_strdup(value);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }

    *out_value = copy;
    return NMO_OK;
}

static void nmo_diff_object_view_clear(nmo_diff_object_view_t *view)
{
    size_t i = 0u;

    if (view == NULL) {
        return;
    }

    free((void *)view->before_name);
    free((void *)view->after_name);
    free((void *)view->before_path);
    free((void *)view->after_path);
    for (i = 0u; i < view->field_diff_count; ++i) {
        free((void *)view->field_diffs[i].field_name);
        free((void *)view->field_diffs[i].before);
        free((void *)view->field_diffs[i].after);
    }
    free(view->field_diffs);
    memset(view, 0, sizeof(*view));
}

static void nmo_diff_identity_view_clear(nmo_diff_identity_view_t *view)
{
    if (view == NULL) {
        return;
    }

    free((void *)view->name);
    free((void *)view->path);
    memset(view, 0, sizeof(*view));
}

static void nmo_diff_view_clear(nmo_diff_view_t *view)
{
    size_t i = 0u;

    if (view == NULL) {
        return;
    }

    for (i = 0u; i < view->changed_count; ++i) {
        nmo_diff_object_view_clear(&view->changed[i]);
    }
    for (i = 0u; i < view->renamed_count; ++i) {
        nmo_diff_object_view_clear(&view->renamed[i]);
    }
    for (i = 0u; i < view->removed_count; ++i) {
        nmo_diff_identity_view_clear(&view->removed[i]);
    }
    for (i = 0u; i < view->added_count; ++i) {
        nmo_diff_identity_view_clear(&view->added[i]);
    }
    free(view->changed);
    free(view->renamed);
    free(view->removed);
    free(view->added);
    memset(view, 0, sizeof(*view));
}

static nmo_status_t nmo_diff_identity_view_from_object(
    nmo_context_t *ctx,
    const nmo_object_t *object,
    nmo_diff_identity_view_t *out_view)
{
    char path[256];

    if (ctx == NULL || object == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(path, 0, sizeof(path));
    out_view->id = nmo_object_get_id(object);
    out_view->class_id = nmo_object_get_class_id(object);
    out_view->name = nmo_report_view_strdup(nmo_object_get_name(object));
    if (out_view->name == NULL && nmo_object_get_name(object) != NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_object_format_path(path, sizeof(path), ctx, object);
    out_view->path = nmo_report_view_strdup(path);
    if (out_view->path == NULL && path[0] != '\0') {
        return NMO_ERR_NOMEM;
    }

    return NMO_OK;
}

static nmo_status_t nmo_diff_object_view_from_changed(
    nmo_context_t *ctx1,
    nmo_context_t *ctx2,
    const nmo_object_diff_t *changed,
    nmo_diff_object_view_t *out_view)
{
    size_t i = 0u;
    char path[256];

    if (ctx1 == NULL || ctx2 == NULL || changed == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    out_view->before_id = changed->obj1 ? nmo_object_get_id(changed->obj1) : 0u;
    out_view->after_id = changed->obj2 ? nmo_object_get_id(changed->obj2) : 0u;
    out_view->before_class_id = changed->obj1 ? nmo_object_get_class_id(changed->obj1) : 0u;
    out_view->after_class_id = changed->obj2 ? nmo_object_get_class_id(changed->obj2) : 0u;
    out_view->similarity = changed->similarity;
    out_view->field_diff_count = changed->field_diff_count;
    out_view->field_diff_total = changed->field_diff_total;

    out_view->before_name = nmo_report_view_strdup(
        changed->obj1 ? nmo_object_get_name(changed->obj1) : NULL);
    if (out_view->before_name == NULL &&
        changed->obj1 != NULL &&
        nmo_object_get_name(changed->obj1) != NULL) {
        return NMO_ERR_NOMEM;
    }
    out_view->after_name = nmo_report_view_strdup(
        changed->obj2 ? nmo_object_get_name(changed->obj2) : NULL);
    if (out_view->after_name == NULL &&
        changed->obj2 != NULL &&
        nmo_object_get_name(changed->obj2) != NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(path, 0, sizeof(path));
    if (changed->obj1 != NULL) {
        nmo_object_format_path(path, sizeof(path), ctx1, changed->obj1);
        out_view->before_path = nmo_report_view_strdup(path);
        if (out_view->before_path == NULL && path[0] != '\0') {
            return NMO_ERR_NOMEM;
        }
    }
    memset(path, 0, sizeof(path));
    if (changed->obj2 != NULL) {
        nmo_object_format_path(path, sizeof(path), ctx2, changed->obj2);
        out_view->after_path = nmo_report_view_strdup(path);
        if (out_view->after_path == NULL && path[0] != '\0') {
            return NMO_ERR_NOMEM;
        }
    }

    if (out_view->field_diff_count > 0u) {
        out_view->field_diffs = (nmo_diff_field_view_t *)calloc(
            out_view->field_diff_count, sizeof(*out_view->field_diffs));
        if (out_view->field_diffs == NULL) {
            return NMO_ERR_NOMEM;
        }

        for (i = 0u; i < out_view->field_diff_count; ++i) {
            out_view->field_diffs[i].field_name =
                nmo_report_view_strdup(changed->field_diffs[i].field_name);
            out_view->field_diffs[i].before =
                nmo_report_view_strdup(changed->field_diffs[i].before);
            out_view->field_diffs[i].after =
                nmo_report_view_strdup(changed->field_diffs[i].after);
            if ((changed->field_diffs[i].field_name != NULL &&
                 out_view->field_diffs[i].field_name == NULL) ||
                (changed->field_diffs[i].before[0] != '\0' &&
                 out_view->field_diffs[i].before == NULL) ||
                (changed->field_diffs[i].after[0] != '\0' &&
                 out_view->field_diffs[i].after == NULL)) {
                return NMO_ERR_NOMEM;
            }
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_diff_build_view(
    const nmo_document_t *document1,
    const nmo_document_t *document2,
    nmo_diff_view_t *out_view)
{
    nmo_context_t *ctx1 = NULL;
    nmo_context_t *ctx2 = NULL;
    nmo_diff_result_t result;
    nmo_status_t status = NMO_OK;
    size_t i = 0u;

    if (document1 == NULL || document2 == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    ctx1 = nmo_document_get_context(document1);
    ctx2 = nmo_document_get_context(document2);
    if (ctx1 == NULL || ctx2 == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    memset(&result, 0, sizeof(result));
    status = nmo_diff_objects(document1, document2, NULL, &result);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_diff_result_collect_stats(&result, &out_view->stats);
    if (status != NMO_OK) {
        nmo_diff_result_destroy(&result);
        return status;
    }

    out_view->changed_count = result.changed_count;
    out_view->renamed_count = result.renamed_count;
    out_view->removed_count = result.removed_count;
    out_view->added_count = result.added_count;

    if (out_view->changed_count > 0u) {
        out_view->changed = (nmo_diff_object_view_t *)calloc(
            out_view->changed_count, sizeof(*out_view->changed));
        if (out_view->changed == NULL) {
            nmo_diff_result_destroy(&result);
            nmo_diff_view_clear(out_view);
            return NMO_ERR_NOMEM;
        }
        for (i = 0u; i < out_view->changed_count; ++i) {
            status = nmo_diff_object_view_from_changed(
                ctx1, ctx2, &result.changed[i], &out_view->changed[i]);
            if (status != NMO_OK) {
                nmo_diff_result_destroy(&result);
                nmo_diff_view_clear(out_view);
                return status;
            }
        }
    }

    if (out_view->renamed_count > 0u) {
        out_view->renamed = (nmo_diff_object_view_t *)calloc(
            out_view->renamed_count, sizeof(*out_view->renamed));
        if (out_view->renamed == NULL) {
            nmo_diff_result_destroy(&result);
            nmo_diff_view_clear(out_view);
            return NMO_ERR_NOMEM;
        }
        for (i = 0u; i < out_view->renamed_count; ++i) {
            const nmo_rename_diff_t *rename = &result.renamed[i];
            char before_path[256];
            char after_path[256];
            out_view->renamed[i].before_id = rename->obj1 ? nmo_object_get_id(rename->obj1) : 0u;
            out_view->renamed[i].after_id = rename->obj2 ? nmo_object_get_id(rename->obj2) : 0u;
            out_view->renamed[i].before_class_id = rename->obj1 ? nmo_object_get_class_id(rename->obj1) : 0u;
            out_view->renamed[i].after_class_id = rename->obj2 ? nmo_object_get_class_id(rename->obj2) : 0u;
            out_view->renamed[i].similarity = rename->similarity;
            status = nmo_report_view_copy_string_field(rename->before_name,
                                                       &out_view->renamed[i].before_name);
            if (status != NMO_OK) {
                nmo_diff_result_destroy(&result);
                nmo_diff_view_clear(out_view);
                return status;
            }
            status = nmo_report_view_copy_string_field(rename->after_name,
                                                       &out_view->renamed[i].after_name);
            if (status != NMO_OK) {
                nmo_diff_result_destroy(&result);
                nmo_diff_view_clear(out_view);
                return status;
            }
            memset(before_path, 0, sizeof(before_path));
            if (rename->obj1 != NULL) {
                nmo_object_format_path(before_path, sizeof(before_path), ctx1, rename->obj1);
                status = nmo_report_view_copy_string_field(before_path,
                                                           &out_view->renamed[i].before_path);
                if (status != NMO_OK) {
                    nmo_diff_result_destroy(&result);
                    nmo_diff_view_clear(out_view);
                    return status;
                }
            }
            memset(after_path, 0, sizeof(after_path));
            if (rename->obj2 != NULL) {
                nmo_object_format_path(after_path, sizeof(after_path), ctx2, rename->obj2);
                status = nmo_report_view_copy_string_field(after_path,
                                                           &out_view->renamed[i].after_path);
                if (status != NMO_OK) {
                    nmo_diff_result_destroy(&result);
                    nmo_diff_view_clear(out_view);
                    return status;
                }
            }
        }
    }

    if (out_view->removed_count > 0u) {
        out_view->removed = (nmo_diff_identity_view_t *)calloc(
            out_view->removed_count, sizeof(*out_view->removed));
        if (out_view->removed == NULL) {
            nmo_diff_result_destroy(&result);
            nmo_diff_view_clear(out_view);
            return NMO_ERR_NOMEM;
        }
        for (i = 0u; i < out_view->removed_count; ++i) {
            status = nmo_diff_identity_view_from_object(
                ctx1, result.removed[i], &out_view->removed[i]);
            if (status != NMO_OK) {
                nmo_diff_result_destroy(&result);
                nmo_diff_view_clear(out_view);
                return status;
            }
        }
    }

    if (out_view->added_count > 0u) {
        out_view->added = (nmo_diff_identity_view_t *)calloc(
            out_view->added_count, sizeof(*out_view->added));
        if (out_view->added == NULL) {
            nmo_diff_result_destroy(&result);
            nmo_diff_view_clear(out_view);
            return NMO_ERR_NOMEM;
        }
        for (i = 0u; i < out_view->added_count; ++i) {
            status = nmo_diff_identity_view_from_object(
                ctx2, result.added[i], &out_view->added[i]);
            if (status != NMO_OK) {
                nmo_diff_result_destroy(&result);
                nmo_diff_view_clear(out_view);
                return status;
            }
        }
    }

    nmo_diff_result_destroy(&result);
    return NMO_OK;
}

NMO_API void nmo_diff_view_destroy(
    nmo_diff_view_t *view)
{
    nmo_diff_view_clear(view);
}
