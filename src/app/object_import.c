/**
 * @file object_import.c
 * @brief JSON-to-object import via reflection (inverse of object_summary)
 *
 * Parses a JSON document containing object field values and writes them
 * into live objects using the type system's from_string converters.
 */

#include "app/nmo_object_import.h"
#include "session/nmo_session_edit.h"

#include "session/nmo_session.h"
#include "session/nmo_context.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"

#include "yyjson.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define IMPORT_MAX_HIERARCHY_DEPTH 16
#define IMPORT_VALUE_BUF_SIZE      512

typedef struct {
    const nmo_type_descriptor_t *type;
    void *state;
} import_hierarchy_level_t;

/**
 * @brief Check if a field is a base-class embedding that should be skipped.
 *
 * Mirrors the logic in object_summary.c nmo_summary_is_base_embedding().
 */
static bool is_base_embedding(const nmo_type_descriptor_t *owner_type,
                              const nmo_type_field_t *field)
{
    if (!owner_type || !field) return false;
    if (field->flags & NMO_FIELD_REPEATED) return false;
    if (nmo_guid_is_null(owner_type->base_type)) return false;

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

/**
 * @brief Build flattened class hierarchy (base-first) from a type and its state.
 *
 * Mirrors nmo_summary_build_hierarchy() in object_summary.c, but uses mutable
 * state pointers for write access.
 */
static size_t build_hierarchy(const nmo_type_registry_t *registry,
                              const nmo_type_descriptor_t *root_type,
                              void *root_state,
                              import_hierarchy_level_t *levels,
                              size_t max_levels)
{
    if (!registry || !root_type || !root_state || !levels || max_levels == 0) {
        return 0;
    }

    import_hierarchy_level_t stack[IMPORT_MAX_HIERARCHY_DEPTH];
    size_t depth = 0;

    const nmo_type_descriptor_t *cur_type = root_type;
    void *cur_state = root_state;

    while (cur_type && depth < IMPORT_MAX_HIERARCHY_DEPTH) {
        stack[depth].type = cur_type;
        stack[depth].state = cur_state;
        depth++;

        bool found_base = false;
        for (size_t i = 0; i < cur_type->field_count; ++i) {
            const nmo_type_field_t *field = &cur_type->fields[i];
            if (is_base_embedding(cur_type, field)) {
                void *base_ptr = (uint8_t *)cur_state + field->offset;

                const nmo_type_descriptor_t *parent_type = NULL;
                if (!nmo_guid_equals(field->type_guid, CKPGUID_NONE)) {
                    parent_type = nmo_type_registry_find_by_guid(registry, field->type_guid);
                }
                if (!parent_type && !nmo_guid_is_null(cur_type->base_type)) {
                    parent_type = nmo_type_registry_find_by_guid(registry, cur_type->base_type);
                }

                if (parent_type && nmo_type_has_reflection(parent_type)) {
                    cur_type = parent_type;
                    cur_state = base_ptr;
                    found_base = true;
                    break;
                }
            }
        }

        if (!found_base) break;
    }

    /* Reverse so index 0 is deepest base */
    size_t count = depth < max_levels ? depth : max_levels;
    for (size_t i = 0; i < count; ++i) {
        levels[i] = stack[depth - 1 - i];
    }
    return count;
}

/**
 * @brief Convert a yyjson value to a string suitable for from_string parsing.
 *
 * JSON strings are used as-is. Numbers and booleans are converted to their
 * text representation. Returns NULL for null/object/array values.
 */
static const char *json_val_to_str(yyjson_val *val, char *buf, size_t buf_size)
{
    if (!val || yyjson_is_null(val)) return NULL;

    if (yyjson_is_str(val)) {
        return yyjson_get_str(val);
    }
    if (yyjson_is_bool(val)) {
        return yyjson_get_bool(val) ? "true" : "false";
    }
    if (yyjson_is_int(val)) {
        snprintf(buf, buf_size, "%lld", (long long)yyjson_get_sint(val));
        return buf;
    }
    if (yyjson_is_real(val)) {
        snprintf(buf, buf_size, "%.17g", yyjson_get_real(val));
        return buf;
    }
    /* For uint that isn't covered by int */
    if (yyjson_is_num(val)) {
        snprintf(buf, buf_size, "%llu", (unsigned long long)yyjson_get_uint(val));
        return buf;
    }
    return NULL;
}

/**
 * @brief Write an integer count value into a field of variable size.
 */
static void write_count_field(void *state, const nmo_type_field_t *count_field, uint64_t count)
{
    void *ptr = nmo_field_get_ptr(state, count_field);
    if (!ptr) return;

    switch (count_field->size) {
    case 1: *(uint8_t *)ptr  = (uint8_t)count; break;
    case 2: *(uint16_t *)ptr = (uint16_t)count; break;
    case 4: *(uint32_t *)ptr = (uint32_t)count; break;
    case 8: *(uint64_t *)ptr = count; break;
    default: break;
    }
}

static nmo_status_t write_array_count_field(
    void *state,
    const nmo_type_field_t *array_field,
    const nmo_type_field_t *count_field,
    uint64_t element_count)
{
    if (!count_field) {
        return NMO_ERR_NOT_FOUND;
    }
    uint32_t multiplier = nmo_field_get_count_multiplier(array_field);
    if (multiplier == 0u) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (element_count % (uint64_t)multiplier != 0u) {
        return NMO_ERR_INVALID_FORMAT;
    }
    write_count_field(state, count_field, element_count / (uint64_t)multiplier);
    return NMO_OK;
}

/**
 * @brief Determine element size for a field type GUID (same heuristic as summary).
 */
static size_t guess_element_size(nmo_guid_t field_guid,
                                 const nmo_type_descriptor_t *field_type)
{
    if (field_type && field_type->size > 0) {
        return (size_t)field_type->size;
    }
    if (nmo_guid_equals(field_guid, CKPGUID_ID)) return 4;
    /* Conservative default */
    return sizeof(uint32_t);
}

static size_t import_element_size_for_field(
    const nmo_type_field_t *field,
    const nmo_type_descriptor_t *field_type)
{
    if (field && field->name &&
        (strcmp(field->name, "vertex_colors") == 0 ||
         strcmp(field->name, "vertex_specular") == 0)) {
        return sizeof(uint32_t);
    }
    return guess_element_size(field ? field->type_guid : (nmo_guid_t){0}, field_type);
}

/* ============================================================================
 * Field Import
 * ============================================================================ */

/**
 * @brief Import a single scalar field value from a JSON value.
 *
 * Converts the JSON value to a string, then uses nmo_type_value_from_string
 * to parse it into the destination buffer.
 *
 * @param fptr       Destination buffer (pre-allocated, field->size bytes)
 * @param field      Field descriptor
 * @param registry   Type registry
 * @param json_val   JSON value to import
 * @param dry_run    If true, parse but do not write
 * @return NMO_OK on success
 */
static nmo_status_t import_scalar_value(void *fptr,
                                        const nmo_type_field_t *field,
                                        const nmo_type_registry_t *registry,
                                        yyjson_val *json_val,
                                        bool dry_run)
{
    char conv_buf[IMPORT_VALUE_BUF_SIZE];
    const char *str = json_val_to_str(json_val, conv_buf, sizeof(conv_buf));
    if (!str) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_type_descriptor_t *field_type =
        nmo_type_registry_find_by_guid(registry, field->type_guid);
    if (!field_type) {
        return NMO_ERR_NOT_FOUND;
    }

    if (dry_run) {
        /* Parse into a temporary buffer to validate */
        uint8_t temp[256];
        void *parse_buf = temp;
        if (field_type->size > sizeof(temp)) {
            parse_buf = calloc(1, field_type->size);
            if (!parse_buf) return NMO_ERR_NOMEM;
        }
        nmo_status_t st = nmo_type_value_from_string(parse_buf, field_type, registry, str);
        if (parse_buf != temp) free(parse_buf);
        return st;
    }

    return nmo_type_value_from_string(fptr, field_type, registry, str);
}

/**
 * @brief Import a field value from JSON, dispatching by field flags.
 */
static nmo_status_t import_field_value(void *state,
                                       const nmo_type_descriptor_t *owner_type,
                                       const nmo_type_field_t *field,
                                       const nmo_type_registry_t *registry,
                                       nmo_arena_t *arena,
                                       yyjson_val *json_val,
                                       nmo_import_result_t *result,
                                       uint32_t flags)
{
    if (!field || !json_val || yyjson_is_null(json_val)) {
        return NMO_OK; /* null => skip */
    }

    bool dry_run = (flags & NMO_IMPORT_DRY_RUN) != 0;
    void *fptr = nmo_field_get_ptr(state, field);
    if (!fptr) {
        result->errors++;
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_type_descriptor_t *field_type =
        nmo_type_registry_find_by_guid(registry, field->type_guid);

    /* ---- REPEATED + POINTER: raw pointer array (e.g. vertices, faces) ---- */
    if ((field->flags & NMO_FIELD_REPEATED) && (field->flags & NMO_FIELD_POINTER)) {
        if (!yyjson_is_arr(json_val)) {
            result->fields_skipped++;
            return NMO_OK;
        }

        size_t elem_size = import_element_size_for_field(field, field_type);
        size_t arr_count = yyjson_arr_size(json_val);
        const nmo_type_field_t *count_field =
            nmo_field_resolve_count_field(owner_type, field);
        if (!count_field) {
            result->errors++;
            return NMO_ERR_INVALID_ARGUMENT;
        }

        if (arr_count == 0) {
            if (!dry_run) {
                *(void **)fptr = NULL;
                nmo_status_t st = write_array_count_field(state, field, count_field, 0);
                if (st != NMO_OK) {
                    result->errors++;
                    return st;
                }
            }
            result->fields_written++;
            return NMO_OK;
        }

        void *buf = nmo_arena_alloc(arena, arr_count * elem_size, 8);
        if (!buf) {
            result->errors++;
            return NMO_ERR_NOMEM;
        }
        memset(buf, 0, arr_count * elem_size);

        size_t idx = 0;
        yyjson_val *elem;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(json_val, &iter);
        while ((elem = yyjson_arr_iter_next(&iter)) != NULL) {
            char conv_buf[IMPORT_VALUE_BUF_SIZE];
            const char *str = json_val_to_str(elem, conv_buf, sizeof(conv_buf));
            if (!str || !field_type) {
                result->errors++;
                return NMO_ERR_INVALID_ARGUMENT;
            }
            void *dest = (uint8_t *)buf + idx * elem_size;
            nmo_status_t st = nmo_type_value_from_string(dest, field_type, registry, str);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
            idx++;
        }

        if (!dry_run) {
            *(void **)fptr = buf;
            nmo_status_t st = write_array_count_field(state, field, count_field, arr_count);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
        }
        result->fields_written++;
        return NMO_OK;
    }

    /* ---- REPEATED (nmo_array_t): dynamic array ---- */
    if (field->flags & NMO_FIELD_REPEATED) {
        if (!yyjson_is_arr(json_val)) {
            result->fields_skipped++;
            return NMO_OK;
        }

        size_t elem_size = import_element_size_for_field(field, field_type);
        size_t arr_count = yyjson_arr_size(json_val);

        /* Only operate on nmo_array_t sized fields */
        if (field->size != sizeof(nmo_array_t)) {
            result->fields_skipped++;
            return NMO_OK;
        }

        nmo_array_t *arr = (nmo_array_t *)fptr;
        uint8_t stack_buf[256];
        void *elem_buf = stack_buf;
        if (elem_size > sizeof(stack_buf)) {
            elem_buf = nmo_arena_alloc(arena, elem_size, 8);
            if (!elem_buf) {
                result->errors++;
                return NMO_ERR_NOMEM;
            }
        }

        if (!field_type) {
            result->errors++;
            return NMO_ERR_INVALID_ARGUMENT;
        }

        const nmo_allocator_t *array_allocator =
            (arr->element_size != 0 && arr->allocator.alloc && arr->allocator.free)
                ? &arr->allocator
                : NULL;
        nmo_array_t temp;
        nmo_status_t st = nmo_array_init(&temp, elem_size, arr_count, array_allocator);
        if (st != NMO_OK) {
            result->errors++;
            return st;
        }
        if (arr->element_size != 0) {
            nmo_array_set_lifecycle(&temp, &arr->lifecycle);
        }

        yyjson_val *elem;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(json_val, &iter);
        while ((elem = yyjson_arr_iter_next(&iter)) != NULL) {
            memset(elem_buf, 0, elem_size);
            char conv_buf[IMPORT_VALUE_BUF_SIZE];
            const char *str = json_val_to_str(elem, conv_buf, sizeof(conv_buf));
            if (!str) {
                nmo_array_dispose(&temp);
                result->errors++;
                return NMO_ERR_INVALID_ARGUMENT;
            }
            st = nmo_type_value_from_string(elem_buf, field_type, registry, str);
            if (st != NMO_OK) {
                nmo_array_dispose(&temp);
                result->errors++;
                return st;
            }
            st = nmo_array_append(&temp, elem_buf);
            if (st != NMO_OK) {
                nmo_array_dispose(&temp);
                result->errors++;
                return st;
            }
        }

        if (!dry_run) {
            if (arr->element_size == 0) {
                *arr = temp;
                memset(&temp, 0, sizeof(temp));
            } else {
                st = nmo_array_swap(arr, &temp);
                if (st != NMO_OK) {
                    nmo_array_dispose(&temp);
                    result->errors++;
                    return st;
                }
            }
        }
        nmo_array_dispose(&temp);

        result->fields_written++;
        return NMO_OK;
    }

    /* ---- POINTER (single pointer, not repeated) ---- */
    if (field->flags & NMO_FIELD_POINTER) {
        if (!field_type || field_type->size == 0) {
            result->fields_skipped++;
            return NMO_OK;
        }

        /* Nested struct behind pointer */
        if ((field_type->category & NMO_TYPE_CATEGORY_STRUCT) && yyjson_is_obj(json_val)) {
            void *pointee = nmo_arena_alloc(arena, field_type->size, field_type->alignment > 0 ? field_type->alignment : 8);
            if (!pointee) {
                result->errors++;
                return NMO_ERR_NOMEM;
            }
            memset(pointee, 0, field_type->size);

            /* Iterate JSON object keys and match to struct fields */
            yyjson_obj_iter obj_iter;
            yyjson_obj_iter_init(json_val, &obj_iter);
            yyjson_val *key;
            while ((key = yyjson_obj_iter_next(&obj_iter)) != NULL) {
                yyjson_val *val = yyjson_obj_iter_get_val(key);
                const char *key_str = yyjson_get_str(key);
                if (!key_str) continue;

                const nmo_type_field_t *sub_field = nmo_type_get_field_by_name(field_type, key_str);
                if (!sub_field) continue;
                if (sub_field->flags & (NMO_FIELD_DEPRECATED | NMO_FIELD_RUNTIME_ONLY | NMO_FIELD_ID)) continue;

                import_field_value(pointee, field_type, sub_field, registry, arena, val, result, flags);
            }

            if (!dry_run) {
                *(void **)fptr = pointee;
            }
            result->fields_written++;
            return NMO_OK;
        }

        /* Scalar behind pointer */
        void *pointee = nmo_arena_alloc(arena, field_type->size, field_type->alignment > 0 ? field_type->alignment : 8);
        if (!pointee) {
            result->errors++;
            return NMO_ERR_NOMEM;
        }
        memset(pointee, 0, field_type->size);

        /* Build a temporary field descriptor pointing at offset 0 for from_string */
        nmo_type_field_t tmp_field = *field;
        tmp_field.offset = 0;
        tmp_field.size = field_type->size;
        tmp_field.flags = field->flags & (uint32_t)~NMO_FIELD_POINTER;

        nmo_status_t st = import_scalar_value(pointee, &tmp_field, registry, json_val, dry_run);
        if (st == NMO_OK) {
            if (!dry_run) {
                *(void **)fptr = pointee;
            }
            result->fields_written++;
        } else {
            result->fields_skipped++;
        }
        return NMO_OK;
    }

    /* ---- REFERENCE (object ID field) ---- */
    if (field->flags & NMO_FIELD_REFERENCE) {
        /* Handled the same as scalar: from_string accepts "#123" or name */
        nmo_status_t st = import_scalar_value(fptr, field, registry, json_val, dry_run);
        if (st == NMO_OK) {
            result->fields_written++;
        } else {
            result->fields_skipped++;
        }
        return NMO_OK;
    }

    /* ---- Nested STRUCT (inline, not pointer) ---- */
    if (field_type && (field_type->category & NMO_TYPE_CATEGORY_STRUCT) &&
        nmo_type_has_reflection(field_type) && yyjson_is_obj(json_val))
    {
        yyjson_obj_iter obj_iter;
        yyjson_obj_iter_init(json_val, &obj_iter);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&obj_iter)) != NULL) {
            yyjson_val *val = yyjson_obj_iter_get_val(key);
            const char *key_str = yyjson_get_str(key);
            if (!key_str) continue;

            const nmo_type_field_t *sub_field = nmo_type_get_field_by_name(field_type, key_str);
            if (!sub_field) continue;
            if (sub_field->flags & (NMO_FIELD_DEPRECATED | NMO_FIELD_RUNTIME_ONLY | NMO_FIELD_ID)) continue;

            import_field_value(fptr, field_type, sub_field, registry, arena, val, result, flags);
        }
        result->fields_written++;
        return NMO_OK;
    }

    /* ---- Scalar (default) ---- */
    {
        nmo_status_t st = import_scalar_value(fptr, field, registry, json_val, dry_run);
        if (st == NMO_OK) {
            result->fields_written++;
        } else {
            result->fields_skipped++;
        }
    }

    return NMO_OK;
}

