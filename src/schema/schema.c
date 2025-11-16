/**
 * @file schema.c
 * @brief Schema system core implementation
 */

#include "schema/nmo_schema.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * TYPE UTILITIES
 * ============================================================================= */

const char *nmo_type_kind_name(nmo_type_kind_t kind) {
    switch (kind) {
        case NMO_TYPE_U8: return "u8";
        case NMO_TYPE_U16: return "u16";
        case NMO_TYPE_U32: return "u32";
        case NMO_TYPE_U64: return "u64";
        case NMO_TYPE_I8: return "i8";
        case NMO_TYPE_I16: return "i16";
        case NMO_TYPE_I32: return "i32";
        case NMO_TYPE_I64: return "i64";
        case NMO_TYPE_F32: return "f32";
        case NMO_TYPE_F64: return "f64";
        case NMO_TYPE_BOOL: return "bool";
        case NMO_TYPE_STRING: return "string";
        case NMO_TYPE_STRUCT: return "struct";
        case NMO_TYPE_ARRAY: return "array";
        case NMO_TYPE_FIXED_ARRAY: return "fixed_array";
        case NMO_TYPE_BINARY: return "binary";
        case NMO_TYPE_RESOURCE_REF: return "resource_ref";
        case NMO_TYPE_ENUM: return "enum";
        default: return "unknown";
    }
}

size_t nmo_type_scalar_size(nmo_type_kind_t kind) {
    switch (kind) {
        case NMO_TYPE_U8:
        case NMO_TYPE_I8:
            return 1;
        case NMO_TYPE_U16:
        case NMO_TYPE_I16:
            return 2;
        case NMO_TYPE_U32:
        case NMO_TYPE_I32:
        case NMO_TYPE_F32:
        case NMO_TYPE_BOOL:
            return 4;
        case NMO_TYPE_U64:
        case NMO_TYPE_I64:
        case NMO_TYPE_F64:
            return 8;
        default:
            return 0;
    }
}

int nmo_type_is_variable_size(const nmo_schema_type_t *type) {
    if (type == NULL) return 0;
    
    switch (type->kind) {
        case NMO_TYPE_STRING:
        case NMO_TYPE_ARRAY:
        case NMO_TYPE_BINARY:
            return 1;
        case NMO_TYPE_STRUCT:
            /* Struct is variable-size if any field is */
            for (size_t i = 0; i < type->field_count; i++) {
                if (nmo_type_is_variable_size(type->fields[i].type)) {
                    return 1;
                }
            }
            return 0;
        default:
            return 0;
    }
}

const char *nmo_annotation_name(nmo_field_annotation_t annotation) {
    switch (annotation) {
        case NMO_ANNOTATION_SINCE: return "since";
        case NMO_ANNOTATION_DEPRECATED: return "deprecated";
        case NMO_ANNOTATION_EDITOR_ONLY: return "editor_only";
        case NMO_ANNOTATION_ID_FIELD: return "id_field";
        case NMO_ANNOTATION_REFERENCE: return "reference";
        case NMO_ANNOTATION_POSITION: return "position";
        case NMO_ANNOTATION_ROTATION: return "rotation";
        case NMO_ANNOTATION_SCALE: return "scale";
        case NMO_ANNOTATION_COLOR: return "color";
        case NMO_ANNOTATION_NORMAL: return "normal";
        case NMO_ANNOTATION_SECONDS: return "seconds";
        case NMO_ANNOTATION_DEGREES: return "degrees";
        case NMO_ANNOTATION_METERS: return "meters";
        default: return "unknown";
    }
}

/* =============================================================================
 * REFLECTION-BASED READ/WRITE
 * ============================================================================= */

/* Forward declarations for recursive calls */
static nmo_result_t read_field(
    const nmo_schema_field_t *field,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *struct_base);

static nmo_result_t write_field(
    const nmo_schema_field_t *field,
    nmo_chunk_t *chunk,
    const void *struct_base,
    nmo_arena_t *arena);

