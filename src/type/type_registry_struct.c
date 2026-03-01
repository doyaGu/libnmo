/**
 * @file type_registry_struct.c
 * @brief Struct type registration with automatic layout calculation (Phase 6.2 Task 6.2.4)
 * 
 * Implements dynamic registration of struct types with:
 * - Field definitions with names and types
 * - Automatic offset and size calculation
 * - Alignment handling (natural or custom)
 * - Packed struct support
 * - Nested struct support
 * 
 * Reference: CKParameterManager::RegisterNewStructure
 */

#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "core/nmo_debug.h"
#include "core/nmo_error.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdalign.h>
#include <stddef.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void free_heap_type_fields(nmo_allocator_t *allocator, nmo_type_field_t *fields, size_t count) {
    if (!allocator || !fields) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (fields[i].name) {
            nmo_free(allocator, (void *)fields[i].name);
            fields[i].name = NULL;
        }
        if (fields[i].description) {
            nmo_free(allocator, (void *)fields[i].description);
            fields[i].description = NULL;
        }
        if (fields[i].default_value) {
            nmo_free(allocator, (void *)fields[i].default_value);
            fields[i].default_value = NULL;
        }
    }
    nmo_free(allocator, fields);
}

static const nmo_type_descriptor_t* resolve_field_type(
    const nmo_type_registry_t *type_registry,
    nmo_guid_t *io_guid
) {
    if (!type_registry || !io_guid) {
        return NULL;
    }

    return nmo_type_registry_find_by_guid(type_registry, *io_guid);
}

static nmo_status_t validate_struct_field_consistency(
    const nmo_struct_descriptor_t *struct_fields,
    const nmo_type_field_t *type_fields,
    size_t field_count,
    uint32_t total_size,
    bool is_union
) {
    if (!struct_fields || !type_fields || field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Field descriptors must not be empty");
    }

    for (size_t i = 0; i < field_count; i++) {
        const nmo_struct_descriptor_t *s = &struct_fields[i];
        const nmo_type_field_t *t = &type_fields[i];

        if (!s->name || !t->name || strcmp(s->name, t->name) != 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Field name mismatch at index %zu", i);
        }
        if (!nmo_guid_equals(s->type_guid, t->type_guid)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Field type mismatch for '%s'", s->name);
        }
        if (s->offset != t->offset) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Field offset mismatch for '%s'", s->name);
        }
        if (s->size != t->size) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Field size mismatch for '%s'", s->name);
        }
        if ((uint64_t)t->offset + (uint64_t)t->size > (uint64_t)total_size) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Field '%s' exceeds type size", s->name);
        }
        if (is_union && t->offset != 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Union field '%s' must have offset 0", s->name);
        }
        if (s->array_count > 0) {
            if ((t->flags & NMO_FIELD_REPEATED) == 0u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Array field '%s' missing repeated flag", s->name);
            }
        } else if ((t->flags & NMO_FIELD_REPEATED) != 0u) {
            if (nmo_guid_equals(s->type_guid, NMO_TYPE_GUID_POINTER)) {
                continue;
            }
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Non-array field '%s' marked as repeated", s->name);
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Layout Calculation
 * ============================================================================ */

uint32_t nmo_type_get_alignment(
    const nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid
) {
    if (!type_registry) return 1;

    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type_desc) return 1;
    
    return type_desc->alignment;
}

uint32_t nmo_type_get_size(
    const nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid
) {
    if (!type_registry) return 0;

    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type_desc) return 0;
    
    return type_desc->size;
}

