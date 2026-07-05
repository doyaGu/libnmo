/**
 * @file type_registry_enum.c
 * @brief Enum and flags type registration implementation (Phase 6.2 Task 6.2.3)
 * 
 * Implements dynamic registration of enum and flags types with:
 * - Named value definitions (name-to-value mapping)
 * - Default values
 * - String-to-value and value-to-string conversion
 * - Enum (mutually exclusive) vs Flags (combinable) distinction
 * 
 * Reference: CKParameterManager::RegisterNewEnum/RegisterNewFlags
 */

#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "type_value_internal.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "core/nmo_debug.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_parse.h"
#include "core/nmo_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <stdint.h>
#include <limits.h>

/* Upper bound for the O(n²) duplicate-name check in validation.
 * Virtools types never approach this limit in practice (typical: 2-64 values).
 * For larger inputs the uniqueness check is skipped; a hash-set pass could be
 * added in the future if needed. */
#define VALIDATE_MAX_QUADRATIC_N 256u

/**
 * @brief Validate enum value definitions
 * @return NMO_OK if valid, error code otherwise
 */

/* ============================================================================
 * Enum/Flags Converters (require type metadata)
 * ============================================================================ */

static void format_enum_fallback(
    const nmo_type_descriptor_t *type,
    int32_t enum_value,
    char *buffer,
    size_t buffer_size)
{
    if (type->name) {
        snprintf(buffer, buffer_size, "%s(%d)", type->name, enum_value);
    } else {
        snprintf(buffer, buffer_size, "enum(%d)", enum_value);
    }
}

static void format_flags_fallback(
    const nmo_type_descriptor_t *type,
    uint32_t flags_value,
    char *buffer,
    size_t buffer_size)
{
    if (type->name) {
        snprintf(buffer, buffer_size, "%s(0x%X)", type->name, flags_value);
    } else {
        snprintf(buffer, buffer_size, "flags(0x%X)", flags_value);
    }
}

static const nmo_specialized_metadata_t *get_matching_metadata(
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    uint16_t metadata_type)
{
    if (!type || !registry ||
        type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID ||
        type->specialized_index >= registry->metadata.count) {
        return NULL;
    }

    const nmo_specialized_metadata_t *const *slot =
        (const nmo_specialized_metadata_t *const *)nmo_arena_array_get(
            (nmo_arena_array_t *)&registry->metadata,
            type->specialized_index);
    const nmo_specialized_metadata_t *metadata = slot ? *slot : NULL;
    if (!metadata || metadata->metadata_type != metadata_type) {
        return NULL;
    }

    return metadata;
}

static nmo_status_t get_change_metadata(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    nmo_type_category_t category,
    uint16_t metadata_type,
    const char *category_message,
    const char *missing_message,
    const char *mismatch_message,
    nmo_specialized_metadata_t **out_metadata)
{
    *out_metadata = NULL;

    nmo_type_descriptor_t *type =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "Type not found");
    }
    if (!(type->category & category)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "%s", category_message);
    }
    if (type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID ||
        type->specialized_index >= type_registry->metadata.count) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                         "%s", missing_message);
    }

    nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t **)
        nmo_arena_array_get(&type_registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != metadata_type) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                         "%s", mismatch_message);
    }

    *out_metadata = metadata;
    NMO_RETURN_OK();
}

static nmo_status_t copy_wrapped_type_value(
    const nmo_type_descriptor_t *type,
    const char *string,
    char *stack_buf,
    size_t stack_buf_size,
    const char *oom_message,
    char **out_inner,
    bool *out_matched)
{
    *out_inner = NULL;
    *out_matched = false;

    if (!type->name) {
        NMO_RETURN_OK();
    }

    size_t name_len = strlen(type->name);
    size_t string_len = strlen(string);
    if (string_len <= name_len + 2u ||
        strncmp(string, type->name, name_len) != 0 ||
        string[name_len] != '(' ||
        string[string_len - 1u] != ')') {
        NMO_RETURN_OK();
    }

    size_t inner_len = string_len - name_len - 2u;
    char *inner = stack_buf;
    if (inner_len + 1u > stack_buf_size) {
        inner = (char *)malloc(inner_len + 1u);
        if (!inner) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "%s", oom_message);
        }
    }

    memcpy(inner, string + name_len + 1u, inner_len);
    inner[inner_len] = '\0';
    *out_inner = inner;
    *out_matched = true;
    NMO_RETURN_OK();
}

