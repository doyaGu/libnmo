/**
 * @file type_registry_struct_values.c
 * @brief Reflected struct and object-ref value behavior
 */

#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_guids.h"
#include "type_value_internal.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { NMO_MAX_TO_STRING_DEPTH = 6 };

/* ============================================================================
 * Reflected struct formatting helpers
 * ============================================================================ */
typedef struct nmo_string_builder_t {
    char *buf;
    size_t cap;
    size_t len;
} nmo_string_builder_t;

static nmo_status_t nmo_sb_append(nmo_string_builder_t *sb, const char *fmt, ...) {
    if (!sb || !fmt) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid string builder args");
    }
    if (sb->cap == 0 || sb->len >= sb->cap) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    va_list args;
    va_start(args, fmt);
    int wrote = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);

    if (wrote < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to format string");
    }
    if ((size_t)wrote >= sb->cap - sb->len) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    sb->len += (size_t)wrote;
    NMO_RETURN_OK();
}

static const nmo_type_descriptor_t *nmo_to_string_resolve_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid
) {
    if (!registry) {
        return NULL;
    }

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(registry, guid);
    return t;
}

static bool nmo_get_specialized_struct_fields(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const nmo_struct_descriptor_t **out_fields,
    size_t *out_field_count)
{
    *out_fields = NULL;
    *out_field_count = 0;

    if (!registry || !type) {
        return false;
    }

    const nmo_specialized_metadata_t *meta =
        nmo_type_registry_get_metadata(registry, type->id);
    if (!meta) {
        return false;
    }

    if (meta->metadata_type == NMO_METADATA_TYPE_STRUCT) {
        *out_fields = meta->struct_meta.fields;
        *out_field_count = meta->struct_meta.field_count;
    } else if (meta->metadata_type == NMO_METADATA_TYPE_UNION) {
        *out_fields = meta->union_meta.fields;
        *out_field_count = meta->union_meta.field_count;
    } else {
        return false;
    }

    return *out_fields && *out_field_count > 0;
}



/* ============================================================================
 * Reflected struct formatter
 * ============================================================================ */
static nmo_status_t nmo_struct_like_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth
) {
    if (!value || !type || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for struct_to_string");
    }
    if (buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    if (depth >= NMO_MAX_TO_STRING_DEPTH) {
        if (type->name) {
            snprintf(buffer, buffer_size, "<%s ...>", type->name);
        } else {
            snprintf(buffer, buffer_size, "<%s %u bytes>",
                     (type->category & NMO_TYPE_CATEGORY_UNION) ? "union" : "struct",
                     type->size);
        }
        NMO_RETURN_OK();
    }

    /* If no reflection fields, try specialized struct metadata */
    const nmo_struct_descriptor_t *sfields = NULL;
    size_t scount = 0;
    if ((!type->fields || type->field_count == 0) &&
        nmo_get_specialized_struct_fields(registry, type, &sfields, &scount)) {
        nmo_string_builder_t sb = { .buf = buffer, .cap = buffer_size, .len = 0 };
        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "{"));

        bool first = true;
        for (size_t i = 0; i < scount; i++) {
            const nmo_struct_descriptor_t *sf = &sfields[i];
            if ((uint64_t)sf->offset + sf->size > type->size) continue;

            const nmo_type_descriptor_t *ft =
                nmo_to_string_resolve_type(registry, sf->type_guid);
            const uint8_t *fptr = (const uint8_t *)value + sf->offset;

            if (!first) {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, ", "));
            }
            first = false;
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s=",
                sf->name ? sf->name : "<unnamed>"));

            if (!ft) {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<unknown>"));
                continue;
            }

            if (sf->array_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<array[%u]>",
                    (unsigned)sf->array_count));
                continue;
            }

            size_t saved = sb.len;
            size_t avail = sb.cap > sb.len ? sb.cap - sb.len : 0;
            if (avail == 0) break;
            char *slot = sb.buf + sb.len;
            nmo_status_t r = nmo_type_value_to_string_depth_internal(
                fptr, ft, registry, slot, avail, depth + 1);
            if (r == NMO_OK) {
                sb.len += strlen(slot);
            } else {
                sb.len = saved;
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<error>"));
            }
        }

        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "}"));
        NMO_RETURN_OK();
    }

    nmo_string_builder_t sb = { .buf = buffer, .cap = buffer_size, .len = 0 };
    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "{"));

    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_type_field_t *field = &type->fields[i];
        const nmo_type_descriptor_t *field_type = nmo_to_string_resolve_type(registry, field->type_guid);
        const uint8_t *field_ptr = (const uint8_t *)value + field->offset;

        if (i > 0) {
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, ", "));
        }
        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s=", field->name ? field->name : "<unnamed>"));

        if (!field_type) {
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<unknown>"));
            continue;
        }

        /* Handle pointer fields: dereference before formatting */
        if (field->flags & NMO_FIELD_POINTER) {
            if (!(field->flags & NMO_FIELD_REPEATED)) {
                const void *ptr_val = field_ptr ? *(const void *const *)field_ptr : NULL;
                if (!ptr_val) {
                    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "(null)"));
                    continue;
                }
                field_ptr = (const uint8_t *)ptr_val;
            } else {
                uint32_t cnt = 0;
                if (nmo_field_resolve_count(type, field, value, &cnt) == NMO_OK) {
                    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[%u items]", cnt));
                    continue;
                }
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[...]"));
                continue;
            }
        }

        if (field->flags & NMO_FIELD_REPEATED) {
            uint64_t count = 0;
            bool has_count = false;

            if (field->size == sizeof(nmo_array_t)) {
                const nmo_array_t *arr = (const nmo_array_t *)field_ptr;
                count = arr->count;
                has_count = true;
            } else {
                uint32_t resolved_count = 0;
                if (nmo_field_resolve_count(type, field, value, &resolved_count) == NMO_OK) {
                    count = resolved_count;
                    has_count = true;
                }
            }

            if (has_count) {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[%llu]", (unsigned long long)count));
            } else {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[...]"));
            }
            continue;
        }

        size_t saved = sb.len;
        size_t avail = sb.cap > sb.len ? sb.cap - sb.len : 0;
        if (avail == 0) break;
        char *slot = sb.buf + sb.len;
        nmo_object_id_t ref_id = NMO_OBJECT_ID_NONE;
        const void *format_value = field_ptr;
        if ((field->flags & NMO_FIELD_REFERENCE) != 0u &&
            nmo_guid_equals(field->type_guid, CKPGUID_ID) &&
            field->size == sizeof(nmo_ref_t)) {
            const nmo_ref_t *ref = (const nmo_ref_t *)field_ptr;
            ref_id = ref->state == NMO_REF_RESOLVED ? ref->id : ref->raw_id;
            format_value = &ref_id;
        }
        nmo_status_t r = nmo_type_value_to_string_depth_internal(
            format_value, field_type, registry, slot, avail, depth + 1);
        if (r == NMO_OK) {
            sb.len += strlen(slot);
        } else {
            sb.len = saved;
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<error>"));
        }
    }

    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "}"));
    NMO_RETURN_OK();
}


