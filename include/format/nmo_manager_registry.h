/**
 * @file nmo_manager_registry.h
 * @brief Manager registry for managing object managers
 */

#ifndef NMO_MANAGER_REGISTRY_H
#define NMO_MANAGER_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Manager registry
 */
typedef struct nmo_manager_registry nmo_manager_registry_t;

/**
 * Create manager registry
 * @return Registry or NULL on error
 */
NMO_API nmo_manager_registry_t* nmo_manager_registry_create(void);

/**
 * Destroy manager registry
 * @param registry Registry to destroy
 */
NMO_API void nmo_manager_registry_destroy(nmo_manager_registry_t* registry);

/**
 * Register manager
 * @param registry Registry
 * @param manager_id Manager ID
 * @param manager Manager instance
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_manager_registry_register(
    nmo_manager_registry_t* registry, uint32_t manager_id, void* manager);

/**
 * Unregister manager
 * @param registry Registry
 * @param manager_id Manager ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_manager_registry_unregister(nmo_manager_registry_t* registry, uint32_t manager_id);

/**
 * Get manager by ID
 * @param registry Registry
 * @param manager_id Manager ID
 * @return Manager or NULL if not found
 */
NMO_API void* nmo_manager_registry_get(const nmo_manager_registry_t* registry, uint32_t manager_id);

/**
 * Check if manager is registered
 * @param registry Registry
 * @param manager_id Manager ID
 * @return 1 if registered, 0 otherwise
 */
NMO_API int nmo_manager_registry_contains(const nmo_manager_registry_t* registry, uint32_t manager_id);

/**
 * Get registered manager count
 * @param registry Registry
 * @return Number of registered managers
 */
NMO_API uint32_t nmo_manager_registry_get_count(const nmo_manager_registry_t* registry);

/**
 * Get manager ID at index
 * @param registry Registry
 * @param index Index
 * @return Manager ID or 0 if index out of bounds
 */
NMO_API uint32_t nmo_manager_registry_get_id_at(const nmo_manager_registry_t* registry, uint32_t index);

/**
 * Clear all managers
 * @param registry Registry
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_manager_registry_clear(nmo_manager_registry_t* registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MANAGER_REGISTRY_H */
