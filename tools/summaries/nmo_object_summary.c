/**
 * @file nmo_object_summary.c
 * @brief Object semantic summary system (v2 - Reflection-first implementation)
 *
 * This is a complete rewrite of the summary system using reflection as the
 * PRIMARY mechanism. All object types with reflection metadata get automatic
 * summaries without requiring hardcoded type-specific handlers.
 *
 * Architecture:
 * 1. ValueFormatter - Type-aware value formatting (uses type system)
 * 2. FieldRenderer - Renders individual fields with proper formatting
 * 3. SummaryEngine - Orchestrates the full summary generation
 * 4. EnricherRegistry - Pluggable type-specific computed values
 */

#include "nmo_object_summary.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_json.h"

#include "type/nmo_reflection.h"
#include "type/nmo_type_string.h"
#include "type/nmo_builtin_type_guids.h"
#include "core/nmo_guid.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define NMO_SUMMARY_DEFAULT_ARRAY_PREVIEW_MAX   16u
#define NMO_SUMMARY_DEFAULT_TEXT_PREVIEW_MAX    8u
#define NMO_SUMMARY_DEFAULT_MAX_DEPTH           2u
#define NMO_SUMMARY_MAX_ENRICHERS               64u
#define NMO_SUMMARY_VALUE_BUFFER_SIZE           512u

/* ============================================================================
 * UTF-8 Safe JSON String Helper
 * ============================================================================ */

/**
 * @brief Create a yyjson_mut_val string that is safe for JSON output.
 *
 * Validates UTF-8 and replaces invalid bytes with U+FFFD replacement character.
 * Always copies the string into the document's allocator.
 */
