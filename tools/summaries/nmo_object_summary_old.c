/**
 * @file nmo_object_summary.c
 * @brief Object semantic summary system implementation
 *
 * Provides type-specific semantic summaries for Virtools objects.
 * Covers the priority types: CK3dEntity, CKMesh, CKMaterial, CKTexture,
 * CKCamera, CKLight, and CKBehavior.
 */

#include "nmo_object_summary.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_json.h"

#include "type/nmo_reflection.h"
#include "type/nmo_type_string.h"
#include "core/nmo_guid.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"

/* Object schema headers for state structures */
#include "object/nmo_class_ids.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ckmesh_schemas.h"
#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_cktexture_schemas.h"
#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_cklight_schemas.h"
#include "object/nmo_ckbehavior_schemas.h"
#include "object/nmo_ckscene_schemas.h"
#include "object/nmo_cklevel_schemas.h"
#include "object/nmo_ckparameter_schemas.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX 16u
#define NMO_SUMMARY_REFLECTION_TEXT_PREVIEW_MAX 8u
#define NMO_SUMMARY_REFLECTION_MAX_DEPTH 2u

static bool nmo_summary_semantic_auto_from_reflection(nmo_object_t *obj, nmo_summary_output_t *out);

/* Forward declarations for helpers used by semantic auto-summary (defined later in this TU). */
static const nmo_type_registry_t *nmo_summary_get_registry(const nmo_summary_output_t *out);
static const char *nmo_summary_resolve_object_name(const nmo_summary_output_t *out, nmo_object_id_t id);
static const nmo_type_descriptor_t *nmo_summary_lookup_value_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid);
static bool nmo_summary_field_is_object_ref(const nmo_type_field_t *field);
static uint64_t nmo_summary_read_uint_sized(const void *ptr, size_t size);
static bool nmo_summary_guess_repeated_count(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    uint64_t *out_count);
static size_t nmo_summary_guess_field_guid_scalar_size(nmo_guid_t field_guid);
static void nmo_summary_add_object_ref_preview_array(nmo_object_t *owner,
                                                     nmo_summary_output_t *out,
                                                     const char *key,
                                                     const uint32_t *ids,
                                                     uint32_t count,
                                                     uint32_t preview_max);

static bool nmo_summary_semantic_base(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) {
        return false;
    }

    const int LW = 18;
    nmo_summary_add_section(out, "Semantic (Base)");

    /* Identity-ish metadata beyond what object.show prints at top-level. */
    nmo_summary_add_uint(out, "File Index", (uint64_t)obj->file_index, LW);
    nmo_summary_add_uint(out, "File ID", (uint64_t)obj->file_id, LW);

    if (!nmo_guid_is_null(obj->type_guid)) {
        char guid_str[32];
        (void)nmo_guid_format(obj->type_guid, guid_str, sizeof(guid_str));
        nmo_summary_add_string(out, "Type GUID", guid_str, LW);
    }

    nmo_summary_add_bool(out, "Has State", obj->state != NULL, LW);
    nmo_summary_add_uint(out, "State Size", (uint64_t)obj->state_size, LW);

    nmo_summary_add_bool(out, "Has Chunk", obj->chunk != NULL, LW);
    if (obj->chunk) {
        nmo_summary_add_uint(out, "Chunk Data Size", (uint64_t)nmo_chunk_get_data_size(obj->chunk), LW);
    }

    /* Hierarchy metadata (object relationships are stable and useful for diff). */
    nmo_object_id_t parent_id = obj->parent ? nmo_object_get_id(obj->parent) : 0;
    const char *parent_name = parent_id ? nmo_summary_resolve_object_name(out, parent_id) : NULL;
    nmo_summary_add_object_ref(out, "Parent", parent_id, parent_name, LW);
    nmo_summary_add_uint(out, "Child Count", (uint64_t)obj->child_count, LW);

    return true;
}

typedef struct nmo_summary_semantic_auto_ctx {
    nmo_summary_output_t *out;
    const nmo_type_registry_t *registry;
    const nmo_type_descriptor_t *owner_type;
    const void *owner_instance;

    uint64_t field_count;
    uint64_t repeated_field_count;
    uint64_t reference_field_count;
    uint64_t optional_field_count;
    uint64_t object_ref_field_count;
    uint64_t repeated_total_count_estimate;

    uint32_t object_ref_preview_ids[NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX];
    uint32_t object_ref_preview_count;
} nmo_summary_semantic_auto_ctx_t;

static void nmo_summary_semantic_auto_try_add_preview_id(
    nmo_summary_semantic_auto_ctx_t *ctx,
    uint32_t id)
{
    if (!ctx || id == 0) {
        return;
    }
    if (ctx->object_ref_preview_count >= NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX) {
        return;
    }

    for (uint32_t i = 0; i < ctx->object_ref_preview_count; ++i) {
        if (ctx->object_ref_preview_ids[i] == id) {
            return;
        }
    }

    ctx->object_ref_preview_ids[ctx->object_ref_preview_count++] = id;
}

static bool nmo_summary_semantic_auto_field_visitor(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    nmo_summary_semantic_auto_ctx_t *ctx = (nmo_summary_semantic_auto_ctx_t*)user_data;
    if (!ctx || !field) {
        return true;
    }

    ctx->field_count++;

    if (field->flags & NMO_FIELD_OPTIONAL) {
        ctx->optional_field_count++;
    }
    if (field->flags & NMO_FIELD_REPEATED) {
        ctx->repeated_field_count++;
    }
    if (field->flags & NMO_FIELD_REFERENCE) {
        ctx->reference_field_count++;
    }
    if (nmo_summary_field_is_object_ref(field)) {
        ctx->object_ref_field_count++;
    }

    if (!ctx->owner_type || !ctx->owner_instance) {
        return true;
    }

    /* Best-effort reference preview across fields for stable, type-agnostic semantic output. */
    if (nmo_summary_field_is_object_ref(field)) {
        /* Repeated object ref field: pointer to an array of object IDs. */
        if (field->flags & NMO_FIELD_REPEATED) {
            uint64_t count = 0;
            (void)nmo_summary_guess_repeated_count(ctx->owner_type, ctx->owner_instance, field, &count);
            ctx->repeated_total_count_estimate += count;

            const void *base = NULL;
            if (field_ptr) {
                base = *(const void* const*)field_ptr;
            }
            if (!base || count == 0) {
                return true;
            }

            const nmo_type_descriptor_t *field_type = nmo_summary_lookup_value_type(ctx->registry, field->type_guid);
            size_t elem_size = field_type ? (size_t)field_type->size : 0;
            if (elem_size == 0) {
                elem_size = nmo_summary_guess_field_guid_scalar_size(field->type_guid);
            }
            if (elem_size == 0) {
                elem_size = sizeof(uint32_t);
            }

            uint64_t emit = count;
            if (emit > NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX) {
                emit = NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX;
            }

            for (uint64_t i = 0; i < emit; ++i) {
                const uint8_t *elem_ptr = (const uint8_t*)base + (size_t)i * elem_size;
                uint32_t id = (uint32_t)nmo_summary_read_uint_sized(elem_ptr, elem_size);
                nmo_summary_semantic_auto_try_add_preview_id(ctx, id);
            }
            return true;
        }

        /* Scalar object ref field. */
        if (field_ptr) {
            uint32_t id = (uint32_t)nmo_summary_read_uint_sized(field_ptr, field->size);
            nmo_summary_semantic_auto_try_add_preview_id(ctx, id);
        }
        return true;
    }

    if (field->flags & NMO_FIELD_REPEATED) {
        uint64_t count = 0;
        (void)nmo_summary_guess_repeated_count(ctx->owner_type, ctx->owner_instance, field, &count);
        ctx->repeated_total_count_estimate += count;
    }

    return true;
}

