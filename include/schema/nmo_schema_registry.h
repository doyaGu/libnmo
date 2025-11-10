/**
 * @file nmo_schema_registry.h
 * @brief Schema registry for managing schemas
 */

#ifndef NMO_SCHEMA_REGISTRY_H
#define NMO_SCHEMA_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Schema registry
 */
typedef struct nmo_schema_registry nmo_schema_registry_t;

/**
 * Create schema registry
 * @return Registry or NULL on error
 */
NMO_API nmo_schema_registry_t* nmo_schema_registry_create(void);

/**
 * Destroy schema registry
 * @param registry Registry to destroy
 */
NMO_API void nmo_schema_registry_destroy(nmo_schema_registry_t* registry);

/**
 * Register schema
 * @param registry Registry
 * @param schema_id Schema ID
 * @param schema Schema instance
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_registry_register(
    nmo_schema_registry_t* registry, uint32_t schema_id, void* schema);

/**
 * Unregister schema
 * @param registry Registry
 * @param schema_id Schema ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_registry_unregister(nmo_schema_registry_t* registry, uint32_t schema_id);

/**
 * Get schema by ID
 * @param registry Registry
 * @param schema_id Schema ID
 * @return Schema or NULL if not found
 */
NMO_API void* nmo_schema_registry_get(const nmo_schema_registry_t* registry, uint32_t schema_id);

/**
 * Check if schema is registered
 * @param registry Registry
 * @param schema_id Schema ID
 * @return 1 if registered, 0 otherwise
 */
NMO_API int nmo_schema_registry_contains(const nmo_schema_registry_t* registry, uint32_t schema_id);

/**
 * Get registered schema count
 * @param registry Registry
 * @return Number of registered schemas
 */
NMO_API uint32_t nmo_schema_registry_get_count(const nmo_schema_registry_t* registry);

/**
 * Get schema ID at index
 * @param registry Registry
 * @param index Index
 * @return Schema ID or 0 if index out of bounds
 */
NMO_API uint32_t nmo_schema_registry_get_id_at(const nmo_schema_registry_t* registry, uint32_t index);

/**
 * Clear all schemas
 * @param registry Registry
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_registry_clear(nmo_schema_registry_t* registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_REGISTRY_H */