static yyjson_mut_val *nmo_summary_json_strcpy_safe(yyjson_mut_doc *doc, const char *str) {
    if (!doc) return NULL;
    if (!str) return yyjson_mut_null(doc);

    /* Quick UTF-8 validation */
    const unsigned char *s = (const unsigned char *)str;
    size_t slen = strlen(str);
    bool valid = true;

    for (size_t i = 0; i < slen; ) {
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < slen && (s[i+1] & 0xC0) == 0x80) { i += 2; continue; }
        if ((c & 0xF0) == 0xE0 && i + 2 < slen && (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80) {
            if (c == 0xE0 && s[i+1] < 0xA0) { valid = false; break; }
            if (c == 0xED && s[i+1] >= 0xA0) { valid = false; break; }
            i += 3; continue;
        }
        if ((c & 0xF8) == 0xF0 && i + 3 < slen &&
            (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80 && (s[i+3] & 0xC0) == 0x80) {
            if (c == 0xF0 && s[i+1] < 0x90) { valid = false; break; }
            if (c > 0xF4 || (c == 0xF4 && s[i+1] > 0x8F)) { valid = false; break; }
            i += 4; continue;
        }
        valid = false; break;
    }

    if (valid) {
        return yyjson_mut_strcpy(doc, str);
    }

    /* Sanitize: preserve valid UTF-8 sequences; replace invalid bytes with U+FFFD. */
    const size_t cap = (slen * 3u) + 1u;
    char *buf = (char *)malloc(cap);
    if (!buf) return yyjson_mut_null(doc);

    size_t out_len = 0;
    for (size_t i = 0; i < slen; ) {
        unsigned char c = s[i];

        /* Allow ASCII printable + common whitespace; replace other controls. */
        if (c < 0x80) {
            if (c == '\t' || c == '\n' || c == '\r' || c >= 0x20) {
                buf[out_len++] = (char)c;
            } else {
                buf[out_len++] = (char)0xEF;
                buf[out_len++] = (char)0xBF;
                buf[out_len++] = (char)0xBD;
            }
            i++;
            continue;
        }

        /* Copy valid multi-byte sequences as-is. */
        if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < slen && (s[i+1] & 0xC0) == 0x80) {
            buf[out_len++] = (char)s[i++];
            buf[out_len++] = (char)s[i++];
            continue;
        }

        if ((c & 0xF0) == 0xE0 && i + 2 < slen &&
            (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80) {
            if (c == 0xE0 && s[i+1] < 0xA0) {
                /* Overlong */
            } else if (c == 0xED && s[i+1] >= 0xA0) {
                /* Surrogate */
            } else {
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                continue;
            }
        }

        if ((c & 0xF8) == 0xF0 && i + 3 < slen &&
            (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80 && (s[i+3] & 0xC0) == 0x80) {
            if (c == 0xF0 && s[i+1] < 0x90) {
                /* Overlong */
            } else if (c > 0xF4 || (c == 0xF4 && s[i+1] > 0x8F)) {
                /* Out of range */
            } else {
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                buf[out_len++] = (char)s[i++];
                continue;
            }
        }

        /* Invalid byte: replace and advance 1. */
        buf[out_len++] = (char)0xEF;
        buf[out_len++] = (char)0xBF;
        buf[out_len++] = (char)0xBD;
        i++;
    }

    buf[out_len] = '\0';
    yyjson_mut_val *val = yyjson_mut_strcpy(doc, buf);
    free(buf);
    return val ? val : yyjson_mut_null(doc);
}

/* ============================================================================
 * Enricher Registry (Simple Static Table)
 * ============================================================================ */

typedef struct {
    nmo_class_id_t class_id;
    nmo_summary_enricher_fn enricher;
} nmo_enricher_entry_t;

static nmo_enricher_entry_t g_enrichers[NMO_SUMMARY_MAX_ENRICHERS];
static size_t g_enricher_count = 0;
static bool g_enrichers_initialized = false;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static const nmo_type_registry_t *nmo_summary_get_registry(const nmo_summary_output_t *out);
static const char *nmo_summary_resolve_object_name(const nmo_summary_output_t *out, nmo_object_id_t id);
static const nmo_type_descriptor_t *nmo_summary_get_type_for_object(
    const nmo_type_registry_t *registry, nmo_object_t *obj);

/* Value formatting */
static bool nmo_summary_format_value(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    char *buffer,
    size_t buffer_size);

static bool nmo_summary_format_value_to_json(
    const nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    yyjson_mut_val **out_val);

/* Summary sections */
static bool nmo_summary_emit_reflection_fields(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config);
static bool nmo_summary_emit_enrichments(nmo_object_t *obj, nmo_summary_output_t *out);

/* ============================================================================
 * Configuration
 * ============================================================================ */

nmo_summary_config_t nmo_summary_config_default(void) {
    return (nmo_summary_config_t){
        .array_preview_max = NMO_SUMMARY_DEFAULT_ARRAY_PREVIEW_MAX,
        .text_preview_max = NMO_SUMMARY_DEFAULT_TEXT_PREVIEW_MAX,
        .max_depth = NMO_SUMMARY_DEFAULT_MAX_DEPTH,
        .show_field_metadata = false,
        .resolve_object_refs = true,
        .format_enum_names = true,
        .format_flags_names = true,
    };
}

/* ============================================================================
 * Registry Access Helpers
 * ============================================================================ */

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
    nmo_object_repository_t *repo = nmo_session_get_repository(out->session);
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

static const nmo_type_descriptor_t *nmo_summary_get_type_for_object(
    const nmo_type_registry_t *registry,
    nmo_object_t *obj)
{
    if (!registry || !obj) {
        return NULL;
    }

    /* Try by type GUID first (most precise) */
    if (!nmo_guid_is_null(obj->type_guid)) {
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, obj->type_guid);
        if (type && nmo_type_has_reflection(type)) {
            return type;
        }
    }

    /* Fall back to class ID (with inheritance support) */
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    return nmo_type_registry_find_by_class_id_inherited(registry, class_id);
}

/* ============================================================================
 * Field GUID Classification
 * ============================================================================ */

static bool nmo_summary_is_field_guid(nmo_guid_t guid) {
    return (guid.d1 & NMO_GUID_FIELD_BASE_MASK) == NMO_GUID_FIELD_BASE;
}

static uint32_t nmo_summary_get_field_class(nmo_guid_t guid) {
    if (!nmo_summary_is_field_guid(guid)) {
        return 0;
    }
    return guid.d1 & 0xFFu;
}

static uint32_t nmo_summary_get_field_size_bits(nmo_guid_t guid) {
    return (guid.d2 >> 16) & 0xFFFFu;
}

static bool nmo_summary_is_object_ref_field(const nmo_type_field_t *field) {
    if (!field) return false;
    if (field->flags & NMO_FIELD_REFERENCE) return true;
    if (field->semantic == NMO_SEMANTIC_OBJECT_REF) return true;
    if (nmo_summary_get_field_class(field->type_guid) == NMO_GUID_FIELD_CLASS_OBJECT_ID) return true;
    return false;
}

/* ============================================================================
 * Value Formatting (Text)
 * ============================================================================ */

/**
 * @brief Format a primitive value based on field GUID
 */
static bool nmo_summary_format_primitive(
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    char *buffer,
    size_t buffer_size)
{
    if (!value_ptr || !buffer || buffer_size == 0) {
        return false;
    }

    uint32_t field_class = nmo_summary_get_field_class(field_guid);
    uint32_t size_bits = nmo_summary_get_field_size_bits(field_guid);

    switch (field_class) {
        case NMO_GUID_FIELD_CLASS_BOOL: {
            bool v = (value_size >= 1) ? (*(const uint8_t*)value_ptr != 0) : false;
            snprintf(buffer, buffer_size, "%s", v ? "true" : "false");
            return true;
        }

        case NMO_GUID_FIELD_CLASS_INT: {
            int64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const int8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const int16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const int32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const int64_t*)value_ptr;
            else if (value_size >= 4) v = *(const int32_t*)value_ptr;
            snprintf(buffer, buffer_size, "%lld", (long long)v);
            return true;
        }

        case NMO_GUID_FIELD_CLASS_UINT: {
            uint64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const uint8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const uint16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const uint32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const uint64_t*)value_ptr;
            else if (value_size >= 4) v = *(const uint32_t*)value_ptr;
            snprintf(buffer, buffer_size, "%llu", (unsigned long long)v);
            return true;
        }

        case NMO_GUID_FIELD_CLASS_FLOAT: {
            if (size_bits == 64 && value_size >= 8) {
                snprintf(buffer, buffer_size, "%.6g", *(const double*)value_ptr);
            } else if (value_size >= 4) {
                snprintf(buffer, buffer_size, "%.6g", (double)*(const float*)value_ptr);
            }
            return true;
        }

        case NMO_GUID_FIELD_CLASS_STRING: {
            const char *str = *(const char*const*)value_ptr;
            snprintf(buffer, buffer_size, "%s", str ? str : "(null)");
            return true;
        }

        case NMO_GUID_FIELD_CLASS_OBJECT_ID: {
            nmo_object_id_t id = (value_size >= 4) ? *(const nmo_object_id_t*)value_ptr : 0;
            snprintf(buffer, buffer_size, "#%u", id);
            return true;
        }

        case NMO_GUID_FIELD_CLASS_COMPOSITE: {
            /* Handle well-known composite types */
            uint32_t sub_id = field_guid.d2 & 0xFFFFu;
            if (sub_id == 2 && size_bits == 96 && value_size >= 12) {
                /* Vector3 */
                const float *v = (const float*)value_ptr;
                snprintf(buffer, buffer_size, "(%.4g, %.4g, %.4g)", v[0], v[1], v[2]);
                return true;
            }
            if (sub_id == 1 && size_bits == 64 && value_size >= 8) {
                /* Vector2 */
                const float *v = (const float*)value_ptr;
                snprintf(buffer, buffer_size, "(%.4g, %.4g)", v[0], v[1]);
                return true;
            }
            if (sub_id == 3 && size_bits == 128 && value_size >= 16) {
                /* Vector4 */
                const float *v = (const float*)value_ptr;
                snprintf(buffer, buffer_size, "(%.4g, %.4g, %.4g, %.4g)", v[0], v[1], v[2], v[3]);
                return true;
            }
            if (sub_id == 5 && size_bits == 128 && value_size >= 16) {
                /* Quaternion */
                const float *q = (const float*)value_ptr;
                snprintf(buffer, buffer_size, "(%.4g, %.4g, %.4g, %.4g)", q[0], q[1], q[2], q[3]);
                return true;
            }
            if (sub_id == 7 && size_bits == 128 && value_size >= 16) {
                /* Color (RGBA float) */
                const float *c = (const float*)value_ptr;
                snprintf(buffer, buffer_size, "rgba(%.3g, %.3g, %.3g, %.3g)", c[0], c[1], c[2], c[3]);
                return true;
            }
            if (sub_id == 8 && size_bits == 128 && value_size >= 8) {
                /* GUID */
                nmo_guid_t g = *(const nmo_guid_t*)value_ptr;
                nmo_guid_format(g, buffer, buffer_size);
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

/**
 * @brief Try to format using type system's to_string (for enum/flags/complex)
 */
static bool nmo_summary_format_via_type_system(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *value_ptr,
    char *buffer,
    size_t buffer_size)
{
    if (!type || !value_ptr || !buffer) {
        return false;
    }

    nmo_status_t status = nmo_type_value_to_string(value_ptr, type, registry, buffer, buffer_size);
    return status == NMO_OK;
}

/**
 * @brief Master value formatter - tries type system first, then primitive
 */
static bool nmo_summary_format_value(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    char *buffer,
    size_t buffer_size)
{
    if (!value_ptr || !buffer || buffer_size == 0) {
        snprintf(buffer, buffer_size, "-");
        return true;
    }

    /* First try type system (handles enum, flags, complex types with custom to_string) */
    if (field_type && nmo_summary_format_via_type_system(registry, field_type, value_ptr, buffer, buffer_size)) {
        return true;
    }

    /* Fall back to primitive formatting */
    if (nmo_summary_format_primitive(field_guid, value_ptr, value_size, buffer, buffer_size)) {
        return true;
    }

    /* Last resort: hex dump for unknown types */
    if (value_size <= 8) {
        uint64_t v = 0;
        memcpy(&v, value_ptr, value_size < 8 ? value_size : 8);
        snprintf(buffer, buffer_size, "0x%llx", (unsigned long long)v);
        return true;
    }

    snprintf(buffer, buffer_size, "<binary %zu bytes>", value_size);
    return true;
}

/* ============================================================================
 * Value Formatting (JSON)
 * ============================================================================ */

static yyjson_mut_val *nmo_summary_primitive_to_json(
    yyjson_mut_doc *doc,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size)
{
    if (!doc || !value_ptr) {
        return yyjson_mut_null(doc);
    }

    uint32_t field_class = nmo_summary_get_field_class(field_guid);
    uint32_t size_bits = nmo_summary_get_field_size_bits(field_guid);

    switch (field_class) {
        case NMO_GUID_FIELD_CLASS_BOOL: {
            bool v = (value_size >= 1) ? (*(const uint8_t*)value_ptr != 0) : false;
            return yyjson_mut_bool(doc, v);
        }

        case NMO_GUID_FIELD_CLASS_INT: {
            int64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const int8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const int16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const int32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const int64_t*)value_ptr;
            else if (value_size >= 4) v = *(const int32_t*)value_ptr;
            return yyjson_mut_sint(doc, v);
        }

        case NMO_GUID_FIELD_CLASS_UINT: {
            uint64_t v = 0;
            if (size_bits == 8 && value_size >= 1) v = *(const uint8_t*)value_ptr;
            else if (size_bits == 16 && value_size >= 2) v = *(const uint16_t*)value_ptr;
            else if (size_bits == 32 && value_size >= 4) v = *(const uint32_t*)value_ptr;
            else if (size_bits == 64 && value_size >= 8) v = *(const uint64_t*)value_ptr;
            else if (value_size >= 4) v = *(const uint32_t*)value_ptr;
            return yyjson_mut_uint(doc, v);
        }

        case NMO_GUID_FIELD_CLASS_FLOAT: {
            double v = 0.0;
            if (size_bits == 64 && value_size >= 8) {
                v = *(const double*)value_ptr;
            } else if (value_size >= 4) {
                v = (double)*(const float*)value_ptr;
            }
            if (isnan(v) || isinf(v)) {
                return yyjson_mut_null(doc);
            }
            return yyjson_mut_real(doc, v);
        }

        case NMO_GUID_FIELD_CLASS_STRING: {
            const char *str = *(const char*const*)value_ptr;
            return nmo_summary_json_strcpy_safe(doc, str);
        }

        case NMO_GUID_FIELD_CLASS_OBJECT_ID: {
            nmo_object_id_t id = (value_size >= 4) ? *(const nmo_object_id_t*)value_ptr : 0;
            return yyjson_mut_uint(doc, id);
        }

        case NMO_GUID_FIELD_CLASS_COMPOSITE: {
            uint32_t sub_id = field_guid.d2 & 0xFFFFu;
            /* Vector2/3/4, Quaternion, Color as arrays */
            if ((sub_id >= 1 && sub_id <= 3) || sub_id == 5 || sub_id == 7) {
                size_t count = 0;
                if (sub_id == 1) count = 2;      /* Vector2 */
                else if (sub_id == 2) count = 3; /* Vector3 */
                else if (sub_id == 3) count = 4; /* Vector4 */
                else if (sub_id == 5) count = 4; /* Quaternion */
                else if (sub_id == 7) count = 4; /* Color */

                if (value_size >= count * sizeof(float)) {
                    yyjson_mut_val *arr = yyjson_mut_arr(doc);
                    const float *v = (const float*)value_ptr;
                    for (size_t i = 0; i < count; ++i) {
                        yyjson_mut_arr_add_real(doc, arr, (double)v[i]);
                    }
                    return arr;
                }
            }
            if (sub_id == 8 && value_size >= 8) {
                /* GUID as string */
                nmo_guid_t g = *(const nmo_guid_t*)value_ptr;
                char buf[32];
                nmo_guid_format(g, buf, sizeof(buf));
                return yyjson_mut_strcpy(doc, buf);
            }
            break;
        }

        default:
            break;
    }

    return NULL;
}

static bool nmo_summary_format_value_to_json(
    const nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    yyjson_mut_val **out_val)
{
    if (!out || !out->json_doc || !out_val) {
        return false;
    }

    if (!value_ptr) {
        *out_val = yyjson_mut_null(out->json_doc);
        return true;
    }

    /* Try primitive JSON conversion first (preserves types) */
    yyjson_mut_val *val = nmo_summary_primitive_to_json(out->json_doc, field_guid, value_ptr, value_size);
    if (val) {
        *out_val = val;
        return true;
    }

    /* Fall back to string representation */
    char buffer[NMO_SUMMARY_VALUE_BUFFER_SIZE];
    if (nmo_summary_format_value(registry, field_type, field_guid, value_ptr, value_size, buffer, sizeof(buffer))) {
        *out_val = nmo_summary_json_strcpy_safe(out->json_doc, buffer);
        return true;
    }

    *out_val = yyjson_mut_null(out->json_doc);
    return true;
}

/* ============================================================================
 * Field Visitor Context
 * ============================================================================ */

typedef struct {
    nmo_summary_output_t *out;
    const nmo_type_registry_t *registry;
    const nmo_type_descriptor_t *owner_type;
    const void *owner_instance;
    const nmo_summary_config_t *config;
    yyjson_mut_val *json_fields;  /* JSON array for fields */
    int label_width;
    uint32_t depth;

    /* Stats collection */
    uint64_t field_count;
    uint64_t repeated_field_count;
    uint64_t reference_field_count;
    uint64_t optional_field_count;
    uint64_t object_ref_field_count;
} nmo_field_render_ctx_t;

static size_t nmo_summary_guess_element_size(nmo_guid_t field_guid, const nmo_type_descriptor_t *field_type) {
    if (field_type && field_type->size > 0) {
        return (size_t)field_type->size;
    }

    uint32_t size_bits = nmo_summary_get_field_size_bits(field_guid);
    if (size_bits > 0) {
        return (size_t)((size_bits + 7) / 8);
    }

    /* Default fallback */
    return sizeof(uint32_t);
}

static bool nmo_summary_read_u64_field(
    const void *owner_instance,
    const nmo_type_field_t *field,
    uint64_t *out_value)
{
    if (!owner_instance || !field || !out_value) {
        return false;
    }

    if (field->size != 1 && field->size != 2 && field->size != 4 && field->size != 8) {
        return false;
    }

    const void *ptr = nmo_field_get_ptr_const(owner_instance, field);
    if (!ptr) {
        return false;
    }

    uint64_t value = 0;
    if (field->size == 1) value = *(const uint8_t*)ptr;
    else if (field->size == 2) value = *(const uint16_t*)ptr;
    else if (field->size == 4) value = *(const uint32_t*)ptr;
    else if (field->size == 8) value = *(const uint64_t*)ptr;
    *out_value = value;
    return true;
}

static bool nmo_summary_str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) {
        return false;
    }
    const size_t slen = strlen(str);
    const size_t tlen = strlen(suffix);
    if (tlen > slen) {
        return false;
    }
    return memcmp(str + (slen - tlen), suffix, tlen) == 0;
}

