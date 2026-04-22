#include "app/nmo_report_view.h"

#include "app/nmo_comparison.h"
#include "app/nmo_object_diff.h"
#include "app/nmo_object_summary.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "yyjson.h"

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

static void nmo_report_view_free_string_array(const char **items, size_t count)
{
    size_t i = 0u;

    if (items == NULL) {
        return;
    }

    for (i = 0u; i < count; ++i) {
        free((void *)items[i]);
    }
    free((void *)items);
}

static void nmo_object_summary_field_view_clear(nmo_object_summary_field_view_t *field)
{
    if (field == NULL) {
        return;
    }

    free((void *)field->name);
    free((void *)field->kind);
    free((void *)field->value_str);
    free((void *)field->ref_name);
    nmo_report_view_free_string_array(field->items, field->item_count);
    memset(field, 0, sizeof(*field));
}

static void nmo_object_summary_view_clear(nmo_object_summary_view_t *view)
{
    size_t i = 0u;

    if (view == NULL) {
        return;
    }

    for (i = 0u; i < view->field_count; ++i) {
        nmo_object_summary_field_view_clear(&view->fields[i]);
    }
    free(view->fields);
    memset(view, 0, sizeof(*view));
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

static nmo_status_t nmo_report_view_format_json_value(
    yyjson_mut_val *value,
    const char **out_string)
{
    char buffer[64];

    if (out_string == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (value == NULL || yyjson_mut_is_null(value)) {
        *out_string = NULL;
        return NMO_OK;
    }
    if (yyjson_mut_is_str(value)) {
        return nmo_report_view_copy_string_field(yyjson_mut_get_str(value), out_string);
    }
    if (yyjson_mut_is_bool(value)) {
        return nmo_report_view_copy_string_field(
            yyjson_mut_get_bool(value) ? "true" : "false",
            out_string);
    }
    if (yyjson_mut_is_uint(value)) {
        snprintf(buffer, sizeof(buffer), "%llu",
                 (unsigned long long)yyjson_mut_get_uint(value));
        return nmo_report_view_copy_string_field(buffer, out_string);
    }
    if (yyjson_mut_is_sint(value)) {
        snprintf(buffer, sizeof(buffer), "%lld",
                 (long long)yyjson_mut_get_sint(value));
        return nmo_report_view_copy_string_field(buffer, out_string);
    }
    if (yyjson_mut_is_real(value)) {
        snprintf(buffer, sizeof(buffer), "%.17g", yyjson_mut_get_real(value));
        return nmo_report_view_copy_string_field(buffer, out_string);
    }

    return nmo_report_view_copy_string_field("<complex>", out_string);
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

static nmo_status_t nmo_object_summary_snapshot_fields(
    yyjson_mut_val *root,
    nmo_object_summary_view_t *out_view)
{
    yyjson_mut_val *fields = NULL;
    yyjson_mut_arr_iter iter;
    yyjson_mut_val *field = NULL;
    size_t index = 0u;

    if (root == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    fields = yyjson_mut_obj_get(root, "fields");
    if (fields == NULL || !yyjson_mut_is_arr(fields)) {
        return NMO_OK;
    }

    out_view->field_count = yyjson_mut_arr_size(fields);
    if (out_view->field_count == 0u) {
        return NMO_OK;
    }

    out_view->fields = (nmo_object_summary_field_view_t *)calloc(
        out_view->field_count, sizeof(*out_view->fields));
    if (out_view->fields == NULL) {
        return NMO_ERR_NOMEM;
    }

    yyjson_mut_arr_iter_init(fields, &iter);
    while ((field = yyjson_mut_arr_iter_next(&iter)) != NULL) {
        nmo_object_summary_field_view_t *out_field = &out_view->fields[index];
        yyjson_mut_val *items = NULL;
        nmo_status_t status = NMO_OK;
        size_t item_index = 0u;

        status = nmo_report_view_copy_string_field(
            yyjson_mut_get_str(yyjson_mut_obj_get(field, "name")),
            &out_field->name);
        if (status != NMO_OK) {
            return status;
        }
        status = nmo_report_view_copy_string_field(
            yyjson_mut_get_str(yyjson_mut_obj_get(field, "kind")),
            &out_field->kind);
        if (status != NMO_OK) {
            return status;
        }
        status = nmo_report_view_copy_string_field(
            yyjson_mut_get_str(yyjson_mut_obj_get(field, "value_str")),
            &out_field->value_str);
        if (status != NMO_OK) {
            return status;
        }
        status = nmo_report_view_copy_string_field(
            yyjson_mut_get_str(yyjson_mut_obj_get(field, "ref_name")),
            &out_field->ref_name);
        if (status != NMO_OK) {
            return status;
        }

        {
            yyjson_mut_val *count = yyjson_mut_obj_get(field, "count");
            if (count != NULL && yyjson_mut_is_uint(count)) {
                out_field->has_count = true;
                out_field->count = (size_t)yyjson_mut_get_uint(count);
            }
        }

        items = yyjson_mut_obj_get(field, "items");
        if (items != NULL && yyjson_mut_is_arr(items)) {
            out_field->item_count = yyjson_mut_arr_size(items);
            if (out_field->item_count > 0u) {
                yyjson_mut_arr_iter item_iter;
                yyjson_mut_val *item = NULL;

                out_field->items = (const char **)calloc(
                    out_field->item_count, sizeof(*out_field->items));
                if (out_field->items == NULL) {
                    return NMO_ERR_NOMEM;
                }

                yyjson_mut_arr_iter_init(items, &item_iter);
                while ((item = yyjson_mut_arr_iter_next(&item_iter)) != NULL) {
                    status = nmo_report_view_format_json_value(
                        item, &out_field->items[item_index]);
                    if (status != NMO_OK) {
                        return status;
                    }
                    item_index++;
                }
            }
        }

        index++;
    }

    return NMO_OK;
}

static const nmo_type_descriptor_t *nmo_report_view_find_field_owner(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor,
    const char *field_name,
    const nmo_type_field_t **out_field)
{
    size_t i = 0u;
    const nmo_type_descriptor_t *base = NULL;

    if (out_field != NULL) {
        *out_field = NULL;
    }
    if (registry == NULL || descriptor == NULL || field_name == NULL || out_field == NULL) {
        return NULL;
    }

    for (i = 0u; i < descriptor->field_count; ++i) {
        if (descriptor->fields[i].name != NULL &&
            strcmp(descriptor->fields[i].name, field_name) == 0) {
            *out_field = &descriptor->fields[i];
            return descriptor;
        }
    }

    if (!nmo_guid_is_null(descriptor->base_type)) {
        base = nmo_type_registry_find_by_guid(registry, descriptor->base_type);
    } else if (descriptor->base_type_id != NMO_TYPE_ID_INVALID) {
        base = nmo_type_registry_get_by_id(registry, descriptor->base_type_id);
    }
    if (base == NULL) {
        return NULL;
    }

    return nmo_report_view_find_field_owner(registry, base, field_name, out_field);
}

static nmo_status_t nmo_object_summary_fill_ref_names(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_object_t *object,
    nmo_object_summary_view_t *out_view)
{
    const nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *descriptor = NULL;
    size_t i = 0u;

    if (ctx == NULL || session == NULL || object == NULL || out_view == NULL) {
        return NMO_OK;
    }

    registry = nmo_context_get_type_registry(ctx);
    if (registry == NULL) {
        return NMO_OK;
    }

    if (!nmo_guid_is_null(nmo_object_get_type_guid(object))) {
        descriptor = nmo_type_registry_find_by_guid(
            registry, nmo_object_get_type_guid(object));
    } else {
        descriptor = nmo_type_registry_find_by_class_id_inherited(
            registry, nmo_object_get_class_id(object));
    }
    if (descriptor == NULL || nmo_object_get_state((nmo_object_t *)object) == NULL) {
        return NMO_OK;
    }

    for (i = 0u; i < out_view->field_count; ++i) {
        const nmo_type_field_t *field = NULL;
        const nmo_type_descriptor_t *owner = NULL;
        const uint8_t *field_ptr = NULL;
        nmo_object_id_t id = 0u;
        nmo_object_t *target = NULL;
        const char *name = NULL;

        if (out_view->fields[i].ref_name != NULL ||
            out_view->fields[i].name == NULL) {
            continue;
        }

        owner = nmo_report_view_find_field_owner(
            registry, descriptor, out_view->fields[i].name, &field);
        if (owner == NULL || field == NULL) {
            continue;
        }
        if ((field->flags & (NMO_FIELD_POINTER | NMO_FIELD_REPEATED)) != 0u) {
            continue;
        }
        if ((field->flags & NMO_FIELD_REFERENCE) == 0u &&
            field->semantic != NMO_SEMANTIC_OBJECT_REF &&
            !nmo_guid_equals(field->type_guid, CKPGUID_ID)) {
            continue;
        }
        if (field->size < sizeof(nmo_object_id_t) ||
            owner->size < field->offset + sizeof(nmo_object_id_t)) {
            continue;
        }

        field_ptr = (const uint8_t *)nmo_object_get_state((nmo_object_t *)object) + field->offset;
        memcpy(&id, field_ptr, sizeof(id));
        if (id == 0u) {
            continue;
        }

        target = nmo_object_repository_find_by_id(nmo_session_get_repository(session), id);
        if (target == NULL) {
            continue;
        }

        name = nmo_object_get_name(target);
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        if (nmo_report_view_copy_string_field(name, &out_view->fields[i].ref_name) != NMO_OK) {
            return NMO_ERR_NOMEM;
        }
    }

    return NMO_OK;
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

NMO_API nmo_status_t nmo_object_summary_build_view(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_object_t *object,
    nmo_object_summary_view_t *out_view)
{
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root = NULL;
    nmo_summary_output_t output;
    nmo_status_t status = NMO_OK;

    if (ctx == NULL || object == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    status = nmo_object_summary_collect_stats(ctx, object, &out_view->stats);
    if (status != NMO_OK) {
        return status;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NMO_ERR_NOMEM;
    }

    root = yyjson_mut_obj(doc);
    if (root == NULL) {
        yyjson_mut_doc_free(doc);
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_doc_set_root(doc, root);

    memset(&output, 0, sizeof(output));
    output.json_doc = doc;
    output.json_data = root;
    output.is_json = true;
    output.ctx = ctx;
    output.session = session;

    if (!nmo_object_summary((nmo_object_t *)object, &output)) {
        yyjson_mut_doc_free(doc);
        return NMO_ERR_INVALID_STATE;
    }

    status = nmo_object_summary_snapshot_fields(root, out_view);
    if (status == NMO_OK) {
        status = nmo_object_summary_fill_ref_names(ctx, session, object, out_view);
    }
    yyjson_mut_doc_free(doc);
    if (status != NMO_OK) {
        nmo_object_summary_view_clear(out_view);
    }

    return status;
}

NMO_API void nmo_object_summary_view_destroy(
    nmo_object_summary_view_t *view)
{
    nmo_object_summary_view_clear(view);
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
        status = nmo_report_view_copy_string_field(
            nmo_comparison_diff_type_name((uint32_t)result.diffs[i].type),
            &out_view->diffs[i].type_name);
        if (status != NMO_OK) {
            nmo_comparison_view_clear(out_view);
            return status;
        }
        out_view->diffs[i].object_id = result.diffs[i].object_id;
        status = nmo_report_view_copy_string_field(
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

NMO_API nmo_status_t nmo_diff_build_view(
    nmo_session_t *session1,
    nmo_session_t *session2,
    nmo_diff_view_t *out_view)
{
    nmo_context_t *ctx1 = NULL;
    nmo_context_t *ctx2 = NULL;
    nmo_diff_result_t result;
    nmo_status_t status = NMO_OK;
    size_t i = 0u;

    if (session1 == NULL || session2 == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    ctx1 = nmo_session_get_context(session1);
    ctx2 = nmo_session_get_context(session2);
    if (ctx1 == NULL || ctx2 == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    memset(&result, 0, sizeof(result));
    status = nmo_diff_objects(ctx1, session1, ctx2, session2, NULL, &result);
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
