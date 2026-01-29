/**
 * @file type_registry.c
 * @brief Implementation of unified type registry with GUID-based O(1) lookup
 *
 * Implements the design validated against Virtools SDK:
 * - GUID-indexed hash table (CKParameterManager.cpp:175-181)
 * - Slot recycling (CKParameterManager.cpp:11-37)
 * - Lazy derivation mask updates (CKParameterManager.cpp:1265-1276)
 * - Plugin tracking (CKParameterManager.cpp:38-47)
 */

#include "type/type_system.h"
#include "app/nmo_plugin.h"  /* Need full nmo_plugin_t definition */
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Note: struct nmo_type_registry_t is already defined in type_system.h
 * We just need to implement the functions that operate on it.
 * ============================================================================ */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Hash function adapter for GUID keys
 */
static size_t guid_hash_func(const void *key, size_t key_size) {
    (void)key_size; // Always sizeof(nmo_guid_t)
    return (size_t)nmo_guid_hash(*(const nmo_guid_t *)key);
}

/**
 * @brief Compare function adapter for GUID keys
 */
static int guid_compare_func(const void *key1, const void *key2, size_t key_size) {
    (void)key_size; // Always sizeof(nmo_guid_t)
    return !nmo_guid_equals(*(const nmo_guid_t *)key1, *(const nmo_guid_t *)key2);
}

/**
 * @brief Hash function for string keys
 */
static size_t string_hash_func(const void *key, size_t key_size) {
    (void)key_size;
    const char *str = *(const char **)key;
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

/**
 * @brief Compare function for string keys
 */
static int string_compare_func(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const char *str1 = *(const char **)key1;
    const char *str2 = *(const char **)key2;
    return strcmp(str1, str2);
}

/**
 * @brief Find first NULL slot in types array (slot recycling)
 *
 * Implements Virtools pattern (CKParameterManager.cpp:11-23):
 * Unregistered types leave NULL slots that can be reused before expanding.
 *
 * @param registry Type registry
 * @return First free slot index, or registry->type_count if none
 */
static size_t find_free_slot(const nmo_type_registry_t *registry) {
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t **slot = (nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (slot && *slot == NULL) {
            return i;
        }
    }
    return registry->types.count;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

nmo_type_registry_t* nmo_type_registry_create(nmo_arena_t *arena) {
    if (!arena) return NULL;

    // Allocate registry struct
    nmo_type_registry_t *registry = nmo_arena_alloc(arena, 
        sizeof(nmo_type_registry_t), 
        _Alignof(nmo_type_registry_t));
    if (!registry) return NULL;

    // Initialize all fields to zero
    memset(registry, 0, sizeof(nmo_type_registry_t));
    
    registry->arena = arena;
    registry->derivation_masks_valid = false;

    // Initialize type array
    if (nmo_result_is_error(nmo_arena_array_init(&registry->types,
                                                 sizeof(nmo_type_descriptor_t*),
                                                 32,
                                                 arena))) {
        return NULL;
    }

    // Create GUID hash table
    registry->guid_map = nmo_hash_table_create(
        NULL, // Use default allocator
        sizeof(nmo_guid_t),
        sizeof(nmo_type_id_t),
        128,
        guid_hash_func,
        guid_compare_func
    );
    if (!registry->guid_map) return NULL;

    // Create name hash table
    registry->name_map = nmo_hash_table_create(
        NULL,
        sizeof(const char *),
        sizeof(nmo_type_id_t),
        128,
        string_hash_func,
        string_compare_func
    );
    if (!registry->name_map) return NULL;

    // Create class_id hash table (for Virtools object types)
    registry->class_id_map = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t),
        sizeof(nmo_type_id_t),
        64,  // Most Virtools files have < 64 object types
        NULL,  // Use default uint32 hash
        NULL   // Use default uint32 compare
    );
    if (!registry->class_id_map) return NULL;

    // Create plugin hash tables
    registry->plugin_map = nmo_hash_table_create(
        NULL,
        sizeof(nmo_guid_t),
        sizeof(nmo_plugin_t*),  // Store pointer, not struct
        32,
        guid_hash_func,
        guid_compare_func
    );
    if (!registry->plugin_map) return NULL;

    registry->type_to_plugin = nmo_hash_table_create(
        NULL,
        sizeof(nmo_type_id_t),
        sizeof(nmo_guid_t),
        128,
        NULL, // Default hash for integers
        NULL  // Default compare for integers
    );
    if (!registry->type_to_plugin) return NULL;

    // Create metadata management structures
    registry->type_to_metadata = nmo_hash_table_create(
        NULL,
        sizeof(nmo_type_id_t),
        sizeof(size_t),  // metadata index
        64,
        NULL,  // Default hash for integers
        NULL   // Default compare for integers
    );
    if (!registry->type_to_metadata) return NULL;

    // Initialize metadata array (lazy init with 0 capacity)
    nmo_arena_array_init(&registry->metadata, sizeof(nmo_specialized_metadata_t*), 0, arena);
    
    // Initialize saver managers array (lazy init with 0 capacity)
    nmo_arena_array_init(&registry->saver_managers, sizeof(nmo_saver_manager_t*), 0, arena);

    return registry;
}