static size_t nmo_summary_common_prefix_len(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return i;
}

static bool nmo_summary_is_count_field_name(const char *name) {
    return name && nmo_summary_str_ends_with(name, "_count");
}

static uint64_t nmo_summary_guess_array_count(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field)
{
    if (!owner_type || !owner_instance || !field) {
        return 0;
    }

    /* Look for a count field with naming convention: {field_name}_count */
    char count_field_name[128];
    snprintf(count_field_name, sizeof(count_field_name), "%s_count", field->name);

    const nmo_type_field_t *count_field = nmo_type_get_field_by_name(owner_type, count_field_name);
    uint64_t count = 0;
    if (count_field && nmo_summary_read_u64_field(owner_instance, count_field, &count)) {
        return count;
    }

    /* Alternative: {field_name}s -> {field_name}_count (plural removal) */
    size_t name_len = strlen(field->name);
    if (name_len > 1 && field->name[name_len - 1] == 's') {
        char singular[128];
        snprintf(singular, sizeof(singular), "%.*s_count", (int)(name_len - 1), field->name);
        count_field = nmo_type_get_field_by_name(owner_type, singular);
        if (count_field && nmo_summary_read_u64_field(owner_instance, count_field, &count)) {
            return count;
        }
    }

    /* Fallback: choose the best matching "*_count" field by prefix similarity.
     * This handles common mismatches like "vertices" -> "vertex_count" without hardcoding plural rules. */
    const size_t owner_field_count = nmo_type_get_field_count(owner_type);
    const nmo_type_field_t *best_field = NULL;
    size_t best_score = 0;

    for (size_t i = 0; i < owner_field_count; ++i) {
        const nmo_type_field_t *cand = nmo_type_get_field_by_index(owner_type, i);
        if (!cand || !cand->name) {
            continue;
        }
        if (cand->flags & NMO_FIELD_REPEATED) {
            continue;
        }
        if (!nmo_summary_str_ends_with(cand->name, "_count")) {
            continue;
        }
        if (cand->size != 1 && cand->size != 2 && cand->size != 4 && cand->size != 8) {
            continue;
        }

        const size_t cand_len = strlen(cand->name);
        const size_t base_len = cand_len - strlen("_count");
        if (base_len == 0) {
            continue;
        }

        /* Compare repeated field name with base name (without "_count"). */
        char base_buf[128];
        if (base_len >= sizeof(base_buf)) {
            continue;
        }
        memcpy(base_buf, cand->name, base_len);
        base_buf[base_len] = '\0';

        size_t score = nmo_summary_common_prefix_len(field->name, base_buf);
        if (score > best_score) {
            best_score = score;
            best_field = cand;
        }
    }

    if (best_field) {
        /* Require some similarity to avoid spurious matches. */
        const size_t min_score = (strlen(field->name) <= 3) ? 1 : 3;
        if (best_score >= min_score && nmo_summary_read_u64_field(owner_instance, best_field, &count)) {
            return count;
        }
    }

    return 0;
}