/* ============================================================================
 * Snapshot Schema Import
 * ============================================================================ */

static nmo_status_t decode_raw_hex_exact(yyjson_val *raw_val,
                                         void *out,
                                         size_t expected_size)
{
    if (!raw_val || !yyjson_is_str(raw_val) || (!out && expected_size > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t decoded = 0;
    nmo_status_t st = nmo_parse_hex_bytes(
        yyjson_get_str(raw_val), (uint8_t *)out, expected_size, &decoded);
    if (st != NMO_OK) {
        return st;
    }
    return decoded == expected_size ? NMO_OK : NMO_ERR_INVALID_FORMAT;
}

static nmo_status_t import_array_raw_hex(
    void *state,
    const nmo_type_descriptor_t *owner_type,
    const nmo_type_field_t *field,
    const nmo_type_descriptor_t *field_type,
    nmo_arena_t *arena,
    yyjson_val *raw_hex,
    uint64_t count,
    bool dry_run,
    nmo_import_result_t *result)
{
    size_t elem_size = import_element_size_for_field(field, field_type);
    size_t expected_size = (size_t)count * elem_size;
    void *fptr = nmo_field_get_ptr(state, field);
    if (!fptr) {
        result->errors++;
        return NMO_ERR_INVALID_STATE;
    }

    if (field->size != sizeof(nmo_array_t)) {
        const nmo_type_field_t *count_field =
            nmo_field_resolve_count_field(owner_type, field);
        void *buf = NULL;
        if (expected_size > 0) {
            buf = nmo_arena_alloc(arena, expected_size, 8);
            if (!buf) {
                result->errors++;
                return NMO_ERR_NOMEM;
            }
            nmo_status_t st = decode_raw_hex_exact(raw_hex, buf, expected_size);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
        }
        if (!dry_run) {
            *(void **)fptr = buf;
            if (count_field) {
                nmo_status_t st = write_array_count_field(state, field, count_field, count);
                if (st != NMO_OK) {
                    result->errors++;
                    return st;
                }
            }
        }
        result->fields_written++;
        return NMO_OK;
    }

    if (field->size != sizeof(nmo_array_t)) {
        result->fields_skipped++;
        return NMO_OK;
    }

    nmo_array_t *arr = (nmo_array_t *)fptr;
    const nmo_allocator_t *array_allocator =
        (arr->element_size != 0 && arr->allocator.alloc && arr->allocator.free)
            ? &arr->allocator
            : NULL;
    nmo_array_t temp;
    nmo_status_t st = nmo_array_init(&temp, elem_size, (size_t)count, array_allocator);
    if (st != NMO_OK) {
        result->errors++;
        return st;
    }
    if (arr->element_size != 0) {
        nmo_array_set_lifecycle(&temp, &arr->lifecycle);
    }
    if (expected_size > 0) {
        void *data = NULL;
        st = nmo_array_extend(&temp, (size_t)count, &data);
        if (st == NMO_OK) {
            st = decode_raw_hex_exact(raw_hex, data, expected_size);
        }
        if (st != NMO_OK) {
            nmo_array_dispose(&temp);
            result->errors++;
            return st;
        }
    }
    if (!dry_run) {
        if (arr->element_size == 0) {
            *arr = temp;
            memset(&temp, 0, sizeof(temp));
        } else {
            st = nmo_array_swap(arr, &temp);
            if (st != NMO_OK) {
                nmo_array_dispose(&temp);
                result->errors++;
                return st;
            }
        }
    }
    nmo_array_dispose(&temp);
    result->fields_written++;
    return NMO_OK;
}

static nmo_status_t import_snapshot_field_value(
    void *state,
    const nmo_type_descriptor_t *owner_type,
    const nmo_type_field_t *field,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    yyjson_val *field_item,
    nmo_import_result_t *result,
    uint32_t flags)
{
    const char *kind = yyjson_get_str(yyjson_obj_get(field_item, "kind"));
    yyjson_val *value = yyjson_obj_get(field_item, "value");
    yyjson_val *raw_hex = yyjson_obj_get(field_item, "raw_hex");
    if (!kind || !value) {
        result->errors++;
        return NMO_ERR_INVALID_FORMAT;
    }

    bool dry_run = (flags & NMO_IMPORT_DRY_RUN) != 0u;
    void *fptr = nmo_field_get_ptr(state, field);
    if (!fptr) {
        result->errors++;
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_type_descriptor_t *field_type =
        nmo_type_registry_find_by_guid(registry, field->type_guid);

    if (strcmp(kind, "array") == 0) {
        yyjson_val *items = yyjson_obj_get(field_item, "items");
        yyjson_val *count_val = yyjson_obj_get(field_item, "count");
        if (!items || !yyjson_is_arr(items) || !count_val || !yyjson_is_uint(count_val)) {
            result->errors++;
            return NMO_ERR_INVALID_FORMAT;
        }
        uint64_t count = yyjson_get_uint(count_val);
        if (yyjson_arr_size(items) != count) {
            result->errors++;
            return NMO_ERR_INVALID_FORMAT;
        }
        if ((raw_hex && yyjson_is_str(raw_hex)) || count == 0) {
            return import_array_raw_hex(
                state, owner_type, field, field_type, arena, raw_hex, count, dry_run, result);
        }
        return import_field_value(
            state, owner_type, field, registry, arena, items, result, flags);
    }

    if (raw_hex && yyjson_is_str(raw_hex) &&
        strcmp(kind, "scalar") == 0 && field->size > 0) {
        if (!dry_run) {
            nmo_status_t st = decode_raw_hex_exact(raw_hex, fptr, field->size);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
        } else {
            uint8_t tmp[512];
            void *buf = tmp;
            if (field->size > sizeof(tmp)) {
                buf = malloc(field->size);
                if (!buf) {
                    result->errors++;
                    return NMO_ERR_NOMEM;
                }
            }
            nmo_status_t st = decode_raw_hex_exact(raw_hex, buf, field->size);
            if (buf != tmp) free(buf);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
        }
        result->fields_written++;
        return NMO_OK;
    }

    if (strcmp(kind, "struct") == 0 && raw_hex && yyjson_is_str(raw_hex) && field->size > 0) {
        if (!dry_run) {
            nmo_status_t st = decode_raw_hex_exact(raw_hex, fptr, field->size);
            if (st != NMO_OK) {
                result->errors++;
                return st;
            }
        }
        result->fields_written++;
        return NMO_OK;
    }

    if (strcmp(kind, "pointer") == 0) {
        if (yyjson_is_null(value)) {
            if (!dry_run) {
                *(void **)fptr = NULL;
            }
            result->fields_written++;
            return NMO_OK;
        }
        if (!field_type || field_type->size == 0 || !raw_hex || !yyjson_is_str(raw_hex)) {
            result->errors++;
            return NMO_ERR_INVALID_FORMAT;
        }
        void *pointee = nmo_arena_alloc(
            arena, field_type->size, field_type->alignment > 0 ? field_type->alignment : 8);
        if (!pointee) {
            result->errors++;
            return NMO_ERR_NOMEM;
        }
        nmo_status_t st = decode_raw_hex_exact(raw_hex, pointee, field_type->size);
        if (st != NMO_OK) {
            result->errors++;
            return st;
        }
        if (!dry_run) {
            *(void **)fptr = pointee;
        }
        result->fields_written++;
        return NMO_OK;
    }

    return import_field_value(
        state, owner_type, field, registry, arena, value, result, flags);
}

static nmo_status_t import_snapshot_fields(void *state,
                                           const nmo_type_descriptor_t *type,
                                           const nmo_type_registry_t *registry,
                                           nmo_arena_t *arena,
                                           yyjson_val *fields_arr,
                                           nmo_import_result_t *result,
                                           uint32_t flags)
{
    if (!state || !type || !yyjson_is_arr(fields_arr)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    import_hierarchy_level_t levels[IMPORT_MAX_HIERARCHY_DEPTH];
    size_t level_count = build_hierarchy(registry, type, state, levels, IMPORT_MAX_HIERARCHY_DEPTH);
    if (level_count == 0) {
        return NMO_ERR_INVALID_STATE;
    }

    yyjson_arr_iter iter;
    yyjson_arr_iter_init(fields_arr, &iter);
    yyjson_val *item;
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_obj(item)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        const char *name = yyjson_get_str(yyjson_obj_get(item, "name"));
        const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
        const char *type_guid = yyjson_get_str(yyjson_obj_get(item, "type_guid"));
        if (!name || !kind || !type_guid || !yyjson_obj_get(item, "value")) {
            return NMO_ERR_INVALID_FORMAT;
        }

        bool found = false;
        for (size_t lvl = 0; lvl < level_count; ++lvl) {
            const nmo_type_descriptor_t *lvl_type = levels[lvl].type;
            void *lvl_state = levels[lvl].state;
            const nmo_type_field_t *field = nmo_type_get_field_by_name(lvl_type, name);
            if (!field || is_base_embedding(lvl_type, field)) {
                continue;
            }
            found = true;
            if (field->flags & (NMO_FIELD_DEPRECATED | NMO_FIELD_RUNTIME_ONLY | NMO_FIELD_ID)) {
                result->fields_skipped++;
                break;
            }
            nmo_status_t st = import_snapshot_field_value(
                lvl_state, lvl_type, field, registry, arena, item, result, flags);
            if (st != NMO_OK && st != NMO_ERR_INVALID_ARGUMENT) {
                return st;
            }
            break;
        }
        if (!found) {
            result->fields_skipped++;
        }
    }
    return NMO_OK;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

nmo_status_t nmo_object_import_json(
    nmo_session_t *session,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    const char *json_data,
    size_t json_size,
    uint32_t flags,
    nmo_import_result_t *result)
{
    nmo_import_result_t local_result;
    memset(&local_result, 0, sizeof(local_result));
    if (!result) result = &local_result;
    else memset(result, 0, sizeof(*result));

    if (!session || !registry || !arena || !json_data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "nmo_object_import_json: NULL argument");
    }

    if (json_size == 0) {
        json_size = strlen(json_data);
    }

    /* Parse JSON (immutable doc) */
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)json_data, json_size, 0, NULL, &err);
    if (!doc) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "JSON parse error at position %zu: %s",
                         err.pos, err.msg ? err.msg : "unknown");
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "JSON root must be an object");
    }

    /* Get "objects" array -- support both flat format and envelope format.
     * Flat:     {"objects": [...]}
     * Envelope: {"data": {"objects": [...]}} (from nmo --format json object export) */
    yyjson_val *objects_arr = yyjson_obj_get(root, "objects");
    if (!objects_arr || !yyjson_is_arr(objects_arr)) {
        /* Try envelope format: root.data.objects */
        yyjson_val *data_val = yyjson_obj_get(root, "data");
        if (data_val && yyjson_is_obj(data_val)) {
            objects_arr = yyjson_obj_get(data_val, "objects");
        }
    }
    if (!objects_arr || !yyjson_is_arr(objects_arr)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "JSON missing \"objects\" array");
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Session has no object repository");
    }

    /* Iterate each object entry */
    yyjson_arr_iter arr_iter;
    yyjson_arr_iter_init(objects_arr, &arr_iter);
    yyjson_val *entry;
    while ((entry = yyjson_arr_iter_next(&arr_iter)) != NULL) {
        if (!yyjson_is_obj(entry)) {
            result->errors++;
            continue;
        }

        /* Read identity fields */
        yyjson_val *id_val = yyjson_obj_get(entry, "id");
        yyjson_val *class_name_val = yyjson_obj_get(entry, "class_name");
        yyjson_val *name_val = yyjson_obj_get(entry, "name");
        yyjson_val *fields_val = yyjson_obj_get(entry, "fields");

        if (!fields_val || !yyjson_is_arr(fields_val)) {
            yyjson_doc_free(doc);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "object import snapshot requires fields array");
        }

        nmo_object_id_t obj_id = 0;
        if (id_val && yyjson_is_num(id_val)) {
            obj_id = (nmo_object_id_t)yyjson_get_uint(id_val);
        }

        const char *class_name = NULL;
        if (class_name_val && yyjson_is_str(class_name_val)) {
            class_name = yyjson_get_str(class_name_val);
        }

        const char *obj_name = NULL;
        if (name_val && yyjson_is_str(name_val)) {
            obj_name = yyjson_get_str(name_val);
        }

        /* Find the object in the repository */
        nmo_object_t *obj = NULL;
        if (obj_id != 0) {
            obj = nmo_object_repository_find_by_id(repo, obj_id);
        }
        if (!obj && obj_name) {
            obj = nmo_object_repository_find_by_name(repo, obj_name);
        }
        nmo_object_id_t created_id = 0;

        /* Object not found -- create or skip */
        if (!obj) {
            if (flags & NMO_IMPORT_CREATE_MISSING) {
                nmo_class_id_t cid = 0;
                if (class_name) {
                    cid = nmo_type_query_class_id_from_name(registry, class_name);
                }
                if (cid == 0) {
                    result->errors++;
                    continue;
                }

                nmo_guid_t null_guid;
                memset(&null_guid, 0, sizeof(null_guid));
                int rc = nmo_session_create_object(session, cid, obj_name, null_guid, &created_id, NULL);
                if (rc != NMO_OK || created_id == 0) {
                    result->errors++;
                    continue;
                }

                obj = nmo_object_repository_find_by_id(repo, created_id);
                if (!obj) {
                    result->errors++;
                    continue;
                }
                result->objects_created++;
            } else {
                result->errors++;
                continue;
            }
        }

        /* Resolve type descriptor */
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_class_id_inherited(registry, cid);
        if (!type || !nmo_type_has_reflection(type)) {
            result->errors++;
            continue;
        }

        /* Get mutable state */
        void *state = nmo_object_get_state(obj);
        if (!state) {
            result->errors++;
            continue;
        }

        nmo_session_edit_t *edit = NULL;
        if ((flags & NMO_IMPORT_DRY_RUN) == 0u) {
            nmo_status_t edit_st =
                nmo_session_edit_begin(session, "object.import", &edit);
            if (edit_st != NMO_OK) {
                result->errors++;
                continue;
            }
            if (created_id != 0) {
                edit_st = nmo_session_edit_track_created_object(edit, created_id);
                if (edit_st != NMO_OK) {
                    nmo_session_edit_rollback(edit);
                    result->errors++;
                    continue;
                }
            }
            if (type->size > 0u) {
                edit_st = nmo_session_edit_snapshot_bytes(edit, state, type->size);
                if (edit_st != NMO_OK) {
                    nmo_session_edit_rollback(edit);
                    result->errors++;
                    continue;
                }
            }
        }

        size_t fields_before = result->fields_written;

        /* Import snapshot fields */
        nmo_status_t import_st =
            import_snapshot_fields(state, type, registry, arena, fields_val, result, flags);
        if (import_st != NMO_OK) {
            if (edit != NULL) {
                nmo_session_edit_rollback(edit);
            }
            yyjson_doc_free(doc);
            return import_st;
        }
        if (edit != NULL) {
            if (result->fields_written > fields_before) {
                nmo_session_edit_mark(
                    edit,
                    NMO_SESSION_EDIT_OBJECT_STATE |
                    NMO_SESSION_EDIT_REFERENCES |
                    NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                    NMO_SESSION_EDIT_NAMES |
                    NMO_SESSION_EDIT_RESOURCES);
            }
            nmo_status_t edit_st = nmo_session_edit_commit(edit);
            if (edit_st != NMO_OK) {
                yyjson_doc_free(doc);
                return edit_st;
            }
        }
        result->objects_updated++;
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}
