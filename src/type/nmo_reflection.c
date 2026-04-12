/**
 * @file nmo_reflection.c
 * @brief Runtime reflection API implementation
 */

#include "type/nmo_reflection.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Field Access API
 * ============================================================================ */

NMO_API const nmo_type_field_t* nmo_type_get_field_by_name(
    const nmo_type_descriptor_t *type,
    const char *name)
{
    if (!type || !name || !type->fields) {
        return NULL;
    }
    
    for (size_t i = 0; i < type->field_count; ++i) {
        if (type->fields[i].name && strcmp(type->fields[i].name, name) == 0) {
            return &type->fields[i];
        }
    }
    
    return NULL;
}

NMO_API const nmo_type_field_t* nmo_type_get_field_by_index(
    const nmo_type_descriptor_t *type,
    size_t index)
{
    if (!type || !type->fields || index >= type->field_count) {
        return NULL;
    }
    
    return &type->fields[index];
}

NMO_API size_t nmo_type_get_field_count(const nmo_type_descriptor_t *type)
{
    if (!type) {
        return 0;
    }
    return type->field_count;
}

NMO_API bool nmo_type_has_reflection(const nmo_type_descriptor_t *type)
{
    return type && type->fields && type->field_count > 0;
}

/* ============================================================================
 * Field Iteration
 * ============================================================================ */

NMO_API nmo_status_t nmo_type_foreach_field(
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_field_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(type != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type descriptor");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor callback");
    
    if (!type->fields || type->field_count == 0) {
        NMO_RETURN_OK(); /* No fields, nothing to iterate */
    }
    
    for (size_t i = 0; i < type->field_count; ++i) {
        const nmo_type_field_t *field = &type->fields[i];
        const void *field_ptr = instance ? nmo_field_get_ptr_const(instance, field) : NULL;
        
        if (!visitor(user_data, field, field_ptr)) {
            break; /* Visitor requested stop */
        }
    }
    
    NMO_RETURN_OK();
}

NMO_API nmo_status_t nmo_type_foreach_ref_field(
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_field_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(type != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type descriptor");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor callback");
    
    if (!type->fields || type->field_count == 0) {
        NMO_RETURN_OK();
    }
    
    for (size_t i = 0; i < type->field_count; ++i) {
        const nmo_type_field_t *field = &type->fields[i];
        
        /* Skip non-reference fields */
        if (!(field->flags & NMO_FIELD_REFERENCE)) {
            continue;
        }
        
        const void *field_ptr = instance ? nmo_field_get_ptr_const(instance, field) : NULL;
        
        if (!visitor(user_data, field, field_ptr)) {
            break;
        }
    }
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Type Name Helpers
 * ============================================================================ */

NMO_API const char* nmo_field_type_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid)
{
    if (!registry) {
        return "unknown";
    }

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, type_guid);
    if (type && type->name) {
        return type->name;
    }

    return "unknown";
}

/* ============================================================================
 * Specialized Metadata Reflection
 * ============================================================================ */

static const nmo_specialized_metadata_t* nmo_get_metadata(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type)
{
    if (!registry || !type) {
        return NULL;
    }

    return nmo_type_registry_get_metadata(registry, type->id);
}

static bool nmo_meta_get_struct_fields(
    const nmo_specialized_metadata_t *metadata,
    const nmo_struct_descriptor_t **out_fields,
    size_t *out_count)
{
    if (!metadata || !out_fields || !out_count) {
        return false;
    }

    if (metadata->metadata_type == NMO_METADATA_TYPE_STRUCT) {
        *out_fields = metadata->struct_meta.fields;
        *out_count = metadata->struct_meta.field_count;
        return true;
    }

    if (metadata->metadata_type == NMO_METADATA_TYPE_UNION) {
        *out_fields = metadata->union_meta.fields;
        *out_count = metadata->union_meta.field_count;
        return true;
    }

    return false;
}

NMO_API const nmo_specialized_metadata_t* nmo_type_get_specialized_metadata(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type)
{
    return nmo_get_metadata(registry, type);
}

NMO_API const nmo_struct_descriptor_t* nmo_type_get_struct_field_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name)
{
    if (!name) {
        return NULL;
    }

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    const nmo_struct_descriptor_t *fields = NULL;
    size_t count = 0;

    if (!nmo_meta_get_struct_fields(metadata, &fields, &count)) {
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        if (fields[i].name && strcmp(fields[i].name, name) == 0) {
            return &fields[i];
        }
    }

    return NULL;
}