/* ============================================================================
 * Reflected struct parser
 * ============================================================================ */
static const char *nmo_parse_skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *nmo_parse_trim_end(const char *start, const char *end)
{
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    return end;
}

static const char *nmo_find_struct_field_end(const char *p)
{
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (; *p; ++p) {
        char ch = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '(') paren_depth++;
        else if (ch == ')' && paren_depth > 0) paren_depth--;
        else if (ch == '{') brace_depth++;
        else if (ch == '}' && brace_depth > 0) brace_depth--;
        else if (ch == '[') bracket_depth++;
        else if (ch == ']' && bracket_depth > 0) bracket_depth--;
        else if ((ch == ',' || ch == '}') &&
                 paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            return p;
        }
    }

    return p;
}

static bool nmo_field_name_matches(
    const char *expected,
    const char *start,
    const char *end)
{
    if (!expected) {
        return false;
    }

    start = nmo_parse_skip_ws(start);
    end = nmo_parse_trim_end(start, end);
    size_t len = (size_t)(end - start);
    return strlen(expected) == len && strncmp(expected, start, len) == 0;
}

static nmo_status_t nmo_copy_trimmed_segment(
    const char *start,
    const char *end,
    char **out_string)
{
    if (!start || !end || end < start || !out_string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid string segment");
    }

    start = nmo_parse_skip_ws(start);
    end = nmo_parse_trim_end(start, end);
    size_t len = (size_t)(end - start);
    char *copy = (char *)malloc(len + 1u);
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate string segment");
    }

    memcpy(copy, start, len);
    copy[len] = '\0';
    *out_string = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_reflected_struct_field(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *name_start,
    const char *name_end,
    const char *value_start,
    const char *value_end)
{
    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_type_field_t *field = &type->fields[i];
        if (!nmo_field_name_matches(field->name, name_start, name_end)) {
            continue;
        }
        if ((uint64_t)field->offset + field->size > type->size) {
            NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                             "Struct field extends beyond value size");
        }
        if (field->flags & (NMO_FIELD_POINTER | NMO_FIELD_REPEATED)) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                             "Struct array/pointer field parsing is not implemented");
        }

        const nmo_type_descriptor_t *field_type =
            nmo_to_string_resolve_type(registry, field->type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Struct field type not found");
        }

        char *field_string = NULL;
        NMO_RETURN_IF_ERROR(nmo_copy_trimmed_segment(value_start, value_end, &field_string));
        void *field_ptr = (uint8_t *)value + field->offset;
        nmo_status_t st;
        if ((field->flags & NMO_FIELD_REFERENCE) != 0u &&
            nmo_guid_equals(field->type_guid, CKPGUID_ID) &&
            field->size == sizeof(nmo_ref_t)) {
            nmo_object_id_t id = NMO_OBJECT_ID_NONE;
            st = nmo_type_value_from_string(
                &id, field_type, registry, field_string);
            if (st == NMO_OK) {
                nmo_ref_t *ref = (nmo_ref_t *)field_ptr;
                ref->raw_id = id;
                ref->id = id;
                ref->state = (id == NMO_OBJECT_ID_NONE ||
                              id == NMO_OBJECT_ID_INVALID)
                    ? NMO_REF_NONE : NMO_REF_RESOLVED;
            }
        } else {
            st = nmo_type_value_from_string(
                field_ptr, field_type, registry, field_string);
        }
        free(field_string);
        return st;
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                     "Unknown struct field");
}

