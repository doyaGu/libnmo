#include "app/nmo_report_result.h"

#include "app/nmo_comparison.h"
#include "app/nmo_object_diff.h"
#include "app/nmo_object_summary.h"
#include "format/nmo_object.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_view.h"

#include <string.h>

typedef struct nmo_report_field_count_ctx {
    const nmo_type_descriptor_t *owner_type;
    size_t total_fields;
    size_t array_fields;
    size_t reference_fields;
    size_t optional_fields;
    size_t object_ref_fields;
} nmo_report_field_count_ctx_t;

#define NMO_REPORT_RESULT_MAX_HIERARCHY_DEPTH 16

typedef struct nmo_report_hierarchy_level {
    const nmo_type_descriptor_t *type;
    const void *state;
} nmo_report_hierarchy_level_t;

static void nmo_object_summary_stats_clear(nmo_object_summary_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

static void nmo_comparison_result_stats_clear(nmo_comparison_result_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

static void nmo_diff_result_stats_clear(nmo_diff_result_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

static bool nmo_report_result_count_field(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    nmo_report_field_count_ctx_t *ctx =
        (nmo_report_field_count_ctx_t *)user_data;
    (void)field_ptr;

    if (ctx == NULL || field == NULL || field->name == NULL) {
        return true;
    }

    if (!(field->flags & NMO_FIELD_REPEATED) &&
        ctx->owner_type != NULL &&
        !nmo_guid_is_null(ctx->owner_type->base_type))
    {
        nmo_guid_t parent_guid = ctx->owner_type->base_type;
        if (nmo_guid_equals(field->type_guid, parent_guid)) {
            return true;
        }
        if (nmo_guid_equals(field->type_guid, CKPGUID_NONE)) {
            if (strcmp(field->name, "base") == 0 ||
                strcmp(field->name, "entity") == 0 ||
                strcmp(field->name, "beobject") == 0 ||
                strcmp(field->name, "object") == 0) {
                return true;
            }
        }
    }

    ctx->total_fields++;
    if (field->flags & NMO_FIELD_REPEATED) {
        ctx->array_fields++;
    }
    if (field->flags & NMO_FIELD_REFERENCE) {
        ctx->reference_fields++;
    }
    if (field->flags & NMO_FIELD_OPTIONAL) {
        ctx->optional_fields++;
    }
    if ((field->flags & NMO_FIELD_REFERENCE) ||
        field->semantic == NMO_SEMANTIC_OBJECT_REF ||
        nmo_guid_equals(field->type_guid, CKPGUID_ID)) {
        ctx->object_ref_fields++;
    }
    return true;
}

static bool nmo_report_result_is_base_embedding(
    const nmo_type_descriptor_t *owner_type,
    const nmo_type_field_t *field)
{
    if (owner_type == NULL || field == NULL) {
        return false;
    }
    if (field->flags & NMO_FIELD_REPEATED) {
        return false;
    }
    if (nmo_guid_is_null(owner_type->base_type)) {
        return false;
    }

    if (nmo_guid_equals(field->type_guid, owner_type->base_type)) {
        return true;
    }

    if (nmo_guid_equals(field->type_guid, CKPGUID_NONE)) {
        if (strcmp(field->name, "base") == 0 ||
            strcmp(field->name, "entity") == 0 ||
            strcmp(field->name, "beobject") == 0 ||
            strcmp(field->name, "object") == 0) {
            return true;
        }
    }

    return false;
}

static size_t nmo_report_result_build_hierarchy(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *root_type,
    const void *root_state,
    nmo_report_hierarchy_level_t *levels,
    size_t max_levels)
{
    nmo_report_hierarchy_level_t stack[NMO_REPORT_RESULT_MAX_HIERARCHY_DEPTH];
    const nmo_type_descriptor_t *cur_type = root_type;
    const void *cur_state = root_state;
    size_t depth = 0;

    if (registry == NULL || root_type == NULL || root_state == NULL ||
        levels == NULL || max_levels == 0) {
        return 0;
    }

    while (cur_type != NULL && depth < NMO_REPORT_RESULT_MAX_HIERARCHY_DEPTH) {
        stack[depth].type = cur_type;
        stack[depth].state = cur_state;
        depth++;

        bool found_base = false;
        for (size_t i = 0; i < cur_type->field_count; ++i) {
            const nmo_type_field_t *field = &cur_type->fields[i];
            if (!nmo_report_result_is_base_embedding(cur_type, field)) {
                continue;
            }

            const void *base_ptr = (const uint8_t *)cur_state + field->offset;
            const nmo_type_descriptor_t *parent_type = NULL;

            if (!nmo_guid_equals(field->type_guid, CKPGUID_NONE)) {
                parent_type = nmo_type_registry_find_by_guid(registry, field->type_guid);
            }
            if (parent_type == NULL && !nmo_guid_is_null(cur_type->base_type)) {
                parent_type = nmo_type_registry_find_by_guid(registry, cur_type->base_type);
            }

            if (parent_type != NULL && nmo_type_has_reflection(parent_type)) {
                cur_type = parent_type;
                cur_state = base_ptr;
                found_base = true;
                break;
            }
        }

        if (!found_base) {
            break;
        }
    }

    size_t count = depth < max_levels ? depth : max_levels;
    for (size_t i = 0; i < count; ++i) {
        levels[i] = stack[depth - 1 - i];
    }
    return count;
}

static const nmo_type_descriptor_t *nmo_report_result_resolve_object_type(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object)
{
    if (registry == NULL || object == NULL) {
        return NULL;
    }

    nmo_guid_t explicit_guid = nmo_object_get_type_guid(object);
    if (!nmo_guid_is_null(explicit_guid)) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(registry, explicit_guid);
        if (type != NULL) {
            return type;
        }
    }

    return nmo_type_registry_find_by_class_id_inherited(
        registry, nmo_object_get_class_id(object));
}

NMO_API nmo_status_t nmo_object_summary_collect_stats(
    nmo_context_t *ctx,
    const nmo_object_t *object,
    nmo_object_summary_stats_t *out_stats)
{
    const nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    const void *state = NULL;
    nmo_type_view_t type_view;
    nmo_report_hierarchy_level_t levels[NMO_REPORT_RESULT_MAX_HIERARCHY_DEPTH];
    size_t level_count = 0;

    if (ctx == NULL || object == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_summary_stats_clear(out_stats);
    out_stats->class_id = nmo_object_get_class_id(object);

    registry = nmo_context_get_type_registry(ctx);
    if (registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    out_stats->class_name = nmo_type_query_class_name_from_id(
        registry, out_stats->class_id);
    if (nmo_type_view_from_object(registry, object, &type_view) == NMO_OK) {
        out_stats->type_guid = type_view.guid;
        out_stats->type_name = type_view.name;
        out_stats->has_reflection = type_view.has_reflection;
    }

    if (!out_stats->has_reflection) {
        return NMO_OK;
    }

    type = nmo_report_result_resolve_object_type(registry, object);
    state = nmo_object_get_state(object);
    if (type == NULL || state == NULL || !nmo_type_has_reflection(type)) {
        return NMO_OK;
    }

    level_count = nmo_report_result_build_hierarchy(
        registry, type, state, levels, NMO_REPORT_RESULT_MAX_HIERARCHY_DEPTH);

    for (size_t lvl = 0; lvl < level_count; ++lvl) {
        nmo_report_field_count_ctx_t count_ctx = {
            .owner_type = levels[lvl].type
        };
        nmo_type_foreach_field(
            levels[lvl].type,
            levels[lvl].state,
            nmo_report_result_count_field,
            &count_ctx);
        out_stats->total_fields += count_ctx.total_fields;
        out_stats->array_fields += count_ctx.array_fields;
        out_stats->reference_fields += count_ctx.reference_fields;
        out_stats->optional_fields += count_ctx.optional_fields;
        out_stats->object_ref_fields += count_ctx.object_ref_fields;
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_comparison_result_collect_stats(
    const nmo_comparison_result_t *result,
    nmo_comparison_result_stats_t *out_stats)
{
    if (result == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_comparison_result_stats_clear(out_stats);
    out_stats->match = result->match != 0;
    out_stats->objects_compared = result->objects_compared;
    out_stats->objects_matched = result->objects_matched;
    out_stats->managers_compared = result->managers_compared;
    out_stats->managers_matched = result->managers_matched;
    out_stats->diff_count = result->diff_count;
    out_stats->diff_overflow = result->diff_overflow != 0;

    for (int i = 0; i < result->diff_count; ++i) {
        switch (result->diffs[i].type) {
            case NMO_DIFF_OBJECT_COUNT:
                out_stats->object_count_diffs++;
                break;
            case NMO_DIFF_MANAGER_COUNT:
                out_stats->manager_count_diffs++;
                break;
            case NMO_DIFF_OBJECT_MISSING:
                out_stats->object_missing_diffs++;
                break;
            case NMO_DIFF_OBJECT_ORDER:
                out_stats->object_order_diffs++;
                break;
            case NMO_DIFF_OBJECT_ID:
                out_stats->object_id_diffs++;
                break;
            case NMO_DIFF_OBJECT_NAME:
                out_stats->object_name_diffs++;
                break;
            case NMO_DIFF_OBJECT_CLASS_ID:
                out_stats->object_class_id_diffs++;
                break;
            case NMO_DIFF_OBJECT_REFERENCE_FLAG:
                out_stats->object_reference_flag_diffs++;
                break;
            case NMO_DIFF_OBJECT_CHUNK_SIZE:
                out_stats->object_chunk_size_diffs++;
                break;
            case NMO_DIFF_OBJECT_CHUNK_DATA:
                out_stats->object_chunk_data_diffs++;
                break;
            case NMO_DIFF_MANAGER_MISSING:
                out_stats->manager_missing_diffs++;
                break;
            case NMO_DIFF_MANAGER_GUID:
                out_stats->manager_guid_diffs++;
                break;
            case NMO_DIFF_MANAGER_CHUNK_SIZE:
                out_stats->manager_chunk_size_diffs++;
                break;
            case NMO_DIFF_MANAGER_CHUNK_DATA:
                out_stats->manager_chunk_data_diffs++;
                break;
            case NMO_DIFF_FILE_VERSION:
                out_stats->file_version_diffs++;
                break;
            case NMO_DIFF_CK_VERSION:
                out_stats->ck_version_diffs++;
                break;
            case NMO_DIFF_SHADOW_DATA:
                out_stats->shadow_data_diffs++;
                break;
            case NMO_DIFF_NONE:
            default:
                break;
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_diff_result_collect_stats(
    const nmo_diff_result_t *result,
    nmo_diff_result_stats_t *out_stats)
{
    if (result == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_diff_result_stats_clear(out_stats);
    out_stats->changed_count = result->changed_count;
    out_stats->renamed_count = result->renamed_count;
    out_stats->removed_count = result->removed_count;
    out_stats->added_count = result->added_count;
    out_stats->identical_count = result->identical_count;
    out_stats->total_objects1 = result->total_objects1;
    out_stats->total_objects2 = result->total_objects2;

    for (size_t i = 0; i < result->changed_count; ++i) {
        out_stats->reported_field_diffs += result->changed[i].field_diff_count;
        out_stats->total_field_diffs += result->changed[i].field_diff_total;
    }

    return NMO_OK;
}