static bool nmo_summary_semantic_auto_from_reflection(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    const nmo_type_descriptor_t *type = NULL;
    if (registry && !nmo_guid_is_null(obj->type_guid)) {
        type = nmo_type_registry_find_by_guid(registry, obj->type_guid);
    }

    const bool has_type = (type != NULL);
    const bool has_reflection = has_type ? nmo_type_has_reflection(type) : false;

    if (out->is_json) {
        if (!out->json_doc || !out->json_data) {
            return false;
        }

        yyjson_mut_val *auto_obj = yyjson_mut_obj(out->json_doc);
        nmo_summary_output_t auto_out = *out;
        auto_out.json_data = auto_obj;

        nmo_cli_json_add_bool_safe(auto_out.json_doc, auto_out.json_data, "has_type", has_type);
        nmo_cli_json_add_bool_safe(auto_out.json_doc, auto_out.json_data, "has_reflection", has_reflection);

        if (has_type && type->name) {
            nmo_cli_json_add_str_safe(auto_out.json_doc, auto_out.json_data, "type_name", type->name);
        }

        nmo_summary_semantic_auto_ctx_t ctx = {
            .out = &auto_out,
            .registry = registry,
            .owner_type = type,
            .owner_instance = nmo_object_get_state(obj),
        };

        if (has_reflection) {
            (void)nmo_type_foreach_field(type, ctx.owner_instance, nmo_summary_semantic_auto_field_visitor, &ctx);
        }

        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data, "field_count", ctx.field_count);
        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data, "repeated_field_count", ctx.repeated_field_count);
        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data, "reference_field_count", ctx.reference_field_count);
        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data, "optional_field_count", ctx.optional_field_count);
        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data, "object_ref_field_count", ctx.object_ref_field_count);
        nmo_cli_json_add_uint_safe(auto_out.json_doc, auto_out.json_data,
                                   "repeated_total_count_estimate", ctx.repeated_total_count_estimate);

        if (ctx.object_ref_preview_count > 0) {
            nmo_summary_add_object_ref_preview_array(obj, &auto_out,
                                                     "object_ref_preview",
                                                     ctx.object_ref_preview_ids,
                                                     ctx.object_ref_preview_count,
                                                     NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX);
        }

        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "auto", auto_obj);
        return true;
    }

    nmo_summary_add_section(out, "Semantic (Auto)");
    nmo_summary_add_bool(out, "Has Type", has_type, 18);
    nmo_summary_add_bool(out, "Has Reflection", has_reflection, 18);
    if (has_type && type->name) {
        nmo_summary_add_string(out, "Type Name", type->name, 18);
    }

    nmo_summary_semantic_auto_ctx_t ctx = {
        .out = out,
        .registry = registry,
        .owner_type = type,
        .owner_instance = nmo_object_get_state(obj),
    };

    if (has_reflection) {
        (void)nmo_type_foreach_field(type, ctx.owner_instance, nmo_summary_semantic_auto_field_visitor, &ctx);
    }

    nmo_summary_add_uint(out, "Field Count", ctx.field_count, 18);
    nmo_summary_add_uint(out, "Repeated Fields", ctx.repeated_field_count, 18);
    nmo_summary_add_uint(out, "Reference Fields", ctx.reference_field_count, 18);
    nmo_summary_add_uint(out, "Optional Fields", ctx.optional_field_count, 18);
    nmo_summary_add_uint(out, "ObjectRef Fields", ctx.object_ref_field_count, 18);
    nmo_summary_add_uint(out, "Repeated Count (Est)", ctx.repeated_total_count_estimate, 18);

    if (ctx.object_ref_preview_count > 0) {
        nmo_summary_add_object_ref_preview_array(obj, out,
                                                 "ObjectRefs Preview",
                                                 ctx.object_ref_preview_ids,
                                                 ctx.object_ref_preview_count,
                                                 NMO_SUMMARY_REFLECTION_TEXT_PREVIEW_MAX);
    }

    return true;
}

static const nmo_type_registry_t *nmo_summary_get_registry(const nmo_summary_output_t *out) {
    if (!out || !out->ctx) {
        return NULL;
    }
    return nmo_context_get_type_registry(out->ctx);
}

static const char *nmo_summary_resolve_object_name(const nmo_summary_output_t *out, nmo_object_id_t id) {
    if (!out || !out->session || id == 0) {
        return NULL;
    }

    const nmo_object_repository_t *repo = nmo_session_get_repository(out->session);
    if (!repo) {
        return NULL;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        return NULL;
    }

    const char *name = nmo_object_get_name(obj);
    return (name && name[0]) ? name : NULL;
}

static nmo_guid_t nmo_summary_map_field_guid_to_type_guid(nmo_guid_t guid) {
    if ((guid.d1 & NMO_GUID_FIELD_BASE_MASK) == NMO_GUID_FIELD_BASE) {
        nmo_guid_t mapped;
        mapped.d1 = (NMO_TYPE_GUID_BASE | (guid.d1 & 0xFFu));
        mapped.d2 = guid.d2;
        return mapped;
    }
    return guid;
}

static const nmo_type_descriptor_t *nmo_summary_lookup_value_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid)
{
    if (!registry) {
        return NULL;
    }

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, type_guid);
    if (type) {
        return type;
    }

    nmo_guid_t mapped = nmo_summary_map_field_guid_to_type_guid(type_guid);
    if (!nmo_guid_equals(mapped, type_guid)) {
        type = nmo_type_registry_find_by_guid(registry, mapped);
    }

    return type;
}

static bool nmo_summary_try_format_field_guid_scalar(
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    char *buffer,
    size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return false;
    }
    buffer[0] = '\0';

    if (!value_ptr) {
        snprintf(buffer, buffer_size, "-");
        return true;
    }

    if ((field_guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return false;
    }

    uint32_t class_id = field_guid.d1 & 0xFFu;
    uint32_t size_bits = (field_guid.d2 >> 16) & 0xFFFFu;

    switch (class_id) {
        case NMO_GUID_FIELD_CLASS_VOID:
            snprintf(buffer, buffer_size, "-");
            return true;
        case NMO_GUID_FIELD_CLASS_BOOL: {
            bool b = false;
            if (value_size >= sizeof(bool)) {
                b = *(const bool*)value_ptr;
            } else if (value_size >= 1) {
                b = (*(const uint8_t*)value_ptr) != 0;
            }
            snprintf(buffer, buffer_size, "%s", b ? "true" : "false");
            return true;
        }
        case NMO_GUID_FIELD_CLASS_INT:
            if (size_bits == 8 && value_size >= 1) {
                snprintf(buffer, buffer_size, "%d", (int)*(const int8_t*)value_ptr);
                return true;
            }
            if (size_bits == 16 && value_size >= 2) {
                snprintf(buffer, buffer_size, "%d", (int)*(const int16_t*)value_ptr);
                return true;
            }
            if (size_bits == 32 && value_size >= 4) {
                snprintf(buffer, buffer_size, "%d", (int)*(const int32_t*)value_ptr);
                return true;
            }
            if (size_bits == 64 && value_size >= 8) {
                snprintf(buffer, buffer_size, "%lld", (long long)*(const int64_t*)value_ptr);
                return true;
            }
            return false;
        case NMO_GUID_FIELD_CLASS_UINT:
            if (size_bits == 8 && value_size >= 1) {
                snprintf(buffer, buffer_size, "%u", (unsigned)*(const uint8_t*)value_ptr);
                return true;
            }
            if (size_bits == 16 && value_size >= 2) {
                snprintf(buffer, buffer_size, "%u", (unsigned)*(const uint16_t*)value_ptr);
                return true;
            }
            if (size_bits == 32 && value_size >= 4) {
                snprintf(buffer, buffer_size, "%u", (unsigned)*(const uint32_t*)value_ptr);
                return true;
            }
            if (size_bits == 64 && value_size >= 8) {
                snprintf(buffer, buffer_size, "%llu", (unsigned long long)*(const uint64_t*)value_ptr);
                return true;
            }
            return false;
        case NMO_GUID_FIELD_CLASS_FLOAT:
            if (size_bits == 32 && value_size >= 4) {
                snprintf(buffer, buffer_size, "%.6g", *(const float*)value_ptr);
                return true;
            }
            if (size_bits == 64 && value_size >= 8) {
                snprintf(buffer, buffer_size, "%.12g", *(const double*)value_ptr);
                return true;
            }
            return false;
        case NMO_GUID_FIELD_CLASS_STRING: {
            const char *s = NULL;
            if (value_size == sizeof(const char*)) {
                s = *(const char* const*)value_ptr;
            }
            snprintf(buffer, buffer_size, "%s", (s && s[0]) ? s : "-");
            return true;
        }
        case NMO_GUID_FIELD_CLASS_POINTER: {
            const void *p = NULL;
            if (value_size == sizeof(void*)) {
                p = *(const void* const*)value_ptr;
            }
            snprintf(buffer, buffer_size, "%p", p);
            return true;
        }
        case NMO_GUID_FIELD_CLASS_OBJECT_ID: {
            nmo_object_id_t id = 0;
            if (value_size >= sizeof(nmo_object_id_t)) {
                id = *(const nmo_object_id_t*)value_ptr;
            } else if (value_size >= 4) {
                id = *(const uint32_t*)value_ptr;
            }
            snprintf(buffer, buffer_size, "#%u", (unsigned)id);
            return true;
        }
        default:
            return false;
    }
}

static bool nmo_summary_try_add_field_guid_typed_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size)
{
    if (!doc || !obj || !key || !value_ptr) {
        return false;
    }

    if ((field_guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return false;
    }

    uint32_t class_id = field_guid.d1 & 0xFFu;
    uint32_t size_bits = (field_guid.d2 >> 16) & 0xFFFFu;

    switch (class_id) {
        case NMO_GUID_FIELD_CLASS_BOOL: {
            bool b = false;
            if (value_size >= sizeof(bool)) {
                b = *(const bool*)value_ptr;
            } else if (value_size >= 1) {
                b = (*(const uint8_t*)value_ptr) != 0;
            }
            return nmo_cli_json_add_bool_safe(doc, obj, key, b);
        }

        case NMO_GUID_FIELD_CLASS_INT: {
            int64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const int8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const int16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const int32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const int64_t*)value_ptr;
            else return false;
            return nmo_cli_json_add_int_safe(doc, obj, key, v);
        }

        case NMO_GUID_FIELD_CLASS_UINT: {
            uint64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const uint8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const uint16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const uint32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const uint64_t*)value_ptr;
            else return false;
            return nmo_cli_json_add_uint_safe(doc, obj, key, v);
        }

        case NMO_GUID_FIELD_CLASS_FLOAT: {
            double v = 0.0;
            if (size_bits == 32 && value_size >= 4) v = (double)*(const float*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const double*)value_ptr;
            else return false;
            return nmo_cli_json_add_real_safe(doc, obj, key, v);
        }

        case NMO_GUID_FIELD_CLASS_OBJECT_ID: {
            nmo_object_id_t id = 0;
            if (value_size >= sizeof(nmo_object_id_t)) {
                id = *(const nmo_object_id_t*)value_ptr;
            } else if (value_size >= 4) {
                id = *(const uint32_t*)value_ptr;
            } else {
                return false;
            }
            return nmo_cli_json_add_uint_safe(doc, obj, key, (uint64_t)id);
        }

        default:
            return false;
    }
}

