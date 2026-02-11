/**
 * @file nmo_object_summary.c
 * @brief Object semantic summary system (Reflection-first implementation)
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
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "object/nmo_param_guids.h"
#include "core/nmo_guid.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "object/nmo_object_guids.h"
#include "core/nmo_array.h"

#include "dsl/nmo_dsl.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define NMO_SUMMARY_DEFAULT_ARRAY_PREVIEW_MAX   16u
#define NMO_SUMMARY_DEFAULT_TEXT_PREVIEW_MAX    8u
#define NMO_SUMMARY_DEFAULT_MAX_DEPTH           2u
#define NMO_SUMMARY_MAX_ENRICHERS               64u
#define NMO_SUMMARY_VALUE_BUFFER_SIZE           512u

nmo_summary_config_t nmo_summary_config_default(void) {
    nmo_summary_config_t config = {
        .array_preview_max = NMO_SUMMARY_DEFAULT_ARRAY_PREVIEW_MAX,
        .text_preview_max = NMO_SUMMARY_DEFAULT_TEXT_PREVIEW_MAX,
        .max_depth = NMO_SUMMARY_DEFAULT_MAX_DEPTH,
        .show_field_metadata = false,
        .resolve_object_refs = true,
        .format_enum_names = true,
        .format_flags_names = true,
    };
    return config;
}

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
    nmo_guid_t base_guid;
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
static bool nmo_summary_is_object_ref_field(const nmo_type_field_t *field);

/* Value formatting */
static bool nmo_summary_format_via_type_system(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *value_ptr,
    char *buffer,
    size_t buffer_size);

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
 * Summary Context Helpers
 * ============================================================================ */

static const nmo_type_registry_t *nmo_summary_get_registry(const nmo_summary_output_t *out) {
    if (!out) return NULL;
    nmo_context_t *ctx = out->ctx;
    if (!ctx && out->session) {
        ctx = nmo_session_get_context(out->session);
    }
    if (!ctx) return NULL;
    return nmo_context_get_type_registry(ctx);
}

static const char *nmo_summary_resolve_object_name(const nmo_summary_output_t *out, nmo_object_id_t id) {
    if (!out || id == 0) return NULL;
    if (!out->session) return NULL;

    nmo_object_repository_t *repo = nmo_session_get_repository(out->session);
    if (!repo) return NULL;
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) return NULL;
    return nmo_object_get_name(obj);
}

static const nmo_type_descriptor_t *nmo_summary_get_type_for_object(
    const nmo_type_registry_t *registry, nmo_object_t *obj)
{
    if (!registry || !obj) return NULL;
    if (!nmo_guid_is_null(obj->type_guid)) {
        return nmo_type_registry_find_by_guid(registry, obj->type_guid);
    }
    return nmo_type_registry_find_by_class_id_inherited(registry, obj->class_id);
}

static bool nmo_summary_is_object_ref_field(const nmo_type_field_t *field) {
    if (!field) return false;
    if (field->flags & NMO_FIELD_REFERENCE) return true;
    if (field->semantic == NMO_SEMANTIC_OBJECT_REF) return true;
    return nmo_guid_equals(field->type_guid, CKPGUID_ID);
}

/* ============================================================================
 * Value Formatting (Text)
 * ============================================================================ */

static const nmo_type_descriptor_t *nmo_summary_resolve_value_type(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid)
{
    if (field_type) {
        return field_type;
    }
    if (!registry) {
        return NULL;
    }
    return nmo_type_registry_find_by_guid(registry, field_guid);
}

/* ============================================================================
 * Value Formatting (JSON)
 * ============================================================================ */

