/**
 * @file plugin_support.c
 * @brief Plugin management and cascade deletion implementation
 *
 * Implements plugin tracking, cascade deletion, type invalidation,
 * and specialized metadata management.
 * Reference: CKParameterManager.cpp lines 38-47, 84-129, 140-145
 */

#include "type/type_system.h"
#include "app/nmo_plugin.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Plugin Registration
 * ============================================================================ */

nmo_result_t nmo_type_registry_register_plugin(
    nmo_type_registry_t *registry,
    const nmo_plugin_t *plugin) 
{
    if (!registry || !plugin) {
        return nmo_result_error(NMO_ERROR(
            registry ? registry->arena : NULL,
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "Invalid registry or plugin pointer"));
    }

    // Check if already registered
    if (nmo_hash_table_contains(registry->plugin_map, &plugin->guid)) {
        return nmo_result_ok();  // Already registered, no-op
    }

    // Insert plugin pointer into map
    nmo_hash_table_insert(registry->plugin_map, &plugin->guid, &plugin);
    
    return nmo_result_ok();
}

const nmo_plugin_t* nmo_type_registry_get_plugin(
    const nmo_type_registry_t *registry,
    nmo_guid_t plugin_guid) 
{
    if (!registry) return NULL;

    const nmo_plugin_t *plugin = NULL;
    int found = nmo_hash_table_get(registry->plugin_map, &plugin_guid, &plugin);
    
    return found ? plugin : NULL;
}

/* ============================================================================
 * Cascade Deletion (Recursive)
 * Reference: CKParameterManager.cpp, lines 140-145
 * ============================================================================ */

nmo_result_t nmo_type_registry_unregister_derived(
    nmo_type_registry_t *registry,
    nmo_guid_t base_guid) 
{
    if (!registry) {
        return nmo_result_error(NMO_ERROR(
            NULL,
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "Invalid registry pointer"));
    }

    // Find all types derived from base_guid
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (!type || !type->valid) continue;

        // Check if this type derives from base_guid
        if (nmo_guid_equals(type->base_type, base_guid)) {
            // Recursively unregister all types derived from this one
            nmo_result_t result = nmo_type_registry_unregister_derived(registry, type->guid);
            if (result.code != NMO_OK) {
                return result;
            }

            // Then unregister this type
            result = nmo_type_registry_unregister(registry, type->guid);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Plugin Batch Unregistration
 * ============================================================================ */

nmo_result_t nmo_type_registry_unregister_plugin_types(
    nmo_type_registry_t *registry,
    nmo_guid_t plugin_guid) 
{
    if (!registry) {
        return nmo_result_error(NMO_ERROR(
            NULL,
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "Invalid registry pointer"));
    }

    // First pass: collect all types from this plugin
    nmo_guid_t *types_to_remove = NULL;
    size_t remove_count = 0;
    size_t remove_capacity = 0;

    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (!type || !type->valid) continue;

        // Check if this type belongs to the plugin
        nmo_guid_t stored_plugin_guid;
        int found = nmo_hash_table_get(registry->type_to_plugin, &type->id, &stored_plugin_guid);
        
        if (found && nmo_guid_equals(stored_plugin_guid, plugin_guid)) {
            // Expand array if needed
            if (remove_count >= remove_capacity) {
                remove_capacity = remove_capacity == 0 ? 16 : remove_capacity * 2;
                nmo_guid_t *new_array = (nmo_guid_t*)nmo_arena_alloc(
                    registry->arena,
                    remove_capacity * sizeof(nmo_guid_t),
                    _Alignof(nmo_guid_t));
                if (new_array) {
                    if (types_to_remove) {
                        memcpy(new_array, types_to_remove, remove_count * sizeof(nmo_guid_t));
                    }
                    types_to_remove = new_array;
                }
            }

            types_to_remove[remove_count++] = type->guid;
        }
    }

    // Second pass: unregister all collected types (with cascade deletion)
    for (size_t i = 0; i < remove_count; i++) {
        // Unregister derived types first
        nmo_result_t result = nmo_type_registry_unregister_derived(registry, types_to_remove[i]);
        if (result.code != NMO_OK) {
            return result;
        }

        // Then unregister the type itself
        result = nmo_type_registry_unregister(registry, types_to_remove[i]);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    // Remove plugin from registry
    nmo_hash_table_remove(registry->plugin_map, &plugin_guid);
    if (remove_count > 0) {
        registry->plugin_count--;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Type Invalidation (Soft Delete)
 * Reference: CKParameterManager.cpp, lines 84-129
 * ============================================================================ */

nmo_result_t nmo_type_registry_invalidate(
    nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) {
        return nmo_result_error(NMO_ERROR(
            NULL,
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "Invalid registry pointer"));
    }

    // Find type by GUID
    nmo_type_descriptor_t *type = (nmo_type_descriptor_t*)nmo_type_registry_find_by_guid(registry, guid);
    if (!type) {
        return nmo_result_error(NMO_ERROR(
            registry->arena,
            NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR,
            "Type not found in registry"));
    }

    // Mark as invalid (soft delete)
    type->valid = false;

    // Invalidate derivation masks (will be rebuilt on next compatibility check)
    registry->derivation_masks_valid = false;

    // Note: We keep the slot for potential recycling
    // The actual slot cleanup happens in nmo_type_registry_unregister()

    return nmo_result_ok();
}

/* ============================================================================
 * Specialized Metadata Management
 * ============================================================================ */

nmo_result_t nmo_type_registry_register_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata) 
{
    if (!registry || !metadata) {
        return nmo_result_error(NMO_ERROR(
            registry ? registry->arena : NULL,
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "Invalid registry or metadata pointer"));
    }

    // Copy metadata to arena
    nmo_specialized_metadata_t *meta_copy = (nmo_specialized_metadata_t*)nmo_arena_alloc(
        registry->arena,
        sizeof(nmo_specialized_metadata_t),
        _Alignof(nmo_specialized_metadata_t));
    
    if (!meta_copy) {
        return nmo_result_error(NMO_ERROR(
            registry->arena,
            NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR,
            "Failed to allocate metadata copy"));
    }

    memcpy(meta_copy, metadata, sizeof(nmo_specialized_metadata_t));

    // Store in array and create index mapping
    size_t index = registry->metadata.count;
    nmo_result_t res = nmo_arena_array_append(&registry->metadata, &meta_copy);
    if (res.code != NMO_OK) return res;

    // Create fast lookup mapping (type_id -> metadata_index)
    nmo_hash_table_insert(registry->type_to_metadata, &metadata->type_id, &index);

    // Update type descriptor's specialized_index (1-based index, 0 means no metadata)
    if (metadata->type_id >= 0 && (size_t)metadata->type_id < registry->types.count) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, metadata->type_id);
        if (type && type->valid) {
            type->specialized_index = (uint32_t)(index + 1);  // 1-based index
        }
    }

    return nmo_result_ok();
}

const nmo_specialized_metadata_t* nmo_type_registry_get_metadata(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) 
{
    if (!registry) return NULL;

    // Fast lookup via hash table
    size_t index;
    int found = nmo_hash_table_get(registry->type_to_metadata, &type_id, &index);
    if (!found || index >= registry->metadata.count) {
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

    // Clear specialized_index in type descriptor (set to 0 = no metadata)
    if (type_id >= 0 && (size_t)type_id < registry->types.count) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
        if (type && type->valid) {
            type->specialized_index = 0;  // Reset to "no metadata"
        }
    }

    // Note: We keep the metadata in the array for now (it's arena-allocated anyway)
    // A full cleanup would require compacting the array and updating all indices
}