/* Read single value based on type kind */
static nmo_result_t read_value(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    nmo_result_t result;
    
    /* Check for vtable fast path */
    if (type->vtable != NULL && type->vtable->read != NULL) {
        return type->vtable->read(type, chunk, arena, out_ptr);
    }
    
    switch (type->kind) {
        case NMO_TYPE_U8:
        case NMO_TYPE_I8: {
            uint8_t value;
            result = nmo_chunk_read_byte(chunk, &value);
            if (result.code != NMO_OK) return result;
            *(uint8_t*)out_ptr = value;
            break;
        }
        
        case NMO_TYPE_U16:
        case NMO_TYPE_I16: {
            uint16_t value;
            result = nmo_chunk_read_word(chunk, &value);
            if (result.code != NMO_OK) return result;
            *(uint16_t*)out_ptr = value;
            break;
        }
        
        case NMO_TYPE_U32:
        case NMO_TYPE_I32:
        case NMO_TYPE_BOOL: {
            uint32_t value;
            result = nmo_chunk_read_dword(chunk, &value);
            if (result.code != NMO_OK) return result;
            *(uint32_t*)out_ptr = value;
            break;
        }
        
        case NMO_TYPE_F32: {
            float value;
            result = nmo_chunk_read_float(chunk, &value);
            if (result.code != NMO_OK) return result;
            *(float*)out_ptr = value;
            break;
        }
        
        case NMO_TYPE_STRING: {
            char *str = NULL;
            size_t len = nmo_chunk_read_string(chunk, &str);
            if (len == 0 && str == NULL) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_EOF,
                    NMO_SEVERITY_ERROR, "Failed to read string"));
            }
            *(char**)out_ptr = str;
            break;
        }
        
        case NMO_TYPE_STRUCT: {
            /* Read all fields */
            for (size_t i = 0; i < type->field_count; i++) {
                result = read_field(&type->fields[i], chunk, arena, out_ptr);
                if (result.code != NMO_OK) return result;
            }
            break;
        }
        
        case NMO_TYPE_ARRAY: {
            /* Read count, then elements */
            uint32_t count;
            result = nmo_chunk_read_dword(chunk, &count);
            if (result.code != NMO_OK) return result;
            
            if (count == 0) {
                *(void**)out_ptr = NULL;
                *((uint32_t*)out_ptr + 1) = 0;
                break;
            }
            
            /* Allocate array */
            size_t elem_size = type->element_type->size;
            if (elem_size == 0) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Cannot read array of variable-size elements"));
            }
            
            void *array = nmo_arena_alloc(arena, elem_size * count, 8);
            if (array == NULL) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate array"));
            }
            
            /* Read elements */
            for (uint32_t i = 0; i < count; i++) {
                void *elem_ptr = (char*)array + i * elem_size;
                result = read_value(type->element_type, chunk, arena, elem_ptr);
                if (result.code != NMO_OK) return result;
            }
            
            *(void**)out_ptr = array;
            *((uint32_t*)out_ptr + 1) = count;
            break;
        }
        
        case NMO_TYPE_FIXED_ARRAY: {
            /* Read fixed number of elements */
            size_t elem_size = type->element_type->size;
            for (size_t i = 0; i < type->array_length; i++) {
                void *elem_ptr = (char*)out_ptr + i * elem_size;
                result = read_value(type->element_type, chunk, arena, elem_ptr);
                if (result.code != NMO_OK) return result;
            }
            break;
        }
        
        case NMO_TYPE_BINARY: {
            /* Read size, then bytes */
            uint32_t size;
            result = nmo_chunk_read_dword(chunk, &size);
            if (result.code != NMO_OK) return result;
            
            if (size == 0) {
                *(void**)out_ptr = NULL;
                *((uint32_t*)out_ptr + 1) = 0;
                break;
            }
            
            void *data = nmo_arena_alloc(arena, size, 1);
            if (data == NULL) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate binary buffer"));
            }
            
            size_t actual_size = size;
            result = nmo_chunk_read_buffer(chunk, data, &actual_size);
            if (result.code != NMO_OK) return result;
            
            *(void**)out_ptr = data;
            *((uint32_t*)out_ptr + 1) = size;
            break;
        }
        
        case NMO_TYPE_RESOURCE_REF: {
            /* Read object ID */
            nmo_object_id_t id;
            result = nmo_chunk_read_object_id(chunk, &id);
            if (result.code != NMO_OK) return result;
            *(nmo_object_id_t*)out_ptr = id;
            break;
        }
        
        case NMO_TYPE_ENUM: {
            /* Read underlying integer */
            return read_value(type->element_type, chunk, arena, out_ptr);
        }
        
        default:
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Unknown type kind in schema"));
    }
    
    return nmo_result_ok();
}

/* Read single field */
static nmo_result_t read_field(
    const nmo_schema_field_t *field,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *struct_base)
{
    void *field_ptr = (char*)struct_base + field->offset;
    return read_value(field->type, chunk, arena, field_ptr);
}