static bool nmo_summary_format_value_string(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *field_type,
    nmo_guid_t field_guid,
    const void *value_ptr,
    size_t value_size,
    char *buffer,
    size_t buffer_size)
{
    const nmo_type_descriptor_t *type = nmo_summary_resolve_value_type(registry, field_type, field_guid);
    if (type && nmo_summary_format_via_type_system(registry, type, value_ptr, buffer, buffer_size)) {
        return true;
    }

    if (value_size <= 8) {
        uint64_t v = 0;
        memcpy(&v, value_ptr, value_size < 8 ? value_size : 8);
        snprintf(buffer, buffer_size, "0x%llx", (unsigned long long)v);
        return true;
    }

    snprintf(buffer, buffer_size, "<binary %zu bytes>", value_size);
    return true;
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

    return nmo_summary_format_value_string(
        registry, field_type, field_guid, value_ptr, value_size, buffer, buffer_size);
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

    /* Use type system string representation for JSON values */
    char buffer[NMO_SUMMARY_VALUE_BUFFER_SIZE];
    if (nmo_summary_format_value_string(registry, field_type, field_guid,
                                        value_ptr, value_size, buffer, sizeof(buffer))) {
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

    if (nmo_guid_equals(field_guid, CKPGUID_BOOL) ||
        nmo_guid_equals(field_guid, CKPGUID_INT8) ||
        nmo_guid_equals(field_guid, CKPGUID_UINT8)) {
        return 1;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_INT16) ||
        nmo_guid_equals(field_guid, CKPGUID_UINT16)) {
        return 2;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_INT) ||
        nmo_guid_equals(field_guid, CKPGUID_UINT32) ||
        nmo_guid_equals(field_guid, CKPGUID_FLOAT) ||
        nmo_guid_equals(field_guid, CKPGUID_ID)) {
        return 4;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_INT64) ||
        nmo_guid_equals(field_guid, CKPGUID_UINT64) ||
        nmo_guid_equals(field_guid, CKPGUID_DOUBLE) ||
        nmo_guid_equals(field_guid, CKPGUID_GUID)) {
        return 8;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_2DVECTOR)) {
        return sizeof(float) * 2u;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_VECTOR)) {
        return sizeof(float) * 3u;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_VECTOR4) ||
        nmo_guid_equals(field_guid, CKPGUID_QUATERNION) ||
        nmo_guid_equals(field_guid, CKPGUID_COLOR) ||
        nmo_guid_equals(field_guid, CKPGUID_RECT)) {
        return sizeof(float) * 4u;
    }

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

    if (field->flags & NMO_FIELD_REPEATED) {
        const void *field_ptr = nmo_field_get_ptr_const(owner_instance, field);
        if (field_ptr && field->size == sizeof(nmo_array_t)) {
            const nmo_array_t *array = (const nmo_array_t *)field_ptr;
            return (uint64_t)array->count;
        }
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

/* ============================================================================
 * Field Path Selection (dot navigation + [index])
 * ============================================================================ */

typedef enum {
    NMO_SELECT_STATUS_OK = 0,
    NMO_SELECT_STATUS_INVALID_ARG,
    NMO_SELECT_STATUS_PARSE_ERROR,
    NMO_SELECT_STATUS_NO_REFLECTION,
    NMO_SELECT_STATUS_FIELD_NOT_FOUND,
    NMO_SELECT_STATUS_INDEX_REQUIRED,
    NMO_SELECT_STATUS_INDEX_OOB,
    NMO_SELECT_STATUS_CANNOT_TRAVERSE,
} nmo_select_status_t;

static const nmo_type_descriptor_t *nmo_summary_lookup_field_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t field_guid)
{
    if (!registry) {
        return NULL;
    }

    return nmo_type_registry_find_by_guid(registry, field_guid);
}

static bool nmo_summary_parse_ident(const char **p, char *out, size_t out_cap) {
    if (!p || !*p || !out || out_cap == 0) {
        return false;
    }

    const char *s = *p;
    if (*s == '\0' || *s == '.' || *s == '[' || *s == ']') {
        return false;
    }

    size_t len = 0;
    while (*s && *s != '.' && *s != '[' && *s != ']') {
        if (len + 1 >= out_cap) {
            return false;
        }
        out[len++] = *s++;
    }
    out[len] = '\0';
    *p = s;
    return len > 0;
}

static bool nmo_summary_parse_index(const char **p, uint64_t *out_index) {
    if (!p || !*p || !out_index) {
        return false;
    }

    const char *s = *p;
    if (*s != '[') {
        return false;
    }
    s++;

    if (!isdigit((unsigned char)*s)) {
        return false;
    }

    uint64_t idx = 0;
    while (isdigit((unsigned char)*s)) {
        uint64_t d = (uint64_t)(*s - '0');
        if (idx > (UINT64_MAX - d) / 10u) {
            return false;
        }
        idx = (idx * 10u) + d;
        s++;
    }

    if (*s != ']') {
        return false;
    }
    s++;

    *out_index = idx;
    *p = s;
    return true;
}