static bool nmo_summary_field_guid_supports_typed_json(nmo_guid_t field_guid) {
    if ((field_guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return false;
    }

    uint32_t class_id = field_guid.d1 & 0xFFu;
    switch (class_id) {
        case NMO_GUID_FIELD_CLASS_BOOL:
        case NMO_GUID_FIELD_CLASS_INT:
        case NMO_GUID_FIELD_CLASS_UINT:
        case NMO_GUID_FIELD_CLASS_FLOAT:
        case NMO_GUID_FIELD_CLASS_OBJECT_ID:
            return true;
        default:
            return false;
    }
}

static size_t nmo_summary_guess_field_guid_scalar_size(nmo_guid_t field_guid) {
    if ((field_guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return 0;
    }

    uint32_t class_id = field_guid.d1 & 0xFFu;
    uint32_t size_bits = (field_guid.d2 >> 16) & 0xFFFFu;

    if (class_id == NMO_GUID_FIELD_CLASS_STRING || class_id == NMO_GUID_FIELD_CLASS_POINTER) {
        return sizeof(void*);
    }

    if (size_bits == 8 || size_bits == 16 || size_bits == 32 || size_bits == 64) {
        return (size_t)(size_bits / 8);
    }
    return 0;
}

static bool nmo_summary_try_add_field_guid_typed_json_to_array(
    yyjson_mut_doc *doc,
    yyjson_mut_val *arr,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size)
{
    if (!doc || !arr || !value_ptr) {
        return false;
    }

    if ((field_guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return false;
    }

    uint32_t class_id = field_guid.d1 & 0xFFu;
    uint32_t size_bits = (field_guid.d2 >> 16) & 0xFFFFu;

    switch (class_id) {
        case NMO_GUID_FIELD_CLASS_BOOL: {
            bool b = false;
            if (value_size >= sizeof(bool)) {
                b = *(const bool*)value_ptr;
            } else if (value_size >= 1) {
                b = (*(const uint8_t*)value_ptr) != 0;
            }
            return yyjson_mut_arr_add_bool(doc, arr, b);
        }

        case NMO_GUID_FIELD_CLASS_INT: {
            int64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const int8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const int16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const int32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const int64_t*)value_ptr;
            else return false;
            return yyjson_mut_arr_add_sint(doc, arr, v);
        }

        case NMO_GUID_FIELD_CLASS_UINT: {
            uint64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const uint8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const uint16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const uint32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const uint64_t*)value_ptr;
            else return false;
            return yyjson_mut_arr_add_uint(doc, arr, v);
        }

        case NMO_GUID_FIELD_CLASS_FLOAT: {
            double v = 0.0;
            if (size_bits == 32 && value_size >= 4) v = (double)*(const float*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const double*)value_ptr;
            else return false;
            return yyjson_mut_arr_add_real(doc, arr, v);
        }

        case NMO_GUID_FIELD_CLASS_OBJECT_ID: {
            nmo_object_id_t id = 0;
            if (value_size >= sizeof(nmo_object_id_t)) {
                id = *(const nmo_object_id_t*)value_ptr;
            } else if (value_size >= 4) {
                id = *(const uint32_t*)value_ptr;
            } else {
                return false;
            }
            return yyjson_mut_arr_add_uint(doc, arr, (uint64_t)id);
        }

        default:
            return false;
    }
}

static bool nmo_summary_field_is_object_ref(const nmo_type_field_t *field) {
    if (!field) {
        return false;
    }
    if (field->flags & NMO_FIELD_REFERENCE) {
        return true;
    }
    if (field->semantic == NMO_SEMANTIC_OBJECT_REF) {
        return true;
    }
    if (nmo_guid_equals(field->type_guid, (nmo_guid_t)NMO_GUID_FIELD_OBJECT_ID) ||
        nmo_guid_equals(field->type_guid, NMO_TYPE_GUID_OBJECT_ID))
    {
        return true;
    }
    return false;
}

static uint64_t nmo_summary_read_uint_sized(const void *ptr, size_t size) {
    if (!ptr) {
        return 0;
    }
    if (size == sizeof(uint8_t)) {
        return *(const uint8_t*)ptr;
    }
    if (size == sizeof(uint16_t)) {
        return *(const uint16_t*)ptr;
    }
    if (size == sizeof(uint32_t)) {
        return *(const uint32_t*)ptr;
    }
    if (size == sizeof(uint64_t)) {
        return *(const uint64_t*)ptr;
    }
    if (size == sizeof(size_t)) {
        return *(const size_t*)ptr;
    }
    return 0;
}

static bool nmo_summary_guess_repeated_count(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    uint64_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!owner_type || !owner_instance || !field || !field->name || !out_count) {
        return false;
    }

    char cand1[128];
    char cand2[128];
    char cand3[128];
    const char *name = field->name;

    snprintf(cand1, sizeof(cand1), "%s_count", name);

    /* Singularize a trailing 's' (very common: materials -> material_count) */
    if (strlen(name) > 1 && name[strlen(name) - 1] == 's') {
        snprintf(cand2, sizeof(cand2), "%.*s_count", (int)(strlen(name) - 1), name);
    } else {
        cand2[0] = '\0';
    }

    /* Common naming: <name>_size */
    snprintf(cand3, sizeof(cand3), "%s_size", name);

    const nmo_type_field_t *count_field = nmo_type_get_field_by_name(owner_type, cand1);
    if (!count_field && cand2[0]) {
        count_field = nmo_type_get_field_by_name(owner_type, cand2);
    }
    if (!count_field) {
        count_field = nmo_type_get_field_by_name(owner_type, cand3);
    }
    if (!count_field) {
        return false;
    }

    const void *count_ptr = nmo_field_get_ptr_const(owner_instance, count_field);
    *out_count = nmo_summary_read_uint_sized(count_ptr, count_field->size);
    return true;
}

static bool nmo_summary_value_to_string(
    const nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *value_type,
    const void *value_ptr,
    char *buffer,
    size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return false;
    }
    buffer[0] = '\0';

    if (!value_ptr) {
        snprintf(buffer, buffer_size, "-");
        return true;
    }
    if (!value_type) {
        snprintf(buffer, buffer_size, "<unknown>");
        return true;
    }

    if (value_type->vtable && value_type->vtable->to_string) {
        nmo_status_t st = value_type->vtable->to_string(
            value_ptr,
            value_type,
            buffer,
            buffer_size,
            (void*)out->session);
        return st == NMO_OK;
    }

    nmo_status_t st = nmo_type_value_to_string(value_ptr, value_type, registry, buffer, buffer_size);
    return st == NMO_OK;
}

static yyjson_mut_val *nmo_summary_reflection_struct_to_json(
    nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *instance,
    uint32_t depth);

static yyjson_mut_val *nmo_summary_reflection_value_to_json(
    nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *value_type,
    const void *value_ptr,
    uint32_t depth)
{
    if (!out || !out->json_doc) {
        return NULL;
    }
    if (!value_ptr) {
        return yyjson_mut_null(out->json_doc);
    }

    if (value_type) {
        const nmo_specialized_metadata_t *meta = nmo_type_get_specialized_metadata(registry, value_type);
        if (meta && (meta->metadata_type == NMO_METADATA_TYPE_STRUCT || meta->metadata_type == NMO_METADATA_TYPE_UNION) &&
            depth < NMO_SUMMARY_REFLECTION_MAX_DEPTH)
        {
            return nmo_summary_reflection_struct_to_json(out, registry, value_type, value_ptr, depth + 1);
        }
    }

    char buf[256];
    if (!nmo_summary_value_to_string(out, registry, value_type, value_ptr, buf, sizeof(buf))) {
        snprintf(buf, sizeof(buf), "<unprintable>");
    }
    return yyjson_mut_strcpy(out->json_doc, buf);
}

typedef struct nmo_summary_struct_json_ctx {
    nmo_summary_output_t *out;
    const nmo_type_registry_t *registry;
    yyjson_mut_val *obj;
    uint32_t depth;
} nmo_summary_struct_json_ctx_t;

