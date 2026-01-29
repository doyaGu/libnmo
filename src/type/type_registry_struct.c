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

#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <string.h>
#include <stdio.h>
#include <stdalign.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Round up to next multiple of alignment
 */
static uint32_t align_up(uint32_t offset, uint32_t alignment) {
    if (alignment == 0) return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief Get maximum alignment requirement
 */
static uint32_t max_alignment(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
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

nmo_result_t nmo_type_calculate_layout(
    const nmo_type_registry_t *type_registry,
    nmo_struct_field_def_t *fields,
    size_t field_count,
    uint32_t desired_alignment,
    bool packed,
    uint32_t *out_total_size,
    uint32_t *out_alignment
) {
    if (!type_registry || !fields || field_count == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Invalid parameters for layout calculation");
    }
    
    uint32_t offset = 0;
    uint32_t max_align = 1;
    
    /* Calculate offset and alignment for each field */
    for (size_t i = 0; i < field_count; i++) {
        nmo_struct_field_def_t *field = &fields[i];
        
        /* Resolve field type GUID and parse array syntax */
        nmo_guid_t field_type_guid;
        uint32_t array_count = 0;
        
        if (nmo_guid_is_null(field->type_guid)) {
            /* Parse type name to get GUID and array info */
            if (!field->type_name) {
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                         NMO_SEVERITY_ERROR,
                                         "Field '%s' has no type specified",
                                         field->name ? field->name : "(unnamed)");
            }
            
            nmo_type_parse_result_t parse_result;
            nmo_result_t result = nmo_type_registry_parse_type_name(
                type_registry, field->type_name, &parse_result);
            if (nmo_result_is_error(result)) {
                return result;
            }
            field_type_guid = parse_result.base_type_guid;
            array_count = parse_result.array_count;
            
            /* Store parsed array count in field for later use */
            /* Note: We'll need to store this in struct_descriptor during registration */
        } else {
            field_type_guid = field->type_guid;
            /* Array count would need to be specified separately if using GUID */
        }
        
        /* Get field type info */
        const nmo_type_descriptor_t *field_type = nmo_type_registry_find_by_guid(
            type_registry, field_type_guid);
        if (!field_type) {
            return nmo_result_errorf(NULL, NMO_ERR_NOT_FOUND,
                                     NMO_SEVERITY_ERROR,
                                     "Field type not found for field '%s'",
                                     field->name ? field->name : "(unnamed)");
        }
        
        /* Support nested structs - they're just regular types with struct category */
        if (field_type->category == NMO_TYPE_CATEGORY_STRUCT && !field_type->valid) {
            return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                     NMO_SEVERITY_ERROR,
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
        max_align = max_alignment(max_align, field_align);
        
        /* Align offset to field alignment */
        offset = align_up(offset, field_align);
        
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
    uint32_t total_size = align_up(offset, max_align);
    
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

nmo_result_t nmo_type_registry_register_struct(
    nmo_type_registry_t *type_registry,
    const nmo_struct_type_def_t *struct_def,
    nmo_guid_t *out_guid
) {
    if (!type_registry || !struct_def) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "NULL type_registry or struct_def");
    }
    
    if (!struct_def->name || struct_def->name[0] == '\0') {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Struct type name cannot be empty");
    }
    
    if (!struct_def->fields || struct_def->field_count == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Struct must have at least one field");
    }
    
    /* Generate GUID for the struct type */
    nmo_guid_t type_guid = nmo_guid_is_null(struct_def->guid) ?
        nmo_type_generate_guid(struct_def->name) : struct_def->guid;
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (existing) {
        return nmo_result_errorf(NULL, NMO_ERR_ALREADY_EXISTS,
                                 NMO_SEVERITY_ERROR,
                                 "Struct type '%s' already registered",
                                 struct_def->name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate field definitions array (mutable copy for layout calculation) */
    nmo_struct_field_def_t *fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_field_def_t) * struct_def->field_count,
                        alignof(nmo_struct_field_def_t));
    if (!fields) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate fields array");
    }
    
    /* Copy fields */
    memcpy(fields, struct_def->fields, sizeof(nmo_struct_field_def_t) * struct_def->field_count);
    
    /* Calculate layout */
    uint32_t total_size, struct_alignment;
    nmo_result_t result = nmo_type_calculate_layout(
        type_registry, fields, struct_def->field_count,
        struct_def->alignment, struct_def->packed,
        &total_size, &struct_alignment);
    if (nmo_result_is_error(result)) {
        return result;
    }
    
    /* Allocate struct descriptors array for specialized metadata */
    nmo_struct_descriptor_t *struct_fields = (nmo_struct_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_descriptor_t) * struct_def->field_count,
                        alignof(nmo_struct_descriptor_t));
    if (!struct_fields) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate struct descriptors");
    }
    
    /* Build struct descriptors with calculated offsets */
    uint32_t offset = 0;
    for (size_t i = 0; i < struct_def->field_count; i++) {
        const nmo_struct_field_def_t *field_def = &fields[i];
        nmo_struct_descriptor_t *field_desc = &struct_fields[i];
        
        /* Copy field name */
        field_desc->name = nmo_arena_strdup(arena, field_def->name);
        if (!field_desc->name) {
            return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                     NMO_SEVERITY_ERROR,
                                     "Failed to copy field name");
        }
        
        /* Parse type name to get array info if needed */
        uint32_t array_count = 0;
        nmo_guid_t field_type_guid = field_def->type_guid;
        
        if (field_def->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_result_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field_def->type_name, &parse_result);
            if (nmo_result_is_ok(parse_res)) {
                field_type_guid = parse_result.base_type_guid;
                array_count = parse_result.array_count;
            }
        }
        
        /* Get field type info */
        const nmo_type_descriptor_t *field_type = nmo_type_registry_find_by_guid(
            type_registry, field_type_guid);
        if (!field_type) {
            return nmo_result_errorf(NULL, NMO_ERR_NOT_FOUND,
                                     NMO_SEVERITY_ERROR,
                                     "Field type not found");
        }
        
        /* Calculate field size (including arrays) */
        uint32_t element_size = field_type->size;
        uint32_t total_field_size = element_size;
        if (array_count > 0) {
            total_field_size = element_size * array_count;
        }
        
        uint32_t field_align = struct_def->packed ? 1 : field_type->alignment;
        offset = align_up(offset, field_align);
        
        field_desc->type_guid = field_type_guid;
        field_desc->offset = offset;
        field_desc->size = total_field_size;
        field_desc->array_count = array_count;
        field_desc->flags = field_def->flags;
        field_desc->description = field_def->description ?
            nmo_arena_strdup(arena, field_def->description) : NULL;
        
        offset += total_field_size;
    }
    
    /* Allocate specialized_metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = NMO_TYPE_ID_INVALID;  /* Will be set during registration */
    spec_meta->metadata_type = NMO_METADATA_TYPE_STRUCT;
    spec_meta->reserved = 0;
    spec_meta->struct_meta.fields = struct_fields;
    spec_meta->struct_meta.field_count = struct_def->field_count;
    
    /* Allocate type descriptor */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate struct type descriptor");
    }
    
    /* Initialize all fields to zero */
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    
    /* Copy struct type name */
    const char *type_name = nmo_arena_strdup(arena, struct_def->name);
    if (!type_name) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to copy struct type name");
    }
    
    /* Initialize type descriptor */
    type_desc->guid = type_guid;
    type_desc->name = type_name;
    type_desc->size = total_size;
    type_desc->alignment = struct_alignment;
    type_desc->category = NMO_TYPE_CATEGORY_STRUCT;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE;
    if (struct_def->packed) {
        /* Packed structs are POD if all fields are POD */
        type_desc->flags |= NMO_TYPE_FLAG_POD;
    }
    type_desc->fields = NULL;
    type_desc->field_count = 0;
    type_desc->vtable = NULL;
    type_desc->description = struct_def->description ? 
        nmo_arena_strdup(arena, struct_def->description) : NULL;
    type_desc->valid = true;
    
    /* Register type in registry */
    result = nmo_type_registry_register(type_registry, type_desc);
    if (nmo_result_is_error(result)) {
        return result;
    }
    
    /* Update type_id in metadata */
    spec_meta->type_id = type_desc->id;
    
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