void nmo_type_registry_destroy(nmo_type_registry_t *registry) {
    if (!registry) return;

    if (registry->guid_map) {
        nmo_hash_table_destroy(registry->guid_map);
    }
    if (registry->name_map) {
        nmo_hash_table_destroy(registry->name_map);
    }
    if (registry->class_id_map) {
        nmo_hash_table_destroy(registry->class_id_map);
    }
    if (registry->plugin_map) {
        nmo_hash_table_destroy(registry->plugin_map);
    }
    if (registry->type_to_plugin) {
        nmo_hash_table_destroy(registry->type_to_plugin);
    }
    if (registry->type_to_metadata) {
        nmo_hash_table_destroy(registry->type_to_metadata);
    }
    if (registry->manager_guid_map) {
        nmo_hash_table_destroy(registry->manager_guid_map);
    }
    if (registry->type_to_manager) {
        nmo_hash_table_destroy(registry->type_to_manager);
    }

    // Arena owns all memory, no need to free types/metadata/manager arrays
}

nmo_result_t nmo_type_registry_register(
    nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor) 
{
    if (!registry || !descriptor) {
        nmo_result_t result = { NMO_ERR_INVALID_ARGUMENT, NULL };
        return result;
    }

    // Check for GUID collision
    nmo_type_id_t existing_id;
    if (nmo_result_is_ok(nmo_hash_table_get(registry->guid_map,
                                           &descriptor->guid,
                                           &existing_id))) {
        nmo_result_t result = { NMO_ERR_ALREADY_EXISTS, NULL };
        return result;
    }

    // Find slot (reuse NULL slots before expanding)
    size_t slot = find_free_slot(registry);

    // Allocate and copy descriptor
    nmo_type_descriptor_t *type = nmo_arena_alloc(registry->arena,
        sizeof(nmo_type_descriptor_t),
        _Alignof(nmo_type_descriptor_t));
    if (!type) {
        nmo_result_t result = { NMO_ERR_NOMEM, NULL };
        return result;
    }
    
    memcpy(type, descriptor, sizeof(nmo_type_descriptor_t));
    
    // Assign ID and store descriptor
    nmo_type_id_t type_id = (nmo_type_id_t)slot;
    type->id = type_id;
    type->valid = true;
    
    if (slot < registry->types.count) {
        // Reuse slot
        nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
        *slot_ptr = type;
    } else {
        // Append new slot
        nmo_result_t res = nmo_arena_array_append(&registry->types, &type);
        if (nmo_result_is_error(res)) return res;
    }

    // Insert into hash tables
    nmo_result_t result = nmo_hash_table_insert(registry->guid_map, &type->guid, &type_id);
    if (nmo_result_is_error(result)) {
        nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
        *slot_ptr = NULL;
        nmo_result_t res = { result.code, NULL };
        return res;
    }

    if (type->name) {
        result = nmo_hash_table_insert(registry->name_map, &type->name, &type_id);
        if (nmo_result_is_error(result)) {
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            return result;
        }
    }

    // Insert into class_id map if this is a Virtools object type
    if (type->class_id != 0) {
        result = nmo_hash_table_insert(registry->class_id_map, &type->class_id, &type_id);
        if (nmo_result_is_error(result)) {
            if (type->name) {
                nmo_hash_table_remove(registry->name_map, &type->name);
            }
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            return result;
        }
    }

    // Track plugin ownership if provided
    if (type->creator_plugin) {
        // Store plugin pointer in maps
        result = nmo_hash_table_insert(registry->type_to_plugin,
                                       &type_id,
                                       &type->creator_plugin->guid);
        if (nmo_result_is_error(result)) {
            if (type->class_id != 0) {
                nmo_hash_table_remove(registry->class_id_map, &type->class_id);
            }
            if (type->name) {
                nmo_hash_table_remove(registry->name_map, &type->name);
            }
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            return result;
        }
        
        // Ensure plugin is registered
        if (!nmo_hash_table_contains(registry->plugin_map, &type->creator_plugin->guid)) {
            result = nmo_hash_table_insert(registry->plugin_map,
                                           &type->creator_plugin->guid,
                                           &type->creator_plugin);
            if (nmo_result_is_error(result)) {
                nmo_hash_table_remove(registry->type_to_plugin, &type_id);
                if (type->class_id != 0) {
                    nmo_hash_table_remove(registry->class_id_map, &type->class_id);
                }
                if (type->name) {
                    nmo_hash_table_remove(registry->name_map, &type->name);
                }
                nmo_hash_table_remove(registry->guid_map, &type->guid);
                nmo_type_descriptor_t **slot_ptr =
                    (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
                *slot_ptr = NULL;
                return result;
            }
            registry->plugin_count++;
        }
    } else {
        registry->builtin_count++;
    }

    // Invalidate derivation masks (lazy update)
    registry->derivation_masks_valid = false;
    registry->registry_version++;

    nmo_result_t success = { NMO_OK, NULL };
    return success;
}

nmo_result_t nmo_type_registry_unregister(
    nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) {
        nmo_result_t result = { NMO_ERR_INVALID_ARGUMENT, NULL };
        return result;
    }

    // Find type by GUID
    nmo_type_id_t type_id;
    if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map, &guid, &type_id))) {
        nmo_result_t result = { NMO_ERR_NOT_FOUND, NULL };
        return result;
    }

    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
    if (!type) {
        nmo_result_t result = { NMO_ERR_NOT_FOUND, NULL };
        return result;
    }

    // Remove from hash tables
    nmo_hash_table_remove(registry->guid_map, &guid);
    if (type->name) {
        nmo_hash_table_remove(registry->name_map, &type->name);
    }
    nmo_hash_table_remove(registry->type_to_plugin, &type_id);

    // Update stats
    if (type->creator_plugin) {
        // Note: Don't decrement plugin_count here, done in unregister_plugin_types
    } else {
        if (registry->builtin_count > 0) {
            registry->builtin_count--;
        }
    }

    // Soft delete: mark invalid, keep slot for recycling
    type->valid = false;
    nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
    *slot_ptr = NULL;

    // Invalidate derivation masks
    registry->derivation_masks_valid = false;
    registry->registry_version++;

    nmo_result_t success = { NMO_OK, NULL };
    return success;
}

