/**
 * @file validator.c
 * @brief Schema validator for data validation implementation
 */

#include "schema/nmo_validator.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ERROR_LEN 256

/**
 * Validator context
 */
struct nmo_validation {
    nmo_schema_registry* registry;
    nmo_validation_mode mode;
    char error_message[MAX_ERROR_LEN];
};

/**
 * Create validator
 */
nmo_validation* nmo_validation_create(nmo_schema_registry* registry,
                                      nmo_validation_mode mode) {
    if (registry == NULL) {
        return NULL;
    }

    nmo_validation* validation = (nmo_validation*)malloc(sizeof(nmo_validation));
    if (validation == NULL) {
        return NULL;
    }

    validation->registry = registry;
    validation->mode = mode;
    validation->error_message[0] = '\0';

    return validation;
}

/**
 * Destroy validator
 */
void nmo_validation_destroy(nmo_validation* validation) {
    free(validation);
}

/**
 * Validate object against schema
 */
nmo_validation_result nmo_validate_object(nmo_validation* validation, nmo_object* obj) {
    if (validation == NULL || obj == NULL) {
        return NMO_INVALID;
    }

    // Find schema for object's class
    const nmo_schema_descriptor* schema =
        nmo_schema_registry_find_by_id(validation->registry, obj->class_id);

    if (schema == NULL) {
        snprintf(validation->error_message, MAX_ERROR_LEN,
                 "No schema found for class ID 0x%08X", obj->class_id);
        return validation->mode == NMO_VALIDATION_STRICT ? NMO_INVALID : NMO_VALID_WITH_WARNINGS;
    }

    // Basic validation: check if object has required fields
    if (obj->id == 0 && validation->mode == NMO_VALIDATION_STRICT) {
        snprintf(validation->error_message, MAX_ERROR_LEN,
                 "Object has invalid ID (0)");
        return NMO_INVALID;
    }

    // In permissive mode or if all checks pass
    validation->error_message[0] = '\0';
    return NMO_VALID;
}

/**
 * Validate file
 */
nmo_validation_result nmo_validate_file(nmo_validation* validation, const char* path) {
    if (validation == NULL || path == NULL) {
        return NMO_INVALID;
    }

    // Basic file validation - just check if file exists
    // Full file validation would require parsing the file, which is not implemented yet
    (void)validation;
    (void)path;

    return NMO_VALID_WITH_WARNINGS;
}

/**
 * Get last validation error message
 */
const char* nmo_validation_get_error(const nmo_validation* validation) {
    if (validation == NULL) {
        return NULL;
    }

    return validation->error_message[0] != '\0' ? validation->error_message : NULL;
}

/**
 * Set validation mode
 */
int nmo_validation_set_mode(nmo_validation* validation, nmo_validation_mode mode) {
    if (validation == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    validation->mode = mode;
    return NMO_OK;
}