static void nmo_summary_emit_select_error(
    nmo_summary_output_t *out,
    yyjson_mut_val *json_select_arr,
    const char *path,
    nmo_select_status_t status,
    const char *detail)
{
    const char *msg = "error";
    switch (status) {
        case NMO_SELECT_STATUS_INVALID_ARG: msg = "invalid argument"; break;
        case NMO_SELECT_STATUS_PARSE_ERROR: msg = "parse error"; break;
        case NMO_SELECT_STATUS_NO_REFLECTION: msg = "no reflection"; break;
        case NMO_SELECT_STATUS_FIELD_NOT_FOUND: msg = "field not found"; break;
        case NMO_SELECT_STATUS_INDEX_REQUIRED: msg = "index required"; break;
        case NMO_SELECT_STATUS_INDEX_OOB: msg = "index out of bounds"; break;
        case NMO_SELECT_STATUS_CANNOT_TRAVERSE: msg = "cannot traverse"; break;
        default: msg = "error"; break;
    }

    if (out->is_json) {
        yyjson_mut_val *item = yyjson_mut_obj(out->json_doc);
        nmo_cli_json_add_str_safe(out->json_doc, item, "path", path ? path : "-");
        yyjson_mut_obj_add_bool(out->json_doc, item, "ok", false);
        nmo_cli_json_add_str_safe(out->json_doc, item, "error", msg);
        if (detail && detail[0]) {
            nmo_cli_json_add_str_safe(out->json_doc, item, "detail", detail);
        }
        yyjson_mut_arr_add_val(json_select_arr, item);
    } else {
        char buf[256];
        if (detail && detail[0]) {
            snprintf(buf, sizeof(buf), "<%s: %s>", msg, detail);
        } else {
            snprintf(buf, sizeof(buf), "<%s>", msg);
        }
        nmo_cli_print_kv(out->stream, path ? path : "-", buf, 30, out->colorize);
    }
}