/* Note: nmo_type_registry_unregister_plugin_types() is now implemented in plugin_support.c
 * with full cascade deletion support (Phase 5.6)
 */

const nmo_type_descriptor_t* nmo_type_registry_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) return NULL;

    nmo_type_id_t type_id;
    if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map, &guid, &type_id))) {
        return NULL;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    return (type && type->valid) ? type : NULL;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_name(
    const nmo_type_registry_t *registry,
    const char *name) 
{
    if (!registry || !name) return NULL;

    nmo_type_id_t type_id;
    if (nmo_result_is_error(nmo_hash_table_get(registry->name_map, &name, &type_id))) {
        return NULL;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    return (type && type->valid) ? type : NULL;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id) 
{
    if (!registry || class_id == 0) return NULL;

    nmo_type_id_t type_id;
    if (nmo_result_is_error(nmo_hash_table_get(registry->class_id_map,
                                               &class_id,
                                               &type_id))) {
        return NULL;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    return (type && type->valid) ? type : NULL;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id_inherited(
    const nmo_type_registry_t *registry,
    uint32_t class_id) 
{
    if (!registry || class_id == 0) return NULL;

    // First try direct lookup
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(registry, class_id);
    if (type) return type;

    // Walk up class hierarchy to find parent with schema
    // Uses nmo_ckclass API (from include/schema/nmo_ckobject_hierarchy.h)
    extern const char *nmo_ckclass_get_name_by_id(uint32_t class_id);
    extern const char *nmo_ckclass_get_parent(const char *class_name);
    extern uint32_t nmo_ckclass_get_id_by_name(const char *class_name);
    
    uint32_t current_class_id = class_id;
    int max_depth = 50;  // Prevent infinite loops
    
    while (max_depth-- > 0) {
        // Get current class name
        const char *class_name = nmo_ckclass_get_name_by_id(current_class_id);
        if (!class_name) break;  // Unknown class
        
        // Get parent class name
        const char *parent_name = nmo_ckclass_get_parent(class_name);
        if (!parent_name) break;  // Reached root
        
        // Get parent class ID
        current_class_id = nmo_ckclass_get_id_by_name(parent_name);
        if (current_class_id == 0) break;  // Invalid parent
        
        type = nmo_type_registry_find_by_class_id(registry, current_class_id);
        if (type) return type;
    }

    return NULL;  // No schema found in hierarchy
}

const nmo_type_descriptor_t* nmo_type_registry_get_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t id) 
{
    if (!registry || id < 0 || (size_t)id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, id);
    return (type && type->valid) ? type : NULL;
}

void nmo_type_registry_update_derivation_masks(nmo_type_registry_t *registry) {
    if (!registry || registry->derivation_masks_valid) return;

    // Reset all masks
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
        if (type && type->valid) {
            nmo_compat_mask_clear(&type->compat_mask);
            nmo_compat_mask_set(&type->compat_mask, (nmo_type_id_t)i); // Self-compatible
        }
    }

    // Build derivation chains
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
        if (!type || !type->valid) continue;
        
        // If has parent, inherit parent's compatibility mask
        if (!nmo_guid_is_null(type->base_type)) {
            const nmo_type_descriptor_t *parent = nmo_type_registry_find_by_guid(registry, type->base_type);
            if (parent && parent->valid) {
                for (size_t j = 0; j < registry->types.count; j++) {
                    if (nmo_compat_mask_is_set(&parent->compat_mask, (nmo_type_id_t)j)) {
                        nmo_compat_mask_set(&type->compat_mask, (nmo_type_id_t)j);
                    }
                }
            }
        }
    }

    registry->derivation_masks_valid = true;
}

void nmo_type_registry_get_stats(
    const nmo_type_registry_t *registry,
    size_t *total_types,
    size_t *builtin_types,
    size_t *plugin_types) {
    
    if (!registry) {
        if (total_types) *total_types = 0;
        if (builtin_types) *builtin_types = 0;
        if (plugin_types) *plugin_types = 0;
        return;
    }
    
    // Count valid types
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type != NULL && type->valid) {
            count++;
        }
    }
    
    if (total_types) *total_types = count;
    if (builtin_types) *builtin_types = registry->builtin_count;
    if (plugin_types) *plugin_types = registry->plugin_count;
}