static bool nmo_summary_render_field(void *user_data, const nmo_type_field_t *field, const void *field_ptr) {
    nmo_field_render_ctx_t *ctx = (nmo_field_render_ctx_t*)user_data;
    if (!ctx || !ctx->out || !field || !field->name) {
        return true;
    }

    ctx->field_count++;

    /* Collect stats */
    if (field->flags & NMO_FIELD_OPTIONAL) ctx->optional_field_count++;
    if (field->flags & NMO_FIELD_REPEATED) ctx->repeated_field_count++;
    if (field->flags & NMO_FIELD_REFERENCE) ctx->reference_field_count++;
    if (nmo_summary_is_object_ref_field(field)) ctx->object_ref_field_count++;

    /* Look up field type for advanced formatting */
    const nmo_type_descriptor_t *field_type = NULL;
    if (ctx->registry) {
        field_type = nmo_type_registry_find_by_guid(ctx->registry, field->type_guid);
        if (!field_type && nmo_guid_is_field_type(field->type_guid)) {
            nmo_guid_t mapped = nmo_guid_field_to_type(field->type_guid);
            field_type = nmo_type_registry_find_by_guid(ctx->registry, mapped);
        }
    }

    /* Handle repeated (array) fields */
    if (field->flags & NMO_FIELD_REPEATED) {
        const void *array_ptr = field_ptr ? *(const void*const*)field_ptr : NULL;
        uint64_t count = nmo_summary_guess_array_count(ctx->owner_type, ctx->owner_instance, field);

        if (ctx->out->is_json) {
            yyjson_mut_val *item = yyjson_mut_obj(ctx->out->json_doc);
            nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "name", field->name);
            yyjson_mut_obj_add_bool(ctx->out->json_doc, item, "is_array", true);
            yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "count", count);

            /* Preview array elements */
            if (array_ptr && count > 0) {
                yyjson_mut_val *preview = yyjson_mut_arr(ctx->out->json_doc);
                size_t elem_size = nmo_summary_guess_element_size(field->type_guid, field_type);
                uint64_t emit = count < ctx->config->array_preview_max ? count : ctx->config->array_preview_max;

                for (uint64_t i = 0; i < emit; ++i) {
                    const uint8_t *elem_ptr = (const uint8_t*)array_ptr + i * elem_size;
                    yyjson_mut_val *elem_val = NULL;
                    nmo_summary_format_value_to_json(ctx->out, ctx->registry, field_type,
                                                     field->type_guid, elem_ptr, elem_size, &elem_val);
                    yyjson_mut_arr_add_val(preview, elem_val ? elem_val : yyjson_mut_null(ctx->out->json_doc));
                }
                yyjson_mut_obj_add_val(ctx->out->json_doc, item, "preview", preview);
            }

            yyjson_mut_arr_add_val(ctx->json_fields, item);
        } else {
            /* Text output for arrays */
            char label[128];
            snprintf(label, sizeof(label), "%s[%llu]", field->name, (unsigned long long)count);

            if (!array_ptr || count == 0) {
                nmo_cli_print_kv(ctx->out->stream, label, "(empty)", ctx->label_width, ctx->out->colorize);
            } else {
                size_t elem_size = nmo_summary_guess_element_size(field->type_guid, field_type);
                uint64_t emit = count < ctx->config->text_preview_max ? count : ctx->config->text_preview_max;

                char preview[512] = "";
                size_t pos = 0;
                for (uint64_t i = 0; i < emit && pos < sizeof(preview) - 32; ++i) {
                    const uint8_t *elem_ptr = (const uint8_t*)array_ptr + i * elem_size;
                    char elem_buf[256];
                    nmo_summary_format_value(ctx->registry, field_type, field->type_guid,
                                            elem_ptr, elem_size, elem_buf, sizeof(elem_buf));
                    if (i > 0) {
                        pos += snprintf(preview + pos, sizeof(preview) - pos, ", ");
                    }
                    pos += snprintf(preview + pos, sizeof(preview) - pos, "%s", elem_buf);
                }
                if (count > emit) {
                    snprintf(preview + pos, sizeof(preview) - pos, " ... (+%llu)", (unsigned long long)(count - emit));
                }
                nmo_cli_print_kv(ctx->out->stream, label, preview, ctx->label_width, ctx->out->colorize);
            }
        }
        return true;
    }

    /* Handle scalar fields */
    char value_buf[NMO_SUMMARY_VALUE_BUFFER_SIZE];
    bool is_count = nmo_summary_is_count_field_name(field->name);
    uint64_t count_value = 0;
    if (is_count && field_ptr && nmo_summary_read_u64_field(ctx->owner_instance, field, &count_value)) {
        (void)snprintf(value_buf, sizeof(value_buf), "%llu", (unsigned long long)count_value);
    } else {
        nmo_summary_format_value(ctx->registry, field_type, field->type_guid,
                                field_ptr, field->size, value_buf, sizeof(value_buf));
    }

    if (ctx->out->is_json) {
        yyjson_mut_val *item = yyjson_mut_obj(ctx->out->json_doc);
        nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "name", field->name);

        if (is_count && nmo_summary_read_u64_field(ctx->owner_instance, field, &count_value)) {
            yyjson_mut_obj_add_uint(ctx->out->json_doc, item, "value", count_value);
        } else {
            yyjson_mut_val *json_val = NULL;
            nmo_summary_format_value_to_json(ctx->out, ctx->registry, field_type,
                                             field->type_guid, field_ptr, field->size, &json_val);
            yyjson_mut_obj_add_val(ctx->out->json_doc, item, "value", json_val);
        }

        /* Add string representation for complex types */
        if (field_type || !nmo_summary_is_field_guid(field->type_guid)) {
            nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "value_str", value_buf);
        }

        /* Resolve object reference names */
        if (nmo_summary_is_object_ref_field(field) && field_ptr && ctx->config->resolve_object_refs) {
            nmo_object_id_t id = *(const nmo_object_id_t*)field_ptr;
            const char *name = nmo_summary_resolve_object_name(ctx->out, id);
            if (name) {
                nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "ref_name", name);
            }
        }

        yyjson_mut_arr_add_val(ctx->json_fields, item);
    } else {
        /* Text output with object reference resolution */
        if (nmo_summary_is_object_ref_field(field) && field_ptr && ctx->config->resolve_object_refs) {
            nmo_object_id_t id = *(const nmo_object_id_t*)field_ptr;
            const char *name = nmo_summary_resolve_object_name(ctx->out, id);
            if (name) {
                char ref_buf[256];
                snprintf(ref_buf, sizeof(ref_buf), "#%u (%s)", id, name);
                nmo_cli_print_kv(ctx->out->stream, field->name, ref_buf, ctx->label_width, ctx->out->colorize);
            } else {
                nmo_cli_print_kv(ctx->out->stream, field->name, value_buf, ctx->label_width, ctx->out->colorize);
            }
        } else {
            nmo_cli_print_kv(ctx->out->stream, field->name, value_buf, ctx->label_width, ctx->out->colorize);
        }
    }

    return true;
}

