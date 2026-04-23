#include "object/nmo_object_summary.h"

#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_system.h"
#include "yyjson.h"

#include <stdio.h>
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