nmo_status_t nmo_type_calculate_layout(
    const nmo_type_registry_t *type_registry,
    nmo_struct_field_def_t *fields,
    size_t field_count,
    uint32_t desired_alignment,
    bool packed,
    uint32_t *out_total_size,
    uint32_t *out_alignment
) {
    if (!type_registry || !fields || field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters for layout calculation");
    }

    if (desired_alignment > 0 && !NMO_IS_POWER_OF_TWO(desired_alignment)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Struct alignment must be a power of two");
    }
    
    uint32_t offset = 0;
    uint32_t max_align = 1;
    
    /* Calculate offset and alignment for each field */
    for (size_t i = 0; i < field_count; i++) {
        nmo_struct_field_def_t *field = &fields[i];
        
        /* Resolve field type GUID and parse array syntax */
        nmo_guid_t field_type_guid;
        uint32_t array_count = 0;
        
        if (field->type_name) {
            /* Parse type name to get GUID and array info (supports pointers/arrays). */
            nmo_type_parse_result_t parse_result;
            nmo_status_t result = nmo_type_registry_parse_type_name(
                type_registry, field->type_name, &parse_result);
            if (result != NMO_OK) {
                return result;
            }
            field_type_guid = parse_result.base_type_guid;
            array_count = parse_result.array_count;

            if (parse_result.is_pointer) {
                field_type_guid = NMO_TYPE_GUID_POINTER;
            }
        } else if (!nmo_guid_is_null(field->type_guid)) {
            field_type_guid = field->type_guid;
            /* Array count would need to be specified separately if using GUID */
        } else {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Field '%s' has no type specified",
                                    field->name ? field->name : "(unnamed)");
        }
        
        /* Get field type info */
        const nmo_type_descriptor_t *field_type = resolve_field_type(type_registry, &field_type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type not found for field '%s'",
                                    field->name ? field->name : "(unnamed)");
        }
        
        /* Support nested structs - they're just regular types with struct category */
        if (field_type->category == NMO_TYPE_CATEGORY_STRUCT && !field_type->valid) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Cannot use incomplete struct type '%s' as field",
                                    field_type->name);
        }
        
        /* Calculate field size (including arrays) */
        uint32_t element_size = field_type->size;
        uint32_t field_size = element_size;
        if (array_count > 0) {
            field_size = element_size * array_count;
        }
        
        uint32_t field_align = packed ? 1 : field_type->alignment;
        
        /* Update maximum alignment */
        max_align = NMO_MAX(max_align, field_align);
        
        /* Align offset to field alignment */
        offset = (uint32_t)nmo_align((size_t)(offset), (size_t)(field_align));
        
        /* Store field info for later use in registration */
        field->type_guid = field_type_guid;
        
        /* Advance offset by total field size */
        offset += field_size;
    }
    
    /* Apply desired alignment if specified */
    if (desired_alignment > 0) {
        max_align = desired_alignment;
    }
    
    /* Align total size to struct alignment */
    uint32_t total_size = (uint32_t)nmo_align((size_t)(offset), (size_t)(max_align));
    
    if (out_total_size) {
        *out_total_size = total_size;
    }
    if (out_alignment) {
        *out_alignment = max_align;
    }
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Struct Type Registration
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_struct(
    nmo_type_registry_t *type_registry,
    const nmo_struct_type_def_t *struct_def,
    nmo_guid_t *out_guid
) {
    if (!type_registry || !struct_def) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry or struct_def");
    }

    if (type_registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot register struct types");
    }

    if (!struct_def->name || struct_def->name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Struct type name cannot be empty");
    }
    
    if (!struct_def->fields || struct_def->field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Struct must have at least one field");
    }
    
    /* Generate GUID for the struct type */
    nmo_guid_t type_guid = nmo_guid_is_null(struct_def->guid) ?
        nmo_type_generate_guid(struct_def->name) : struct_def->guid;

    /* Resolve optional base struct type */
    const nmo_type_descriptor_t *base_type = NULL;
    if (!nmo_guid_is_null(struct_def->base_type_guid)) {
        base_type = nmo_type_registry_find_by_guid(type_registry, struct_def->base_type_guid);
        if (!base_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Base struct type not found");
        }
        if (base_type->category != NMO_TYPE_CATEGORY_STRUCT) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Base type must be a struct");
        }
        if (nmo_guid_equals(base_type->guid, type_guid)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Struct cannot inherit from itself");
        }
    }
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                                "Struct type '%s' already registered",
                                struct_def->name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate field definitions array (mutable copy for layout calculation) */
    nmo_struct_field_def_t *fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_field_def_t) * struct_def->field_count,
                        alignof(nmo_struct_field_def_t));
    if (!fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate fields array");
    }
    
    /* Copy fields */
    memcpy(fields, struct_def->fields, sizeof(nmo_struct_field_def_t) * struct_def->field_count);
    
    /* Calculate layout */
    uint32_t total_size, struct_alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        type_registry, fields, struct_def->field_count,
        struct_def->alignment, struct_def->packed,
        &total_size, &struct_alignment);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Allocate struct descriptors array for specialized metadata */
    nmo_struct_descriptor_t *struct_fields = (nmo_struct_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_descriptor_t) * struct_def->field_count,
                        alignof(nmo_struct_descriptor_t));
    if (!struct_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate struct descriptors");
    }

    /* Allocate field descriptors for generic reflection (includes defaults). */
    nmo_type_field_t *type_fields = (nmo_type_field_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_field_t) * struct_def->field_count,
                        alignof(nmo_type_field_t));
    if (!type_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate type field descriptors");
    }
    
    /* Build struct descriptors with calculated offsets */
    uint32_t offset = 0;
    for (size_t i = 0; i < struct_def->field_count; i++) {
        const nmo_struct_field_def_t *field_def = &fields[i];
        nmo_struct_descriptor_t *field_desc = &struct_fields[i];
        
        /* Copy field name */
        field_desc->name = nmo_arena_strdup(arena, field_def->name);
        if (!field_desc->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy field name");
        }
        
        /* Parse type name to get array info if needed */
        uint32_t array_count = 0;
        nmo_guid_t field_type_guid = field_def->type_guid;
        
        nmo_guid_t pointee_guid = NMO_NULL_GUID;
        uint32_t pointer_depth = 0;

        if (field_def->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_status_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field_def->type_name, &parse_result);
            if (parse_res == NMO_OK) {
                field_type_guid = parse_result.base_type_guid;
                array_count = parse_result.array_count;
                if (parse_result.is_pointer) {
                    pointee_guid = parse_result.base_type_guid;
                    pointer_depth = parse_result.pointer_depth;
                    field_type_guid = NMO_TYPE_GUID_POINTER;
                }
            }
        }
        
        /* Get field type info */
        const nmo_type_descriptor_t *field_type = resolve_field_type(type_registry, &field_type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type not found");
        }
        
        /* Calculate field size (including arrays) */
        uint32_t element_size = field_type->size;
        uint32_t total_field_size = element_size;
        if (array_count > 0) {
            total_field_size = element_size * array_count;
        }
        
        uint32_t field_align = struct_def->packed ? 1 : field_type->alignment;
        offset = (uint32_t)nmo_align((size_t)(offset), (size_t)(field_align));
        
        field_desc->type_guid = field_type_guid;
        field_desc->offset = offset;
        field_desc->size = total_field_size;
        field_desc->array_count = array_count;
        field_desc->flags = field_def->flags;
        field_desc->description = field_def->description ?
            nmo_arena_strdup(arena, field_def->description) : NULL;
        field_desc->pointee_guid = pointee_guid;
        field_desc->pointer_depth = pointer_depth;

        /* Populate generic field descriptor */
        memset(&type_fields[i], 0, sizeof(type_fields[i]));
        type_fields[i].name = field_desc->name;
        type_fields[i].description = field_desc->description;
        type_fields[i].type_guid = field_type_guid;
        type_fields[i].offset = offset;
        type_fields[i].size = total_field_size;
        type_fields[i].flags = field_def->flags;
        if (array_count > 0) {
            type_fields[i].flags |= NMO_FIELD_REPEATED;
        }
        type_fields[i].added_version = 0;
        type_fields[i].removed_version = 0;
        type_fields[i].semantic = NMO_SEMANTIC_NONE;
        type_fields[i].units = NMO_UNITS_NONE;
        type_fields[i].default_value = field_def->default_value;

        offset += total_field_size;
    }

    nmo_status_t consistency_res = validate_struct_field_consistency(
        struct_fields,
        type_fields,
        struct_def->field_count,
        total_size,
        false);
    if (consistency_res != NMO_OK) {
        return consistency_res;
    }
    
    /* Allocate specialized_metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = NMO_TYPE_ID_INVALID;  /* Will be set during registration */
    spec_meta->metadata_type = NMO_METADATA_TYPE_STRUCT;
    spec_meta->ownership = NMO_OWNERSHIP_ARENA; /* arena-owned; do not free via type_allocator */
    spec_meta->struct_meta.fields = struct_fields;
    spec_meta->struct_meta.field_count = struct_def->field_count;
    
    /* Allocate type descriptor */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate struct type descriptor");
    }
    
    /* Initialize all fields to zero */
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    
    /* Copy struct type name */
    const char *type_name = nmo_arena_strdup(arena, struct_def->name);
    if (!type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to copy struct type name");
    }
    
    /* Initialize type descriptor */
    type_desc->guid = type_guid;
    type_desc->name = type_name;
    type_desc->base_type = base_type ? base_type->guid : NMO_GUID_NULL;
    type_desc->size = total_size;
    type_desc->alignment = struct_alignment;
    type_desc->category = NMO_TYPE_CATEGORY_STRUCT;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE;
    if (struct_def->packed) {
        /* Packed structs are POD if all fields are POD */
        type_desc->flags |= NMO_TYPE_FLAG_POD;
    }
    type_desc->fields = type_fields;
    type_desc->field_count = struct_def->field_count;
    type_desc->vtable = NULL;
    type_desc->description = struct_def->description ? 
        nmo_arena_strdup(arena, struct_def->description) : NULL;
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
                                "Failed to find registered struct type");
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
 * Incremental Struct Building State
 * ============================================================================ */