/* ============================================================================
 * Summary Sections
 * ============================================================================ */

static bool nmo_summary_emit_reflection_fields(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config)
{
    if (!obj || !out) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    if (!registry) {
        return false;
    }

    const nmo_type_descriptor_t *type = nmo_summary_get_type_for_object(registry, obj);
    if (!type || !nmo_type_has_reflection(type)) {
        if (!out->is_json) {
            nmo_summary_add_section(out, "Fields");
            fprintf(out->stream, "  (no reflection data available)\n");
        }
        return false;
    }

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        return false;
    }

    nmo_summary_add_section(out, "Fields");

    if (!out->is_json) {
        char type_info[128];
        snprintf(type_info, sizeof(type_info), "%s (%zu fields)",
                type->name ? type->name : "(unnamed)",
                nmo_type_get_field_count(type));
        nmo_cli_print_kv(out->stream, "Schema", type_info, 18, out->colorize);
        fprintf(out->stream, "\n");
    }

    nmo_field_render_ctx_t ctx = {
        .out = out,
        .registry = registry,
        .owner_type = type,
        .owner_instance = state,
        .config = config,
        .json_fields = out->is_json ? yyjson_mut_arr(out->json_doc) : NULL,
        .label_width = 24,
        .depth = 0,
    };

    nmo_type_foreach_field(type, state, nmo_summary_render_field, &ctx);

    if (out->is_json) {
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "fields", ctx.json_fields);

        /* Add stats */
        yyjson_mut_val *stats = yyjson_mut_obj(out->json_doc);
        yyjson_mut_obj_add_uint(out->json_doc, stats, "total_fields", ctx.field_count);
        yyjson_mut_obj_add_uint(out->json_doc, stats, "array_fields", ctx.repeated_field_count);
        yyjson_mut_obj_add_uint(out->json_doc, stats, "reference_fields", ctx.reference_field_count);
        yyjson_mut_obj_add_uint(out->json_doc, stats, "optional_fields", ctx.optional_field_count);
        yyjson_mut_obj_add_uint(out->json_doc, stats, "object_ref_fields", ctx.object_ref_field_count);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "stats", stats);
    }

    return true;
}

