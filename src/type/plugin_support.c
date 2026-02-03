/**
 * @file plugin_support.c
 * @brief Plugin tracking and cascade deletion implementation
 *
 * Implements plugin tracking, cascade deletion, type invalidation,
 * and specialized metadata management.
 * Reference: CKParameterManager.cpp lines 38-47, 84-129, 140-145
 */

#include "type/type_system.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Metadata Deep Copy Helpers
 * ============================================================================ */

static const char *nmo_arena_strdup_optional(nmo_arena_t *arena, const char *src) {
    if (!src) {
        return NULL;
    }
    return nmo_arena_strdup(arena, src);
}

static nmo_status_t nmo_copy_enum_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t *out_copy
) {
    if (!metadata->enum_meta.values || metadata->enum_meta.value_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Enum metadata must include values");
    }

    nmo_enum_descriptor_t *values_copy = (nmo_enum_descriptor_t *)nmo_arena_alloc(
        registry->arena,
        metadata->enum_meta.value_count * sizeof(nmo_enum_descriptor_t),
        _Alignof(nmo_enum_descriptor_t));
    if (!values_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate enum metadata values");
    }

    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        const nmo_enum_descriptor_t *src = &metadata->enum_meta.values[i];
        nmo_enum_descriptor_t *dst = &values_copy[i];
        dst->name = nmo_arena_strdup_optional(registry->arena, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy enum value name");
        }
        dst->description = nmo_arena_strdup_optional(registry->arena, src->description);
        if (src->description && !dst->description) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy enum value description");
        }
        dst->value = src->value;
        dst->flags = src->flags;
    }

    out_copy->enum_meta.values = values_copy;
    out_copy->enum_meta.value_count = metadata->enum_meta.value_count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_copy_flags_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t *out_copy
) {
    if (!metadata->flags_meta.bits || metadata->flags_meta.bit_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Flags metadata must include bits");
    }

    nmo_flags_descriptor_t *bits_copy = (nmo_flags_descriptor_t *)nmo_arena_alloc(
        registry->arena,
        metadata->flags_meta.bit_count * sizeof(nmo_flags_descriptor_t),
        _Alignof(nmo_flags_descriptor_t));
    if (!bits_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate flags metadata bits");
    }

    for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
        const nmo_flags_descriptor_t *src = &metadata->flags_meta.bits[i];
        nmo_flags_descriptor_t *dst = &bits_copy[i];
        dst->name = nmo_arena_strdup_optional(registry->arena, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy flags bit name");
        }
        dst->description = nmo_arena_strdup_optional(registry->arena, src->description);
        if (src->description && !dst->description) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy flags bit description");
        }
        dst->mask = src->mask;
        dst->flags = src->flags;
    }

    out_copy->flags_meta.bits = bits_copy;
    out_copy->flags_meta.bit_count = metadata->flags_meta.bit_count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_copy_struct_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t *out_copy
) {
    if (!metadata->struct_meta.fields || metadata->struct_meta.field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Struct metadata must include fields");
    }

    nmo_struct_descriptor_t *fields_copy = (nmo_struct_descriptor_t *)nmo_arena_alloc(
        registry->arena,
        metadata->struct_meta.field_count * sizeof(nmo_struct_descriptor_t),
        _Alignof(nmo_struct_descriptor_t));
    if (!fields_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate struct metadata fields");
    }

    for (size_t i = 0; i < metadata->struct_meta.field_count; i++) {
        const nmo_struct_descriptor_t *src = &metadata->struct_meta.fields[i];
        nmo_struct_descriptor_t *dst = &fields_copy[i];
        dst->name = nmo_arena_strdup_optional(registry->arena, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy struct field name");
        }
        dst->description = nmo_arena_strdup_optional(registry->arena, src->description);
        if (src->description && !dst->description) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy struct field description");
        }
        dst->type_guid = src->type_guid;
        dst->offset = src->offset;
        dst->size = src->size;
        dst->array_count = src->array_count;
        dst->flags = src->flags;
    }

    out_copy->struct_meta.fields = fields_copy;
    out_copy->struct_meta.field_count = metadata->struct_meta.field_count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_deep_copy_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t **out_copy
) {
    if (!registry || !metadata || !out_copy) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid metadata copy arguments");
    }

    if (metadata->metadata_type != NMO_METADATA_TYPE_ENUM &&
        metadata->metadata_type != NMO_METADATA_TYPE_STRUCT &&
        metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Unknown metadata type");
    }

    nmo_specialized_metadata_t *copy = (nmo_specialized_metadata_t *)nmo_arena_alloc(
        registry->arena,
        sizeof(nmo_specialized_metadata_t),
        _Alignof(nmo_specialized_metadata_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate metadata copy");
    }

    memset(copy, 0, sizeof(*copy));
    copy->type_id = metadata->type_id;
    copy->metadata_type = metadata->metadata_type;
    copy->reserved = metadata->reserved;

    nmo_last_error_clear();
    nmo_status_t result = NMO_OK;
    switch (metadata->metadata_type) {
        case NMO_METADATA_TYPE_ENUM:
            result = nmo_copy_enum_metadata(registry, metadata, copy);
            break;
        case NMO_METADATA_TYPE_STRUCT:
            result = nmo_copy_struct_metadata(registry, metadata, copy);
            break;
        case NMO_METADATA_TYPE_FLAGS:
            result = nmo_copy_flags_metadata(registry, metadata, copy);
            break;
    }

    if (result != NMO_OK) {
        return result;
    }

    *out_copy = copy;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Plugin Tracking
 * ============================================================================ */

nmo_status_t nmo_type_registry_set_creator_plugin(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_guid_t plugin_guid)
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }

    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }

    /* Set the creator plugin GUID in the descriptor */
    type->creator_plugin_guid = plugin_guid;

    /* Also track in the type_to_plugin map for cascade deletion */
    nmo_status_t insert_result = nmo_hash_table_insert(registry->type_to_plugin, &type_id, &plugin_guid);
    if (insert_result != NMO_OK && insert_result != NMO_ERR_ALREADY_EXISTS) {
        return insert_result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Cascade Deletion (Recursive)
 * Reference: CKParameterManager.cpp, lines 140-145
 * ============================================================================ */

nmo_status_t nmo_type_registry_unregister_derived(
    nmo_type_registry_t *registry,
    nmo_guid_t base_guid) 
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    // Find all types derived from base_guid
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (!type || !type->valid) continue;

        // Check if this type derives from base_guid
        if (nmo_guid_equals(type->base_type, base_guid)) {
            // Recursively unregister all types derived from this one
            nmo_status_t result = nmo_type_registry_unregister_derived(registry, type->guid);
            if (result != NMO_OK) {
                return result;
            }

            // Then unregister this type
            result = nmo_type_registry_unregister(registry, type->guid);
            if (result != NMO_OK) {
                return result;
            }
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Plugin Batch Unregistration
 * ============================================================================ */

nmo_status_t nmo_type_registry_unregister_plugin_types(
    nmo_type_registry_t *registry,
    nmo_guid_t plugin_guid) 
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    /* First pass: count all types from this plugin */
    size_t remove_count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (!type || !type->valid) continue;

        nmo_guid_t stored_plugin_guid;
        nmo_status_t found = nmo_hash_table_get(registry->type_to_plugin, &type->id, &stored_plugin_guid);
        if (found == NMO_OK && nmo_guid_equals(stored_plugin_guid, plugin_guid)) {
            remove_count++;
        }
    }

    nmo_guid_t *types_to_remove = NULL;
    if (remove_count > 0) {
        types_to_remove = (nmo_guid_t *)nmo_arena_alloc(
            registry->arena,
            remove_count * sizeof(nmo_guid_t),
            _Alignof(nmo_guid_t));
        if (!types_to_remove) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate plugin removal list");
        }

        size_t index = 0;
        for (size_t i = 0; i < registry->types.count; i++) {
            const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
            if (!type || !type->valid) continue;

            nmo_guid_t stored_plugin_guid;
            nmo_status_t found = nmo_hash_table_get(registry->type_to_plugin, &type->id, &stored_plugin_guid);
            if (found == NMO_OK && nmo_guid_equals(stored_plugin_guid, plugin_guid)) {
                types_to_remove[index++] = type->guid;
            }
        }
    }

    /* Second pass: unregister all collected types (with cascade deletion) */
    for (size_t i = 0; i < remove_count; i++) {
        /* Unregister derived types first */
        nmo_status_t result = nmo_type_registry_unregister_derived(registry, types_to_remove[i]);
        if (result != NMO_OK) {
            return result;
        }

        /* Then unregister the type itself */
        result = nmo_type_registry_unregister(registry, types_to_remove[i]);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Type Invalidation (Soft Delete)
 * Reference: CKParameterManager.cpp, lines 84-129
 * ============================================================================ */

nmo_status_t nmo_type_registry_invalidate(
    nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    // Find type by GUID
    nmo_type_descriptor_t *type = (nmo_type_descriptor_t*)nmo_type_registry_find_by_guid(registry, guid);
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found in registry");
    }

    // Mark as invalid (soft delete)
    type->valid = false;

    // Invalidate derivation masks (will be rebuilt on next compatibility check)
    registry->derivation_masks_valid = false;

    // Note: We keep the slot for potential recycling
    // The actual slot cleanup happens in nmo_type_registry_unregister()

    NMO_RETURN_OK();
}

