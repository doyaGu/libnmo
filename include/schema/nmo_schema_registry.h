/**
 * @file nmo_schema_registry.h
 * @brief Schema registry for managing schemas
 */

#ifndef NMO_SCHEMA_REGISTRY_H
#define NMO_SCHEMA_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "schema/nmo_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Schema registry
 */
typedef struct nmo_schema_registry nmo_schema_registry;

/**
 * @brief Create schema registry
 * @return Registry or NULL on error
 */
NMO_API nmo_schema_registry* nmo_schema_registry_create(void);

/**
 * @brief Destroy schema registry
 * @param registry Registry to destroy
 */
NMO_API void nmo_schema_registry_destroy(nmo_schema_registry* registry);

/**
 * @brief Register schema
 * @param registry Registry
 * @param schema Schema descriptor
 * @return NMO_OK on success
 */
NMO_API int nmo_schema_registry_add(nmo_schema_registry* registry,
                                      const nmo_schema_descriptor* schema);

/**
 * @brief Register all built-in schemas
 * @param registry Registry
 * @return NMO_OK on success
 */
NMO_API int nmo_schema_registry_add_builtin(nmo_schema_registry* registry);

/**
 * @brief Find schema by class ID
 * @param registry Registry
 * @param class_id Class ID
 * @return Schema descriptor or NULL if not found
 */
NMO_API const nmo_schema_descriptor* nmo_schema_registry_find_by_id(
    const nmo_schema_registry* registry, nmo_class_id class_id);

/**
 * @brief Find schema by class name
 * @param registry Registry
 * @param class_name Class name
 * @return Schema descriptor or NULL if not found
 */
NMO_API const nmo_schema_descriptor* nmo_schema_registry_find_by_name(
    const nmo_schema_registry* registry, const char* class_name);

/**
 * @brief Get number of registered schemas
 * @param registry Registry
 * @return Schema count
 */
NMO_API size_t nmo_schema_registry_get_count(const nmo_schema_registry* registry);

/**
 * @brief Verify schema consistency
 * @param registry Registry
 * @return NMO_OK if all schemas are consistent
 */
NMO_API int nmo_verify_schema_consistency(nmo_schema_registry* registry);

/**
 * @brief Clear all schemas from registry
 * @param registry Registry
 * @return NMO_OK on success
 */
NMO_API int nmo_schema_registry_clear(nmo_schema_registry* registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_REGISTRY_H */