nmo_result_t nmo_type_registry_begin_struct(
    nmo_type_registry_t *type_registry,
    const char *name,
    nmo_guid_t guid,
    nmo_type_id_t *out_type_id
) {
    if (!type_registry || !name || !name[0]) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "NULL type_registry or empty name");
    }
    
    /* Generate GUID if null */
    if (nmo_guid_is_null(guid)) {
        guid = nmo_type_generate_guid(name);
    }
    
    /* Check if type already exists */
    const nmo_type_descriptor_t *existing = nmo_type_registry_find_by_guid(type_registry, guid);
    if (existing) {
        return nmo_result_errorf(NULL, NMO_ERR_ALREADY_EXISTS,
                                 NMO_SEVERITY_ERROR,
                                 "Struct type '%s' already registered", name);
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Allocate incomplete struct state */
    incomplete_struct_t *incomplete = (incomplete_struct_t*)
        nmo_arena_alloc(arena, sizeof(incomplete_struct_t), alignof(incomplete_struct_t));
    if (!incomplete) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
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
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate field array");
    }
    
    /* Create placeholder type descriptor (invalid until finalized) */
    nmo_type_descriptor_t *type_desc = (nmo_type_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    if (!type_desc) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate type descriptor");
    }
    
    memset(type_desc, 0, sizeof(nmo_type_descriptor_t));
    type_desc->guid = guid;
    type_desc->name = incomplete->name;
    type_desc->category = NMO_TYPE_CATEGORY_STRUCT;
    type_desc->valid = false;  /* Mark as incomplete */
    type_desc->description = (const char*)incomplete;  /* Store incomplete state */
    
    /* Register placeholder (will be updated on finalize) */
    nmo_result_t result = nmo_type_registry_register(type_registry, type_desc);
    if (nmo_result_is_error(result)) {
        return result;
    }
    
    /* Find registered type to get assigned ID and reset valid flag */
    nmo_type_descriptor_t *registered = (nmo_type_descriptor_t*)nmo_type_registry_find_by_guid(type_registry, guid);
    if (!registered) {
        return nmo_result_errorf(NULL, NMO_ERR_INTERNAL,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to find just-registered type");
    }
    
    /* Mark as incomplete (register sets it to true) */
    registered->valid = false;
    
    if (out_type_id) {
        *out_type_id = registered->id;
    }
    
    NMO_RETURN_OK();
}