static bool nmo_summary_emit_select_path(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    yyjson_mut_val *json_select_arr,
    const nmo_summary_config_t *config,
    const char *path)
{
    if (!obj || !out || !config || !path || !path[0]) {
        nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INVALID_ARG, NULL);
        return false;
    }

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    if (!registry) {
        nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INVALID_ARG, "no registry");
        return false;
    }

    const nmo_type_descriptor_t *root_type = nmo_summary_get_type_for_object(registry, obj);
    if (!root_type || !nmo_type_has_reflection(root_type)) {
        nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_NO_REFLECTION, NULL);
        return false;
    }

    const void *root_state = nmo_object_get_state(obj);
    if (!root_state) {
        nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INVALID_ARG, "no state");
        return false;
    }

    const nmo_type_descriptor_t *cur_type = root_type;
    const void *cur_instance = root_state;

    const char *p = path;
    bool emitted = false;

    while (*p) {
        char field_name[128];
        if (!nmo_summary_parse_ident(&p, field_name, sizeof(field_name))) {
            nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_PARSE_ERROR, "bad identifier");
            return false;
        }

        bool has_index = false;
        uint64_t index = 0;
        if (*p == '[') {
            has_index = nmo_summary_parse_index(&p, &index);
            if (!has_index) {
                nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_PARSE_ERROR, "bad index" );
                return false;
            }
        }

        const nmo_type_field_t *field = nmo_type_get_field_by_name(cur_type, field_name);
        if (!field) {
            nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_FIELD_NOT_FOUND, field_name);
            return false;
        }

        const void *field_ptr = nmo_field_get_ptr_const(cur_instance, field);
        const nmo_type_descriptor_t *field_type = nmo_summary_lookup_field_type(registry, field->type_guid);

        const bool is_last = (*p == '\0');
        const bool has_more = (*p == '.');
        if (has_more) {
            p++; /* consume '.' */
        } else if (!is_last) {
            nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_PARSE_ERROR, "unexpected character");
            return false;
        }

        if (field->flags & NMO_FIELD_REPEATED) {
            const void *array_ptr = field_ptr ? *(const void *const *)field_ptr : NULL;
            uint64_t count = nmo_summary_guess_array_count(cur_type, cur_instance, field);

            if (!has_index) {
                if (!is_last) {
                    nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INDEX_REQUIRED, field_name);
                    return false;
                }

                /* Selecting the array itself: emit a preview like the Fields section. */
                if (out->is_json) {
                    yyjson_mut_val *item = yyjson_mut_obj(out->json_doc);
                    nmo_cli_json_add_str_safe(out->json_doc, item, "path", path);
                    yyjson_mut_obj_add_bool(out->json_doc, item, "ok", true);
                    yyjson_mut_obj_add_bool(out->json_doc, item, "is_array", true);
                    yyjson_mut_obj_add_uint(out->json_doc, item, "count", count);

                    if (array_ptr && count > 0) {
                        yyjson_mut_val *preview = yyjson_mut_arr(out->json_doc);
                        size_t elem_size = nmo_summary_guess_element_size(field->type_guid, field_type);
                        uint64_t emit_n = count < config->array_preview_max ? count : config->array_preview_max;
                        for (uint64_t i = 0; i < emit_n; ++i) {
                            const uint8_t *elem_ptr = (const uint8_t *)array_ptr + i * elem_size;
                            yyjson_mut_val *elem_val = NULL;
                            nmo_summary_format_value_to_json(out, registry, field_type,
                                                            field->type_guid, elem_ptr, elem_size, &elem_val);
                            yyjson_mut_arr_add_val(preview, elem_val ? elem_val : yyjson_mut_null(out->json_doc));
                        }
                        yyjson_mut_obj_add_val(out->json_doc, item, "preview", preview);
                    }

                    yyjson_mut_arr_add_val(json_select_arr, item);
                } else {
                    char label[256];
                    snprintf(label, sizeof(label), "%s[%llu]", path, (unsigned long long)count);

                    if (!array_ptr || count == 0) {
                        nmo_cli_print_kv(out->stream, label, "(empty)", 30, out->colorize);
                    } else {
                        size_t elem_size = nmo_summary_guess_element_size(field->type_guid, field_type);
                        uint64_t emit_n = count < config->text_preview_max ? count : config->text_preview_max;

                        char preview[512] = "";
                        size_t pos = 0;
                        for (uint64_t i = 0; i < emit_n && pos < sizeof(preview) - 32; ++i) {
                            const uint8_t *elem_ptr = (const uint8_t *)array_ptr + i * elem_size;
                            char elem_buf[256];
                            nmo_summary_format_value(registry, field_type, field->type_guid,
                                                    elem_ptr, elem_size, elem_buf, sizeof(elem_buf));
                            if (i > 0) {
                                pos += snprintf(preview + pos, sizeof(preview) - pos, ", ");
                            }
                            pos += snprintf(preview + pos, sizeof(preview) - pos, "%s", elem_buf);
                        }
                        if (count > emit_n) {
                            snprintf(preview + pos, sizeof(preview) - pos, " ... (+%llu)",
                                     (unsigned long long)(count - emit_n));
                        }
                        nmo_cli_print_kv(out->stream, label, preview, 30, out->colorize);
                    }
                }
                emitted = true;
                return true;
            }

            /* Selecting an array element: validate and continue traversal or emit at end. */
            if (!array_ptr) {
                nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INDEX_OOB, "null array");
                return false;
            }
            if (index >= count) {
                char detail[128];
                snprintf(detail, sizeof(detail), "%.48s[%llu] (count=%llu)", field_name,
                         (unsigned long long)index, (unsigned long long)count);
                nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_INDEX_OOB, detail);
                return false;
            }

            size_t elem_size = nmo_summary_guess_element_size(field->type_guid, field_type);
            const uint8_t *elem_ptr = (const uint8_t *)array_ptr + (size_t)index * elem_size;

            if (is_last) {
                /* Emit element value */
                char value_buf[NMO_SUMMARY_VALUE_BUFFER_SIZE];
                nmo_summary_format_value(registry, field_type, field->type_guid, elem_ptr, elem_size,
                                        value_buf, sizeof(value_buf));

                if (out->is_json) {
                    yyjson_mut_val *item = yyjson_mut_obj(out->json_doc);
                    nmo_cli_json_add_str_safe(out->json_doc, item, "path", path);
                    yyjson_mut_obj_add_bool(out->json_doc, item, "ok", true);

                    yyjson_mut_val *json_val = NULL;
                    nmo_summary_format_value_to_json(out, registry, field_type,
                                                    field->type_guid, elem_ptr, elem_size, &json_val);
                    yyjson_mut_obj_add_val(out->json_doc, item, "value", json_val);
                    nmo_cli_json_add_str_safe(out->json_doc, item, "value_str", value_buf);

                    if (nmo_summary_is_object_ref_field(field) && config->resolve_object_refs) {
                        nmo_object_id_t id = (elem_size >= 4) ? *(const nmo_object_id_t *)elem_ptr : 0;
                        const char *ref_name = nmo_summary_resolve_object_name(out, id);
                        if (ref_name) {
                            nmo_cli_json_add_str_safe(out->json_doc, item, "ref_name", ref_name);
                        }
                    }

                    yyjson_mut_arr_add_val(json_select_arr, item);
                } else {
                    if (nmo_summary_is_object_ref_field(field) && config->resolve_object_refs) {
                        nmo_object_id_t id = (elem_size >= 4) ? *(const nmo_object_id_t *)elem_ptr : 0;
                        const char *ref_name = nmo_summary_resolve_object_name(out, id);
                        if (ref_name) {
                            char ref_buf[256];
                            snprintf(ref_buf, sizeof(ref_buf), "#%u (%s)", id, ref_name);
                            nmo_cli_print_kv(out->stream, path, ref_buf, 30, out->colorize);
                        } else {
                            nmo_cli_print_kv(out->stream, path, value_buf, 30, out->colorize);
                        }
                    } else {
                        nmo_cli_print_kv(out->stream, path, value_buf, 30, out->colorize);
                    }
                }
                emitted = true;
                return true;
            }

            /* Continue traversal into element (requires reflection type) */
            if (!field_type || !nmo_type_has_reflection(field_type)) {
                nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_CANNOT_TRAVERSE, field_name);
                return false;
            }
            cur_type = field_type;
            cur_instance = elem_ptr;
            continue;
        }

        /* Scalar field */
        if (has_index) {
            nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_PARSE_ERROR, "index on scalar");
            return false;
        }

        if (is_last) {
            char value_buf[NMO_SUMMARY_VALUE_BUFFER_SIZE];
            nmo_summary_format_value(registry, field_type, field->type_guid,
                                    field_ptr, field->size, value_buf, sizeof(value_buf));

            if (out->is_json) {
                yyjson_mut_val *item = yyjson_mut_obj(out->json_doc);
                nmo_cli_json_add_str_safe(out->json_doc, item, "path", path);
                yyjson_mut_obj_add_bool(out->json_doc, item, "ok", true);

                yyjson_mut_val *json_val = NULL;
                nmo_summary_format_value_to_json(out, registry, field_type,
                                                field->type_guid, field_ptr, field->size, &json_val);
                yyjson_mut_obj_add_val(out->json_doc, item, "value", json_val);
                nmo_cli_json_add_str_safe(out->json_doc, item, "value_str", value_buf);

                if (nmo_summary_is_object_ref_field(field) && field_ptr && config->resolve_object_refs) {
                    nmo_object_id_t id = *(const nmo_object_id_t *)field_ptr;
                    const char *ref_name = nmo_summary_resolve_object_name(out, id);
                    if (ref_name) {
                        nmo_cli_json_add_str_safe(out->json_doc, item, "ref_name", ref_name);
                    }
                }

                yyjson_mut_arr_add_val(json_select_arr, item);
            } else {
                if (nmo_summary_is_object_ref_field(field) && field_ptr && config->resolve_object_refs) {
                    nmo_object_id_t id = *(const nmo_object_id_t *)field_ptr;
                    const char *ref_name = nmo_summary_resolve_object_name(out, id);
                    if (ref_name) {
                        char ref_buf[256];
                        snprintf(ref_buf, sizeof(ref_buf), "#%u (%s)", id, ref_name);
                        nmo_cli_print_kv(out->stream, path, ref_buf, 30, out->colorize);
                    } else {
                        nmo_cli_print_kv(out->stream, path, value_buf, 30, out->colorize);
                    }
                } else {
                    nmo_cli_print_kv(out->stream, path, value_buf, 30, out->colorize);
                }
            }

            emitted = true;
            return true;
        }

        /* Continue traversal into scalar composite (requires reflection type) */
        if (!field_type || !nmo_type_has_reflection(field_type)) {
            nmo_summary_emit_select_error(out, json_select_arr, path, NMO_SELECT_STATUS_CANNOT_TRAVERSE, field_name);
            return false;
        }
        cur_type = field_type;
        cur_instance = field_ptr;
    }

    (void)emitted;
    return emitted;
}