static void free_wrapped_type_value(char *inner, const char *stack_buf)
{
    if (inner != stack_buf) {
        free(inner);
    }
}

nmo_status_t nmo_enum_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_name)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for enum_to_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not an enum");
    }

    int32_t enum_value = *(const int32_t*)value;

    /* If name not requested or no registry, output numeric value with type context */
    if (!use_name) {
        format_enum_fallback(type, enum_value, buffer, buffer_size);
        NMO_RETURN_OK();
    }

    const nmo_specialized_metadata_t *metadata =
        get_matching_metadata(type, registry, NMO_METADATA_TYPE_ENUM);
    if (!metadata) {
        format_enum_fallback(type, enum_value, buffer, buffer_size);
        NMO_RETURN_OK();
    }

    /* Search for matching enum value */
    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        if (metadata->enum_meta.values[i].value == enum_value) {
            snprintf(buffer, buffer_size, "%s", metadata->enum_meta.values[i].name);
            NMO_RETURN_OK();
        }
    }

    /* No name found, output numeric value with type context */
    format_enum_fallback(type, enum_value, buffer, buffer_size);
    NMO_RETURN_OK();
}

nmo_status_t nmo_enum_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for enum_from_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not an enum");
    }

    const nmo_specialized_metadata_t *metadata =
        get_matching_metadata(type, registry, NMO_METADATA_TYPE_ENUM);
    if (metadata) {
        for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
            if (strcmp(metadata->enum_meta.values[i].name, string) == 0) {
                *(int32_t*)value = (int32_t)metadata->enum_meta.values[i].value;
                NMO_RETURN_OK();
            }
        }
    }

    char stack_buf[64];
    char *inner = NULL;
    bool matched = false;
    NMO_RETURN_IF_ERROR(copy_wrapped_type_value(
        type, string, stack_buf, sizeof(stack_buf),
        "Failed to allocate enum fallback value", &inner, &matched));
    if (matched) {
        int32_t result = 0;
        nmo_status_t parse_status =
            nmo_parse_i32_range_base(inner, 0, INT32_MIN, INT32_MAX, &result);
        free_wrapped_type_value(inner, stack_buf);
        if (parse_status == NMO_OK) {
            *(int32_t*)value = result;
            NMO_RETURN_OK();
        }
    }

    // Try to parse as integer
    int32_t result = 0;
    if (nmo_parse_i32_range_base(string, 0, INT32_MIN, INT32_MAX, &result) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid enum value");
    }

    *(int32_t*)value = result;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Flags Converters
 * ============================================================================ */

nmo_status_t nmo_flags_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_names)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for flags_to_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not flags");
    }

    uint32_t flags_value = *(const uint32_t*)value;

    /* If names not requested or no registry, output hex with type context */
    if (!use_names) {
        format_flags_fallback(type, flags_value, buffer, buffer_size);
        NMO_RETURN_OK();
    }

    const nmo_specialized_metadata_t *metadata =
        get_matching_metadata(type, registry, NMO_METADATA_TYPE_FLAGS);
    if (!metadata) {
        format_flags_fallback(type, flags_value, buffer, buffer_size);
        NMO_RETURN_OK();
    }

    /* Build name1|name2 format */
    size_t offset = 0;
    bool first = true;
    for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
        if ((flags_value & metadata->flags_meta.bits[i].mask) == metadata->flags_meta.bits[i].mask) {
            if (!first && offset < buffer_size) {
                buffer[offset++] = '|';
            }
            first = false;
            const char *name = metadata->flags_meta.bits[i].name;
            while (*name && offset < buffer_size - 1) {
                buffer[offset++] = *name++;
            }
        }
    }

    /* If no flags matched, output hex with type context */
    if (first) {
        format_flags_fallback(type, flags_value, buffer, buffer_size);
    } else {
        buffer[offset] = '\0';
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_flags_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for flags_from_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not flags");
    }

    // Try numeric format
    uint32_t result = 0;
    if (nmo_parse_u32_range_base(string, 0, 0, UINT32_MAX, &result) == NMO_OK) {
        *(uint32_t*)value = result;
        NMO_RETURN_OK();
    }

    char stack_buf[64];
    char *inner = NULL;
    bool matched = false;
    NMO_RETURN_IF_ERROR(copy_wrapped_type_value(
        type, string, stack_buf, sizeof(stack_buf),
        "Failed to allocate flags fallback value", &inner, &matched));
    if (matched) {
        nmo_status_t parse_status =
            nmo_parse_u32_range_base(inner, 0, 0, UINT32_MAX, &result);
        free_wrapped_type_value(inner, stack_buf);
        if (parse_status == NMO_OK) {
            *(uint32_t*)value = result;
            NMO_RETURN_OK();
        }
    }

    const nmo_specialized_metadata_t *metadata =
        get_matching_metadata(type, registry, NMO_METADATA_TYPE_FLAGS);
    if (metadata) {
        uint32_t flags_result = 0;
        const char *start = string;

        while (*start) {
            // Skip whitespace
            while (*start && isspace(*start)) start++;
            if (!*start) break;

            // Find end of name (| or end of string)
            const char *end = start;
            while (*end && *end != '|') end++;

            // Match name
            size_t name_len = end - start;
            bool found = false;
            for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
                const char *bit_name = metadata->flags_meta.bits[i].name;
                if (strncmp(bit_name, start, name_len) == 0 && bit_name[name_len] == '\0') {
                    flags_result |= (uint32_t)metadata->flags_meta.bits[i].mask;
                    found = true;
                    break;
                }
            }

            if (!found) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Unknown flag name");
            }

            // Move to next
            start = (*end == '|') ? end + 1 : end;
        }

        *(uint32_t*)value = flags_result;
        NMO_RETURN_OK();
    }

    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid flags format");
}