/* ============================================================================
 * Field Annotation API Implementation
 * ============================================================================ */

const char* nmo_field_semantic_name(nmo_field_semantic_t semantic) {
    switch (semantic) {
        case NMO_SEMANTIC_NONE:       return "none";
        case NMO_SEMANTIC_POSITION:   return "position";
        case NMO_SEMANTIC_ROTATION:   return "rotation";
        case NMO_SEMANTIC_SCALE:      return "scale";
        case NMO_SEMANTIC_DIRECTION:  return "direction";
        case NMO_SEMANTIC_NORMAL:     return "normal";
        case NMO_SEMANTIC_COLOR:      return "color";
        case NMO_SEMANTIC_ALPHA:      return "alpha";
        case NMO_SEMANTIC_UV:         return "uv";
        case NMO_SEMANTIC_ID:         return "id";
        case NMO_SEMANTIC_OBJECT_REF: return "object_ref";
        case NMO_SEMANTIC_MANAGER_REF:return "manager_ref";
        case NMO_SEMANTIC_TIME:       return "time";
        case NMO_SEMANTIC_DURATION:   return "duration";
        case NMO_SEMANTIC_NAME:       return "name";
        case NMO_SEMANTIC_PATH:       return "path";
        case NMO_SEMANTIC_USER_DATA:  return "user_data";
        default:                      return "unknown";
    }
}

const char* nmo_field_units_name(nmo_field_units_t units) {
    switch (units) {
        case NMO_UNITS_NONE:          return "none";
        case NMO_UNITS_DEGREES:       return "degrees";
        case NMO_UNITS_RADIANS:       return "radians";
        case NMO_UNITS_METERS:        return "meters";
        case NMO_UNITS_CENTIMETERS:   return "centimeters";
        case NMO_UNITS_UNITS:         return "units";
        case NMO_UNITS_SECONDS:       return "seconds";
        case NMO_UNITS_MILLISECONDS:  return "milliseconds";
        case NMO_UNITS_FRAMES:        return "frames";
        default:                      return "unknown";
    }
}

