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

#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_hash_table.h"
#include <string.h>
#include <stdalign.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Validate enum value definitions
 * @return NMO_OK if valid, error code otherwise
 */
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
        
        /* Check for duplicate names */
        for (size_t j = i + 1; j < value_count; j++) {
            if (strcmp(values[i].name, values[j].name) == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Duplicate enum value name: '%s'",
                                        values[i].name);
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
        
        /* Validate mask is a power of 2 (single bit) */
        if (bits[i].mask == 0 || (bits[i].mask & (bits[i].mask - 1)) != 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Flags bit mask must be a power of 2 (got 0x%llx for '%s')",
                                    (unsigned long long)bits[i].mask, bits[i].name);
        }
        
        /* Check for duplicate names */
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
    
    /* Validate enum values */
    nmo_status_t result = validate_enum_values(enum_def->values, enum_def->value_count);
        if (result != NMO_OK) {
        return result;
    }
    
    /* Use provided GUID or generate from name */
    nmo_guid_t type_guid;
    if (!nmo_guid_is_null(enum_def->guid)) {
        type_guid = enum_def->guid;
    } else {
        type_guid = nmo_type_generate_guid(enum_def->name);
    }
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                                "Enum type '%s' already registered",
                                enum_def->name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate type descriptor */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate enum type descriptor");
    }
    
    /* Initialize all fields to zero */
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    
    /* Copy enum type name */
    const char *type_name = nmo_arena_strdup(arena, enum_def->name);
    if (!type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to copy enum type name");
    }
    
    /* Allocate enum descriptors array */
    nmo_enum_descriptor_t *enum_values = (nmo_enum_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_enum_descriptor_t) * enum_def->value_count,
                        alignof(nmo_enum_descriptor_t));
    if (!enum_values) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate enum values array");
    }
    
    /* Copy enum values */
    for (size_t i = 0; i < enum_def->value_count; i++) {
        enum_values[i].name = nmo_arena_strdup(arena, enum_def->values[i].name);
        if (!enum_values[i].name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy enum value name");
        }
        enum_values[i].value = enum_def->values[i].value;
        enum_values[i].description = enum_def->values[i].description ? 
            nmo_arena_strdup(arena, enum_def->values[i].description) : NULL;
        enum_values[i].flags = 0;
    }
    
    /* Allocate specialized_metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = NMO_TYPE_ID_INVALID;  /* Will be set during registration */
    spec_meta->metadata_type = NMO_METADATA_TYPE_ENUM;
    spec_meta->reserved = 0;
    spec_meta->enum_meta.values = enum_values;
    spec_meta->enum_meta.value_count = enum_def->value_count;
    
    /* Initialize type descriptor */
    type_desc->guid = type_guid;
    type_desc->name = type_name;
    type_desc->size = sizeof(int32_t); /* Enums are int32 */
    type_desc->alignment = alignof(int32_t);
    type_desc->category = NMO_TYPE_CATEGORY_ENUM;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD;
    type_desc->fields = NULL;
    type_desc->field_count = 0;
    type_desc->vtable = NULL;
    type_desc->description = enum_def->description ? nmo_arena_strdup(arena, enum_def->description) : NULL;
    type_desc->valid = true;
    
    /* Register type in registry */
    result = nmo_type_registry_register(type_registry, type_desc);
        if (result != NMO_OK) {
        return result;
    }

    /* Fetch registered descriptor to get assigned ID */
    nmo_type_descriptor_t *registered =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!registered) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Failed to find registered enum type");
    }

    /* Update type_id in metadata */
    spec_meta->type_id = registered->id;

    /* Add metadata to registry */
    size_t metadata_index = type_registry->metadata.count;
    nmo_status_t append_res = nmo_arena_array_append(&type_registry->metadata, &spec_meta);
        if (append_res != NMO_OK) {
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return append_res;
    }

    /* Add to type_id -> metadata_index hash table */
    nmo_status_t map_result = nmo_hash_table_insert(type_registry->type_to_metadata,
                                                    &registered->id,
                                                    &metadata_index);
        if (map_result != NMO_OK) {
        nmo_arena_array_pop(&type_registry->metadata, NULL);
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return map_result;
    }

    /* Update specialized_index (0-based) */
    registered->specialized_index = (uint32_t)metadata_index;
    
    /* Return GUID */
    if (out_guid) {
        *out_guid = type_guid;
    }
    
    NMO_RETURN_OK();
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
    
    /* Validate flags bits */
    nmo_status_t result = validate_flags_bits(flags_def->bits, flags_def->bit_count);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Use provided GUID or generate from name */
    nmo_guid_t type_guid;
    if (!nmo_guid_is_null(flags_def->guid)) {
        type_guid = flags_def->guid;
    } else {
        type_guid = nmo_type_generate_guid(flags_def->name);
    }
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                                "Flags type '%s' already registered",
                                flags_def->name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate type descriptor */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate flags type descriptor");
    }
    
    /* Initialize all fields to zero */
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    
    /* Copy flags type name */
    const char *type_name = nmo_arena_strdup(arena, flags_def->name);
    if (!type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to copy flags type name");
    }
    
    /* Allocate flags descriptors array */
    nmo_flags_descriptor_t *flags_bits = (nmo_flags_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_flags_descriptor_t) * flags_def->bit_count,
                        alignof(nmo_flags_descriptor_t));
    if (!flags_bits) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate flags bits array");
    }
    
    /* Copy flags bits */
    for (size_t i = 0; i < flags_def->bit_count; i++) {
        flags_bits[i].name = nmo_arena_strdup(arena, flags_def->bits[i].name);
        if (!flags_bits[i].name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy flags bit name");
        }
        flags_bits[i].mask = flags_def->bits[i].mask;
        flags_bits[i].description = flags_def->bits[i].description ? 
            nmo_arena_strdup(arena, flags_def->bits[i].description) : NULL;
        flags_bits[i].flags = 0;
    }
    
    /* Allocate specialized_metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = NMO_TYPE_ID_INVALID;  /* Will be set during registration */
    spec_meta->metadata_type = NMO_METADATA_TYPE_FLAGS;
    spec_meta->reserved = 0;
    spec_meta->flags_meta.bits = flags_bits;
    spec_meta->flags_meta.bit_count = flags_def->bit_count;
    
    /* Initialize type descriptor */
    type_desc->guid = type_guid;
    type_desc->name = type_name;
    type_desc->size = sizeof(uint32_t); /* Flags are uint32 */
    type_desc->alignment = alignof(uint32_t);
    type_desc->category = NMO_TYPE_CATEGORY_FLAGS;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD;
    type_desc->fields = NULL;
    type_desc->field_count = 0;
    type_desc->vtable = NULL;
    type_desc->description = flags_def->description ? nmo_arena_strdup(arena, flags_def->description) : NULL;
    type_desc->valid = true;
    
    /* Register type in registry */
    result = nmo_type_registry_register(type_registry, type_desc);
    if (result != NMO_OK) {
        return result;
    }

    /* Fetch registered descriptor to get assigned ID */
    nmo_type_descriptor_t *registered =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!registered) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Failed to find registered flags type");
    }

    /* Update type_id in metadata */
    spec_meta->type_id = registered->id;

    /* Add metadata to registry */
    size_t metadata_index = type_registry->metadata.count;
    nmo_status_t append_res = nmo_arena_array_append(&type_registry->metadata, &spec_meta);
    if (append_res != NMO_OK) {
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return append_res;
    }

    /* Add to type_id -> metadata_index hash table */
    nmo_status_t map_result = nmo_hash_table_insert(type_registry->type_to_metadata,
                                                    &registered->id,
                                                    &metadata_index);
    if (map_result != NMO_OK) {
        nmo_arena_array_pop(&type_registry->metadata, NULL);
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return map_result;
    }

    /* Update specialized_index (0-based) */
    registered->specialized_index = (uint32_t)metadata_index;
    
    /* Return GUID */
    if (out_guid) {
        *out_guid = type_guid;
    }
    
    NMO_RETURN_OK();
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

    nmo_type_descriptor_t *type =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "Type not found");
    }
    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Type is not an enum");
    }
    if (type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID ||
        type->specialized_index >= type_registry->metadata.count) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Enum type has no specialized metadata");
    }

    nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t **)
        nmo_arena_array_get(&type_registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Enum metadata mismatch");
    }

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

    nmo_arena_t *arena = type_registry->arena;
    size_t new_count = old_count + add_count;
    nmo_enum_descriptor_t *new_values = (nmo_enum_descriptor_t *)nmo_arena_alloc(
        arena,
        sizeof(nmo_enum_descriptor_t) * new_count,
        alignof(nmo_enum_descriptor_t));
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

        const char *name_copy = nmo_arena_strdup(arena, parsed_values[j].name);
        if (!name_copy) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy new enum value name");
        }

        new_values[out_index].name = name_copy;
        new_values[out_index].value = parsed_values[j].value;
        new_values[out_index].description = parsed_values[j].description ?
            nmo_arena_strdup(arena, parsed_values[j].description) : NULL;
        new_values[out_index].flags = 0;
        out_index++;
    }

    /* Update metadata to point to the extended list. */
    metadata->enum_meta.values = new_values;
    metadata->enum_meta.value_count = new_count;

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

    nmo_type_descriptor_t *type =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "Type not found");
    }
    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Type is not flags");
    }
    if (type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID ||
        type->specialized_index >= type_registry->metadata.count) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Flags type has no specialized metadata");
    }

    nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t **)
        nmo_arena_array_get(&type_registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Flags metadata mismatch");
    }

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

    nmo_arena_t *arena = type_registry->arena;
    size_t new_count = old_count + add_count;
    nmo_flags_descriptor_t *new_bits = (nmo_flags_descriptor_t *)nmo_arena_alloc(
        arena,
        sizeof(nmo_flags_descriptor_t) * new_count,
        alignof(nmo_flags_descriptor_t));
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

        const char *name_copy = nmo_arena_strdup(arena, parsed_bits[j].name);
        if (!name_copy) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy new flags bit name");
        }

        new_bits[out_index].name = name_copy;
        new_bits[out_index].mask = parsed_bits[j].mask;
        new_bits[out_index].description = parsed_bits[j].description ?
            nmo_arena_strdup(arena, parsed_bits[j].description) : NULL;
        new_bits[out_index].flags = 0;
        out_index++;
    }

    metadata->flags_meta.bits = new_bits;
    metadata->flags_meta.bit_count = new_count;

    nmo_arena_destroy(temp_arena);
    NMO_RETURN_OK();
}