/* ============================================================================
 * Specialized Metadata Management
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata) 
{
    if (!registry || !metadata) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry or metadata pointer");
    }

    if (metadata->type_id < 0 || (size_t)metadata->type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid metadata type ID");
    }

    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, metadata->type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }

    nmo_specialized_metadata_t *meta_copy = NULL;
    nmo_status_t copy_result = nmo_deep_copy_metadata(registry, metadata, &meta_copy);
    if (copy_result != NMO_OK) {
        return copy_result;
    }

    // Store in array and create index mapping
    size_t index = registry->metadata.count;
    nmo_status_t res = nmo_arena_array_append(&registry->metadata, &meta_copy);
    if (res != NMO_OK) return res;

    // Create fast lookup mapping (type_id -> metadata_index)
    nmo_status_t insert_result = nmo_hash_table_insert(registry->type_to_metadata,
                                                       &metadata->type_id,
                                                       &index);
    if (insert_result != NMO_OK) {
        nmo_arena_array_pop(&registry->metadata, NULL);
        return insert_result;
    }

    // Update type descriptor's specialized_index (0-based index, invalid sentinel if none)
    type->specialized_index = (uint32_t)index;

    NMO_RETURN_OK();
}

const nmo_specialized_metadata_t* nmo_type_registry_get_metadata(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) 
{
    if (!registry) return NULL;

    // Fast lookup via hash table
    size_t index;
    nmo_status_t found = nmo_hash_table_get(registry->type_to_metadata, &type_id, &index);
    if (found != NMO_OK || index >= registry->metadata.count) {
        return NULL;
    }

    return *(nmo_specialized_metadata_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, index);
}

void nmo_type_registry_unregister_metadata(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id) 
{
    if (!registry) return;

    // Remove from hash table
    nmo_hash_table_remove(registry->type_to_metadata, &type_id);

    // Clear specialized_index in type descriptor
    if (type_id >= 0 && (size_t)type_id < registry->types.count) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
        if (type && type->valid) {
            type->specialized_index = NMO_SPECIALIZED_INDEX_INVALID;
        }
    }

    // Note: We keep the metadata in the array for now (it's arena-allocated anyway)
    // A full cleanup would require compacting the array and updating all indices
}