/* ============================================================================
 * Phase 6.3: Type Compatibility & Conversion API Implementation
 * 
 * Reference: SCHEMA_V2_IMPLEMENTATION_PLAN.md Phase 6.3
 * ============================================================================ */

/* --- 6.3.1: Inheritance Checking API --- */

bool nmo_type_is_derived_from(
    const nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id) {
    
    if (!registry) return false;
    
    // Invalid IDs
    if (child_id < 0 || (size_t)child_id >= registry->types.count ||
        parent_id < 0 || (size_t)parent_id >= registry->types.count) {
        return false;
    }
    
    // NULL or invalid types
    const nmo_type_descriptor_t *child = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, child_id);
    const nmo_type_descriptor_t *parent = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, parent_id);
    if (!child || !child->valid || !parent || !parent->valid) {
        return false;
    }
    
    // Same type (trivial case)
    if (child_id == parent_id) {
        return true;
    }
    
    // Lazy update of derivation masks if needed
    if (!registry->derivation_masks_valid) {
        // Note: This is const-correct violation, but Virtools does same
        // (CKParameterManager::IsTypeCompatible, line 262)
        nmo_type_registry_update_derivation_masks((nmo_type_registry_t *)registry);
    }
    
    bool result = nmo_compat_mask_is_set(&child->compat_mask, parent_id);
    return result;
}

nmo_result_t nmo_type_get_inheritance_chain(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_type_id_t **out_chain,
    size_t *out_count,
    nmo_arena_t *arena) {
    
    if (!registry || !out_chain || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    // Validate type ID
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid or unregistered type"));
    }
    
    // Count chain length (walk up base_type chain)
    size_t chain_length = 0;
    nmo_type_id_t current_id = type_id;
    
    while (current_id != NMO_TYPE_ID_INVALID) {
        chain_length++;
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);
        
        // Find parent type by base_type GUID
        if (nmo_guid_is_null(current->base_type)) {
            break; // Reached root
        }
        
        // Lookup parent by GUID (returns 1 if found, 0 if not)
        nmo_type_id_t parent_id;
        if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map,
                                                   &current->base_type,
                                                   &parent_id))) {
            break; // Broken chain (parent not registered)
        }
        
        current_id = parent_id;
        
        // Prevent infinite loops
        if (chain_length > 1000) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Circular inheritance detected"));
        }
    }
    
    // Allocate output array
    nmo_type_id_t *chain = nmo_arena_alloc(arena, 
        chain_length * sizeof(nmo_type_id_t), 
        _Alignof(nmo_type_id_t));
    if (!chain) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chain array"));
    }
    
    // Fill array (most-derived to least-derived)
    size_t index = 0;
    current_id = type_id;
    
    while (current_id != NMO_TYPE_ID_INVALID && index < chain_length) {
        chain[index++] = current_id;
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);
        
        if (nmo_guid_is_null(current->base_type)) {
            break;
        }
        
        nmo_type_id_t parent_id;
        if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map,
                                                   &current->base_type,
                                                   &parent_id))) {
            break;
        }
        
        current_id = parent_id;
    }
    
    *out_chain = chain;
    *out_count = index;
    return nmo_result_ok();
}

bool nmo_type_is_compatible(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type1,
    nmo_type_id_t type2) {
    
    // Check if type1 is derived from type2 OR type2 is derived from type1 (symmetric)
    return nmo_type_is_derived_from(registry, type1, type2) ||
           nmo_type_is_derived_from(registry, type2, type1);
}

int32_t nmo_type_get_derivation_depth(
    const nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id) {
    
    if (!registry) return -1;
    
    // Not derived
    if (!nmo_type_is_derived_from(registry, child_id, parent_id)) {
        return -1;
    }
    
    // Same type (depth 0)
    if (child_id == parent_id) {
        return 0;
    }
    
    // Walk up chain counting steps
    int32_t depth = 0;
    nmo_type_id_t current_id = child_id;
    
    while (current_id != NMO_TYPE_ID_INVALID) {
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);
        
        // Find parent
        if (nmo_guid_is_null(current->base_type)) {
            break; // Reached root without finding parent_id
        }
        
        nmo_type_id_t next_id;
        if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map,
                                                   &current->base_type,
                                                   &next_id))) {
            break; // Broken chain
        }
        
        depth++;
        current_id = next_id;
        
        // Found parent
        if (current_id == parent_id) {
            return depth;
        }
        
        // Prevent infinite loops
        if (depth > 1000) {
            return -1;
        }
    }
    
    return -1; // Should not reach here if is_derived_from returned true
}