static nmo_status_t nmo_enum_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth)
{
    (void)depth;
    return nmo_enum_to_string(value, type, registry, buffer, buffer_size, true);
}

static nmo_status_t nmo_enum_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    return nmo_enum_from_string(value, type, registry, string);
}

static nmo_status_t nmo_flags_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth)
{
    (void)depth;
    return nmo_flags_to_string(value, type, registry, buffer, buffer_size, true);
}

static nmo_status_t nmo_flags_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    return nmo_flags_from_string(value, type, registry, string);
}

NMO_DEFINE_ZERO_MEMCPY_TYPE_VTABLE(
    nmo_type_vtable_enum,
    nmo_vt_equals_int32, nmo_vt_hash_int32,
    nmo_enum_vt_to_string,
    nmo_enum_vt_from_string)

NMO_DEFINE_ZERO_MEMCPY_TYPE_VTABLE(
    nmo_type_vtable_flags,
    nmo_vt_equals_uint32, nmo_vt_hash_uint32,
    nmo_flags_vt_to_string,
    nmo_flags_vt_from_string)

static nmo_status_t validate_enum_values(
    const nmo_enum_value_def_t *values,
    size_t value_count
) {
    if (!values || value_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Enum must have at least one value");
    }
    
    /* Check for duplicate names */
    for (size_t i = 0; i < value_count; i++) {
        if (!values[i].name || values[i].name[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Enum value name cannot be empty");
        }

        if (values[i].value < INT32_MIN || values[i].value > INT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Enum value '%s' out of 32-bit range",
                                    values[i].name);
        }
        
        /* O(n²) duplicate-name check — guarded for pathologically large inputs.
         * Virtools types are typically 2–64 values; skip the check beyond the cap. */
        if (value_count <= VALIDATE_MAX_QUADRATIC_N) {
            for (size_t j = i + 1; j < value_count; j++) {
                if (strcmp(values[i].name, values[j].name) == 0) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Duplicate enum value name: '%s'",
                                            values[i].name);
                }
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Validate flags bit definitions
 * @return NMO_OK if valid, error code otherwise
 */
static nmo_status_t validate_flags_bits(
    const nmo_flags_bit_def_t *bits,
    size_t bit_count
) {
    if (!bits || bit_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Flags must have at least one bit");
    }
    
    /* Check for duplicate names and bit masks */
    for (size_t i = 0; i < bit_count; i++) {
        if (!bits[i].name || bits[i].name[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Flags bit name cannot be empty");
        }

        if (bits[i].mask > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Flags bit mask out of 32-bit range (got 0x%llx for '%s')",
                                    (unsigned long long)bits[i].mask, bits[i].name);
        }
        
        /* Validate mask is a power of 2 (single bit) */
        if (!NMO_IS_POWER_OF_TWO(bits[i].mask)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Flags bit mask must be a power of 2 (got 0x%llx for '%s')",
                                    (unsigned long long)bits[i].mask, bits[i].name);
        }
        
        /* O(n²) duplicate-name/mask check — guarded for pathologically large inputs.
         * Virtools types are typically 2–64 bits; skip the check beyond the cap. */
        if (bit_count <= VALIDATE_MAX_QUADRATIC_N) {
            for (size_t j = i + 1; j < bit_count; j++) {
                if (strcmp(bits[i].name, bits[j].name) == 0) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Duplicate flags bit name: '%s'",
                                            bits[i].name);
                }

                /* Check for duplicate bit masks */
                if (bits[i].mask == bits[j].mask) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Duplicate bit mask 0x%llx for '%s' and '%s'",
                                            (unsigned long long)bits[i].mask, bits[i].name, bits[j].name);
                }
            }
        }
    }
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Internal Registration Scaffold
 * ============================================================================ */