static bool nmo_summary_emit_enrichments(nmo_object_t *obj, nmo_summary_output_t *out) {
    if (!obj || !out || g_enricher_count == 0) {
        return false;
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const void *state = nmo_object_get_state(obj);

    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (g_enrichers[i].class_id == class_id && g_enrichers[i].enricher) {
            return g_enrichers[i].enricher(obj, state, out);
        }
    }

    return false;
}

/* ============================================================================
 * Enricher Registry
 * ============================================================================ */

void nmo_summary_register_enricher(nmo_class_id_t class_id, nmo_summary_enricher_fn enricher) {
    if (g_enricher_count >= NMO_SUMMARY_MAX_ENRICHERS) {
        return;
    }

    /* Check for duplicate */
    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (g_enrichers[i].class_id == class_id) {
            g_enrichers[i].enricher = enricher;
            return;
        }
    }

    g_enrichers[g_enricher_count++] = (nmo_enricher_entry_t){
        .class_id = class_id,
        .enricher = enricher,
    };
}

bool nmo_summary_has_enricher(nmo_class_id_t class_id) {
    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (g_enrichers[i].class_id == class_id) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Built-in Enrichers (Minimal - Computed Values Only)
 * ============================================================================ */

/* Mesh enricher: total face count = sum(face_counts per channel) */
static bool nmo_enricher_mesh(nmo_object_t *obj, const void *state, nmo_summary_output_t *out) {
    (void)obj;
    if (!state || !out) return false;

    /* Access via reflection rather than direct struct cast */
    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    const nmo_type_descriptor_t *type = nmo_summary_get_type_for_object(registry, obj);
    if (!type) return false;

    /* Look for vertex_count file */
    const nmo_type_field_t *vert_field = nmo_type_get_field_by_name(type, "vertex_count");
    const nmo_type_field_t *channel_field = nmo_type_get_field_by_name(type, "channel_count");

    if (vert_field && channel_field) {
        uint32_t verts = nmo_field_get_uint32(state, vert_field);
        uint32_t channels = nmo_field_get_uint32(state, channel_field);

        nmo_summary_add_section(out, "Mesh Summary");
        nmo_summary_add_uint(out, "Total Vertices", verts, 18);
        nmo_summary_add_uint(out, "Channels", channels, 18);
    }

    return true;
}

/* Behavior enricher: complexity metrics */
static bool nmo_enricher_behavior(nmo_object_t *obj, const void *state, nmo_summary_output_t *out) {
    (void)obj;
    if (!state || !out) return false;

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    const nmo_type_descriptor_t *type = nmo_summary_get_type_for_object(registry, obj);
    if (!type) return false;

    /* Compute complexity = sub_behaviors + links + operations */
    const nmo_type_field_t *sub_beh = nmo_type_get_field_by_name(type, "sub_behavior_count");
    const nmo_type_field_t *links = nmo_type_get_field_by_name(type, "sub_behavior_link_count");
    const nmo_type_field_t *ops = nmo_type_get_field_by_name(type, "operation_count");

    if (sub_beh && links && ops) {
        uint32_t total = nmo_field_get_uint32(state, sub_beh) +
                        nmo_field_get_uint32(state, links) +
                        nmo_field_get_uint32(state, ops);

        nmo_summary_add_section(out, "Behavior Summary");
        nmo_summary_add_uint(out, "Complexity Score", total, 18);
    }

    return true;
}

void nmo_summary_init_builtin_enrichers(void) {
    if (g_enrichers_initialized) return;

    nmo_summary_register_enricher(NMO_CID_MESH, nmo_enricher_mesh);
    nmo_summary_register_enricher(NMO_CID_BEHAVIOR, nmo_enricher_behavior);

    g_enrichers_initialized = true;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void nmo_summary_init(void) {
    nmo_summary_init_builtin_enrichers();
}

bool nmo_summary_has_reflection(nmo_context_t *ctx, nmo_class_id_t class_id) {
    if (!ctx) return false;

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) return false;

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id_inherited(registry, class_id);
    return type && nmo_type_has_reflection(type);
}

bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out) {
    nmo_summary_config_t config = nmo_summary_config_default();
    return nmo_object_summary_with_config(obj, out, &config);
}