/**
 * @brief Incomplete struct type being built incrementally
 */
typedef struct incomplete_struct_t {
    nmo_guid_t guid;                    /* Struct GUID */
    const char *name;                   /* Struct name */
    nmo_struct_field_def_t *fields;     /* Dynamic field array */
    size_t field_count;                 /* Current field count */
    size_t field_capacity;              /* Allocated capacity */
    bool finalized;                     /* Whether finalization is complete */
} incomplete_struct_t;

/* Store incomplete structs in registry's arena */
static incomplete_struct_t *find_incomplete_struct(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t type_id
) {
    if (!type_registry || type_id < 0 || (size_t)type_id >= type_registry->types.count) {
        return NULL;
    }
    
    nmo_type_descriptor_t *desc = *(nmo_type_descriptor_t **)nmo_arena_array_get(&type_registry->types, type_id);
    if (!desc || desc->valid) {
        return NULL;  /* Either doesn't exist or already finalized */
    }
    
    /* Incomplete struct data stored in description field temporarily */
    return (incomplete_struct_t*)desc->description;
}

/* ============================================================================
 * Incremental Struct Building Implementation
 * ============================================================================ */

nmo_status_t nmo_type_registry_begin_struct(
    nmo_type_registry_t *type_registry,
    const char *name,
    nmo_guid_t guid,
    nmo_type_id_t *out_type_id
) {
    if (!type_registry || !name || !name[0]) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry or empty name");
    }

    if (type_registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot begin struct definitions");
    }
    
    /* Generate GUID if null */
    if (nmo_guid_is_null(guid)) {
        guid = nmo_type_generate_guid(name);
    }
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                                "Struct type '%s' already registered", name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate incomplete struct state */
    incomplete_struct_t *incomplete = (incomplete_struct_t*)
        nmo_arena_alloc(arena, sizeof(incomplete_struct_t), alignof(incomplete_struct_t));
    if (!incomplete) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate incomplete struct state");
    }
    
    /* Initialize incomplete struct */
    incomplete->guid = guid;
    incomplete->name = nmo_arena_strdup(arena, name);
    incomplete->field_count = 0;
    incomplete->field_capacity = 8;  /* Initial capacity */
    incomplete->finalized = false;
    
    /* Allocate field array */
    incomplete->fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_field_def_t) * incomplete->field_capacity,
                        alignof(nmo_struct_field_def_t));
    if (!incomplete->fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate field array");
    }
    
    /* Create placeholder type descriptor (invalid until finalized) */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate type descriptor");
    }
    
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    type_desc->guid = guid;
    type_desc->name = incomplete->name;
    type_desc->category = NMO_TYPE_CATEGORY_STRUCT;
    type_desc->valid = false;  /* Mark as incomplete */
    type_desc->description = NULL;
    
    /* Register placeholder (will be updated on finalize) */
    nmo_status_t result = nmo_type_registry_register(type_registry, type_desc);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Find registered type to get assigned ID and reset valid flag */
    nmo_type_descriptor_t *registered = (nmo_type_descriptor_t*)nmo_type_registry_find_by_guid(type_registry, guid);
    if (!registered) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Failed to find just-registered type");
    }
    
    /* Store incomplete state after registration */
    registered->description = (const char*)incomplete;

    /* Mark as incomplete (register sets it to true) */
    registered->valid = false;
    
    if (out_type_id) {
        *out_type_id = registered->id;
    }
    
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_add_field(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id,
    const char *field_name,
    const char *field_type_name
) {
    if (!type_registry || !field_name || !field_type_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL parameters");
    }

    if (type_registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot add struct fields");
    }
    
    /* Find incomplete struct */
    incomplete_struct_t *incomplete = find_incomplete_struct(type_registry, struct_type_id);
    if (!incomplete) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid struct type ID or struct already finalized");
    }
    
    if (incomplete->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Struct already finalized");
    }
    
    /* Check capacity */
    if (incomplete->field_count >= incomplete->field_capacity) {
        /* Need to grow array - allocate new larger array */
        size_t new_capacity = incomplete->field_capacity * 2;
        nmo_struct_field_def_t *new_fields = (nmo_struct_field_def_t*)
            nmo_arena_alloc(type_registry->arena, 
                            sizeof(nmo_struct_field_def_t) * new_capacity,
                            alignof(nmo_struct_field_def_t));
        if (!new_fields) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to grow field array");
        }
        
        /* Copy existing fields */
        memcpy(new_fields, incomplete->fields, 
               sizeof(nmo_struct_field_def_t) * incomplete->field_count);
        
        incomplete->fields = new_fields;
        incomplete->field_capacity = new_capacity;
    }
    
    /* Add new field */
    nmo_struct_field_def_t *field = &incomplete->fields[incomplete->field_count];
    field->name = nmo_arena_strdup(type_registry->arena, field_name);
    field->type_name = nmo_arena_strdup(type_registry->arena, field_type_name);
    field->type_guid = NMO_NULL_GUID;
    field->description = NULL;
    field->flags = 0;
    field->default_value = NULL;
    
    if (!field->name || !field->type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to copy field strings");
    }
    
    incomplete->field_count++;
    
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_finalize_struct(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id
) {
    if (!type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    if (type_registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot finalize struct definitions");
    }
    
    /* Find incomplete struct */
    incomplete_struct_t *incomplete = find_incomplete_struct(type_registry, struct_type_id);
    if (!incomplete) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid struct type ID or struct already finalized");
    }
    
    if (incomplete->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Struct already finalized");
    }
    
    if (incomplete->field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Cannot finalize struct with no fields");
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Parse all field types first */
    for (size_t i = 0; i < incomplete->field_count; i++) {
        nmo_struct_field_def_t *field = &incomplete->fields[i];
        
        if (nmo_guid_is_null(field->type_guid) && field->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_status_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field->type_name, &parse_result);
            if (parse_res != NMO_OK) {
                return parse_res;
            }
            field->type_guid = parse_result.base_type_guid;
            if (parse_result.is_pointer) {
                field->type_guid = NMO_TYPE_GUID_POINTER;
            }
        }
    }
    
    /* Calculate layout */
    uint32_t total_size, struct_alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        type_registry, incomplete->fields, incomplete->field_count,
        0, false, &total_size, &struct_alignment);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Build struct descriptors */
    nmo_struct_descriptor_t *struct_fields = (nmo_struct_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_descriptor_t) * incomplete->field_count,
                        alignof(nmo_struct_descriptor_t));
    if (!struct_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate struct descriptors");
    }

    nmo_type_field_t *type_fields = (nmo_type_field_t *)nmo_alloc(
        &type_registry->type_allocator,
        sizeof(nmo_type_field_t) * incomplete->field_count,
        _Alignof(nmo_type_field_t));
    if (!type_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate type field descriptors");
    }
    memset(type_fields, 0, sizeof(nmo_type_field_t) * incomplete->field_count);
    
    /* Build field descriptors with calculated offsets */
    uint32_t offset = 0;
    for (size_t i = 0; i < incomplete->field_count; i++) {
        const nmo_struct_field_def_t *field_def = &incomplete->fields[i];
        nmo_struct_descriptor_t *field_desc = &struct_fields[i];
        
        uint32_t array_count = 0;
        nmo_guid_t field_type_guid = field_def->type_guid;
        nmo_guid_t pointee_guid = NMO_NULL_GUID;
        uint32_t pointer_depth = 0;

        if (field_def->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_status_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field_def->type_name, &parse_result);
            if (parse_res == NMO_OK) {
                field_type_guid = parse_result.base_type_guid;
                array_count = parse_result.array_count;
                if (parse_result.is_pointer) {
                    pointee_guid = parse_result.base_type_guid;
                    pointer_depth = parse_result.pointer_depth;
                    field_type_guid = NMO_TYPE_GUID_POINTER;
                }
            }
        }

        const nmo_type_descriptor_t *field_type = resolve_field_type(
            type_registry, &field_type_guid);
        if (!field_type) {
            char guid_str[64];
            nmo_guid_format(field_def->type_guid, guid_str, sizeof(guid_str));
            free_heap_type_fields(&type_registry->type_allocator, type_fields, i);
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type not found for '%s' (GUID: %s)",
                                    field_def->name, guid_str);
        }
        
        uint32_t field_align = field_type->alignment;
        offset = (uint32_t)nmo_align((size_t)(offset), (size_t)(field_align));
        
        field_desc->name = field_def->name;
        field_desc->type_guid = field_type_guid;
        field_desc->offset = offset;
        uint32_t element_size = field_type->size;
        uint32_t total_field_size = element_size;
        if (array_count > 0) {
            total_field_size = element_size * array_count;
        }
        field_desc->size = total_field_size;
        field_desc->array_count = array_count;
        field_desc->flags = field_def->flags;
        field_desc->description = field_def->description;
        field_desc->pointee_guid = pointee_guid;
        field_desc->pointer_depth = pointer_depth;

        if (field_def->name) {
            type_fields[i].name = nmo_strdup(&type_registry->type_allocator, field_def->name);
            if (!type_fields[i].name) {
                free_heap_type_fields(&type_registry->type_allocator, type_fields, i);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to copy field name");
            }
        }

        if (field_def->description) {
            type_fields[i].description = nmo_strdup(&type_registry->type_allocator, field_def->description);
            if (!type_fields[i].description) {
                free_heap_type_fields(&type_registry->type_allocator, type_fields, i + 1u);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to copy field description");
            }
        }

        type_fields[i].type_guid = field_type_guid;
        type_fields[i].offset = offset;
        type_fields[i].size = total_field_size;
        type_fields[i].flags = field_def->flags;
        if (array_count > 0) {
            type_fields[i].flags |= NMO_FIELD_REPEATED;
        }
        type_fields[i].added_version = 0;
        type_fields[i].removed_version = 0;
        type_fields[i].semantic = NMO_SEMANTIC_NONE;
        type_fields[i].units = NMO_UNITS_NONE;
        if (field_def->default_value && total_field_size > 0) {
            void *default_copy = nmo_alloc(
                &type_registry->type_allocator,
                total_field_size,
                _Alignof(max_align_t));
            if (!default_copy) {
                free_heap_type_fields(&type_registry->type_allocator, type_fields, i + 1u);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to copy field default value");
            }
            memcpy(default_copy, field_def->default_value, total_field_size);
            type_fields[i].default_value = default_copy;
        }

        offset += total_field_size;
    }
    
    /* Create specialized metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = struct_type_id;
    spec_meta->metadata_type = NMO_METADATA_TYPE_STRUCT;
    spec_meta->ownership = NMO_OWNERSHIP_ARENA; /* arena-owned; do not free via type_allocator */
    spec_meta->struct_meta.fields = struct_fields;
    spec_meta->struct_meta.field_count = incomplete->field_count;
    
    /* Add to registry metadata array */
    size_t metadata_index = type_registry->metadata.count;
    nmo_status_t res = nmo_arena_array_append(&type_registry->metadata, &spec_meta);
    if (res != NMO_OK) return res;

    /* Add to type_id -> metadata_index hash table */
    nmo_status_t map_result = nmo_hash_table_insert(type_registry->type_to_metadata,
                                                    &struct_type_id,
                                                    &metadata_index);
    if (map_result != NMO_OK) {
        nmo_arena_array_pop(&type_registry->metadata, NULL);
        return map_result;
    }
    
    /* Update type descriptor to mark as valid */
    nmo_type_descriptor_t *type_desc = *(nmo_type_descriptor_t **)nmo_arena_array_get(&type_registry->types, struct_type_id);
    type_desc->size = total_size;
    type_desc->alignment = struct_alignment;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE;
    type_desc->specialized_index = (uint32_t)metadata_index;
    type_desc->fields = type_fields;
    type_desc->field_count = incomplete->field_count;
    type_desc->description = NULL;  /* Clear incomplete state pointer */
    type_desc->valid = true;  /* Mark as complete */
    
    incomplete->finalized = true;
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * String-Based Struct Registration (Phase 6.2 Task 6.2.3)
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_struct_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char **field_type_names,
    size_t field_count
) {
    if (!type_registry || !type_name || !field_type_names || field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL argument or empty field list");
    }
    
    /* Validate all field type names exist */
    for (size_t i = 0; i < field_count; i++) {
        if (!field_type_names[i] || field_type_names[i][0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Field type name cannot be NULL or empty at index %zu", i);
        }
    }
    
    /* Generate GUID if NULL_GUID */
    nmo_guid_t actual_guid = type_guid;
    if (nmo_guid_is_null(type_guid)) {
        actual_guid = nmo_type_generate_guid(type_name);
    }
    
    /* Create temporary arena for field definitions */
    nmo_arena_t *temp_arena = nmo_arena_create(NULL, 4096);
    if (!temp_arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create temporary arena");
    }
    
    /* Allocate field definitions array */
    nmo_struct_field_def_t *fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(temp_arena, sizeof(nmo_struct_field_def_t) * field_count, 8);
    if (!fields) {
        nmo_arena_destroy(temp_arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate field definitions");
    }
    
    /* Initialize fields by parsing type names */
    for (size_t i = 0; i < field_count; i++) {
        const char *type_name_str = field_type_names[i];
        
        /* Parse type name to get GUID and array info */
        nmo_type_parse_result_t parse_result;
        nmo_status_t parse_res = nmo_type_registry_parse_type_name(
            type_registry, type_name_str, &parse_result);
        
        if (parse_res != NMO_OK) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type '%s' not found at index %zu",
                                    type_name_str, i);
        }
        
        /* Generate default field name */
        char field_name[64];
        snprintf(field_name, sizeof(field_name), "field%u", (unsigned int)i);
        
        /* Initialize field definition */
        fields[i].name = nmo_arena_strdup(temp_arena, field_name);
        fields[i].type_name = nmo_arena_strdup(temp_arena, type_name_str);
        fields[i].type_guid = parse_result.base_type_guid;
        fields[i].description = NULL;
        fields[i].flags = 0;
        fields[i].default_value = NULL;
        
        if (!fields[i].name || !fields[i].type_name) {
            nmo_arena_destroy(temp_arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy field strings");
        }
    }
    
    /* Create struct type definition */
    nmo_struct_type_def_t struct_def = {
        .name = type_name,
        .description = NULL,
        .guid = actual_guid,
        .fields = fields,
        .field_count = field_count,
        .alignment = 0,  /* Auto-calculate */
        .packed = false
    };
    
    /* Register struct */
    nmo_guid_t out_guid;
    nmo_status_t result = nmo_type_registry_register_struct(
        type_registry, &struct_def, &out_guid);
    
    /* Clean up temporary arena */
    nmo_arena_destroy(temp_arena);
    
    return result;
}

/* ============================================================================
 * Union Type Registration
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_union(
    nmo_type_registry_t *type_registry,
    const nmo_union_type_def_t *union_def,
    nmo_guid_t *out_guid
) {
    if (!type_registry || !union_def) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry or union_def");
    }

    if (type_registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot register union types");
    }

    if (!union_def->name || union_def->name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Union type name cannot be empty");
    }

    if (!union_def->fields || union_def->field_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Union must have at least one field");
    }

    if (union_def->alignment > 0 && !NMO_IS_POWER_OF_TWO(union_def->alignment)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Union alignment must be a power of two");
    }

    /* Generate GUID for the union type */
    nmo_guid_t type_guid = nmo_guid_is_null(union_def->guid) ?
        nmo_type_generate_guid(union_def->name) : union_def->guid;

    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (existing) {
        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                                "Union type '%s' already registered",
                                union_def->name);
    }

    nmo_arena_t *arena = type_registry->arena;

    /* Allocate field definitions array (mutable copy) */
    nmo_struct_field_def_t *fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_field_def_t) * union_def->field_count,
                        alignof(nmo_struct_field_def_t));
    if (!fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate fields array");
    }

    memcpy(fields, union_def->fields, sizeof(nmo_struct_field_def_t) * union_def->field_count);

    /* Calculate union size/alignment */
    uint32_t max_size = 0;
    uint32_t max_align = 1;

    for (size_t i = 0; i < union_def->field_count; i++) {
        nmo_struct_field_def_t *field_def = &fields[i];
        nmo_guid_t field_type_guid = field_def->type_guid;
        uint32_t array_count = 0;

        if (field_def->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_status_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field_def->type_name, &parse_result);
            if (parse_res != NMO_OK) {
                return parse_res;
            }
            field_type_guid = parse_result.base_type_guid;
            array_count = parse_result.array_count;
            if (parse_result.is_pointer) {
                field_type_guid = NMO_TYPE_GUID_POINTER;
            }
        }

        const nmo_type_descriptor_t *field_type = resolve_field_type(type_registry, &field_type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type not found for union field '%s'",
                                    field_def->name ? field_def->name : "(unnamed)");
        }

        uint32_t element_size = field_type->size;
        uint32_t total_field_size = element_size;
        if (array_count > 0) {
            total_field_size = element_size * array_count;
        }

        uint32_t field_align = union_def->packed ? 1 : field_type->alignment;
        max_align = NMO_MAX(max_align, field_align);
        if (total_field_size > max_size) {
            max_size = total_field_size;
        }

        field_def->type_guid = field_type_guid;
    }

    if (union_def->alignment > 0) {
        max_align = union_def->alignment;
    }

    uint32_t total_size = (uint32_t)nmo_align((size_t)(max_size), (size_t)(max_align));

    /* Allocate union descriptors array */
    nmo_struct_descriptor_t *union_fields = (nmo_struct_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_descriptor_t) * union_def->field_count,
                        alignof(nmo_struct_descriptor_t));
    if (!union_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate union descriptors");
    }

    nmo_type_field_t *type_fields = (nmo_type_field_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_field_t) * union_def->field_count,
                        alignof(nmo_type_field_t));
    if (!type_fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate union type field descriptors");
    }

    /* Build union descriptors (offset = 0 for all fields) */
    for (size_t i = 0; i < union_def->field_count; i++) {
        const nmo_struct_field_def_t *field_def = &fields[i];
        nmo_struct_descriptor_t *field_desc = &union_fields[i];

        field_desc->name = nmo_arena_strdup(arena, field_def->name);
        if (!field_desc->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                    "Failed to copy union field name");
        }

        /* Determine array count again for size calc */
        uint32_t array_count = 0;
        nmo_guid_t field_type_guid = field_def->type_guid;
        nmo_guid_t pointee_guid = NMO_NULL_GUID;
        uint32_t pointer_depth = 0;
        if (field_def->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_status_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field_def->type_name, &parse_result);
            if (parse_res == NMO_OK) {
                field_type_guid = parse_result.base_type_guid;
                array_count = parse_result.array_count;
                if (parse_result.is_pointer) {
                    pointee_guid = parse_result.base_type_guid;
                    pointer_depth = parse_result.pointer_depth;
                    field_type_guid = NMO_TYPE_GUID_POINTER;
                }
            }
        }

        const nmo_type_descriptor_t *field_type = resolve_field_type(type_registry, &field_type_guid);
        if (!field_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Field type not found for union field '%s'",
                                    field_def->name ? field_def->name : "(unnamed)");
        }

        uint32_t element_size = field_type->size;
        uint32_t total_field_size = element_size;
        if (array_count > 0) {
            total_field_size = element_size * array_count;
        }

        field_desc->type_guid = field_type_guid;
        field_desc->offset = 0;
        field_desc->size = total_field_size;
        field_desc->array_count = array_count;
        field_desc->flags = field_def->flags;
        field_desc->description = field_def->description ?
            nmo_arena_strdup(arena, field_def->description) : NULL;
        field_desc->pointee_guid = pointee_guid;
        field_desc->pointer_depth = pointer_depth;

        memset(&type_fields[i], 0, sizeof(type_fields[i]));
        type_fields[i].name = field_desc->name;
        type_fields[i].description = field_desc->description;
        type_fields[i].type_guid = field_type_guid;
        type_fields[i].offset = 0;
        type_fields[i].size = total_field_size;
        type_fields[i].flags = field_def->flags;
        if (array_count > 0) {
            type_fields[i].flags |= NMO_FIELD_REPEATED;
        }
        type_fields[i].added_version = 0;
        type_fields[i].removed_version = 0;
        type_fields[i].semantic = NMO_SEMANTIC_NONE;
        type_fields[i].units = NMO_UNITS_NONE;
        type_fields[i].default_value = field_def->default_value;
    }

    nmo_status_t consistency_res = validate_struct_field_consistency(
        union_fields,
        type_fields,
        union_def->field_count,
        total_size,
        true);
    if (consistency_res != NMO_OK) {
        return consistency_res;
    }

    /* Allocate specialized metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate specialized metadata");
    }

    spec_meta->type_id = NMO_TYPE_ID_INVALID;
    spec_meta->metadata_type = NMO_METADATA_TYPE_UNION;
    spec_meta->ownership = NMO_OWNERSHIP_ARENA; /* arena-owned; do not free via type_allocator */
    spec_meta->union_meta.fields = union_fields;
    spec_meta->union_meta.field_count = union_def->field_count;

    /* Allocate type descriptor */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate union type descriptor");
    }

    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));

    const char *type_name = nmo_arena_strdup(arena, union_def->name);
    if (!type_name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to copy union type name");
    }

    type_desc->guid = type_guid;
    type_desc->name = type_name;
    type_desc->size = total_size;
    type_desc->alignment = max_align;
    type_desc->category = NMO_TYPE_CATEGORY_UNION;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE;
    if (union_def->packed) {
        type_desc->flags |= NMO_TYPE_FLAG_POD;
    }
    type_desc->fields = type_fields;
    type_desc->field_count = union_def->field_count;
    type_desc->vtable = NULL;
    type_desc->description = union_def->description ?
        nmo_arena_strdup(arena, union_def->description) : NULL;
    type_desc->valid = true;

    nmo_status_t result = nmo_type_registry_register(type_registry, type_desc);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t *registered =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!registered) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Failed to find registered union type");
    }

    spec_meta->type_id = registered->id;

    size_t metadata_index = type_registry->metadata.count;
    nmo_status_t append_res = nmo_arena_array_append(&type_registry->metadata, &spec_meta);
    if (append_res != NMO_OK) {
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return append_res;
    }

    nmo_status_t map_result = nmo_hash_table_insert(type_registry->type_to_metadata,
                                                    &registered->id,
                                                    &metadata_index);
    if (map_result != NMO_OK) {
        nmo_arena_array_pop(&type_registry->metadata, NULL);
        (void)nmo_type_registry_unregister(type_registry, type_guid);
        return map_result;
    }

    registered->specialized_index = (uint32_t)metadata_index;

    if (out_guid) {
        *out_guid = type_guid;
    }

    return NMO_OK;
}