/* Parameters common to both enum and flags registration. */
typedef struct {
    const char *name;          /* type name (required) */
    const char *description;   /* may be NULL */
    nmo_guid_t guid;           /* chosen GUID */
    nmo_specialized_metadata_t *spec_meta; /* pre-built by caller */
    nmo_type_category_t category;
    uint32_t size;
    uint32_t alignment;
} enum_params_t;

/* Shared scaffold: existence check, descriptor alloc, register, metadata link. */
static nmo_status_t register_enum_type(nmo_type_registry_t *reg,
                                             const enum_params_t *p,
                                             nmo_guid_t *out_guid) {
    if (reg->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot register type");
    }

    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(reg, p->guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                         "Type '%s' already registered", p->name);
    }

    nmo_arena_t *arena = reg->arena;

    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t *)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate type descriptor");
    }
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));

    const char *type_name = nmo_arena_strdup(arena, p->name);
    if (!type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to copy type name");
    }

    type_desc->guid        = p->guid;
    type_desc->name        = type_name;
    type_desc->size        = p->size;
    type_desc->alignment   = p->alignment;
    type_desc->category    = p->category;
    type_desc->flags       = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD;
    type_desc->fields      = NULL;
    type_desc->field_count = 0;
    type_desc->vtable      = NULL;
    type_desc->description = p->description ? nmo_arena_strdup(arena, p->description) : NULL;
    type_desc->valid       = true;

    nmo_status_t result = nmo_type_registry_register(reg, type_desc);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t *registered =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(reg, p->guid);
    if (!registered) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                         "Failed to find registered type");
    }

    p->spec_meta->type_id = registered->id;

    result = nmo_type_registry_register_metadata(reg, p->spec_meta);
    if (result != NMO_OK) {
        (void)nmo_type_registry_unregister(reg, p->guid);
        return result;
    }

    if (out_guid) {
        *out_guid = p->guid;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Enum Registration
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_enum(
    nmo_type_registry_t *type_registry,
    const nmo_enum_type_def_t *enum_def,
    nmo_guid_t *out_guid
) {
    if (!type_registry || !enum_def) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry or enum_def");
    }
    if (!enum_def->name || enum_def->name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Enum type name cannot be empty");
    }

    nmo_status_t result = validate_enum_values(enum_def->values, enum_def->value_count);
    if (result != NMO_OK) {
        return result;
    }

    nmo_guid_t type_guid = !nmo_guid_is_null(enum_def->guid)
        ? enum_def->guid
        : nmo_type_generate_guid(enum_def->name);

    nmo_arena_t *arena = type_registry->arena;

    /* Allocate and copy enum values array */
    nmo_enum_descriptor_t *enum_values = (nmo_enum_descriptor_t *)
        nmo_arena_alloc(arena, sizeof(nmo_enum_descriptor_t) * enum_def->value_count,
                        alignof(nmo_enum_descriptor_t));
    if (!enum_values) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate enum values array");
    }
    for (size_t i = 0; i < enum_def->value_count; i++) {
        enum_values[i].name = nmo_arena_strdup(arena, enum_def->values[i].name);
        if (!enum_values[i].name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy enum value name");
        }
        enum_values[i].value       = enum_def->values[i].value;
        enum_values[i].description = enum_def->values[i].description
            ? nmo_arena_strdup(arena, enum_def->values[i].description) : NULL;
        enum_values[i].flags = 0;
    }

    /* Build specialized metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t *)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    spec_meta->type_id              = NMO_TYPE_ID_INVALID;
    spec_meta->metadata_type        = NMO_METADATA_TYPE_ENUM;
    spec_meta->ownership            = NMO_OWNERSHIP_ARENA;
    spec_meta->enum_meta.values     = enum_values;
    spec_meta->enum_meta.value_count = enum_def->value_count;

    const enum_params_t p = {
        .name        = enum_def->name,
        .description = enum_def->description,
        .guid        = type_guid,
        .spec_meta   = spec_meta,
        .category    = NMO_TYPE_CATEGORY_ENUM,
        .size        = sizeof(int32_t),
        .alignment   = alignof(int32_t)
    };

    return register_enum_type(type_registry, &p, out_guid);
}

/* ============================================================================
 * Flags Type Registration
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_flags(
    nmo_type_registry_t *type_registry,
    const nmo_flags_type_def_t *flags_def,
    nmo_guid_t *out_guid
) {
    if (!type_registry || !flags_def) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry or flags_def");
    }
    if (!flags_def->name || flags_def->name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Flags type name cannot be empty");
    }

    nmo_status_t result = validate_flags_bits(flags_def->bits, flags_def->bit_count);
    if (result != NMO_OK) {
        return result;
    }

    nmo_guid_t type_guid = !nmo_guid_is_null(flags_def->guid)
        ? flags_def->guid
        : nmo_type_generate_guid(flags_def->name);

    nmo_arena_t *arena = type_registry->arena;

    /* Allocate and copy flags bits array */
    nmo_flags_descriptor_t *flags_bits = (nmo_flags_descriptor_t *)
        nmo_arena_alloc(arena, sizeof(nmo_flags_descriptor_t) * flags_def->bit_count,
                        alignof(nmo_flags_descriptor_t));
    if (!flags_bits) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate flags bits array");
    }
    for (size_t i = 0; i < flags_def->bit_count; i++) {
        flags_bits[i].name = nmo_arena_strdup(arena, flags_def->bits[i].name);
        if (!flags_bits[i].name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy flags bit name");
        }
        flags_bits[i].mask        = flags_def->bits[i].mask;
        flags_bits[i].description = flags_def->bits[i].description
            ? nmo_arena_strdup(arena, flags_def->bits[i].description) : NULL;
        flags_bits[i].flags = 0;
    }

    /* Build specialized metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t *)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    spec_meta->type_id             = NMO_TYPE_ID_INVALID;
    spec_meta->metadata_type       = NMO_METADATA_TYPE_FLAGS;
    spec_meta->ownership           = NMO_OWNERSHIP_ARENA;
    spec_meta->flags_meta.bits     = flags_bits;
    spec_meta->flags_meta.bit_count = flags_def->bit_count;

    const enum_params_t p = {
        .name        = flags_def->name,
        .description = flags_def->description,
        .guid        = type_guid,
        .spec_meta   = spec_meta,
        .category    = NMO_TYPE_CATEGORY_FLAGS,
        .size        = sizeof(uint32_t),
        .alignment   = alignof(uint32_t)
    };

    return register_enum_type(type_registry, &p, out_guid);
}

/* ============================================================================
 * String-Based Registration API (Phase 6.2, Task 6.2.2)
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_enum_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char *enum_data
) {
    if (!type_registry || !type_name || !enum_data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL argument to register_enum_string");
    }
    
    /* Create temporary arena for parsing */
    nmo_arena_t *temp_arena = nmo_arena_create(NULL, 4096);
    if (!temp_arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create temporary arena");
    }
    
    /* Parse enum definition string */
    nmo_enum_value_def_t *values = NULL;
    size_t value_count = 0;
    nmo_status_t result = nmo_parse_enum_string(enum_data, &values, &value_count, temp_arena);
    
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }
    
    /* Generate GUID if NULL_GUID */
    nmo_guid_t actual_guid = type_guid;
    if (nmo_guid_is_null(type_guid)) {
        actual_guid = nmo_type_generate_guid(type_name);
    }
    
    /* Create enum type definition */
    nmo_enum_type_def_t enum_def = {
        .name = type_name,
        .description = NULL,
        .guid = actual_guid,
        .values = values,
        .value_count = value_count,
        .default_value = (value_count > 0) ? values[0].value : 0
    };
    
    /* Register enum */
    nmo_guid_t out_guid;
    result = nmo_type_registry_register_enum(type_registry, &enum_def, &out_guid);
    
    /* Clean up temporary arena */
    nmo_arena_destroy(temp_arena);
    
    return result;
}