bool nmo_object_summary_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config)
{
    if (!obj || !out) return false;

    /* Lazy auto-init enrichers on first use */
    if (!g_enrichers_initialized) {
        nmo_summary_init_builtin_enrichers();
    }

    nmo_summary_config_t cfg = config ? *config : nmo_summary_config_default();
    bool emitted = false;

    if (out->is_json) {
        /* JSON: add reflection fields + enrichments directly to json_data */
        emitted |= nmo_summary_emit_reflection_fields(obj, out, &cfg);
        emitted |= nmo_summary_emit_enrichments(obj, out);
    } else {
        /* Text: emit reflection fields + enrichments */
        emitted |= nmo_summary_emit_reflection_fields(obj, out, &cfg);
        emitted |= nmo_summary_emit_enrichments(obj, out);
    }

    return emitted;
}

/* ============================================================================
 * Output Helper Functions
 * ============================================================================ */

void nmo_summary_add_section(nmo_summary_output_t *out, const char *title) {
    if (!out || !title) return;

    if (!out->is_json) {
        fprintf(out->stream, "\n");
        nmo_cli_print_heading(out->stream, title, out->colorize);
    }
    /* JSON: sections are implicit via object nesting */
}

void nmo_summary_add_string(nmo_summary_output_t *out, const char *key,
                            const char *value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        nmo_cli_json_add_str_safe(out->json_doc, out->json_data, key, value ? value : "-");
    } else {
        nmo_cli_print_kv(out->stream, key, value ? value : "-", label_width, out->colorize);
    }
}

