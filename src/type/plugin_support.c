/**
 * @file plugin_support.c
 * @brief Plugin tracking and cascade deletion implementation
 *
 * Implements plugin tracking, cascade deletion, type invalidation,
 * and specialized metadata management.
 * Reference: CKParameterManager.cpp lines 38-47, 84-129, 140-145
 */

#include "type/nmo_type_system.h"
#include "type_value_internal.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_debug.h"
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Metadata Deep Copy Helpers
 * ============================================================================ */

static const char *nmo_strdup_optional(nmo_allocator_t *allocator, const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src) + 1u;
    char *dst = (char *)nmo_alloc(allocator, len, _Alignof(char));
    if (!dst) {
        return NULL;
    }
    memcpy(dst, src, len);
    return dst;
}

static nmo_status_t nmo_copy_enum_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t *out_copy
) {
    if (!metadata->enum_meta.values || metadata->enum_meta.value_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Enum metadata must include values");
    }

    nmo_enum_descriptor_t *values_copy = (nmo_enum_descriptor_t *)nmo_alloc(
        &registry->type_allocator,
        metadata->enum_meta.value_count * sizeof(nmo_enum_descriptor_t),
        _Alignof(nmo_enum_descriptor_t));
    if (!values_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate enum metadata values");
    }

    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        const nmo_enum_descriptor_t *src = &metadata->enum_meta.values[i];
        nmo_enum_descriptor_t *dst = &values_copy[i];
        dst->name = nmo_strdup_optional(&registry->type_allocator, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy enum value name");
        }
        dst->description = nmo_strdup_optional(&registry->type_allocator, src->description);
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

    nmo_flags_descriptor_t *bits_copy = (nmo_flags_descriptor_t *)nmo_alloc(
        &registry->type_allocator,
        metadata->flags_meta.bit_count * sizeof(nmo_flags_descriptor_t),
        _Alignof(nmo_flags_descriptor_t));
    if (!bits_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate flags metadata bits");
    }

    for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
        const nmo_flags_descriptor_t *src = &metadata->flags_meta.bits[i];
        nmo_flags_descriptor_t *dst = &bits_copy[i];
        dst->name = nmo_strdup_optional(&registry->type_allocator, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy flags bit name");
        }
        dst->description = nmo_strdup_optional(&registry->type_allocator, src->description);
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

    nmo_struct_descriptor_t *fields_copy = (nmo_struct_descriptor_t *)nmo_alloc(
        &registry->type_allocator,
        metadata->struct_meta.field_count * sizeof(nmo_struct_descriptor_t),
        _Alignof(nmo_struct_descriptor_t));
    if (!fields_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate struct metadata fields");
    }

    for (size_t i = 0; i < metadata->struct_meta.field_count; i++) {
        const nmo_struct_descriptor_t *src = &metadata->struct_meta.fields[i];
        nmo_struct_descriptor_t *dst = &fields_copy[i];
        dst->name = nmo_strdup_optional(&registry->type_allocator, src->name);
        if (src->name && !dst->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy struct field name");
        }
        dst->description = nmo_strdup_optional(&registry->type_allocator, src->description);
        if (src->description && !dst->description) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy struct field description");
        }
        dst->type_guid = src->type_guid;
        dst->offset = src->offset;
        dst->size = src->size;
        dst->array_count = src->array_count;
        dst->flags = src->flags;
        dst->pointee_guid = src->pointee_guid;
        dst->pointer_depth = src->pointer_depth;
    }

    out_copy->struct_meta.fields = fields_copy;
    out_copy->struct_meta.field_count = metadata->struct_meta.field_count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_copy_union_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata,
    nmo_specialized_metadata_t *out_copy
) {
    if (!registry || !metadata || !out_copy) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid union metadata copy arguments");
    }

    if (!metadata->union_meta.fields || metadata->union_meta.field_count == 0) {
        out_copy->union_meta.fields = NULL;
        out_copy->union_meta.field_count = 0;
        NMO_RETURN_OK();
    }

    nmo_struct_descriptor_t *fields_copy = (nmo_struct_descriptor_t *)nmo_alloc(
        &registry->type_allocator,
        sizeof(nmo_struct_descriptor_t) * metadata->union_meta.field_count,
        _Alignof(nmo_struct_descriptor_t));
    if (!fields_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate union field copy");
    }

    for (size_t i = 0; i < metadata->union_meta.field_count; i++) {
        const nmo_struct_descriptor_t *src = &metadata->union_meta.fields[i];
        nmo_struct_descriptor_t *dst = &fields_copy[i];

        memset(dst, 0, sizeof(*dst));
        if (src->name) {
            dst->name = nmo_strdup_optional(&registry->type_allocator, src->name);
            if (!dst->name) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy union field name");
            }
        }
        if (src->description) {
            dst->description = nmo_strdup_optional(&registry->type_allocator, src->description);
            if (!dst->description) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy union field description");
            }
        }

        dst->type_guid = src->type_guid;
        dst->offset = src->offset;
        dst->size = src->size;
        dst->array_count = src->array_count;
        dst->flags = src->flags;
        dst->pointee_guid = src->pointee_guid;
        dst->pointer_depth = src->pointer_depth;
    }

    out_copy->union_meta.fields = fields_copy;
    out_copy->union_meta.field_count = metadata->union_meta.field_count;
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
        metadata->metadata_type != NMO_METADATA_TYPE_FLAGS &&
        metadata->metadata_type != NMO_METADATA_TYPE_UNION) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Unknown metadata type");
    }

    nmo_specialized_metadata_t *copy = (nmo_specialized_metadata_t *)nmo_alloc(
        &registry->type_allocator,
        sizeof(nmo_specialized_metadata_t),
        _Alignof(nmo_specialized_metadata_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate metadata copy");
    }

    memset(copy, 0, sizeof(*copy));
    copy->type_id = metadata->type_id;
    copy->metadata_type = metadata->metadata_type;
    copy->ownership = NMO_OWNERSHIP_HEAP;

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
        case NMO_METADATA_TYPE_UNION:
            result = nmo_copy_union_metadata(registry, metadata, copy);
            break;
    }

    if (result != NMO_OK) {
        return result;
    }

    *out_copy = copy;
    NMO_RETURN_OK();
}

