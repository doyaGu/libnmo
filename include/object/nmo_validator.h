/**
 * @file nmo_validator.h
 * @brief Schema validation API for libnmo
 *
 * This file provides functions for validating data against schemas,
 * checking constraints, and verifying data integrity.
 */

#ifndef NMO_VALIDATOR_H
#define NMO_VALIDATOR_H

#include "nmo_schema.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_validator nmo_validator_t;
typedef struct nmo_validation_result nmo_validation_result_t;

/**
 * @brief Validation severity levels
 */
typedef enum nmo_validation_severity {
    NMO_VALIDATION_INFO = 0,     /**< Informational message */
    NMO_VALIDATION_WARNING,      /**< Warning - data may be questionable */
    NMO_VALIDATION_ERROR         /**< Error - data is invalid */
} nmo_validation_severity_t;

/**
 * @brief Validation result entry
 */
typedef struct nmo_validation_issue {
    nmo_validation_severity_t severity;  /**< Issue severity */
    const char *message;                 /**< Error message */
    const char *field_path;              /**< Path to field with issue */
    uint32_t line;                       /**< Source line (if applicable) */
} nmo_validation_issue_t;

/**
 * @brief Validation result
 */
struct nmo_validation_result {
    bool is_valid;                       /**< Overall validation status */
    nmo_validation_issue_t *issues;     /**< Array of issues */
    size_t issue_count;                  /**< Number of issues */
    nmo_arena_t *arena;                  /**< Arena for allocations */
};

/**
 * @brief Create a validator instance
 * @param arena Arena for allocations
 * @return Validator instance or NULL on failure
 */
nmo_validator_t *nmo_validator_create(nmo_arena_t *arena);

/**
 * @brief Validate data against a schema
 * @param validator Validator instance
 * @param data Data to validate
 * @param type Schema type
 * @param result Validation result (output)
 * @return Result
 */
nmo_result_t nmo_validator_validate(nmo_validator_t *validator,
                                   const void *data,
                                   const nmo_schema_type_t *type,
                                   nmo_validation_result_t *result);

/**
 * @brief Free validation result resources
 * @param result Validation result
 */
void nmo_validation_result_free(nmo_validation_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VALIDATOR_H */