static bool nmo_summary_struct_field_to_json_visitor(
    void *user_data,
    const nmo_struct_descriptor_t *field,
    const void *field_ptr)
{
    nmo_summary_struct_json_ctx_t *ctx = (nmo_summary_struct_json_ctx_t*)user_data;
    if (!ctx || !ctx->out || !ctx->out->json_doc || !ctx->obj || !field || !field->name) {
        return true;
    }

    const nmo_type_descriptor_t *field_type = nmo_summary_lookup_value_type(ctx->registry, field->type_guid);

    if (nmo_guid_equals(field->type_guid, (nmo_guid_t)NMO_GUID_FIELD_GUID) && field_ptr && field->size == sizeof(nmo_guid_t)) {
        char buf[32];
        (void)nmo_guid_format(*(const nmo_guid_t*)field_ptr, buf, sizeof(buf));
        nmo_cli_json_add_str_safe(ctx->out->json_doc, ctx->obj, field->name, buf);
        return true;
    }

    if (field->pointer_depth > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "<ptr depth=%u>", field->pointer_depth);
        nmo_cli_json_add_str_safe(ctx->out->json_doc, ctx->obj, field->name, buf);
        return true;
    }

    if (field->array_count > 0 && field_type && field_type->size > 0) {
        yyjson_mut_val *arr = yyjson_mut_arr(ctx->out->json_doc);
        uint32_t n = field->array_count;
        uint32_t emit = n;
        if (emit > NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX) {
            emit = NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX;
        }

        const uint8_t *base = (const uint8_t*)field_ptr;
        for (uint32_t i = 0; i < emit; ++i) {
            const void *elem_ptr = base + (size_t)i * field_type->size;
            yyjson_mut_arr_add_val(arr, nmo_summary_reflection_value_to_json(
                ctx->out, ctx->registry, field_type, elem_ptr, ctx->depth));
        }

        yyjson_mut_obj_add_val(ctx->out->json_doc, ctx->obj, field->name, arr);
        return true;
    }

    {
        char buf[256];
        if (nmo_summary_try_format_field_guid_scalar(field->type_guid, field_ptr, field->size, buf, sizeof(buf))) {
            nmo_cli_json_add_str_safe(ctx->out->json_doc, ctx->obj, field->name, buf);
        } else {
            yyjson_mut_val *val = nmo_summary_reflection_value_to_json(ctx->out, ctx->registry, field_type, field_ptr, ctx->depth);
            yyjson_mut_obj_add_val(ctx->out->json_doc, ctx->obj, field->name, val);
        }
    }
    return true;
}

static yyjson_mut_val *nmo_summary_reflection_struct_to_json(
    nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *instance,
    uint32_t depth)
{
    if (!out || !out->json_doc) {
        return NULL;
    }

    yyjson_mut_val *obj = yyjson_mut_obj(out->json_doc);
    if (!registry || !type || !instance) {
        return obj;
    }

    nmo_summary_struct_json_ctx_t ctx = {
        .out = out,
        .registry = registry,
        .obj = obj,
        .depth = depth,
    };

    (void)nmo_type_foreach_struct_field(registry, type, instance, nmo_summary_struct_field_to_json_visitor, &ctx);
    return obj;
}

typedef struct nmo_summary_reflection_ctx {
    nmo_summary_output_t *out;
    const nmo_type_registry_t *registry;
    const nmo_type_descriptor_t *owner_type;
    const void *owner_instance;
    yyjson_mut_val *fields_arr; /* JSON only */
    int label_width;            /* text only */
} nmo_summary_reflection_ctx_t;

static bool nmo_summary_reflection_field_visitor(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    nmo_summary_reflection_ctx_t *ctx = (nmo_summary_reflection_ctx_t*)user_data;
    if (!ctx || !ctx->out || !field || !field->name) {
        return true;
    }

    const nmo_type_descriptor_t *field_type = nmo_summary_lookup_value_type(ctx->registry, field->type_guid);

    /* Repeated field (pointer to array of elements) */
    if (field->flags & NMO_FIELD_REPEATED) {
        uint64_t count = 0;
        (void)nmo_summary_guess_repeated_count(ctx->owner_type, ctx->owner_instance, field, &count);
        const void *base = NULL;
        if (field_ptr) {
            base = *(const void* const*)field_ptr;
        }

        if (ctx->out->is_json) {
            yyjson_mut_val *item = yyjson_mut_obj(ctx->out->json_doc);
            nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "name", field->name);

            char guid_str[32];
            (void)nmo_guid_format(field->type_guid, guid_str, sizeof(guid_str));
            nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "type_guid", guid_str);
            const char *type_name = ctx->registry ? nmo_field_type_name(ctx->registry, field->type_guid) : NULL;
            if (type_name) {
                nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "type_name", type_name);
            }
            yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "flags", field->flags);
            yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "semantic", (uint64_t)field->semantic);
            yyjson_mut_obj_add_bool(ctx->out->json_doc, item, "repeated", true);
            yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "count", count);

            if (base && count > 0) {
                yyjson_mut_val *preview = yyjson_mut_arr(ctx->out->json_doc);
                bool typed_supported = nmo_summary_field_guid_supports_typed_json(field->type_guid);
                yyjson_mut_val *preview_typed = typed_supported ? yyjson_mut_arr(ctx->out->json_doc) : NULL;
                uint64_t emit = count;
                if (emit > NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX) {
                    emit = NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX;
                }

                for (uint64_t i = 0; i < emit; ++i) {
                    size_t elem_size = field_type ? (size_t)field_type->size : 0;
                    if (elem_size == 0) {
                        elem_size = nmo_summary_guess_field_guid_scalar_size(field->type_guid);
                    }
                    if (elem_size == 0) {
                        /* Best-effort fallback for unknown layouts */
                        elem_size = sizeof(uint32_t);
                    }
                    const uint8_t *elem_ptr = (const uint8_t*)base + (size_t)i * elem_size;

                    char elem_buf[256];
                    if (nmo_summary_try_format_field_guid_scalar(field->type_guid, elem_ptr, elem_size, elem_buf, sizeof(elem_buf))) {
                        yyjson_mut_arr_add_strcpy(ctx->out->json_doc, preview, elem_buf);
                    } else {
                        yyjson_mut_arr_add_val(preview, nmo_summary_reflection_value_to_json(
                            ctx->out, ctx->registry, field_type, elem_ptr, 0));
                    }

                    if (typed_supported && preview_typed) {
                        if (!nmo_summary_try_add_field_guid_typed_json_to_array(
                                ctx->out->json_doc, preview_typed, field->type_guid, elem_ptr, elem_size))
                        {
                            (void)yyjson_mut_arr_add_null(ctx->out->json_doc, preview_typed);
                        }
                    }
                }

                yyjson_mut_obj_add_val(ctx->out->json_doc, item, "preview", preview);
                if (typed_supported && preview_typed) {
                    yyjson_mut_obj_add_val(ctx->out->json_doc, item, "preview_typed", preview_typed);
                }
                yyjson_mut_obj_add_bool(ctx->out->json_doc, item, "truncated", emit < count);
            }

            yyjson_mut_arr_add_val(ctx->fields_arr, item);
        } else {
            char value_buf[512];
            if (!base || count == 0) {
                snprintf(value_buf, sizeof(value_buf), "[%llu]", (unsigned long long)count);
            } else {
                uint64_t emit = count;
                if (emit > NMO_SUMMARY_REFLECTION_TEXT_PREVIEW_MAX) {
                    emit = NMO_SUMMARY_REFLECTION_TEXT_PREVIEW_MAX;
                }

                size_t used = (size_t)snprintf(value_buf, sizeof(value_buf), "[%llu] ",
                                               (unsigned long long)count);
                for (uint64_t i = 0; i < emit && used + 4 < sizeof(value_buf); ++i) {
                    size_t elem_size = field_type ? (size_t)field_type->size : 0;
                    if (elem_size == 0) {
                        elem_size = sizeof(uint32_t);
                    }
                    const uint8_t *elem_ptr = (const uint8_t*)base + (size_t)i * elem_size;
                    char elem_str[128];
                    if (nmo_summary_try_format_field_guid_scalar(field->type_guid, elem_ptr, elem_size, elem_str, sizeof(elem_str)) ||
                        (field_type && field_type->size > 0 &&
                         nmo_summary_value_to_string(ctx->out, ctx->registry, field_type, elem_ptr, elem_str, sizeof(elem_str)))) {
                        used += (size_t)snprintf(value_buf + used, sizeof(value_buf) - used, "%s%s",
                                                 (i == 0) ? "" : ", ", elem_str);
                    } else {
                        used += (size_t)snprintf(value_buf + used, sizeof(value_buf) - used, "%s?",
                                                 (i == 0) ? "" : ", ");
                    }
                }

                if (emit < count && used + 8 < sizeof(value_buf)) {
                    (void)snprintf(value_buf + used, sizeof(value_buf) - used, ", ...");
                }
            }
            nmo_cli_print_kv(ctx->out->stream, field->name, value_buf, ctx->label_width, ctx->out->colorize);
        }

        return true;
    }

    /* Scalar field */
    if (ctx->out->is_json) {
        yyjson_mut_val *item = yyjson_mut_obj(ctx->out->json_doc);
        nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "name", field->name);

        char guid_str[32];
        (void)nmo_guid_format(field->type_guid, guid_str, sizeof(guid_str));
        nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "type_guid", guid_str);
        const char *type_name = ctx->registry ? nmo_field_type_name(ctx->registry, field->type_guid) : NULL;
        if (type_name) {
            nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "type_name", type_name);
        }

        yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "flags", field->flags);
        yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "semantic", (uint64_t)field->semantic);
        yyjson_mut_obj_add_bool(ctx->out->json_doc, item, "repeated", false);

        if (nmo_summary_field_is_object_ref(field) && field_ptr) {
            nmo_object_id_t id = (nmo_object_id_t)nmo_summary_read_uint_sized(field_ptr, field->size);
            const char *ref_name = nmo_summary_resolve_object_name(ctx->out, id);
            yyjson_mut_val *ref = yyjson_mut_obj(ctx->out->json_doc);
            yyjson_mut_obj_add_uint(ctx->out->json_doc, ref, "id", id);
            if (ref_name) {
                nmo_cli_json_add_str_safe(ctx->out->json_doc, ref, "name", ref_name);
            }
            yyjson_mut_obj_add_val(ctx->out->json_doc, item, "value", ref);
        } else {
            char buf[256];
            if (nmo_summary_try_format_field_guid_scalar(field->type_guid, field_ptr, field->size, buf, sizeof(buf))) {
                yyjson_mut_obj_add_strcpy(ctx->out->json_doc, item, "value", buf);
            } else {
                yyjson_mut_val *val = nmo_summary_reflection_value_to_json(ctx->out, ctx->registry, field_type, field_ptr, 0);
                yyjson_mut_obj_add_val(ctx->out->json_doc, item, "value", val);
            }

            /* Typed scalar value (optional) */
            (void)nmo_summary_try_add_field_guid_typed_json(ctx->out->json_doc, item, "value_typed",
                                                           field->type_guid, field_ptr, field->size);
        }

        yyjson_mut_arr_add_val(ctx->fields_arr, item);
    } else {
        if (nmo_summary_field_is_object_ref(field) && field_ptr) {
            nmo_object_id_t id = (nmo_object_id_t)nmo_summary_read_uint_sized(field_ptr, field->size);
            const char *ref_name = nmo_summary_resolve_object_name(ctx->out, id);
            nmo_summary_add_object_ref(ctx->out, field->name, id, ref_name, ctx->label_width);
        } else {
            char buf[256];
            if (nmo_guid_equals(field->type_guid, (nmo_guid_t)NMO_GUID_FIELD_GUID) && field_ptr && field->size == sizeof(nmo_guid_t)) {
                (void)nmo_guid_format(*(const nmo_guid_t*)field_ptr, buf, sizeof(buf));
            } else if (!nmo_summary_try_format_field_guid_scalar(field->type_guid, field_ptr, field->size, buf, sizeof(buf))) {
                if (!nmo_summary_value_to_string(ctx->out, ctx->registry, field_type, field_ptr, buf, sizeof(buf))) {
                    snprintf(buf, sizeof(buf), "<unprintable>");
                }
            }
            nmo_cli_print_kv(ctx->out->stream, field->name, buf, ctx->label_width, ctx->out->colorize);
        }
    }

    return true;
}

