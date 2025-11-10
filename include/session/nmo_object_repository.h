/**
 * @file nmo_object_repository.h
 * @brief Object repository for storing and managing objects in memory
 */

#ifndef NMO_OBJECT_REPOSITORY_H
#define NMO_OBJECT_REPOSITORY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Object repository
 */
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * Create object repository
 * @param initial_capacity Initial capacity
 * @return Repository or NULL on error
 */
NMO_API nmo_object_repository_t* nmo_object_repository_create(size_t initial_capacity);

/**
 * Destroy object repository
 * @param repository Repository to destroy
 */
NMO_API void nmo_object_repository_destroy(nmo_object_repository_t* repository);

/**
 * Store object in repository
 * @param repository Repository
 * @param object_id Object ID
 * @param data Object data
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_repository_store(
    nmo_object_repository_t* repository, uint32_t object_id, const void* data, size_t size);

/**
 * Retrieve object from repository
 * @param repository Repository
 * @param object_id Object ID
 * @param out_data Output data pointer
 * @param out_size Output data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_repository_get(
    nmo_object_repository_t* repository, uint32_t object_id, const void** out_data, size_t* out_size);

/**
 * Check if object exists in repository
 * @param repository Repository
 * @param object_id Object ID
 * @return 1 if exists, 0 otherwise
 */
NMO_API int nmo_object_repository_contains(nmo_object_repository_t* repository, uint32_t object_id);

/**
 * Remove object from repository
 * @param repository Repository
 * @param object_id Object ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_repository_remove(nmo_object_repository_t* repository, uint32_t object_id);

/**
 * Get object count
 * @param repository Repository
 * @return Number of objects
 */
NMO_API uint32_t nmo_object_repository_get_count(const nmo_object_repository_t* repository);

/**
 * Get object ID at index
 * @param repository Repository
 * @param index Index
 * @return Object ID or 0 if index out of bounds
 */
NMO_API uint32_t nmo_object_repository_get_id_at(const nmo_object_repository_t* repository, uint32_t index);

/**
 * Clear all objects
 * @param repository Repository
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_repository_clear(nmo_object_repository_t* repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_REPOSITORY_H */