/* Write single value based on type kind */
static nmo_result_t write_value(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* Check for vtable fast path */
    if (type->vtable != NULL && type->vtable->write != NULL) {
        return type->vtable->write(type, chunk, in_ptr, arena);
    }
    
    switch (type->kind) {
        case NMO_TYPE_U8:
        case NMO_TYPE_I8:
            result = nmo_chunk_write_byte(chunk, *(const uint8_t*)in_ptr);
            break;
        
        case NMO_TYPE_U16:
        case NMO_TYPE_I16:
            result = nmo_chunk_write_word(chunk, *(const uint16_t*)in_ptr);
            break;
        
        case NMO_TYPE_U32:
        case NMO_TYPE_I32:
        case NMO_TYPE_BOOL:
            result = nmo_chunk_write_dword(chunk, *(const uint32_t*)in_ptr);
            break;
        
        case NMO_TYPE_F32:
            result = nmo_chunk_write_float(chunk, *(const float*)in_ptr);
            break;
        
        case NMO_TYPE_STRING: {
            const char *str = *(const char**)in_ptr;
            result = nmo_chunk_write_string(chunk, str);
            break;
        }
        
        case NMO_TYPE_STRUCT: {
            /* Write all fields */
            for (size_t i = 0; i < type->field_count; i++) {
                result = write_field(&type->fields[i], chunk, in_ptr, arena);
                if (result.code != NMO_OK) return result;
            }
            result = nmo_result_ok();
            break;
        }
        
        case NMO_TYPE_ARRAY: {
            /* Write count, then elements */
            const void *array = *(const void**)in_ptr;
            uint32_t count = *((const uint32_t*)in_ptr + 1);
            
            result = nmo_chunk_write_dword(chunk, count);
            if (result.code != NMO_OK) return result;
            
            if (count > 0 && array != NULL) {
                size_t elem_size = type->element_type->size;
                for (uint32_t i = 0; i < count; i++) {
                    const void *elem_ptr = (const char*)array + i * elem_size;
                    result = write_value(type->element_type, chunk, elem_ptr, arena);
                    if (result.code != NMO_OK) return result;
                }
            }
            result = nmo_result_ok();
            break;
        }
        
        case NMO_TYPE_FIXED_ARRAY: {
            /* Write fixed number of elements */
            size_t elem_size = type->element_type->size;
            for (size_t i = 0; i < type->array_length; i++) {
                const void *elem_ptr = (const char*)in_ptr + i * elem_size;
                result = write_value(type->element_type, chunk, elem_ptr, arena);
                if (result.code != NMO_OK) return result;
            }
            result = nmo_result_ok();
            break;
        }
        
        case NMO_TYPE_BINARY: {
            /* Write size, then bytes */
            const void *data = *(const void**)in_ptr;
            uint32_t size = *((const uint32_t*)in_ptr + 1);
            
            result = nmo_chunk_write_dword(chunk, size);
            if (result.code != NMO_OK) return result;
            
            if (size > 0 && data != NULL) {
                result = nmo_chunk_write_buffer(chunk, data, size);
                if (result.code != NMO_OK) return result;
            }
            result = nmo_result_ok();
            break;
        }
        
        case NMO_TYPE_RESOURCE_REF:
            result = nmo_chunk_write_object_id(chunk, *(const nmo_object_id_t*)in_ptr);
            break;
        
        case NMO_TYPE_ENUM:
            /* Write underlying integer */
            return write_value(type->element_type, chunk, in_ptr, arena);
        
        default:
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Unknown type kind in schema"));
    }
    
    return result;
}

/* Write single field */
static nmo_result_t write_field(
    const nmo_schema_field_t *field,
    nmo_chunk_t *chunk,
    const void *struct_base,
    nmo_arena_t *arena)
{
    const void *field_ptr = (const char*)struct_base + field->offset;
    return write_value(field->type, chunk, field_ptr, arena);
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

nmo_result_t nmo_schema_read_struct(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_struct)
{
    if (type == NULL || chunk == NULL || arena == NULL || out_struct == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument to nmo_schema_read_struct"));
    }
    
    if (type->kind != NMO_TYPE_STRUCT) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not a struct"));
    }
    
    /* Zero the output */
    memset(out_struct, 0, type->size);
    
    /* Use vtable if available */
    if (type->vtable != NULL && type->vtable->read != NULL) {
        return type->vtable->read(type, chunk, arena, out_struct);
    }
    
    /* Generic reflection path */
    return read_value(type, chunk, arena, out_struct);
}

nmo_result_t nmo_schema_write_struct(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_struct,
    nmo_arena_t *arena)
{
    if (type == NULL || chunk == NULL || in_struct == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument to nmo_schema_write_struct"));
    }
    
    if (type->kind != NMO_TYPE_STRUCT) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not a struct"));
    }
    
    /* Use vtable if available */
    if (type->vtable != NULL && type->vtable->write != NULL) {
        return type->vtable->write(type, chunk, in_struct, arena);
    }
    
    /* Generic reflection path */
    return write_value(type, chunk, in_struct, arena);
}

nmo_result_t nmo_schema_validate(
    const nmo_schema_type_t *type,
    const void *data,
    nmo_arena_t *arena)
{
    if (type == NULL || data == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument to nmo_schema_validate"));
    }
    
    /* Use vtable if available */
    if (type->vtable != NULL && type->vtable->validate != NULL) {
        return type->vtable->validate(type, data, arena);
    }
    
    /* Basic validation: check enum values */
    if (type->kind == NMO_TYPE_ENUM) {
        int32_t value = *(const int32_t*)data;
        for (size_t i = 0; i < type->enum_value_count; i++) {
            if (type->enum_values[i].value == value) {
                return nmo_result_ok();
            }
        }
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_WARNING, "Invalid enum value"));
    }
    
    /* TODO: Add more validation (array bounds, null checks, etc.) */
    
    return nmo_result_ok();
}