void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                         int64_t value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        yyjson_mut_obj_add_sint(out->json_doc, out->json_data, key, value);
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
        yyjson_mut_obj_add_uint(out->json_doc, out->json_data, key, value);
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
        if (isnan(value) || isinf(value)) {
            yyjson_mut_obj_add_null(out->json_doc, out->json_data, key);
        } else {
            yyjson_mut_obj_add_real(out->json_doc, out->json_data, key, value);
        }
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6g", value);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                          bool value, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        yyjson_mut_obj_add_bool(out->json_doc, out->json_data, key, value);
    } else {
        nmo_cli_print_kv(out->stream, key, value ? "true" : "false", label_width, out->colorize);
    }
}

void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                nmo_object_id_t id, const char *name, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        yyjson_mut_val *ref = yyjson_mut_obj(out->json_doc);
        yyjson_mut_obj_add_uint(out->json_doc, ref, "id", id);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(out->json_doc, ref, "name", name);
        }
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, key, ref);
    } else {
        char buf[128];
        if (id == 0) {
            snprintf(buf, sizeof(buf), "(none)");
        } else if (name && name[0]) {
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

    if (out->is_json) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", value);
        nmo_cli_json_add_str_safe(out->json_doc, out->json_data, key, buf);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", value);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                             float x, float y, float z, int label_width) {
    if (!out || !key) return;

    if (out->is_json) {
        yyjson_mut_val *arr = yyjson_mut_arr(out->json_doc);
        yyjson_mut_arr_add_real(out->json_doc, arr, (double)x);
        yyjson_mut_arr_add_real(out->json_doc, arr, (double)y);
        yyjson_mut_arr_add_real(out->json_doc, arr, (double)z);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, key, arr);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%.4g, %.4g, %.4g)", x, y, z);
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
        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", a, r, g, b);
        nmo_cli_json_add_str_safe(out->json_doc, color, "hex", hex);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, key, color);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X (ARGB)", a, r, g, b);
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

void nmo_summary_add_guid(nmo_summary_output_t *out, const char *key,
                          nmo_guid_t guid, int label_width) {
    if (!out || !key) return;

    char buf[32];
    nmo_guid_format(guid, buf, sizeof(buf));

    if (out->is_json) {
        nmo_cli_json_add_str_safe(out->json_doc, out->json_data, key, buf);
    } else {
        nmo_cli_print_kv(out->stream, key, buf, label_width, out->colorize);
    }
}

/* ============================================================================
 * Backward Compatibility Functions
 * These delegate to the generic reflection-based summary system.
 * ============================================================================ */

bool nmo_summary_has_handler(nmo_class_id_t class_id) {
    /* In the new system, all types with reflection have "handlers" */
    /* This is a simplified check - ideally we'd check the context */
    return nmo_summary_has_enricher(class_id) ||
           (class_id >= NMO_CID_OBJECT && class_id < NMO_CID_MAXCLASSID);
}

/* Type-specific summary functions - all delegate to generic */
bool nmo_summary_ck3dentity(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckmesh(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckmaterial(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_cktexture(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckcamera(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_cklight(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckbehavior(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckscene(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_cklevel(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}

bool nmo_summary_ckparameter(nmo_object_t *obj, nmo_summary_output_t *out) {
    return nmo_object_summary(obj, out);
}
