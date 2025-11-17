/**
 * @file schema_builder.c
 * @brief Implementation of fluent schema builder API
 */

#include "schema/nmo_schema_builder.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stdalign.h>

#define INITIAL_FIELDS_CAPACITY 16
#define INITIAL_ENUM_CAPACITY 32

/* =============================================================================
 * INTERNAL HELPERS
 * ============================================================================= */

static void init_builder(nmo_schema_builder_t *builder, nmo_arena_t *arena) {
    memset(builder, 0, sizeof(*builder));
    builder->arena = arena;
    builder->type = nmo_arena_alloc(arena, sizeof(nmo_schema_type_t), alignof(nmo_schema_type_t));
    if (builder->type) {
        memset(builder->type, 0, sizeof(*builder->type));
    }
}

static nmo_result_t ensure_fields_capacity(nmo_schema_builder_t *builder, size_t needed) {
    if (builder->fields_count + needed <= builder->fields_capacity) {
        return nmo_result_ok();
    }
    
    size_t new_capacity = builder->fields_capacity == 0 
        ? INITIAL_FIELDS_CAPACITY 
        : builder->fields_capacity * 2;
    
    while (new_capacity < builder->fields_count + needed) {
        new_capacity *= 2;
    }
    
    nmo_schema_field_t *new_buffer = nmo_arena_alloc(
        builder->arena,
        sizeof(nmo_schema_field_t) * new_capacity,
        alignof(nmo_schema_field_t));
    
    if (!new_buffer) {
        return nmo_result_error(NMO_ERROR(builder->arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to grow fields buffer"));
    }
    
    if (builder->fields_buffer) {
        memcpy(new_buffer, builder->fields_buffer, 
               builder->fields_count * sizeof(nmo_schema_field_t));
    }
    
    builder->fields_buffer = new_buffer;
    builder->fields_capacity = new_capacity;
    return nmo_result_ok();
}

static nmo_result_t ensure_enum_capacity(nmo_schema_builder_t *builder, size_t needed) {
    if (builder->enum_count + needed <= builder->enum_capacity) {
        return nmo_result_ok();
    }
    
    size_t new_capacity = builder->enum_capacity == 0 
        ? INITIAL_ENUM_CAPACITY 
        : builder->enum_capacity * 2;
    
    while (new_capacity < builder->enum_count + needed) {
        new_capacity *= 2;
    }
    
    nmo_enum_value_t *new_buffer = nmo_arena_alloc(
        builder->arena,
        sizeof(nmo_enum_value_t) * new_capacity,
        alignof(nmo_enum_value_t));
    
    if (!new_buffer) {
        return nmo_result_error(NMO_ERROR(builder->arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to grow enum buffer"));
    }
    
    if (builder->enum_buffer) {
        memcpy(new_buffer, builder->enum_buffer, 
               builder->enum_count * sizeof(nmo_enum_value_t));
    }
    
    builder->enum_buffer = new_buffer;
    builder->enum_capacity = new_capacity;
    return nmo_result_ok();
}

/* =============================================================================
 * BUILDER INITIALIZATION
 * ============================================================================= */

nmo_schema_builder_t nmo_builder_scalar(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t kind,
    size_t size)
{
    nmo_schema_builder_t builder;
    init_builder(&builder, arena);
    
    if (builder.type) {
        builder.type->name = name;
        builder.type->kind = kind;
        builder.type->size = size;
        builder.type->align = size;
    }
    
    return builder;
}

nmo_schema_builder_t nmo_builder_struct(
    nmo_arena_t *arena,
    const char *name,
    size_t size,
    size_t align)
{
    nmo_schema_builder_t builder;
    init_builder(&builder, arena);
    
    if (builder.type) {
        builder.type->name = name;
        builder.type->kind = NMO_TYPE_STRUCT;
        builder.type->size = size;
        builder.type->align = align;
    }
    
    return builder;
}

nmo_schema_builder_t nmo_builder_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type)
{
    nmo_schema_builder_t builder;
    init_builder(&builder, arena);
    
    if (builder.type) {
        builder.type->name = name;
        builder.type->kind = NMO_TYPE_ARRAY;
        builder.type->size = 0;  /* Variable size */
        builder.type->align = 4;
        builder.type->element_type = element_type;
    }
    
    return builder;
}

nmo_schema_builder_t nmo_builder_fixed_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type,
    size_t length)
{
    nmo_schema_builder_t builder;
    init_builder(&builder, arena);
    
    if (builder.type) {
        builder.type->name = name;
        builder.type->kind = NMO_TYPE_FIXED_ARRAY;
        builder.type->size = element_type->size * length;
        builder.type->align = element_type->align;
        builder.type->element_type = element_type;
        builder.type->array_length = length;
    }
    
    return builder;
}

nmo_schema_builder_t nmo_builder_enum(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t base_type)
{
    nmo_schema_builder_t builder;
    init_builder(&builder, arena);
    
    if (builder.type) {
        builder.type->name = name;
        builder.type->kind = NMO_TYPE_ENUM;
        builder.type->enum_base_type = base_type;
        
        /* Size matches base type */
        switch (base_type) {
            case NMO_TYPE_U8:
            case NMO_TYPE_I8:
                builder.type->size = 1;
                builder.type->align = 1;
                break;
            case NMO_TYPE_U16:
            case NMO_TYPE_I16:
                builder.type->size = 2;
                builder.type->align = 2;
                break;
            case NMO_TYPE_U32:
            case NMO_TYPE_I32:
            default:
                builder.type->size = 4;
                builder.type->align = 4;
                break;
        }
    }
    
    return builder;
}

/* =============================================================================
 * FIELD CONSTRUCTION
 * ============================================================================= */

nmo_schema_builder_t *nmo_builder_add_field(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset)
{
    return nmo_builder_add_field_ex(builder, field_name, field_type, field_offset, 0);
}

nmo_schema_builder_t *nmo_builder_add_field_ex(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t annotations)
{
    if (!builder || !builder->type) {
        return builder;
    }
    
    nmo_result_t result = ensure_fields_capacity(builder, 1);
    if (result.code != NMO_OK) {
        return builder;
    }
    
    nmo_schema_field_t *field = &builder->fields_buffer[builder->fields_count++];
    field->name = field_name;
    field->type = field_type;
    field->offset = field_offset;
    field->annotations = annotations;
    field->since_version = 0;
    field->deprecated_version = 0;
    
    return builder;
}

nmo_schema_builder_t *nmo_builder_add_field_versioned(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t since_version,
    uint32_t deprecated_version)
{
    if (!builder || !builder->type) {
        return builder;
    }
    
    nmo_result_t result = ensure_fields_capacity(builder, 1);
    if (result.code != NMO_OK) {
        return builder;
    }
    
    nmo_schema_field_t *field = &builder->fields_buffer[builder->fields_count++];
    field->name = field_name;
    field->type = field_type;
    field->offset = field_offset;
    field->annotations = 0;
    field->since_version = since_version;
    field->deprecated_version = deprecated_version;
    
    return builder;
}

nmo_result_t nmo_builder_add_field_manual(
    nmo_schema_builder_t *builder,
    const nmo_schema_field_t *field)
{
    if (!builder || !builder->type || !field) {
        return nmo_result_error(NMO_ERROR(builder->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_builder_add_field_manual"));
    }
    
    nmo_result_t result = ensure_fields_capacity(builder, 1);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Copy entire field structure */
    builder->fields_buffer[builder->fields_count++] = *field;
    
    return nmo_result_ok();
}

/* =============================================================================
 * ENUM CONSTRUCTION
 * ============================================================================= */

nmo_schema_builder_t *nmo_builder_add_enum_value(
    nmo_schema_builder_t *builder,
    const char *value_name,
    int32_t value)
{
    if (!builder || !builder->type) {
        return builder;
    }
    
    nmo_result_t result = ensure_enum_capacity(builder, 1);
    if (result.code != NMO_OK) {
        return builder;
    }
    
    nmo_enum_value_t *enum_val = &builder->enum_buffer[builder->enum_count++];
    enum_val->name = value_name;
    enum_val->value = value;
    
    return builder;
}

/* =============================================================================
 * VTABLE CONFIGURATION
 * ============================================================================= */

nmo_schema_builder_t *nmo_builder_set_vtable(
    nmo_schema_builder_t *builder,
    const nmo_schema_vtable_t *vtable)
{
    if (builder && builder->type) {
        builder->type->vtable = vtable;
    }
    return builder;
}

/* =============================================================================
 * FINALIZATION
 * ============================================================================= */

const nmo_schema_type_t *nmo_builder_build_type(nmo_schema_builder_t *builder)
{
    if (!builder || !builder->type) {
        return NULL;
    }
    
    /* Finalize fields */
    if (builder->fields_count > 0) {
        builder->type->fields = builder->fields_buffer;
        builder->type->field_count = builder->fields_count;
    }
    
    /* Finalize enum values */
    if (builder->enum_count > 0) {
        builder->type->enum_values = builder->enum_buffer;
        builder->type->enum_value_count = builder->enum_count;
    }
    
    /* Copy version information */
    builder->type->since_version = builder->since_version;
    builder->type->deprecated_version = builder->deprecated_version;
    builder->type->removed_version = builder->removed_version;
    
    /* Copy parameter metadata */
    builder->type->param_meta = builder->param_meta;
    
    return builder->type;
}

nmo_result_t nmo_builder_build(
    nmo_schema_builder_t *builder,
    nmo_schema_registry_t *registry)
{
    /* Check for accumulated errors */
    if (builder && builder->first_error) {
        return nmo_result_error(builder->first_error);
    }
    
    const nmo_schema_type_t *type = nmo_builder_build_type(builder);
    if (!type) {
        return nmo_result_error(NMO_ERROR(builder->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid builder state"));
    }
    
    return nmo_schema_registry_add(registry, type);
}

/* =============================================================================
 * BATCH REGISTRATION HELPERS
 * ============================================================================= */

nmo_result_t nmo_register_scalar_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* Register all scalar types */
    const struct {
        const char *name;
        nmo_type_kind_t kind;
        size_t size;
    } scalars[] = {
        {"u8",  NMO_TYPE_U8,  1},
        {"u16", NMO_TYPE_U16, 2},
        {"u32", NMO_TYPE_U32, 4},
        {"u64", NMO_TYPE_U64, 8},
        {"i8",  NMO_TYPE_I8,  1},
        {"i16", NMO_TYPE_I16, 2},
        {"i32", NMO_TYPE_I32, 4},
        {"i64", NMO_TYPE_I64, 8},
        {"f32", NMO_TYPE_F32, 4},
        {"f64", NMO_TYPE_F64, 8},
        {"bool", NMO_TYPE_BOOL, 4},
    };
    
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) {
        nmo_schema_builder_t builder = nmo_builder_scalar(
            arena, scalars[i].name, scalars[i].kind, scalars[i].size);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) {
            return result;
        }
    }
    
    /* String type (variable size) */
    nmo_schema_builder_t string_builder = nmo_builder_scalar(arena, "string", NMO_TYPE_STRING, 0);
    string_builder.type->align = 1;
    result = nmo_builder_build(&string_builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * VERSION MANAGEMENT API
 * ============================================================================= */

nmo_schema_builder_t *nmo_builder_set_since_version(
    nmo_schema_builder_t *builder,
    uint32_t version)
{
    if (!builder || !builder->type) {
        return builder;
    }
    builder->since_version = version;
    return builder;
}

nmo_schema_builder_t *nmo_builder_set_deprecated_version(
    nmo_schema_builder_t *builder,
    uint32_t version)
{
    if (!builder || !builder->type) {
        return builder;
    }
    builder->deprecated_version = version;
    return builder;
}

nmo_schema_builder_t *nmo_builder_set_removed_version(
    nmo_schema_builder_t *builder,
    uint32_t version)
{
    if (!builder || !builder->type) {
        return builder;
    }
    builder->removed_version = version;
    return builder;
}

nmo_schema_builder_t *nmo_builder_set_version_range(
    nmo_schema_builder_t *builder,
    uint32_t since_version,
    uint32_t deprecated_version,
    uint32_t removed_version)
{
    if (!builder || !builder->type) {
        return builder;
    }
    builder->since_version = since_version;
    builder->deprecated_version = deprecated_version;
    builder->removed_version = removed_version;
    return builder;
}

/* =============================================================================
 * PARAMETER METADATA API
 * ============================================================================= */

nmo_schema_builder_t *nmo_builder_set_param_meta(
    nmo_schema_builder_t *builder,
    const nmo_param_meta_t *meta)
{
    if (!builder || !builder->type || !meta) {
        if (builder && !builder->first_error) {
            builder->first_error = NMO_ERROR(builder->arena, NMO_ERR_INVALID_ARGUMENT,
                NMO_SEVERITY_ERROR, "Invalid builder or param_meta pointer");
        }
        return builder;
    }
    
    /* Copy metadata to arena */
    nmo_param_meta_t *copied = nmo_arena_alloc(builder->arena, 
        sizeof(nmo_param_meta_t), alignof(nmo_param_meta_t));
    if (!copied) {
        if (!builder->first_error) {
            builder->first_error = NMO_ERROR(builder->arena, NMO_ERR_NOMEM,
                NMO_SEVERITY_ERROR, "Failed to allocate param_meta");
        }
        return builder;
    }
    
    memcpy(copied, meta, sizeof(nmo_param_meta_t));
    
    /* Copy strings if present */
    if (meta->ui_name) {
        size_t len = strlen(meta->ui_name) + 1;
        char *name = nmo_arena_alloc(builder->arena, len, 1);
        if (name) {
            memcpy(name, meta->ui_name, len);
            copied->ui_name = name;
        }
    }
    
    if (meta->description) {
        size_t len = strlen(meta->description) + 1;
        char *desc = nmo_arena_alloc(builder->arena, len, 1);
        if (desc) {
            memcpy(desc, meta->description, len);
            copied->description = desc;
        }
    }
    
    builder->param_meta = copied;
    return builder;
}