/* --- 6.3.3: Type Conversion API --- */

nmo_type_id_t nmo_type_registry_guid_to_type_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    if (!registry || !registry->guid_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_ok(nmo_hash_table_get(registry->guid_map, &guid, &type_id))) {
        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

nmo_result_t nmo_type_registry_type_id_to_guid(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_guid_t *out_guid) {
    
    if (!registry || !out_guid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid"));
    }
    
    *out_guid = type->guid;
    return nmo_result_ok();
}

const char* nmo_type_registry_guid_to_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return NULL;
    }
    
    return (*(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id))->name;
}

nmo_result_t nmo_type_registry_name_to_guid(
    const nmo_type_registry_t *registry,
    const char *name,
    nmo_guid_t *out_guid) {
    
    if (!registry || !name || !out_guid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->name_map, &name, &type_id))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type name not found"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type invalid"));
    }
    
    *out_guid = type->guid;
    return nmo_result_ok();
}

const char* nmo_type_registry_type_id_to_name(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) {
    
    if (!registry) return NULL;
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return NULL;
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return NULL;
    }
    
    return type->name;
}

nmo_type_id_t nmo_type_registry_name_to_type_id(
    const nmo_type_registry_t *registry,
    const char *name) {
    
    if (!registry || !name || !registry->name_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_ok(nmo_hash_table_get(registry->name_map, &name, &type_id))) {
        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

nmo_result_t nmo_type_registry_class_id_to_guid(
    const nmo_type_registry_t *registry,
    uint32_t class_id,
    nmo_guid_t *out_guid) {
    
    if (!registry || !out_guid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    if (!registry->class_id_map) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "ClassID map not initialized"));
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->class_id_map,
                                               &class_id,
                                               &type_id))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "ClassID not found"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type invalid"));
    }
    
    *out_guid = type->guid;
    return nmo_result_ok();
}

nmo_result_t nmo_type_registry_guid_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid,
    uint32_t *out_class_id) {
    
    if (!registry || !out_class_id) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type->class_id == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type has no ClassID"));
    }
    
    *out_class_id = type->class_id;
    return nmo_result_ok();
}

nmo_result_t nmo_type_registry_type_id_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    uint32_t *out_class_id) {
    
    if (!registry || !out_class_id) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter"));
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID"));
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid"));
    }
    
    if (type->class_id == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type has no ClassID"));
    }
    
    *out_class_id = type->class_id;
    return nmo_result_ok();
}

nmo_type_id_t nmo_type_registry_class_id_to_type_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id) {
    
    if (!registry || !registry->class_id_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_ok(nmo_hash_table_get(registry->class_id_map, &class_id, &type_id))) {
        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

/* ============================================================================
 * Phase 6.5: Type Statistics & Visibility Control Implementation
 * ============================================================================ */

size_t nmo_type_registry_get_type_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type != NULL && type->valid) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_builtin_count(const nmo_type_registry_t *registry) {
    return registry ? registry->builtin_count : 0;
}

size_t nmo_type_registry_get_plugin_count(const nmo_type_registry_t *registry) {
    return registry ? registry->plugin_count : 0;
}

size_t nmo_type_registry_get_flags_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_FLAGS)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_enum_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_ENUM)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_struct_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_STRUCT)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_memory_usage(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t total = 0;
    
    // Registry structure itself
    total += sizeof(nmo_type_registry_t);
    
    // Type descriptor array
    total += registry->types.capacity * sizeof(nmo_type_descriptor_t*);
    
    // Type descriptors (estimate based on valid types)
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid) {
            total += sizeof(nmo_type_descriptor_t);
            
            // Field descriptors
            if (type->fields) {
                total += type->field_count * sizeof(nmo_type_field_t);
            }
        }
    }
    
    // Metadata array
    total += registry->metadata.capacity * sizeof(nmo_specialized_metadata_t*);
    total += registry->metadata.count * sizeof(nmo_specialized_metadata_t);
    
    // Hash tables (estimate: 2x key-value pairs + overhead)
    // guid_map, name_map, class_id_map, type_to_metadata, plugin_map, type_to_plugin
    size_t hash_table_overhead = 6 * 256;  // Rough estimate per table
    total += hash_table_overhead;
    
    // Arena overhead (estimate: 10% of total)
    total += total / 10;
    
    return total;
}