nmo_result_t nmo_type_registry_add_field(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id,
    const char *field_name,
    const char *field_type_name
) {
    if (!type_registry || !field_name || !field_type_name) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "NULL parameters");
    }
    
    /* Find incomplete struct */
    incomplete_struct_t *incomplete = find_incomplete_struct(type_registry, struct_type_id);
    if (!incomplete) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Invalid struct type ID or struct already finalized");
    }
    
    if (incomplete->finalized) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
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
            return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                     NMO_SEVERITY_ERROR,
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
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to copy field strings");
    }
    
    incomplete->field_count++;
    
    NMO_RETURN_OK();
}

nmo_result_t nmo_type_registry_finalize_struct(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id
) {
    if (!type_registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "NULL type_registry");
    }
    
    /* Find incomplete struct */
    incomplete_struct_t *incomplete = find_incomplete_struct(type_registry, struct_type_id);
    if (!incomplete) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Invalid struct type ID or struct already finalized");
    }
    
    if (incomplete->finalized) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Struct already finalized");
    }
    
    if (incomplete->field_count == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "Cannot finalize struct with no fields");
    }
    
    nmo_arena_t *arena = type_registry->arena;
    
    /* Parse all field types first */
    for (size_t i = 0; i < incomplete->field_count; i++) {
        nmo_struct_field_def_t *field = &incomplete->fields[i];
        
        if (nmo_guid_is_null(field->type_guid) && field->type_name) {
            nmo_type_parse_result_t parse_result;
            nmo_result_t parse_res = nmo_type_registry_parse_type_name(
                type_registry, field->type_name, &parse_result);
            if (nmo_result_is_error(parse_res)) {
                return parse_res;
            }
            field->type_guid = parse_result.base_type_guid;
        }
    }
    
    /* Calculate layout */
    uint32_t total_size, struct_alignment;
    nmo_result_t result = nmo_type_calculate_layout(
        type_registry, incomplete->fields, incomplete->field_count,
        0, false, &total_size, &struct_alignment);
    if (nmo_result_is_error(result)) {
        return result;
    }
    
    /* Build struct descriptors */
    nmo_struct_descriptor_t *struct_fields = (nmo_struct_descriptor_t*)
        nmo_arena_alloc(arena, sizeof(nmo_struct_descriptor_t) * incomplete->field_count,
                        alignof(nmo_struct_descriptor_t));
    if (!struct_fields) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate struct descriptors");
    }
    
    /* Build field descriptors with calculated offsets */
    uint32_t offset = 0;
    for (size_t i = 0; i < incomplete->field_count; i++) {
        const nmo_struct_field_def_t *field_def = &incomplete->fields[i];
        nmo_struct_descriptor_t *field_desc = &struct_fields[i];
        
        const nmo_type_descriptor_t *field_type = nmo_type_registry_find_by_guid(
            type_registry, field_def->type_guid);
        if (!field_type) {
            char guid_str[64];
            nmo_guid_format(field_def->type_guid, guid_str, sizeof(guid_str));
            return nmo_result_errorf(NULL, NMO_ERR_NOT_FOUND,
                                     NMO_SEVERITY_ERROR,
                                     "Field type not found for '%s' (GUID: %s)",
                                     field_def->name, guid_str);
        }
        
        uint32_t field_align = field_type->alignment;
        offset = align_up(offset, field_align);
        
        field_desc->name = field_def->name;
        field_desc->type_guid = field_def->type_guid;
        field_desc->offset = offset;
        field_desc->size = field_type->size;
        field_desc->array_count = 0;
        field_desc->flags = field_def->flags;
        field_desc->description = field_def->description;
        
        offset += field_type->size;
    }
    
    /* Create specialized metadata */
    nmo_specialized_metadata_t *spec_meta = (nmo_specialized_metadata_t*)
        nmo_arena_alloc(arena, sizeof(nmo_specialized_metadata_t), alignof(nmo_specialized_metadata_t));
    if (!spec_meta) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate specialized metadata");
    }
    
    spec_meta->type_id = struct_type_id;
    spec_meta->metadata_type = NMO_METADATA_TYPE_STRUCT;
    spec_meta->reserved = 0;
    spec_meta->struct_meta.fields = struct_fields;
    spec_meta->struct_meta.field_count = incomplete->field_count;
    
    /* Add to registry metadata array */
    uint32_t metadata_index = (uint32_t)type_registry->metadata.count;
    nmo_result_t res = nmo_arena_array_append(&type_registry->metadata, &spec_meta);
    if (nmo_result_is_error(res)) return res;
    
    /* Update type descriptor to mark as valid */
    nmo_type_descriptor_t *type_desc = *(nmo_type_descriptor_t **)nmo_arena_array_get(&type_registry->types, struct_type_id);
    type_desc->size = total_size;
    type_desc->alignment = struct_alignment;
    type_desc->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE;
    type_desc->specialized_index = metadata_index;
    type_desc->description = NULL;  /* Clear incomplete state pointer */
    type_desc->valid = true;  /* Mark as complete */
    
    incomplete->finalized = true;
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * String-Based Struct Registration (Phase 6.2 Task 6.2.3)
 * ============================================================================ */

