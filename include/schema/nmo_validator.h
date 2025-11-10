/**
 * @file nmo_validator.h
 * @brief Schema validator for data validation
 */

#ifndef NMO_VALIDATOR_H
#define NMO_VALIDATOR_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validator context
 */
typedef struct nmo_validator nmo_validator_t;

/**
 * @brief Validation result
 */
typedef struct {
    int is_valid;            /* 1 if valid, 0 if invalid */
    const char* error_msg;   /* Error message if invalid */
    uint32_t error_field;    /* Field index of error (if applicable) */
} nmo_validation_result_t;

/**
 * Create validator
 * @param schema_registry Schema registry to use
 * @return Validator or NULL on error
 */
NMO_API nmo_validator_t* nmo_validator_create(void* schema_registry);

/**
 * Destroy validator
 * @param validator Validator to destroy
 */
NMO_API void nmo_validator_destroy(nmo_validator_t* validator);

/**
 * Validate data
 * @param validator Validator
 * @param schema_id Schema ID to validate against
 * @param data Data to validate
 * @param data_size Data size
 * @param out_result Output validation result
 * @return NMO_OK if validation check succeeded (may still be invalid)
 */
NMO_API nmo_result_t nmo_validator_validate(
    nmo_validator_t* validator, uint32_t schema_id, const void* data, size_t data_size,
    nmo_validation_result_t* out_result);

/**
 * Validate data structure
 * @param validator Validator
 * @param schema_id Schema ID
 * @param data Data pointer
 * @return 1 if valid, 0 if invalid
 */
NMO_API int nmo_validator_is_valid(
    nmo_validator_t* validator, uint32_t schema_id, const void* data);

/**
 * Get validation error
 * @param validator Validator
 * @return Last validation error message
 */
NMO_API const char* nmo_validator_get_error(const nmo_validator_t* validator);

/**
 * Clear cached validation results
 * @param validator Validator
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_validator_clear(nmo_validator_t* validator);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VALIDATOR_H */