bool nmo_type_registry_is_ui_visible(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    if (!registry) {
        return false;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map, &guid, &type_id))) {
        return false;
    }
    
    return nmo_type_registry_is_ui_visible_by_id(registry, type_id);
}

bool nmo_type_registry_is_ui_visible_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) {
    
    if (!registry || type_id < 0 || (size_t)type_id >= registry->types.count) {
        return false;
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return false;
    }
    
    // Type is hidden if NMO_TYPE_CATEGORY_HIDDEN flag is set
    return !(type->category & NMO_TYPE_CATEGORY_HIDDEN);
}

nmo_result_t nmo_type_registry_set_ui_visibility(
    nmo_type_registry_t *registry,
    nmo_guid_t guid,
    bool visible) {
    
    if (!registry) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->guid_map, &guid, &type_id))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found"));
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID"));
    }
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid"));
    }
    
    // Toggle NMO_TYPE_CATEGORY_HIDDEN flag
    if (visible) {
        type->category &= ~NMO_TYPE_CATEGORY_HIDDEN;  // Clear hidden flag
    } else {
        type->category |= NMO_TYPE_CATEGORY_HIDDEN;   // Set hidden flag
    }
    
    return nmo_result_ok();
}

/* ============================================================================
 * Phase 6.6: Custom Manager Registration Implementation
 * ============================================================================ */

nmo_result_t nmo_type_registry_register_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid,
    const char *name,
    nmo_manager_serialize_fn serialize,
    nmo_manager_deserialize_fn deserialize,
    void *manager_context) {
    
    if (!registry) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    if (!serialize || !deserialize) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL serialize/deserialize callbacks"));
    }
    
    // Check if manager already registered
    if (registry->manager_guid_map) {
        nmo_manager_index_t existing_index = NMO_MANAGER_INDEX_INVALID;
        if (nmo_result_is_ok(nmo_hash_table_get(registry->manager_guid_map,
                                               &manager_guid,
                                               &existing_index))) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Manager already registered"));
        }
    }
    
    // Lazy initialize hash tables
    if (!registry->manager_guid_map) {
        registry->manager_guid_map = nmo_hash_table_create(
            NULL, sizeof(nmo_guid_t), sizeof(nmo_manager_index_t),
            16, guid_hash_func, guid_compare_func);
        
        registry->type_to_manager = nmo_hash_table_create(
            NULL, sizeof(nmo_type_id_t), sizeof(nmo_manager_index_t),
            16, NULL, NULL);
        
        if (!registry->manager_guid_map || !registry->type_to_manager) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Failed to create manager hash tables"));
        }
    }
    
    // Allocate manager descriptor
    nmo_saver_manager_t *manager = (nmo_saver_manager_t *)nmo_arena_alloc(
        registry->arena, sizeof(nmo_saver_manager_t), _Alignof(nmo_saver_manager_t));
    
    if (!manager) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Failed to allocate manager descriptor"));
    }
    
    // Initialize manager
    manager->guid = manager_guid;
    manager->name = nmo_arena_strdup(registry->arena, name);
    manager->serialize = serialize;
    manager->deserialize = deserialize;
    manager->context = manager_context;
    
    // Add to array and hash table
    nmo_manager_index_t manager_index = (nmo_manager_index_t)registry->saver_managers.count;
    nmo_result_t res = nmo_arena_array_append(&registry->saver_managers, &manager);
    if (nmo_result_is_error(res)) return res;
    
    nmo_result_t map_result = nmo_hash_table_insert(registry->manager_guid_map, &manager_guid, &manager_index);
    if (nmo_result_is_error(map_result)) {
        return map_result;
    }
    
    return nmo_result_ok();
}