static nmo_status_t nmo_parse_specialized_struct_field(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const nmo_struct_descriptor_t *fields,
    size_t field_count,
    const char *name_start,
    const char *name_end,
    const char *value_start,
    const char *value_end)
{
    for (size_t i = 0; i < field_count; i++) {
        const nmo_struct_descriptor_t *field = &fields[i];
        if (!nmo_field_name_matches(field->name, name_start, name_end)) {
            continue;
        }
        if ((uint64_t)field->offset + field->size > type->size) {
            NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                             "Struct metadata field extends beyond value size");
        }
        if (field->array_count > 0 || field->pointer_depth > 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                             "Struct array/pointer field parsing is not implemented");
        }

        const nmo_type_descriptor_t *field_type =
            nmo_to_string_resolve_type(registry, field->type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Struct metadata field type not found");
        }

        char *field_string = NULL;
        NMO_RETURN_IF_ERROR(nmo_copy_trimmed_segment(value_start, value_end, &field_string));
        nmo_status_t st = nmo_type_value_from_string(
            (uint8_t *)value + field->offset, field_type, registry, field_string);
        free(field_string);
        return st;
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                     "Unknown struct metadata field");
}

static nmo_status_t nmo_parse_struct_fields(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const nmo_struct_descriptor_t *specialized_fields,
    size_t specialized_field_count,
    const char *string)
{
    const char *p = nmo_parse_skip_ws(string);
    if (*p != '{') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Struct value must start with '{'");
    }
    p++;

    memset(value, 0, type->size);

    for (;;) {
        p = nmo_parse_skip_ws(p);
        if (*p == '}') {
            p++;
            p = nmo_parse_skip_ws(p);
            if (*p != '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Unexpected trailing characters after struct");
            }
            NMO_RETURN_OK();
        }
        if (*p == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Unclosed struct value");
        }

        const char *name_start = p;
        while (*p && *p != '=' && *p != '}') {
            p++;
        }
        if (*p != '=') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Struct field must use name=value");
        }
        const char *name_end = p;
        p++;

        const char *value_start = p;
        const char *value_end = nmo_find_struct_field_end(p);
        if (value_end == value_start) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Struct field value is empty");
        }

        if (specialized_fields && specialized_field_count > 0) {
            NMO_RETURN_IF_ERROR(nmo_parse_specialized_struct_field(
                value, type, registry, specialized_fields, specialized_field_count,
                name_start, name_end, value_start, value_end));
        } else {
            NMO_RETURN_IF_ERROR(nmo_parse_reflected_struct_field(
                value, type, registry, name_start, name_end, value_start, value_end));
        }

        p = value_end;
        p = nmo_parse_skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            continue;
        }
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Struct fields must be separated by ','");
    }
}

static nmo_status_t nmo_struct_like_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for struct_from_string");
    }

    if (type->fields && type->field_count > 0) {
        return nmo_parse_struct_fields(value, type, registry, NULL, 0, string);
    }

    const nmo_struct_descriptor_t *fields = NULL;
    size_t field_count = 0;
    if (nmo_get_specialized_struct_fields(registry, type, &fields, &field_count)) {
        return nmo_parse_struct_fields(
            value, type, registry, fields, field_count, string);
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                     "Struct-from-string requires field metadata");
}

nmo_status_t nmo_reflected_struct_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth)
{
    return nmo_struct_like_to_string(value, type, registry, buffer, buffer_size, depth);
}

nmo_status_t nmo_reflected_struct_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    return nmo_struct_like_from_string(value, type, registry, string);
}

nmo_status_t nmo_object_ref_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth)
{
    return nmo_struct_like_to_string(value, type, registry, buffer, buffer_size, depth);
}

nmo_status_t nmo_object_ref_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    const char *p = nmo_parse_skip_ws(string);
    if (*p == '{') {
        return nmo_struct_like_from_string(value, type, registry, string);
    }
    const nmo_type_descriptor_t *id_type =
        nmo_to_string_resolve_type(registry, CKPGUID_ID);
    if (!id_type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "Object ID type not found");
    }
    return nmo_type_value_from_string(value, id_type, registry, string);
}