static bool nmo_summary_reflection(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    if (!registry) {
        return false;
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id_inherited(registry, class_id);
    if (!type || !nmo_type_has_reflection(type)) {
        return false;
    }

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        return false;
    }

    if (out->is_json) {
        yyjson_mut_val *reflect = yyjson_mut_obj(out->json_doc);

        if (type->name) {
            nmo_cli_json_add_str_safe(out->json_doc, reflect, "type_name", type->name);
        }
        {
            char type_guid_str[32];
            (void)nmo_guid_format(type->guid, type_guid_str, sizeof(type_guid_str));
            nmo_cli_json_add_str_safe(out->json_doc, reflect, "type_guid", type_guid_str);
        }

        yyjson_mut_val *fields = yyjson_mut_arr(out->json_doc);
        nmo_summary_reflection_ctx_t ctx = {
            .out = out,
            .registry = registry,
            .owner_type = type,
            .owner_instance = state,
            .fields_arr = fields,
            .label_width = 0,
        };

        (void)nmo_type_foreach_field(type, state, nmo_summary_reflection_field_visitor, &ctx);

        yyjson_mut_obj_add_val(out->json_doc, reflect, "fields", fields);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "reflection", reflect);
        return true;
    }

    fprintf(out->stream, "\n");
    nmo_cli_print_heading(out->stream, "Reflection", out->colorize);
    nmo_cli_print_kv(out->stream, "Schema", type->name ? type->name : "-", 14, out->colorize);

    char type_guid_str[32];
    (void)nmo_guid_format(type->guid, type_guid_str, sizeof(type_guid_str));
    nmo_cli_print_kv(out->stream, "Type GUID", type_guid_str, 14, out->colorize);
    fprintf(out->stream, "\n");

    nmo_summary_reflection_ctx_t ctx = {
        .out = out,
        .registry = registry,
        .owner_type = type,
        .owner_instance = state,
        .fields_arr = NULL,
        .label_width = 24,
    };
    (void)nmo_type_foreach_field(type, state, nmo_summary_reflection_field_visitor, &ctx);
    return true;
}

/* ============================================================================
 * Summary Helper Functions
 * ============================================================================ */

void nmo_summary_add_string(nmo_summary_output_t *out, const char *key,
                            const char *value, int label_width) {
    if (!out || !key) return;
    const char *val = value ? value : "-";

    if (out->is_json) {
        nmo_cli_json_add_str_safe(out->json_doc, out->json_data, key, val);
    } else {
        nmo_cli_print_kv(out->stream, key, val, label_width, out->colorize);
    }
}

void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                         int64_t value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        nmo_cli_json_add_int_safe(out->json_doc, out->json_data, key, value);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)value);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_uint(nmo_summary_output_t *out, const char *key,
                          uint64_t value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        nmo_cli_json_add_uint_safe(out->json_doc, out->json_data, key, value);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_float(nmo_summary_output_t *out, const char *key,
                           double value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        nmo_cli_json_add_real_safe(out->json_doc, out->json_data, key, value);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g", value);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                          bool value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        nmo_cli_json_add_bool_safe(out->json_doc, out->json_data, key, value);
    } else {
        nmo_cli_print_kv(out->stream, key, value ? "Yes" : "No", label_width, out->colorize);
    }
}