nmo_result_t nmo_type_registry_register_struct_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char **field_type_names,
    size_t field_count
) {
    if (!type_registry || !type_name || !field_type_names || field_count == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                 NMO_SEVERITY_ERROR,
                                 "NULL argument or empty field list");
    }
    
    /* Validate all field type names exist */
    for (size_t i = 0; i < field_count; i++) {
        if (!field_type_names[i] || field_type_names[i][0] == '\0') {
            return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT,
                                     NMO_SEVERITY_ERROR,
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
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to create temporary arena");
    }
    
    /* Allocate field definitions array */
    nmo_struct_field_def_t *fields = (nmo_struct_field_def_t*)
        nmo_arena_alloc(temp_arena, sizeof(nmo_struct_field_def_t) * field_count, 8);
    if (!fields) {
        nmo_arena_destroy(temp_arena);
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                 NMO_SEVERITY_ERROR,
                                 "Failed to allocate field definitions");
    }
    
    /* Initialize fields by parsing type names */
    for (size_t i = 0; i < field_count; i++) {
        const char *type_name_str = field_type_names[i];
        
        /* Parse type name to get GUID and array info */
        nmo_type_parse_result_t parse_result;
        nmo_result_t parse_res = nmo_type_registry_parse_type_name(
            type_registry, type_name_str, &parse_result);
        
        if (nmo_result_is_error(parse_res)) {
            nmo_arena_destroy(temp_arena);
            return nmo_result_errorf(NULL, NMO_ERR_NOT_FOUND,
                                     NMO_SEVERITY_ERROR,
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
            return nmo_result_errorf(NULL, NMO_ERR_NOMEM,
                                     NMO_SEVERITY_ERROR,
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
    nmo_result_t result = nmo_type_registry_register_struct(
        type_registry, &struct_def, &out_guid);
    
    /* Clean up temporary arena */
    nmo_arena_destroy(temp_arena);
    
    return result;
}
