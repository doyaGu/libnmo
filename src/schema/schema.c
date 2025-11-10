/**
 * @file schema.c
 * @brief Schema descriptors for object structure validation implementation
 */

#include "schema/nmo_schema.h"
#include <string.h>
#include <stddef.h>

/**
 * Get field type name
 */
const char* nmo_field_type_name(nmo_field_type type) {
    switch (type) {
        case NMO_FIELD_INT8:       return "int8";
        case NMO_FIELD_UINT8:      return "uint8";
        case NMO_FIELD_INT16:      return "int16";
        case NMO_FIELD_UINT16:     return "uint16";
        case NMO_FIELD_INT32:      return "int32";
        case NMO_FIELD_UINT32:     return "uint32";
        case NMO_FIELD_FLOAT:      return "float";
        case NMO_FIELD_STRING:     return "string";
        case NMO_FIELD_GUID:       return "guid";
        case NMO_FIELD_OBJECT_ID:  return "object_id";
        case NMO_FIELD_STRUCT:     return "struct";
        case NMO_FIELD_ARRAY:      return "array";
        default:                   return "unknown";
    }
}

/**
 * Get field type size
 */
size_t nmo_field_type_size(nmo_field_type type) {
    switch (type) {
        case NMO_FIELD_INT8:
        case NMO_FIELD_UINT8:
            return 1;
        case NMO_FIELD_INT16:
        case NMO_FIELD_UINT16:
            return 2;
        case NMO_FIELD_INT32:
        case NMO_FIELD_UINT32:
        case NMO_FIELD_FLOAT:
        case NMO_FIELD_OBJECT_ID:
            return 4;
        case NMO_FIELD_GUID:
            return 8;  // Two 32-bit values
        case NMO_FIELD_STRING:
        case NMO_FIELD_STRUCT:
        case NMO_FIELD_ARRAY:
            return 0;  // Variable size
        default:
            return 0;
    }
}

/**
 * Validate field descriptor
 */
int nmo_field_descriptor_validate(const nmo_field_descriptor* field) {
    if (field == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Name is required
    if (field->name == NULL || field->name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check valid type
    if (field->type > NMO_FIELD_ARRAY) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // For fixed-size types, size should match type size
    size_t expected_size = nmo_field_type_size(field->type);
    if (expected_size > 0 && field->size != expected_size) {
        // Allow mismatch for arrays
        if (field->type != NMO_FIELD_ARRAY && field->count <= 1) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    // Arrays must have count > 0
    if (field->type == NMO_FIELD_ARRAY && field->count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Structs must have a class_id
    if (field->type == NMO_FIELD_STRUCT && field->class_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return NMO_OK;
}

/**
 * Validate schema descriptor
 */
int nmo_schema_descriptor_validate(const nmo_schema_descriptor* schema) {
    if (schema == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Class ID is required
    if (schema->class_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Class name is required
    if (schema->class_name == NULL || schema->class_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // If we have fields, validate them
    if (schema->field_count > 0) {
        if (schema->fields == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        for (size_t i = 0; i < schema->field_count; i++) {
            int result = nmo_field_descriptor_validate(&schema->fields[i]);
            if (result != NMO_OK) {
                return result;
            }
        }
    }

    return NMO_OK;
}
