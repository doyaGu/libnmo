/**
 * @file validator.c
 * @brief Schema validator for data validation implementation
 */

#include "schema/nmo_validator.h"

/**
 * Create validator
 */
nmo_validator_t* nmo_validator_create(void* schema_registry) {
    (void)schema_registry;
    return NULL;
}

/**
 * Destroy validator
 */
void nmo_validator_destroy(nmo_validator_t* validator) {
    (void)validator;
}

/**
 * Validate data
 */
nmo_result_t nmo_validator_validate(
    nmo_validator_t* validator, uint32_t schema_id, const void* data, size_t data_size,
    nmo_validation_result_t* out_result) {
    (void)validator;
    (void)schema_id;
    (void)data;
    (void)data_size;
    if (out_result != NULL) {
        out_result->is_valid = 1;
        out_result->error_msg = NULL;
        out_result->error_field = 0;
    }
    return nmo_result_ok();
}

/**
 * Validate data structure
 */
int nmo_validator_is_valid(
    nmo_validator_t* validator, uint32_t schema_id, const void* data) {
    (void)validator;
    (void)schema_id;
    (void)data;
    return 1;
}

/**
 * Get validation error
 */
const char* nmo_validator_get_error(const nmo_validator_t* validator) {
    (void)validator;
    return NULL;
}

/**
 * Clear cached validation results
 */
nmo_result_t nmo_validator_clear(nmo_validator_t* validator) {
    (void)validator;
    return nmo_result_ok();
}