NMO_API const nmo_struct_descriptor_t* nmo_type_get_struct_field_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index)
{
    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    const nmo_struct_descriptor_t *fields = NULL;
    size_t count = 0;

    if (!nmo_meta_get_struct_fields(metadata, &fields, &count)) {
        return NULL;
    }

    if (index >= count) {
        return NULL;
    }

    return &fields[index];
}

NMO_API nmo_status_t nmo_type_foreach_struct_field(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_struct_field_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(type != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type descriptor");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor callback");

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    const nmo_struct_descriptor_t *fields = NULL;
    size_t count = 0;

    if (!nmo_meta_get_struct_fields(metadata, &fields, &count)) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < count; ++i) {
        const nmo_struct_descriptor_t *field = &fields[i];
        const void *field_ptr = NULL;

        if (instance) {
            field_ptr = (const uint8_t*)instance + field->offset;
        }

        if (!visitor(user_data, field, field_ptr)) {
            break;
        }
    }

    NMO_RETURN_OK();
}

NMO_API const nmo_enum_descriptor_t* nmo_type_get_enum_value_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name)
{
    if (!name) {
        return NULL;
    }

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        return NULL;
    }

    for (size_t i = 0; i < metadata->enum_meta.value_count; ++i) {
        const nmo_enum_descriptor_t *value = &metadata->enum_meta.values[i];
        if (value->name && strcmp(value->name, name) == 0) {
            return value;
        }
    }

    return NULL;
}

NMO_API const nmo_enum_descriptor_t* nmo_type_get_enum_value_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index)
{
    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        return NULL;
    }

    if (index >= metadata->enum_meta.value_count) {
        return NULL;
    }

    return &metadata->enum_meta.values[index];
}

NMO_API nmo_status_t nmo_type_foreach_enum_value(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    nmo_enum_value_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(type != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type descriptor");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor callback");

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < metadata->enum_meta.value_count; ++i) {
        if (!visitor(user_data, &metadata->enum_meta.values[i])) {
            break;
        }
    }

    NMO_RETURN_OK();
}

NMO_API const nmo_flags_descriptor_t* nmo_type_get_flags_bit_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name)
{
    if (!name) {
        return NULL;
    }

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        return NULL;
    }

    for (size_t i = 0; i < metadata->flags_meta.bit_count; ++i) {
        const nmo_flags_descriptor_t *bit = &metadata->flags_meta.bits[i];
        if (bit->name && strcmp(bit->name, name) == 0) {
            return bit;
        }
    }

    return NULL;
}

NMO_API const nmo_flags_descriptor_t* nmo_type_get_flags_bit_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index)
{
    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        return NULL;
    }

    if (index >= metadata->flags_meta.bit_count) {
        return NULL;
    }

    return &metadata->flags_meta.bits[index];
}

NMO_API nmo_status_t nmo_type_foreach_flags_bit(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    nmo_flags_bit_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(type != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type descriptor");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor callback");

    const nmo_specialized_metadata_t *metadata = nmo_get_metadata(registry, type);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < metadata->flags_meta.bit_count; ++i) {
        if (!visitor(user_data, &metadata->flags_meta.bits[i])) {
            break;
        }
    }

    NMO_RETURN_OK();
}

NMO_API size_t nmo_field_flags_to_string(uint32_t flags, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    
    buffer[0] = '\0';
    size_t written = 0;
    
    const char *sep = "";
    
#define APPEND_FLAG(flag, name) \
    if ((flags & (flag)) && written < buffer_size - 1) { \
        int n = snprintf(buffer + written, buffer_size - written, "%s%s", sep, name); \
        if (n > 0) { written += (size_t)n; sep = "|"; } \
    }
    
    APPEND_FLAG(NMO_FIELD_REQUIRED, "required")
    APPEND_FLAG(NMO_FIELD_OPTIONAL, "optional")
    APPEND_FLAG(NMO_FIELD_REPEATED, "array")
    APPEND_FLAG(NMO_FIELD_DEPRECATED, "deprecated")
    APPEND_FLAG(NMO_FIELD_EDITOR_ONLY, "editor_only")
    APPEND_FLAG(NMO_FIELD_RUNTIME_ONLY, "runtime_only")
    APPEND_FLAG(NMO_FIELD_ID, "id")
    APPEND_FLAG(NMO_FIELD_REFERENCE, "ref")
    APPEND_FLAG(NMO_FIELD_POINTER, "pointer")

#undef APPEND_FLAG
    
    if (written == 0) {
        written = (size_t)snprintf(buffer, buffer_size, "(none)");
    }
    
    return written;
}