void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                nmo_object_id_t id, const char *name, int label_width) {
    if (!out || !key) return;

    if (id == 0) {
        if (out->is_json) {
            nmo_cli_json_add_null_safe(out->json_doc, out->json_data, key);
        } else {
            nmo_cli_print_kv(out->stream, key, "(none)", label_width, out->colorize);
        }
        return;
    }

    if (out->is_json) {
        yyjson_mut_val *ref = yyjson_mut_obj(out->json_doc);
        yyjson_mut_obj_add_uint(out->json_doc, ref, "id", id);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(out->json_doc, ref, "name", name);
        }
        nmo_cli_json_add_val_safe(out->json_doc, out->json_data, key, ref);
    } else {
        char buf[128];
        if (name && name[0]) {
            snprintf(buf, sizeof(buf), "#%u (%s)", id, name);
        } else {
            snprintf(buf, sizeof(buf), "#%u", id);
        }
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_hex(nmo_summary_output_t *out, const char *key,
                         uint32_t value, int label_width) {
    if (!out || !key) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", value);

    if (out->is_json) {
        nmo_cli_json_add_str_safe(out->json_doc, out->json_data, key, buf);
    } else {
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_section(nmo_summary_output_t *out, const char *title) {
    if (!out || !title) return;

    if (out->is_json) {
        /* For JSON, we create a nested object for the section */
        /* The caller should manage the json_data pointer if needed */
    } else {
        fprintf(out->stream, "\n");
        nmo_cli_print_heading(out->stream, title, out->colorize);
    }
}

void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                             float x, float y, float z, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        yyjson_mut_val *vec = yyjson_mut_arr(out->json_doc);
        yyjson_mut_arr_add_real(out->json_doc, vec, x);
        yyjson_mut_arr_add_real(out->json_doc, vec, y);
        yyjson_mut_arr_add_real(out->json_doc, vec, z);
        nmo_cli_json_add_val_safe(out->json_doc, out->json_data, key, vec);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", x, y, z);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_color(nmo_summary_output_t *out, const char *key,
                           uint32_t argb, int label_width) {
    if (!out || !key) return;

    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;

    if (out->is_json) {
        yyjson_mut_val *color = yyjson_mut_obj(out->json_doc);
        yyjson_mut_obj_add_uint(out->json_doc, color, "r", r);
        yyjson_mut_obj_add_uint(out->json_doc, color, "g", g);
        yyjson_mut_obj_add_uint(out->json_doc, color, "b", b);
        yyjson_mut_obj_add_uint(out->json_doc, color, "a", a);
        nmo_cli_json_add_val_safe(out->json_doc, out->json_data, key, color);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X (A=%d)", r, g, b, a);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

/* ============================================================================
 * CK3dEntity Summary
 * ============================================================================ */

bool nmo_summary_ck3dentity(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ck3dentity_state_t *entity = (nmo_ck3dentity_state_t *)state;
    const int LW = 16;

    nmo_summary_add_section(out, "3D Entity");

    /* Extract position from world matrix (last column) */
    float px = entity->world_matrix[12];
    float py = entity->world_matrix[13];
    float pz = entity->world_matrix[14];
    nmo_summary_add_vector3(out, "Position", px, py, pz, LW);

    /* Extract scale from world matrix (diagonal magnitude) */
    float sx = sqrtf(entity->world_matrix[0]*entity->world_matrix[0] +
                     entity->world_matrix[1]*entity->world_matrix[1] +
                     entity->world_matrix[2]*entity->world_matrix[2]);
    float sy = sqrtf(entity->world_matrix[4]*entity->world_matrix[4] +
                     entity->world_matrix[5]*entity->world_matrix[5] +
                     entity->world_matrix[6]*entity->world_matrix[6]);
    float sz = sqrtf(entity->world_matrix[8]*entity->world_matrix[8] +
                     entity->world_matrix[9]*entity->world_matrix[9] +
                     entity->world_matrix[10]*entity->world_matrix[10]);
    nmo_summary_add_vector3(out, "Scale", sx, sy, sz, LW);

    nmo_summary_add_hex(out, "Entity Flags", entity->entity_flags, LW);
    nmo_summary_add_hex(out, "Moveable Flags", entity->moveable_flags, LW);

    /* Parent reference */
    nmo_summary_add_object_ref(out, "Parent", entity->parent_id, NULL, LW);

    /* Mesh reference */
    nmo_summary_add_object_ref(out, "Current Mesh", entity->current_mesh_id, NULL, LW);
    nmo_summary_add_uint(out, "Mesh Count", entity->mesh_count, LW);

    /* Animation count */
    if (entity->animation_count > 0) {
        nmo_summary_add_uint(out, "Animations", entity->animation_count, LW);
    }

    /* Skin info */
    if (entity->skin) {
        nmo_summary_add_section(out, "Skin Data");
        nmo_summary_add_uint(out, "Bone Count", entity->skin->bone_count, LW);
        nmo_summary_add_uint(out, "Vertex Count", entity->skin->vertex_count, LW);
    }

    return true;
}

/* ============================================================================
 * CKMesh Summary
 * ============================================================================ */

bool nmo_summary_ckmesh(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ck_mesh_state_t *mesh = (nmo_ck_mesh_state_t *)state;
    const int LW = 18;

    nmo_summary_add_section(out, "Mesh Geometry");

    nmo_summary_add_uint(out, "Vertex Count", mesh->vertex_count, LW);
    nmo_summary_add_uint(out, "Face Count", mesh->face_count, LW);
    nmo_summary_add_uint(out, "Line Count", mesh->line_count, LW);

    /* Bounding info */
    nmo_summary_add_vector3(out, "Bary Center", 
                            mesh->bary_center.x, mesh->bary_center.y, mesh->bary_center.z, LW);
    nmo_summary_add_float(out, "Radius", mesh->radius, LW);

    nmo_summary_add_vector3(out, "BBox Min",
                            mesh->local_box_min.x, mesh->local_box_min.y, mesh->local_box_min.z, LW);
    nmo_summary_add_vector3(out, "BBox Max",
                            mesh->local_box_max.x, mesh->local_box_max.y, mesh->local_box_max.z, LW);

    /* Material info */
    nmo_summary_add_section(out, "Materials");
    nmo_summary_add_uint(out, "Material Groups", mesh->material_group_count, LW);
    nmo_summary_add_uint(out, "Mat. Channels", mesh->material_channel_count, LW);

    /* Flags */
    nmo_summary_add_hex(out, "Mesh Flags", mesh->flags, LW);

    /* Vertex data presence */
    nmo_summary_add_bool(out, "Has Colors", mesh->vertex_colors != NULL, LW);
    nmo_summary_add_bool(out, "Has Specular", mesh->vertex_specular != NULL, LW);
    nmo_summary_add_bool(out, "Has Weights", mesh->vertex_weights != NULL, LW);

    /* Progressive mesh */
    if (mesh->has_progressive_mesh) {
        nmo_summary_add_section(out, "Progressive Mesh (LOD)");
        nmo_summary_add_bool(out, "Morph Enabled", mesh->pm_morph_enabled != 0, LW);
        nmo_summary_add_uint(out, "PM Data Size", mesh->pm_data_size, LW);
    }

    return true;
}

/* ============================================================================
 * CKMaterial Summary
 * ============================================================================ */

static const char *blend_mode_name(uint32_t mode) {
    switch (mode) {
        case 1: return "Decal";
        case 2: return "Modulate";
        case 3: return "DecalAlpha";
        case 4: return "ModulateAlpha";
        case 5: return "DecalMask";
        case 6: return "ModulateMask";
        case 7: return "Copy";
        case 8: return "Add";
        case 9: return "DotProduct3";
        default: return "Unknown";
    }
}

bool nmo_summary_ckmaterial(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ck_material_state_t *mat = (nmo_ck_material_state_t *)state;
    const int LW = 16;

    nmo_summary_add_section(out, "Material Colors");

    nmo_summary_add_color(out, "Diffuse", mat->diffuse_color, LW);
    nmo_summary_add_color(out, "Ambient", mat->ambient_color, LW);
    nmo_summary_add_color(out, "Specular", mat->specular_color, LW);
    nmo_summary_add_color(out, "Emissive", mat->emissive_color, LW);
    nmo_summary_add_float(out, "Spec. Power", mat->specular_power, LW);

    /* Textures */
    nmo_summary_add_section(out, "Textures");
    for (int i = 0; i < 4; ++i) {
        if (mat->texture_ids[i] != 0) {
            char label[32];
            snprintf(label, sizeof(label), "Texture %d", i);
            nmo_summary_add_object_ref(out, label, mat->texture_ids[i], NULL, LW);
        }
    }

    /* Render settings (unpack packed_modes) */
    nmo_summary_add_section(out, "Render Settings");
    uint32_t blend_mode = (mat->packed_modes >> 0) & 0x0F;
    nmo_summary_add_string(out, "Blend Mode", blend_mode_name(blend_mode), LW);
    nmo_summary_add_hex(out, "Packed Modes", mat->packed_modes, LW);
    nmo_summary_add_hex(out, "Packed Flags", mat->packed_flags, LW);

    /* Effect */
    if (mat->has_effect) {
        nmo_summary_add_section(out, "Effect");
        nmo_summary_add_uint(out, "Effect ID", mat->effect, LW);
        if (mat->has_effect_param) {
            nmo_summary_add_object_ref(out, "Effect Param", mat->effect_parameter_id, NULL, LW);
        }
    }

    return true;
}

/* ============================================================================
 * CKTexture Summary
 * ============================================================================ */

bool nmo_summary_cktexture(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_cktexture_state_t *tex = (nmo_cktexture_state_t *)state;
    const int LW = 16;

    nmo_summary_add_section(out, "Texture");

    /* Slot info */
    nmo_summary_add_uint(out, "Slot Count", tex->slot_count, LW);
    nmo_summary_add_uint(out, "Mipmap Level", tex->mipmap_level, LW);

    /* Save options */
    const char *save_mode = "Raw";
    if (tex->save_options & NMO_CKTEXTURE_EXTERNAL) save_mode = "External";
    else if (tex->save_options & NMO_CKTEXTURE_IMAGEFORMAT) save_mode = "Compressed";
    nmo_summary_add_string(out, "Save Mode", save_mode, LW);

    nmo_summary_add_bool(out, "Transparent", tex->is_transparent != 0, LW);
    nmo_summary_add_bool(out, "Cubemap", tex->is_cubemap != 0, LW);

    /* Movie filename */
    if (tex->has_movie_filename && tex->movie_filename && tex->movie_filename[0]) {
        nmo_summary_add_string(out, "Movie File", tex->movie_filename, LW);
    }

    /* Slot filenames */
    if (tex->has_slot_filenames && tex->slot_filenames) {
        for (uint32_t i = 0; i < tex->slot_count && i < 4; ++i) {
            if (tex->slot_filenames[i] && tex->slot_filenames[i][0]) {
                char label[32];
                snprintf(label, sizeof(label), "Slot %u File", i);
                nmo_summary_add_string(out, label, tex->slot_filenames[i], LW);
            }
        }
    }

    /* Bitmap kind */
    const char *bitmap_kind_name = "None";
    switch (tex->bitmap_kind) {
        case NMO_CKTEXTURE_BITMAP_READER: bitmap_kind_name = "Reader"; break;
        case NMO_CKTEXTURE_BITMAP_RAW: bitmap_kind_name = "Raw"; break;
        case NMO_CKTEXTURE_BITMAP_BITMAP2: bitmap_kind_name = "Bitmap2"; break;
        default: break;
    }
    nmo_summary_add_string(out, "Bitmap Kind", bitmap_kind_name, LW);

    /* Raw slot dimensions if available */
    if (tex->bitmap_kind == NMO_CKTEXTURE_BITMAP_RAW && tex->raw_slots) {
        nmo_summary_add_section(out, "Raw Bitmap");
        nmo_summary_add_int(out, "Width", tex->raw_slots[0].width, LW);
        nmo_summary_add_int(out, "Height", tex->raw_slots[0].height, LW);
        nmo_summary_add_int(out, "Bits/Pixel", tex->raw_slots[0].bits_per_pixel, LW);
    }

    /* Pick threshold */
    if (tex->has_pick_threshold) {
        nmo_summary_add_int(out, "Pick Threshold", tex->pick_threshold, LW);
    }

    /* User mipmaps */
    if (tex->has_user_mipmaps) {
        nmo_summary_add_uint(out, "User Mipmaps", tex->user_mipmap_count, LW);
    }

    return true;
}

/* ============================================================================
 * CKCamera Summary
 * ============================================================================ */

bool nmo_summary_ckcamera(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ckcamera_state_t *cam = (nmo_ckcamera_state_t *)state;
    const int LW = 16;

    /* First output 3D entity info */
    nmo_summary_ck3dentity(obj, out);

    nmo_summary_add_section(out, "Camera Settings");

    const char *proj_type = cam->projection_type == 1 ? "Perspective" : "Orthographic";
    nmo_summary_add_string(out, "Projection", proj_type, LW);

    if (cam->projection_type == 1) {
        /* Perspective - show FOV in degrees */
        float fov_degrees = cam->fov * 180.0f / 3.14159265f;
        nmo_summary_add_float(out, "FOV (degrees)", fov_degrees, LW);
    } else {
        nmo_summary_add_float(out, "Ortho Zoom", cam->orthographic_zoom, LW);
    }

    nmo_summary_add_float(out, "Near Plane", cam->near_plane, LW);
    nmo_summary_add_float(out, "Far Plane", cam->far_plane, LW);

    if (cam->width > 0 && cam->height > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%dx%d", cam->width, cam->height);
        nmo_summary_add_string(out, "Viewport", buf, LW);
    }

    return true;
}

/* ============================================================================
 * CKLight Summary
 * ============================================================================ */

static const char *light_type_name(nmo_vx_light_type_t type) {
    switch (type) {
        case NMO_LIGHT_POINT: return "Point";
        case NMO_LIGHT_SPOT: return "Spotlight";
        case NMO_LIGHT_DIRECTIONAL: return "Directional";
        default: return "Unknown";
    }
}

bool nmo_summary_cklight(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_cklight_state_t *light = (nmo_cklight_state_t *)state;
    const int LW = 16;

    /* First output 3D entity info */
    nmo_summary_ck3dentity(obj, out);

    nmo_summary_add_section(out, "Light Settings");

    nmo_summary_add_string(out, "Type", light_type_name(light->light_data.type), LW);

    /* Colors - convert from nmo_color_t (float RGBA) to display */
    /* Diffuse color as hex (approximate) */
    uint32_t diff_argb = ((uint32_t)(light->light_data.diffuse.a * 255) << 24) |
                         ((uint32_t)(light->light_data.diffuse.r * 255) << 16) |
                         ((uint32_t)(light->light_data.diffuse.g * 255) << 8) |
                         ((uint32_t)(light->light_data.diffuse.b * 255));
    nmo_summary_add_color(out, "Diffuse", diff_argb, LW);

    nmo_summary_add_float(out, "Range", light->light_data.range, LW);
    nmo_summary_add_float(out, "Power", light->light_power, LW);

    /* Attenuation */
    nmo_summary_add_section(out, "Attenuation");
    nmo_summary_add_float(out, "Constant", light->light_data.attenuation0, LW);
    nmo_summary_add_float(out, "Linear", light->light_data.attenuation1, LW);
    nmo_summary_add_float(out, "Quadratic", light->light_data.attenuation2, LW);

    /* Spotlight-specific */
    if (light->light_data.type == NMO_LIGHT_SPOT) {
        nmo_summary_add_section(out, "Spotlight");
        float inner_deg = light->light_data.inner_spot_cone * 180.0f / 3.14159265f;
        float outer_deg = light->light_data.outer_spot_cone * 180.0f / 3.14159265f;
        nmo_summary_add_float(out, "Inner Cone", inner_deg, LW);
        nmo_summary_add_float(out, "Outer Cone", outer_deg, LW);
        nmo_summary_add_float(out, "Falloff", light->light_data.falloff, LW);
    }

    nmo_summary_add_hex(out, "Flags", light->flags, LW);

    return true;
}

/* ============================================================================
 * CKBehavior Summary
 * ============================================================================ */

bool nmo_summary_ckbehavior(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ckbehavior_state_t *beh = (nmo_ckbehavior_state_t *)state;
    const int LW = 18;

    nmo_summary_add_section(out, "Behavior");

    nmo_summary_add_uint(out, "Sub-behaviors", beh->sub_behavior_count, LW);
    nmo_summary_add_uint(out, "Inputs", beh->input_count, LW);
    nmo_summary_add_uint(out, "Outputs", beh->output_count, LW);
    nmo_summary_add_uint(out, "Param Inputs", beh->in_parameter_count, LW);
    nmo_summary_add_uint(out, "Param Outputs", beh->out_parameter_count, LW);
    nmo_summary_add_uint(out, "Local Params", beh->local_parameter_count, LW);
    nmo_summary_add_uint(out, "Links", beh->sub_behavior_link_count, LW);
    nmo_summary_add_uint(out, "Operations", beh->operation_count, LW);

    nmo_summary_add_hex(out, "Flags", beh->flags, LW);
    nmo_summary_add_int(out, "Priority", beh->priority, LW);

    /* Owner */
    nmo_summary_add_object_ref(out, "Owner", beh->owner_id, NULL, LW);

    /* Building block GUID if present */
    if (!nmo_guid_is_null(beh->block_guid)) {
        char guid_str[32];
        snprintf(guid_str, sizeof(guid_str), "%08X-%08X",
                 beh->block_guid.d1, beh->block_guid.d2);
        nmo_summary_add_string(out, "Block GUID", guid_str, LW);
        nmo_summary_add_uint(out, "Block Version", beh->block_version, LW);
    }

    /* Target parameter */
    if (beh->target_parameter_id != 0) {
        nmo_summary_add_object_ref(out, "Target Param", beh->target_parameter_id, NULL, LW);
    }

    return true;
}

/* ============================================================================
 * CKScene Summary
 * ============================================================================ */

bool nmo_summary_ckscene(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ckscene_state_t *scene = (nmo_ckscene_state_t *)state;
    const int LW = 18;

    nmo_summary_add_section(out, "Scene");

    const char *level_name = nmo_summary_resolve_object_name(out, scene->level_id);
    nmo_summary_add_object_ref(out, "Level", scene->level_id, level_name, LW);
    nmo_summary_add_uint(out, "Object Count", (uint64_t)scene->object_count, LW);

    nmo_summary_add_hex(out, "Environment", scene->environment_settings, LW);

    nmo_summary_add_section(out, "Render Settings");
    nmo_summary_add_color(out, "Background", scene->background_color, LW);
    nmo_summary_add_color(out, "Ambient Light", scene->ambient_light_color, LW);

    nmo_summary_add_section(out, "Fog");
    nmo_summary_add_uint(out, "Fog Mode", (uint64_t)scene->fog_mode, LW);
    nmo_summary_add_color(out, "Fog Color", scene->fog_color, LW);
    nmo_summary_add_float(out, "Fog Start", scene->fog_start, LW);
    nmo_summary_add_float(out, "Fog End", scene->fog_end, LW);
    nmo_summary_add_float(out, "Fog Density", scene->fog_density, LW);

    const char *bg_tex_name = nmo_summary_resolve_object_name(out, scene->background_texture_id);
    nmo_summary_add_object_ref(out, "Background Texture", scene->background_texture_id, bg_tex_name, LW);

    const char *cam_name = nmo_summary_resolve_object_name(out, scene->starting_camera_id);
    nmo_summary_add_object_ref(out, "Starting Camera", scene->starting_camera_id, cam_name, LW);

    return true;
}

/* ============================================================================
 * CKLevel Summary
 * ============================================================================ */

static void nmo_summary_add_object_ref_preview_array(nmo_object_t *owner,
                                                     nmo_summary_output_t *out,
                                                     const char *key,
                                                     const nmo_object_id_t *ids,
                                                     uint32_t count,
                                                     uint32_t preview_max)
{
    if (!out || !key) {
        return;
    }

    if (!ids || count == 0) {
        if (out->is_json) {
            yyjson_mut_val *arr = yyjson_mut_arr(out->json_doc);
            nmo_cli_json_add_val_safe(out->json_doc, out->json_data, key, arr);
        } else {
            nmo_summary_add_string(out, key, "(none)", 18);
        }
        return;
    }

    uint32_t emit = count;
    if (preview_max > 0 && emit > preview_max) {
        emit = preview_max;
    }

    (void)owner;
    if (out->is_json) {
        yyjson_mut_val *arr = yyjson_mut_arr(out->json_doc);
        for (uint32_t i = 0; i < emit; ++i) {
            yyjson_mut_val *ref = yyjson_mut_obj(out->json_doc);
            yyjson_mut_obj_add_uint(out->json_doc, ref, "id", ids[i]);
            const char *nm = nmo_summary_resolve_object_name(out, ids[i]);
            if (nm && nm[0]) {
                nmo_cli_json_add_str_safe(out->json_doc, ref, "name", nm);
            }
            yyjson_mut_arr_add_val(arr, ref);
        }
        nmo_cli_json_add_val_safe(out->json_doc, out->json_data, key, arr);
        return;
    }

    /* Text preview */
    char buf[512];
    size_t pos = 0;
    for (uint32_t i = 0; i < emit; ++i) {
        const char *nm = nmo_summary_resolve_object_name(out, ids[i]);
        char one[128];
        if (nm && nm[0]) {
            snprintf(one, sizeof(one), "#%u(%s)", ids[i], nm);
        } else {
            snprintf(one, sizeof(one), "#%u", ids[i]);
        }

        size_t one_len = strlen(one);
        if (i > 0) {
            if (pos + 2 < sizeof(buf)) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
        }
        if (pos + one_len >= sizeof(buf)) {
            break;
        }
        memcpy(buf + pos, one, one_len);
        pos += one_len;
        buf[pos] = '\0';
    }

    if (count > emit && pos + 16 < sizeof(buf)) {
        snprintf(buf + pos, sizeof(buf) - pos, " ... (+%u)", (unsigned)(count - emit));
    }

    nmo_summary_add_string(out, key, buf, 18);
}

bool nmo_summary_cklevel(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_cklevel_state_t *level = (nmo_cklevel_state_t *)state;
    const int LW = 18;

    nmo_summary_add_section(out, "Level");

    nmo_summary_add_uint(out, "Scene Count", (uint64_t)level->scene_count, LW);
    if (level->scene_ids && level->scene_count > 0) {
        nmo_summary_add_object_ref_preview_array(obj, out,
                                                 "Scenes Preview",
                                                 level->scene_ids,
                                                 level->scene_count,
                                                 NMO_SUMMARY_REFLECTION_ARRAY_PREVIEW_MAX);
    }

    const char *cur_name = nmo_summary_resolve_object_name(out, level->current_scene_id);
    nmo_summary_add_object_ref(out, "Current Scene", level->current_scene_id, cur_name, LW);

    const char *lvl_scene_name = nmo_summary_resolve_object_name(out, level->level_scene_id);
    nmo_summary_add_object_ref(out, "Level Scene", level->level_scene_id, lvl_scene_name, LW);

    nmo_summary_add_bool(out, "Has Level Scene Chunk", level->level_scene_chunk != NULL, LW);

    nmo_summary_add_uint(out, "Inactive Managers", (uint64_t)level->inactive_manager_count, LW);
    nmo_summary_add_uint(out, "Duplicate Managers", (uint64_t)level->duplicate_manager_count, LW);

    return true;
}

/* ============================================================================
 * CKParameter Summary
 * ============================================================================ */

static const char *nmo_ckparameter_mode_name(nmo_ckparameter_mode_t mode) {
    switch (mode) {
        case NMO_CKPARAM_MODE_SUBCHUNK: return "Subchunk";
        case NMO_CKPARAM_MODE_BUFFER: return "Buffer";
        case NMO_CKPARAM_MODE_OBJECT: return "Object";
        case NMO_CKPARAM_MODE_NONE: return "None";
        case NMO_CKPARAM_MODE_MANAGER: return "Manager";
        default: return "Unknown";
    }
}

bool nmo_summary_ckparameter(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    void *state = nmo_object_get_state(obj);
    if (!state) return false;

    nmo_ckparameter_state_t *param = (nmo_ckparameter_state_t *)state;
    const int LW = 18;

    nmo_summary_add_section(out, "Parameter");

    char type_guid_str[32];
    (void)nmo_guid_format(param->type_guid, type_guid_str, sizeof(type_guid_str));
    nmo_summary_add_string(out, "Value Type", type_guid_str, LW);

    nmo_summary_add_string(out, "Mode", nmo_ckparameter_mode_name(param->mode), LW);
    nmo_summary_add_bool(out, "Has State", param->has_state, LW);

    switch (param->mode) {
        case NMO_CKPARAM_MODE_BUFFER:
            nmo_summary_add_uint(out, "Buffer Size", (uint64_t)param->buffer_size, LW);
            break;
        case NMO_CKPARAM_MODE_OBJECT: {
            const char *nm = nmo_summary_resolve_object_name(out, param->object_id);
            nmo_summary_add_object_ref(out, "Object", param->object_id, nm, LW);
            break;
        }
        case NMO_CKPARAM_MODE_MANAGER: {
            char guid_str[32];
            (void)nmo_guid_format(param->manager_guid, guid_str, sizeof(guid_str));
            nmo_summary_add_string(out, "Manager GUID", guid_str, LW);
            nmo_summary_add_uint(out, "Manager Value", (uint64_t)param->manager_value, LW);
            break;
        }
        case NMO_CKPARAM_MODE_SUBCHUNK:
            nmo_summary_add_bool(out, "Has Subchunk", param->subchunk != NULL, LW);
            break;
        case NMO_CKPARAM_MODE_NONE:
        default:
            break;
    }

    return true;
}

/* ============================================================================
 * Summary Dispatch Table
 * ============================================================================ */

typedef struct {
    nmo_class_id_t class_id;
    nmo_summary_fn handler;
} nmo_summary_entry_t;

static const nmo_summary_entry_t summary_handlers[] = {
    { NMO_CID_SCENE, nmo_summary_ckscene },
    { NMO_CID_LEVEL, nmo_summary_cklevel },
    { NMO_CID_3DENTITY, nmo_summary_ck3dentity },
    { NMO_CID_3DOBJECT, nmo_summary_ck3dentity },  /* Uses 3DEntity base */
    { NMO_CID_MESH, nmo_summary_ckmesh },
    { NMO_CID_MATERIAL, nmo_summary_ckmaterial },
    { NMO_CID_TEXTURE, nmo_summary_cktexture },
    { NMO_CID_CAMERA, nmo_summary_ckcamera },
    { NMO_CID_TARGETCAMERA, nmo_summary_ckcamera },
    { NMO_CID_LIGHT, nmo_summary_cklight },
    { NMO_CID_TARGETLIGHT, nmo_summary_cklight },
    { NMO_CID_BEHAVIOR, nmo_summary_ckbehavior },
    { NMO_CID_PARAMETER, nmo_summary_ckparameter },
    { NMO_CID_PARAMETERLOCAL, nmo_summary_ckparameter },
    { NMO_CID_PARAMETERIN, nmo_summary_ckparameter },
    { NMO_CID_PARAMETEROUT, nmo_summary_ckparameter },
    { NMO_CID_PARAMETEROPERATION, nmo_summary_ckparameter },
    { NMO_CID_CHARACTER, nmo_summary_ck3dentity },  /* Uses 3DEntity base */
    { NMO_CID_BODYPART, nmo_summary_ck3dentity },   /* Uses 3DEntity base */
    { NMO_CID_PLACE, nmo_summary_ck3dentity },      /* Uses 3DEntity base */
    { NMO_CID_SPRITE3D, nmo_summary_ck3dentity },   /* Uses 3DEntity base */
    { NMO_CID_CURVE, nmo_summary_ck3dentity },      /* Uses 3DEntity base */
};

static const size_t summary_handler_count = sizeof(summary_handlers) / sizeof(summary_handlers[0]);

/* ============================================================================
 * Public API
 * ============================================================================ */

void nmo_summary_init(void) {
    /* No initialization needed currently */
}

bool nmo_summary_has_handler(nmo_class_id_t class_id) {
    for (size_t i = 0; i < summary_handler_count; ++i) {
        if (summary_handlers[i].class_id == class_id) {
            return true;
        }
    }
    return false;
}

bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out) return false;

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);

    bool emitted = false;

    /* semantic: always present for all built-in types (base + optional type-specific extras). */
    if (out->is_json) {
        yyjson_mut_val *semantic = yyjson_mut_obj(out->json_doc);
        nmo_summary_output_t sem_out = *out;
        sem_out.json_data = semantic;

        (void)nmo_summary_semantic_base(obj, &sem_out);
        (void)nmo_summary_semantic_auto_from_reflection(obj, &sem_out);

        for (size_t i = 0; i < summary_handler_count; ++i) {
            if (summary_handlers[i].class_id == class_id) {
                (void)summary_handlers[i].handler(obj, &sem_out);
                break;
            }
        }

        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "semantic", semantic);
        emitted = true;
    } else {
        emitted = nmo_summary_semantic_base(obj, out) || emitted;
        emitted = nmo_summary_semantic_auto_from_reflection(obj, out) || emitted;
        for (size_t i = 0; i < summary_handler_count; ++i) {
            if (summary_handlers[i].class_id == class_id) {
                emitted = summary_handlers[i].handler(obj, out) || emitted;
                break;
            }
        }
    }

    /* Always try reflection-based summary as a fallback (or additional info). */
    emitted = nmo_summary_reflection(obj, out) || emitted;
    return emitted;
}