static void nmo_free_metadata(
    nmo_type_registry_t *registry,
    nmo_specialized_metadata_t *metadata)
{
    if (!registry || !metadata) {
        return;
    }

    NMO_OWNERSHIP_ASSERT_VALID(metadata->ownership);
    NMO_OWNERSHIP_EXPECT(metadata->ownership, NMO_OWNERSHIP_HEAP);

    switch (metadata->metadata_type) {
        case NMO_METADATA_TYPE_ENUM:
            if (metadata->enum_meta.values) {
                for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
                    const nmo_enum_descriptor_t *value = &metadata->enum_meta.values[i];
                    if (value->name) {
                        nmo_free(&registry->type_allocator, (void *)value->name);
                    }
                    if (value->description) {
                        nmo_free(&registry->type_allocator, (void *)value->description);
                    }
                }
                nmo_free(&registry->type_allocator, (void *)metadata->enum_meta.values);
            }
            break;
        case NMO_METADATA_TYPE_FLAGS:
            if (metadata->flags_meta.bits) {
                for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
                    const nmo_flags_descriptor_t *bit = &metadata->flags_meta.bits[i];
                    if (bit->name) {
                        nmo_free(&registry->type_allocator, (void *)bit->name);
                    }
                    if (bit->description) {
                        nmo_free(&registry->type_allocator, (void *)bit->description);
                    }
                }
                nmo_free(&registry->type_allocator, (void *)metadata->flags_meta.bits);
            }
            break;
        case NMO_METADATA_TYPE_STRUCT:
            if (metadata->struct_meta.fields) {
                for (size_t i = 0; i < metadata->struct_meta.field_count; i++) {
                    const nmo_struct_descriptor_t *field = &metadata->struct_meta.fields[i];
                    if (field->name) {
                        nmo_free(&registry->type_allocator, (void *)field->name);
                    }
                    if (field->description) {
                        nmo_free(&registry->type_allocator, (void *)field->description);
                    }
                }
                nmo_free(&registry->type_allocator, (void *)metadata->struct_meta.fields);
            }
            break;
        case NMO_METADATA_TYPE_UNION:
            if (metadata->union_meta.fields) {
                for (size_t i = 0; i < metadata->union_meta.field_count; i++) {
                    const nmo_struct_descriptor_t *field = &metadata->union_meta.fields[i];
                    if (field->name) {
                        nmo_free(&registry->type_allocator, (void *)field->name);
                    }
                    if (field->description) {
                        nmo_free(&registry->type_allocator, (void *)field->description);
                    }
                }
                nmo_free(&registry->type_allocator, (void *)metadata->union_meta.fields);
            }
            break;
        default:
            break;
    }

    nmo_free(&registry->type_allocator, metadata);
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

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot set creator plugin");
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

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot unregister derived types");
    }

    const nmo_type_descriptor_t *base_type = nmo_type_registry_find_by_guid(registry, base_guid);
    if (!base_type) {
        /* Fallback: if the base type is not registered, scan for direct children. */
        for (size_t i = 0; i < registry->types.count; i++) {
            nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
            if (!type || !type->valid) continue;

            if (nmo_guid_equals(type->base_type, base_guid)) {
                nmo_status_t result = nmo_type_registry_unregister_derived(registry, type->guid);
                if (result != NMO_OK) {
                    return result;
                }

                result = nmo_type_registry_unregister(registry, type->guid);
                if (result != NMO_OK) {
                    return result;
                }
            }
        }

        NMO_RETURN_OK();
    }

    nmo_type_id_t base_id = base_type->id;
    nmo_type_registry_update_derivation_masks(registry);
    if (base_id < 0 || (size_t)base_id >= registry->child_lists.count) {
        NMO_RETURN_OK();
    }

    nmo_type_child_list_t *child_list =
        (nmo_type_child_list_t *)nmo_arena_array_get(&registry->child_lists, (size_t)base_id);
    if (!child_list || child_list->arr.count == 0 || !child_list->arr.data) {
        /* Defensive fallback for registries that had unresolved base links. */
        for (size_t i = 0; i < registry->types.count; i++) {
            nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
            if (!type || !type->valid) continue;
            if (!nmo_guid_equals(type->base_type, base_guid)) continue;

            nmo_status_t result = nmo_type_registry_unregister_derived(registry, type->guid);
            if (result != NMO_OK) {
                return result;
            }

            result = nmo_type_registry_unregister(registry, type->guid);
            if (result != NMO_OK) {
                return result;
            }
        }

        NMO_RETURN_OK();
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_type_id_t *children = (nmo_type_id_t *)nmo_alloc(
        &alloc,
        child_list->arr.count * sizeof(nmo_type_id_t),
        _Alignof(nmo_type_id_t));
    if (!children) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate child list snapshot");
    }

    memcpy(children, child_list->arr.data, child_list->arr.count * sizeof(nmo_type_id_t));
    size_t child_count = child_list->arr.count;

    for (size_t i = 0; i < child_count; i++) {
        nmo_type_descriptor_t *child = (nmo_type_descriptor_t *)nmo_type_registry_get_by_id(registry, children[i]);
        if (!child || !child->valid) {
            continue;
        }

        nmo_guid_t child_guid = child->guid;
        nmo_status_t result = nmo_type_registry_unregister_derived(registry, child_guid);
        if (result != NMO_OK) {
            nmo_free(&alloc, children);
            return result;
        }

        result = nmo_type_registry_unregister(registry, child_guid);
        if (result != NMO_OK) {
            nmo_free(&alloc, children);
            return result;
        }
    }

    nmo_free(&alloc, children);

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

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot unregister plugin types");
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

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot invalidate types");
    }

    // Find type by GUID
    nmo_type_descriptor_t *type = (nmo_type_descriptor_t*)nmo_type_registry_find_by_guid(registry, guid);
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found in registry");
    }

    if (!type->valid) {
        NMO_RETURN_OK();
    }

    // Update stats
    if (!nmo_guid_is_null(type->creator_plugin_guid)) {
        if (registry->plugin_count > 0) {
            registry->plugin_count--;
        }
    } else {
        if (registry->builtin_count > 0) {
            registry->builtin_count--;
        }
    }

    // Mark as invalid (soft delete)
    type->valid = false;

    // Invalidate derivation masks (will be rebuilt on next compatibility check)
    registry->derivation_masks_valid = false;
    registry->class_id_inherited_version = 0;

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

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot register metadata");
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
    if (res != NMO_OK) {
        nmo_free_metadata(registry, meta_copy);
        return res;
    }

    // Create fast lookup mapping (type_id -> metadata_index)
    nmo_status_t insert_result = nmo_hash_table_insert(registry->type_to_metadata,
                                                       &metadata->type_id,
                                                       &index);
    if (insert_result != NMO_OK) {
        nmo_arena_array_pop(&registry->metadata, NULL);
        nmo_free_metadata(registry, meta_copy);
        return insert_result;
    }

    // Update type descriptor's specialized_index (0-based index, invalid sentinel if none)
    type->specialized_index = (uint32_t)index;
    nmo_type_refresh_default_vtable_subtree(registry, type->id);

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
    if (registry->finalized) return;

    size_t index = 0;
    if (nmo_hash_table_get(registry->type_to_metadata, &type_id, &index) == NMO_OK) {
        if (index < registry->metadata.count) {
            nmo_specialized_metadata_t **entry =
                (nmo_specialized_metadata_t **)nmo_arena_array_get(&registry->metadata, index);
            if (entry && *entry) {
                NMO_OWNERSHIP_ASSERT_VALID((*entry)->ownership);
                if ((*entry)->ownership == NMO_OWNERSHIP_HEAP) {
                    nmo_free_metadata(registry, *entry);
                }
                *entry = NULL;
            }
        }
    }

    // Remove from hash table
    nmo_hash_table_remove(registry->type_to_metadata, &type_id);

    // Clear specialized_index in type descriptor
    if (type_id >= 0 && (size_t)type_id < registry->types.count) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
        if (type && type->valid) {
            type->specialized_index = NMO_SPECIALIZED_INDEX_INVALID;
            nmo_type_refresh_default_vtable_subtree(registry, type->id);
        }
    }
}