nmo_result_t nmo_type_registry_unregister_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid) {
    
    if (!registry || !registry->manager_guid_map) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry or no managers"));
    }
    
    // Find manager
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->manager_guid_map,
                                               &manager_guid,
                                               &manager_index))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Manager not found"));
    }
    
    if (manager_index < 0 || (size_t)manager_index >= registry->saver_managers.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid manager index"));
    }
    
    // Clear all type associations with this manager
    if (registry->type_to_manager) {
        // Find all types using this manager and clear their associations
        for (size_t i = 0; i < registry->types.count; i++) {
            nmo_manager_index_t type_manager_idx = NMO_MANAGER_INDEX_INVALID;
            nmo_type_id_t tid = (nmo_type_id_t)i;
            if (nmo_result_is_ok(nmo_hash_table_get(registry->type_to_manager,
                                                   &tid,
                                                   &type_manager_idx))) {
                if (type_manager_idx == manager_index) {
                    nmo_hash_table_remove(registry->type_to_manager, &tid);
                    
                    // Also clear saver_manager field in type descriptor
                    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
                    if (type) {
                        type->saver_manager = NMO_MANAGER_INDEX_INVALID;
                    }
                }
            }
        }
    }

    // Remove manager from array
    nmo_saver_manager_t **manager_ptr = (nmo_saver_manager_t **)nmo_arena_array_get(&registry->saver_managers, manager_index);
    if (manager_ptr && *manager_ptr) {
        // Arena memory cannot be freed individually, just clear the pointer
        *manager_ptr = NULL;
    }

    // Remove from GUID map
    nmo_hash_table_remove(registry->manager_guid_map, &manager_guid);
    
    return nmo_result_ok();
}

const nmo_saver_manager_t* nmo_type_registry_get_saver_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t manager_guid) {
    
    if (!registry || !registry->manager_guid_map) return NULL;
    
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->manager_guid_map,
                                               &manager_guid,
                                               &manager_index))) {
        return NULL;
    }
    
    if (manager_index < 0 || (size_t)manager_index >= registry->saver_managers.count) {
        return NULL;
    }
    
    nmo_saver_manager_t *manager = *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, manager_index);
    return manager;
}

nmo_result_t nmo_type_registry_set_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid,
    nmo_guid_t manager_guid) {
    
    if (!registry) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    // Find type
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found"));
    }
    
    // Find manager
    if (!registry->manager_guid_map) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "No managers registered"));
    }
    
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_result_is_error(nmo_hash_table_get(registry->manager_guid_map,
                                               &manager_guid,
                                               &manager_index))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Manager not found"));
    }
    
    // Update type descriptor
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type) {
        type->saver_manager = manager_index;
    }
    
    // Update mapping
    if (!registry->type_to_manager) {
        registry->type_to_manager = nmo_hash_table_create(
            NULL, sizeof(nmo_type_id_t), sizeof(nmo_manager_index_t),
            16, NULL, NULL);
    }
    
    nmo_result_t map_result = nmo_hash_table_insert(registry->type_to_manager, &type_id, &manager_index);
    if (nmo_result_is_error(map_result)) {
        return map_result;
    }
    
    return nmo_result_ok();
}

const nmo_saver_manager_t* nmo_type_registry_get_type_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid) {
    
    if (!registry) return NULL;
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) return NULL;
    
    // Check type descriptor first (faster)
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type && type->saver_manager != NMO_MANAGER_INDEX_INVALID) {
        if (type->saver_manager >= 0 && (size_t)type->saver_manager < registry->saver_managers.count) {
            return *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, type->saver_manager);
        }
    }
    
    // Fallback to hash table (should match)
    if (registry->type_to_manager) {
        nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
        if (nmo_result_is_ok(nmo_hash_table_get(registry->type_to_manager,
                                               &type_id,
                                               &manager_index))) {
            if (manager_index >= 0 && (size_t)manager_index < registry->saver_managers.count) {
                return *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, manager_index);
            }
        }
    }
    
    return NULL;
}

nmo_result_t nmo_type_registry_clear_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid) {
    
    if (!registry) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) return nmo_result_ok();
    
    // Update type descriptor
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type) {
        type->saver_manager = NMO_MANAGER_INDEX_INVALID;
    }
    
    // Update mapping
    if (registry->type_to_manager) {
        nmo_hash_table_remove(registry->type_to_manager, &type_id);
    }
    
    return nmo_result_ok();
}

size_t nmo_type_registry_get_manager_count(const nmo_type_registry_t *registry) {
    if (!registry) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < registry->saver_managers.count; i++) {
        nmo_saver_manager_t *manager = *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, i);
        if (manager != NULL) {
            count++;
        }
    }
    return count;
}