/* ============================================================================
 * Query Expressions (C-like)
 * ============================================================================ */

static uint64_t nmo_summary_guess_array_count_cb(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    void *user)
{
    (void)user;
    return nmo_summary_guess_array_count(owner_type, owner_instance, field);
}

static const char *nmo_summary_resolve_object_name_cb(uint32_t id, void *user) {
    const nmo_summary_output_t *out = (const nmo_summary_output_t *)user;
    return nmo_summary_resolve_object_name(out, (nmo_object_id_t)id);
}

static bool nmo_summary_query_value_to_text(
    const nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_summary_config_t *config,
    const nmo_dsl_value_t *value,
    char *buffer,
    size_t buffer_size,
    uint64_t *out_seq_count)
{
    if (!buffer || buffer_size == 0) {
        return false;
    }
    buffer[0] = '\0';
    if (out_seq_count) {
        *out_seq_count = 0;
    }

    if (!value) {
        snprintf(buffer, buffer_size, "-");
        return true;
    }

    switch (value->kind) {
        case NMO_DSL_VALUE_NULL:
            snprintf(buffer, buffer_size, "null");
            return true;
        case NMO_DSL_VALUE_BOOL:
            snprintf(buffer, buffer_size, "%s", value->as.b ? "true" : "false");
            return true;
        case NMO_DSL_VALUE_INT:
            snprintf(buffer, buffer_size, "%lld", (long long)value->as.i);
            return true;
        case NMO_DSL_VALUE_UINT:
            snprintf(buffer, buffer_size, "%llu", (unsigned long long)value->as.u);
            return true;
        case NMO_DSL_VALUE_REAL:
            snprintf(buffer, buffer_size, "%.10g", value->as.r);
            return true;
        case NMO_DSL_VALUE_STRING:
            snprintf(buffer, buffer_size, "%s", value->as.s ? value->as.s : "");
            return true;

        case NMO_DSL_VALUE_BYREF:
            return nmo_summary_format_value(registry, value->as.byref.type, value->as.byref.guid,
                                            value->as.byref.ptr, value->as.byref.size,
                                            buffer, buffer_size);

        case NMO_DSL_VALUE_OBJECT: {
            if (!value->as.object.type || !value->as.object.instance) {
                snprintf(buffer, buffer_size, "-");
                return true;
            }
            nmo_status_t st = nmo_type_value_to_string(
                value->as.object.instance,
                value->as.object.type,
                registry,
                buffer,
                buffer_size);
            if (st == NMO_OK) {
                return true;
            }
            snprintf(buffer, buffer_size, "<object>");
            return true;
        }

        case NMO_DSL_VALUE_SEQ: {
            if (!value->as.seq) {
                snprintf(buffer, buffer_size, "(empty)");
                return true;
            }
            uint64_t count = nmo_dsl_seq_count(value->as.seq);
            if (out_seq_count) {
                *out_seq_count = count;
            }
            if (count == 0) {
                snprintf(buffer, buffer_size, "(empty)");
                return true;
            }

            uint64_t emit = count < config->text_preview_max ? count : config->text_preview_max;
            size_t pos = 0;
            for (uint64_t i = 0; i < emit && pos < buffer_size - 32; ++i) {
                nmo_dsl_value_t elem = {0};
                if (!nmo_dsl_seq_get(value->as.seq, i, &elem)) {
                    continue;
                }
                char elem_buf[256];
                uint64_t ignored = 0;
                nmo_summary_query_value_to_text(out, registry, config, &elem, elem_buf, sizeof(elem_buf), &ignored);
                if (i > 0) {
                    pos += snprintf(buffer + pos, buffer_size - pos, ", ");
                }
                pos += snprintf(buffer + pos, buffer_size - pos, "%s", elem_buf);
            }
            if (count > emit && pos < buffer_size - 1) {
                snprintf(buffer + pos, buffer_size - pos, " ... (+%llu)", (unsigned long long)(count - emit));
            }
            return true;
        }

        default:
            snprintf(buffer, buffer_size, "-");
            return true;
    }
}