nmo_status_t nmo_type_registry_register_flags_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char *flags_data
) {
    if (!type_registry || !type_name || !flags_data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL argument to register_flags_string");
    }
    
    /* Create temporary arena for parsing */
    nmo_arena_t *temp_arena = nmo_arena_create(NULL, 4096);
    if (!temp_arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create temporary arena");
    }
    
    /* Parse flags definition string */
    nmo_enum_value_def_t *parsed_values = NULL;
    size_t value_count = 0;
    nmo_status_t result = nmo_parse_flags_string(flags_data, &parsed_values, &value_count, temp_arena);
    
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }
    
    /* Convert enum_value_def_t to flags_bit_def_t */
    nmo_flags_bit_def_t *bits = (nmo_flags_bit_def_t*)
        nmo_arena_alloc(temp_arena, sizeof(nmo_flags_bit_def_t) * value_count, 8);
    if (!bits) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate flags bits array");
    }
    
    for (size_t i = 0; i < value_count; i++) {
        bits[i].name = parsed_values[i].name;
        bits[i].mask = (uint64_t)parsed_values[i].value;
        bits[i].description = parsed_values[i].description;
    }
    
    /* Generate GUID if NULL_GUID */
    nmo_guid_t actual_guid = type_guid;
    if (nmo_guid_is_null(type_guid)) {
        actual_guid = nmo_type_generate_guid(type_name);
    }
    
    /* Create flags type definition */
    nmo_flags_type_def_t flags_def = {
        .name = type_name,
        .description = NULL,
        .guid = actual_guid,
        .bits = bits,
        .bit_count = value_count,
        .default_value = 0
    };
    
    /* Register flags */
    nmo_guid_t out_guid;
    result = nmo_type_registry_register_flags(type_registry, &flags_def, &out_guid);
    
    /* Clean up temporary arena */
    nmo_arena_destroy(temp_arena);
    
    return result;
}

