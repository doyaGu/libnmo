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

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry;
typedef struct nmo_object nmo_object;

/**
 * @brief Validation modes
 */
typedef enum {
    NMO_VALIDATION_STRICT,      /**< Strict validation */
    NMO_VALIDATION_PERMISSIVE,  /**< Permissive validation */
} nmo_validation_mode;

/**
 * @brief Validation results
 */
typedef enum {
    NMO_VALID,                  /**< Data is valid */
    NMO_VALID_WITH_WARNINGS,    /**< Data is valid but has warnings */
    NMO_INVALID,                /**< Data is invalid */
} nmo_validation_result;

/**
 * @brief Validator context
 */
typedef struct nmo_validation nmo_validation;

/**
 * @brief Create validator
 * @param registry Schema registry to use
 * @param mode Validation mode
 * @return Validator or NULL on error
 */
NMO_API nmo_validation* nmo_validation_create(nmo_schema_registry* registry,
                                                nmo_validation_mode mode);

/**
 * @brief Destroy validator
 * @param validation Validator to destroy
 */
NMO_API void nmo_validation_destroy(nmo_validation* validation);

/**
 * @brief Validate object against schema
 * @param validation Validator
 * @param obj Object to validate
 * @return Validation result
 */
NMO_API nmo_validation_result nmo_validate_object(nmo_validation* validation, nmo_object* obj);

/**
 * @brief Validate file
 * @param validation Validator
 * @param path File path
 * @return Validation result
 */
NMO_API nmo_validation_result nmo_validate_file(nmo_validation* validation, const char* path);

/**
 * @brief Get last validation error message
 * @param validation Validator
 * @return Error message or NULL
 */
NMO_API const char* nmo_validation_get_error(const nmo_validation* validation);

/**
 * @brief Set validation mode
 * @param validation Validator
 * @param mode Validation mode
 * @return NMO_OK on success
 */
NMO_API int nmo_validation_set_mode(nmo_validation* validation, nmo_validation_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VALIDATOR_H */