static yyjson_mut_val *nmo_summary_query_value_to_json(
    const nmo_summary_output_t *out,
    const nmo_type_registry_t *registry,
    const nmo_summary_config_t *config,
    const nmo_dsl_value_t *value,
    uint64_t *out_seq_count)
{
    if (out_seq_count) {
        *out_seq_count = 0;
    }
    if (!out || !out->json_doc || !value) {
        return yyjson_mut_null(out->json_doc);
    }

    switch (value->kind) {
        case NMO_DSL_VALUE_NULL: return yyjson_mut_null(out->json_doc);
        case NMO_DSL_VALUE_BOOL: return yyjson_mut_bool(out->json_doc, value->as.b);
        case NMO_DSL_VALUE_INT: return yyjson_mut_sint(out->json_doc, value->as.i);
        case NMO_DSL_VALUE_UINT: return yyjson_mut_uint(out->json_doc, value->as.u);
        case NMO_DSL_VALUE_REAL:
            if (isnan(value->as.r) || isinf(value->as.r)) return yyjson_mut_null(out->json_doc);
            return yyjson_mut_real(out->json_doc, value->as.r);
        case NMO_DSL_VALUE_STRING:
            return nmo_summary_json_strcpy_safe(out->json_doc, value->as.s ? value->as.s : "");

        case NMO_DSL_VALUE_BYREF: {
            yyjson_mut_val *v = NULL;
            (void)nmo_summary_format_value_to_json(out, registry, value->as.byref.type,
                                                  value->as.byref.guid,
                                                  value->as.byref.ptr,
                                                  value->as.byref.size,
                                                  &v);
            return v ? v : yyjson_mut_null(out->json_doc);
        }

        case NMO_DSL_VALUE_OBJECT: {
            char buf[NMO_SUMMARY_VALUE_BUFFER_SIZE];
            buf[0] = '\0';
            if (value->as.object.type && value->as.object.instance) {
                (void)nmo_type_value_to_string(value->as.object.instance, value->as.object.type, registry,
                                               buf, sizeof(buf));
            }
            return nmo_summary_json_strcpy_safe(out->json_doc, buf[0] ? buf : "<object>");
        }

        case NMO_DSL_VALUE_SEQ: {
            yyjson_mut_val *arr = yyjson_mut_arr(out->json_doc);
            if (!value->as.seq) {
                return arr;
            }
            uint64_t count = nmo_dsl_seq_count(value->as.seq);
            if (out_seq_count) {
                *out_seq_count = count;
            }
            uint64_t emit = count < config->array_preview_max ? count : config->array_preview_max;
            for (uint64_t i = 0; i < emit; ++i) {
                nmo_dsl_value_t elem = {0};
                if (!nmo_dsl_seq_get(value->as.seq, i, &elem)) {
                    yyjson_mut_arr_add_val(arr, yyjson_mut_null(out->json_doc));
                    continue;
                }
                uint64_t ignored = 0;
                yyjson_mut_val *ev = nmo_summary_query_value_to_json(out, registry, config, &elem, &ignored);
                yyjson_mut_arr_add_val(arr, ev ? ev : yyjson_mut_null(out->json_doc));
            }
            return arr;
        }

        default:
            return yyjson_mut_null(out->json_doc);
    }
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

        /* Add string representation for debugging */
        nmo_cli_json_add_str_safe(ctx->out->json_doc, item, "value_str", value_buf);

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

    const void *state = nmo_object_get_state(obj);
    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    if (!registry) {
        return false;
    }

    const nmo_type_descriptor_t *type = nmo_summary_get_type_for_object(registry, obj);
    if (!type) {
        return false;
    }

    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (!g_enrichers[i].enricher) {
            continue;
        }

        const nmo_type_descriptor_t *base = nmo_type_registry_find_by_guid(
            registry, g_enrichers[i].base_guid);
        if (!base) {
            continue;
        }

        if (nmo_type_is_derived_from((nmo_type_registry_t *)registry, type->id, base->id)) {
            return g_enrichers[i].enricher(obj, state, out);
        }
    }

    return false;
}