nmo_status_t nmo_type_registry_change_enum_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *new_enum_data
) {
    if (!type_registry || nmo_guid_is_null(type_guid) || !new_enum_data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL argument to change_enum_string");
    }

    nmo_specialized_metadata_t *metadata = NULL;
    NMO_RETURN_IF_ERROR(get_change_metadata(
        type_registry, type_guid,
        NMO_TYPE_CATEGORY_ENUM, NMO_METADATA_TYPE_ENUM,
        "Type is not an enum",
        "Enum type has no specialized metadata",
        "Enum metadata mismatch",
        &metadata));

    /* Parse new enum definition into a temporary arena. */
    nmo_arena_t *temp_arena = nmo_arena_create(NULL, 4096);
    if (!temp_arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create temporary arena");
    }

    nmo_enum_value_def_t *parsed_values = NULL;
    size_t parsed_count = 0;
    nmo_status_t result = nmo_parse_enum_string(new_enum_data, &parsed_values, &parsed_count, temp_arena);
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }

    result = validate_enum_values(parsed_values, parsed_count);
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }

    /* New definition must be a superset of the old definition (by name + value). */
    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        const char *old_name = metadata->enum_meta.values[i].name;
        int64_t old_value = metadata->enum_meta.values[i].value;
        bool found = false;

        for (size_t j = 0; j < parsed_count; j++) {
            if (strcmp(old_name, parsed_values[j].name) == 0) {
                found = true;
                if (parsed_values[j].value != old_value) {
                    nmo_arena_destroy(temp_arena);
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Enum value '%s' cannot change (old=%lld new=%lld)",
                                            old_name,
                                            (long long)old_value,
                                            (long long)parsed_values[j].value);
                }
                break;
            }
        }

        if (!found) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Enum value '%s' cannot be removed", old_name);
        }
    }

    /* Determine which entries are new (name not in old definition). */
    size_t old_count = metadata->enum_meta.value_count;
    size_t add_count = 0;
    for (size_t j = 0; j < parsed_count; j++) {
        bool exists = false;
        for (size_t i = 0; i < old_count; i++) {
            if (strcmp(metadata->enum_meta.values[i].name, parsed_values[j].name) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            add_count++;
        }
    }

    /* No changes needed. */
    if (add_count == 0) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_OK();
    }

    NMO_OWNERSHIP_ASSERT_VALID(metadata->ownership);
    const bool metadata_is_arena_owned = (metadata->ownership == NMO_OWNERSHIP_ARENA);
    nmo_arena_t *arena = type_registry->arena;
    size_t new_count = old_count + add_count;
    const nmo_enum_descriptor_t *old_values = metadata->enum_meta.values;
    nmo_enum_descriptor_t *new_values = NULL;

    if (metadata_is_arena_owned) {
        new_values = (nmo_enum_descriptor_t *)nmo_arena_alloc(
            arena,
            sizeof(nmo_enum_descriptor_t) * new_count,
            alignof(nmo_enum_descriptor_t));
    } else {
        new_values = (nmo_enum_descriptor_t *)nmo_alloc(
            &type_registry->type_allocator,
            sizeof(nmo_enum_descriptor_t) * new_count,
            alignof(nmo_enum_descriptor_t));
    }
    if (!new_values) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate new enum values array");
    }

    /* Copy old values first (preserve existing order and strings). */
    for (size_t i = 0; i < old_count; i++) {
        new_values[i] = metadata->enum_meta.values[i];
    }

    /* Append new values in the order they appear in the new string. */
    size_t out_index = old_count;
    for (size_t j = 0; j < parsed_count; j++) {
        bool exists = false;
        for (size_t i = 0; i < old_count; i++) {
            if (strcmp(metadata->enum_meta.values[i].name, parsed_values[j].name) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        const char *name_copy = NULL;
        const char *desc_copy = NULL;

        if (metadata_is_arena_owned) {
            name_copy = nmo_arena_strdup(arena, parsed_values[j].name);
            desc_copy = parsed_values[j].description ?
                nmo_arena_strdup(arena, parsed_values[j].description) : NULL;
        } else {
            name_copy = nmo_strdup(&type_registry->type_allocator, parsed_values[j].name);
            desc_copy = parsed_values[j].description ?
                nmo_strdup(&type_registry->type_allocator, parsed_values[j].description) : NULL;
        }

        if (!name_copy || (parsed_values[j].description && !desc_copy)) {
            if (!metadata_is_arena_owned) {
                if (desc_copy) {
                    nmo_free(&type_registry->type_allocator, (void *)desc_copy);
                }
                if (name_copy) {
                    nmo_free(&type_registry->type_allocator, (void *)name_copy);
                }
                for (size_t k = old_count; k < out_index; k++) {
                    nmo_free(&type_registry->type_allocator, (void *)new_values[k].name);
                    if (new_values[k].description) {
                        nmo_free(&type_registry->type_allocator, (void *)new_values[k].description);
                    }
                }
                nmo_free(&type_registry->type_allocator, (void *)new_values);
            }
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy new enum value strings");
        }

        new_values[out_index].name = name_copy;
        new_values[out_index].value = parsed_values[j].value;
        new_values[out_index].description = desc_copy;
        new_values[out_index].flags = 0;
        out_index++;
    }

    /* Update metadata to point to the extended list. */
    metadata->enum_meta.values = new_values;
    metadata->enum_meta.value_count = new_count;

    if (!metadata_is_arena_owned && old_values) {
        nmo_free(&type_registry->type_allocator, (void *)old_values);
    }

    nmo_arena_destroy(temp_arena);
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_change_flags_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *new_flags_data
) {
    if (!type_registry || nmo_guid_is_null(type_guid) || !new_flags_data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL argument to change_flags_string");
    }

    nmo_specialized_metadata_t *metadata = NULL;
    NMO_RETURN_IF_ERROR(get_change_metadata(
        type_registry, type_guid,
        NMO_TYPE_CATEGORY_FLAGS, NMO_METADATA_TYPE_FLAGS,
        "Type is not flags",
        "Flags type has no specialized metadata",
        "Flags metadata mismatch",
        &metadata));

    /* Parse new flags definition into a temporary arena. */
    nmo_arena_t *temp_arena = nmo_arena_create(NULL, 4096);
    if (!temp_arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create temporary arena");
    }

    nmo_enum_value_def_t *parsed_values = NULL;
    size_t parsed_count = 0;
    nmo_status_t result = nmo_parse_flags_string(new_flags_data, &parsed_values, &parsed_count, temp_arena);
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }

    /* Convert to bit defs for validation and uniform handling. */
    nmo_flags_bit_def_t *parsed_bits = (nmo_flags_bit_def_t *)nmo_arena_alloc(
        temp_arena, sizeof(nmo_flags_bit_def_t) * parsed_count, alignof(nmo_flags_bit_def_t));
    if (!parsed_bits) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate parsed flags bits");
    }
    for (size_t i = 0; i < parsed_count; i++) {
        parsed_bits[i].name = parsed_values[i].name;
        parsed_bits[i].mask = (uint64_t)parsed_values[i].value;
        parsed_bits[i].description = parsed_values[i].description;
    }

    result = validate_flags_bits(parsed_bits, parsed_count);
    if (result != NMO_OK) {
        nmo_arena_destroy(temp_arena);
        return result;
    }

    /* New definition must be a superset of the old definition (by name + mask). */
    for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
        const char *old_name = metadata->flags_meta.bits[i].name;
        uint64_t old_mask = metadata->flags_meta.bits[i].mask;
        bool found = false;

        for (size_t j = 0; j < parsed_count; j++) {
            if (strcmp(old_name, parsed_bits[j].name) == 0) {
                found = true;
                if (parsed_bits[j].mask != old_mask) {
                    nmo_arena_destroy(temp_arena);
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Flags bit '%s' cannot change (old=0x%llx new=0x%llx)",
                                            old_name,
                                            (unsigned long long)old_mask,
                                            (unsigned long long)parsed_bits[j].mask);
                }
                break;
            }
        }

        if (!found) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Flags bit '%s' cannot be removed", old_name);
        }
    }

    /* Determine which bits are new (name not in old definition). */
    size_t old_count = metadata->flags_meta.bit_count;
    size_t add_count = 0;
    for (size_t j = 0; j < parsed_count; j++) {
        bool exists = false;
        for (size_t i = 0; i < old_count; i++) {
            if (strcmp(metadata->flags_meta.bits[i].name, parsed_bits[j].name) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            add_count++;
        }
    }

    if (add_count == 0) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_OK();
    }

    NMO_OWNERSHIP_ASSERT_VALID(metadata->ownership);
    const bool metadata_is_arena_owned = (metadata->ownership == NMO_OWNERSHIP_ARENA);
    nmo_arena_t *arena = type_registry->arena;
    size_t new_count = old_count + add_count;
    const nmo_flags_descriptor_t *old_bits = metadata->flags_meta.bits;
    nmo_flags_descriptor_t *new_bits = NULL;

    if (metadata_is_arena_owned) {
        new_bits = (nmo_flags_descriptor_t *)nmo_arena_alloc(
            arena,
            sizeof(nmo_flags_descriptor_t) * new_count,
            alignof(nmo_flags_descriptor_t));
    } else {
        new_bits = (nmo_flags_descriptor_t *)nmo_alloc(
            &type_registry->type_allocator,
            sizeof(nmo_flags_descriptor_t) * new_count,
            alignof(nmo_flags_descriptor_t));
    }
    if (!new_bits) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate new flags bits array");
    }

    for (size_t i = 0; i < old_count; i++) {
        new_bits[i] = metadata->flags_meta.bits[i];
    }

    size_t out_index = old_count;
    for (size_t j = 0; j < parsed_count; j++) {
        bool exists = false;
        for (size_t i = 0; i < old_count; i++) {
            if (strcmp(metadata->flags_meta.bits[i].name, parsed_bits[j].name) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        const char *name_copy = NULL;
        const char *desc_copy = NULL;

        if (metadata_is_arena_owned) {
            name_copy = nmo_arena_strdup(arena, parsed_bits[j].name);
            desc_copy = parsed_bits[j].description ?
                nmo_arena_strdup(arena, parsed_bits[j].description) : NULL;
        } else {
            name_copy = nmo_strdup(&type_registry->type_allocator, parsed_bits[j].name);
            desc_copy = parsed_bits[j].description ?
                nmo_strdup(&type_registry->type_allocator, parsed_bits[j].description) : NULL;
        }

        if (!name_copy || (parsed_bits[j].description && !desc_copy)) {
            if (!metadata_is_arena_owned) {
                if (desc_copy) {
                    nmo_free(&type_registry->type_allocator, (void *)desc_copy);
                }
                if (name_copy) {
                    nmo_free(&type_registry->type_allocator, (void *)name_copy);
                }
                for (size_t k = old_count; k < out_index; k++) {
                    nmo_free(&type_registry->type_allocator, (void *)new_bits[k].name);
                    if (new_bits[k].description) {
                        nmo_free(&type_registry->type_allocator, (void *)new_bits[k].description);
                    }
                }
                nmo_free(&type_registry->type_allocator, (void *)new_bits);
            }
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy new flags bit strings");
        }

        new_bits[out_index].name = name_copy;
        new_bits[out_index].mask = parsed_bits[j].mask;
        new_bits[out_index].description = desc_copy;
        new_bits[out_index].flags = 0;
        out_index++;
    }

    metadata->flags_meta.bits = new_bits;
    metadata->flags_meta.bit_count = new_count;

    if (!metadata_is_arena_owned && old_bits) {
        nmo_free(&type_registry->type_allocator, (void *)old_bits);
    }

    nmo_arena_destroy(temp_arena);
    NMO_RETURN_OK();
}