/* ============================================================================
 * Enricher Registry
 * ============================================================================ */

void nmo_summary_register_enricher(nmo_guid_t base_guid, nmo_summary_enricher_fn enricher) {
    if (g_enricher_count >= NMO_SUMMARY_MAX_ENRICHERS) {
        return;
    }

    /* Check for duplicate */
    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (nmo_guid_equals(g_enrichers[i].base_guid, base_guid)) {
            g_enrichers[i].enricher = enricher;
            return;
        }
    }

    g_enrichers[g_enricher_count++] = (nmo_enricher_entry_t){
        .base_guid = base_guid,
        .enricher = enricher,
    };
}

bool nmo_summary_has_enricher(nmo_guid_t base_guid) {
    for (size_t i = 0; i < g_enricher_count; ++i) {
        if (nmo_guid_equals(g_enrichers[i].base_guid, base_guid)) {
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

    nmo_summary_register_enricher(CKPGUID_MESH, nmo_enricher_mesh);
    nmo_summary_register_enricher(CKPGUID_BEHAVIOR, nmo_enricher_behavior);

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

bool nmo_object_summary_select(nmo_object_t *obj, nmo_summary_output_t *out,
                               const char *const *paths, size_t path_count)
{
    nmo_summary_config_t config = nmo_summary_config_default();
    return nmo_object_summary_select_with_config(obj, out, &config, paths, path_count);
}

bool nmo_object_summary_select_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *paths,
    size_t path_count)
{
    if (!obj || !out || !config || !paths || path_count == 0) {
        return false;
    }

    /* Lazy auto-init enrichers on first use (keep consistent with other entrypoints) */
    if (!g_enrichers_initialized) {
        nmo_summary_init_builtin_enrichers();
    }

    if (!out->is_json) {
        nmo_summary_add_section(out, "Select");
    }

    yyjson_mut_val *json_select = NULL;
    if (out->is_json) {
        json_select = yyjson_mut_arr(out->json_doc);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "select", json_select);
    }

    bool any = false;
    for (size_t i = 0; i < path_count; ++i) {
        const char *path = paths[i];
        any |= nmo_summary_emit_select_path(obj, out, json_select, config, path);
    }

    return any;
}

static const char *nmo_summary_query_kind_to_string(nmo_dsl_value_kind_t kind) {
    switch (kind) {
        case NMO_DSL_VALUE_NULL: return "null";
        case NMO_DSL_VALUE_BOOL: return "bool";
        case NMO_DSL_VALUE_INT: return "int";
        case NMO_DSL_VALUE_UINT: return "uint";
        case NMO_DSL_VALUE_REAL: return "real";
        case NMO_DSL_VALUE_STRING: return "string";
        case NMO_DSL_VALUE_BYREF: return "byref";
        case NMO_DSL_VALUE_OBJECT: return "object";
        case NMO_DSL_VALUE_SEQ: return "seq";
        default: return "unknown";
    }
}

bool nmo_object_summary_expr(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const char *const *exprs,
    size_t expr_count)
{
    nmo_summary_config_t config = nmo_summary_config_default();
    return nmo_object_summary_expr_with_config(obj, out, &config, exprs, expr_count);
}

bool nmo_object_summary_expr_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *exprs,
    size_t expr_count)
{
    if (!obj || !out || !config || !exprs || expr_count == 0) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_summary_get_registry(out);
    if (!registry) {
        return false;
    }

    const nmo_type_descriptor_t *type = nmo_summary_get_type_for_object(registry, obj);
    const void *state = nmo_object_get_state(obj);
    if (!type || !state || !nmo_type_has_reflection(type)) {
        if (!out->is_json) {
            nmo_summary_add_section(out, "Expr");
            fprintf(out->stream, "  (no reflection data available)\n");
        }
        return false;
    }

    if (!out->is_json) {
        nmo_summary_add_section(out, "Expr");
    }

    yyjson_mut_val *json_expr = NULL;
    if (out->is_json) {
        json_expr = yyjson_mut_arr(out->json_doc);
        yyjson_mut_obj_add_val(out->json_doc, out->json_data, "expr", json_expr);
    }

    nmo_dsl_eval_context_t qctx = {
        .registry = registry,
        .root_type = type,
        .root_instance = (void *)state,  /* const_cast: summary is read-only */
        .current_type = NULL,
        .current_instance = NULL,
        .guess_array_count = nmo_summary_guess_array_count_cb,
        .guess_array_count_user = NULL,
        .resolve_object_name = nmo_summary_resolve_object_name_cb,
        .resolve_object_name_user = (void *)out,
    };

    bool any = false;
    for (size_t i = 0; i < expr_count; ++i) {
        const char *expr = exprs[i];
        if (!expr || !expr[0]) {
            continue;
        }

        nmo_dsl_value_t v = {0};
        char errbuf[128] = {0};
        nmo_status_t st = nmo_dsl_eval_one(registry, &qctx, expr, &v);
        bool ok = (st == NMO_OK);
        if (!ok) {
            (void)nmo_last_error_message_copy(errbuf, sizeof(errbuf));
        }

        if (out->is_json) {
            yyjson_mut_val *item = yyjson_mut_obj(out->json_doc);
            nmo_cli_json_add_str_safe(out->json_doc, item, "expr", expr);
            yyjson_mut_obj_add_bool(out->json_doc, item, "ok", ok);

            if (!ok) {
                nmo_cli_json_add_str_safe(out->json_doc, item, "error", errbuf[0] ? errbuf : "error");
            } else {
                nmo_cli_json_add_str_safe(out->json_doc, item, "kind", nmo_summary_query_kind_to_string(v.kind));

                uint64_t seq_count = 0;
                if (v.kind == NMO_DSL_VALUE_SEQ) {
                    yyjson_mut_val *preview = nmo_summary_query_value_to_json(out, registry, config, &v, &seq_count);
                    yyjson_mut_obj_add_uint(out->json_doc, item, "count", seq_count);
                    yyjson_mut_obj_add_val(out->json_doc, item, "preview", preview);

                    char preview_str[512];
                    uint64_t ignored = 0;
                    (void)nmo_summary_query_value_to_text(out, registry, config, &v, preview_str, sizeof(preview_str), &ignored);
                    nmo_cli_json_add_str_safe(out->json_doc, item, "preview_str", preview_str);
                } else {
                    yyjson_mut_val *json_val = nmo_summary_query_value_to_json(out, registry, config, &v, &seq_count);
                    yyjson_mut_obj_add_val(out->json_doc, item, "value", json_val);

                    char value_str[NMO_SUMMARY_VALUE_BUFFER_SIZE];
                    uint64_t ignored = 0;
                    (void)nmo_summary_query_value_to_text(out, registry, config, &v, value_str, sizeof(value_str), &ignored);
                    nmo_cli_json_add_str_safe(out->json_doc, item, "value_str", value_str);
                }
            }

            yyjson_mut_arr_add_val(json_expr, item);
        } else {
            if (!ok) {
                char msg[256];
                snprintf(msg, sizeof(msg), "<error: %s>", errbuf[0] ? errbuf : "error");
                nmo_cli_print_kv(out->stream, expr, msg, 30, out->colorize);
            } else {
                char value_str[512];
                uint64_t seq_count = 0;
                (void)nmo_summary_query_value_to_text(out, registry, config, &v, value_str, sizeof(value_str), &seq_count);
                if (v.kind == NMO_DSL_VALUE_SEQ) {
                    char label[512];
                    snprintf(label, sizeof(label), "%s[%llu]", expr, (unsigned long long)seq_count);
                    nmo_cli_print_kv(out->stream, label, value_str, 30, out->colorize);
                } else {
                    nmo_cli_print_kv(out->stream, expr, value_str, 30, out->colorize);
                }
            }
        }

        nmo_dsl_value_destroy(&v);
        any = true;
    }

    return any;
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
