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

/* Forward declarations */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Object repository
 */
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * @brief Create object repository
 * @param arena Arena for allocations
 * @return Repository or NULL on error
 */
NMO_API nmo_object_repository_t *nmo_object_repository_create(nmo_arena_t *arena);

/**
 * @brief Destroy object repository
 * @param repository Repository to destroy
 */
NMO_API void nmo_object_repository_destroy(nmo_object_repository_t *repository);

/**
 * @brief Add object to repository
 * @param repository Repository
 * @param object Object to add
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_add(nmo_object_repository_t *repository, nmo_object_t *object);

/**
 * @brief Find object by ID
 * @param repository Repository
 * @param id Object ID
 * @return Object or NULL if not found
 */
NMO_API nmo_object_t *nmo_object_repository_find_by_id(const nmo_object_repository_t *repository,
                                                       nmo_object_id_t id);

/**
 * @brief Find object by name
 * @param repository Repository
 * @param name Object name
 * @return Object or NULL if not found
 */
NMO_API nmo_object_t *nmo_object_repository_find_by_name(const nmo_object_repository_t *repository,
                                                         const char *name);

/**
 * @brief Find objects by class
 * @param repository Repository
 * @param class_id Class ID
 * @param out_count Output count of found objects
 * @return Array of objects or NULL (caller must not free)
 */
NMO_API nmo_object_t **nmo_object_repository_find_by_class(const nmo_object_repository_t *repository,
                                                           nmo_class_id_t class_id,
                                                           size_t *out_count);

/**
 * @brief Get object count
 * @param repository Repository
 * @return Number of objects
 */
NMO_API size_t nmo_object_repository_get_count(const nmo_object_repository_t *repository);

/**
 * @brief Get all objects
 * @param repository Repository
 * @param out_count Output count
 * @return Array of objects (caller must not free)
 */
NMO_API nmo_object_t **nmo_object_repository_get_all(const nmo_object_repository_t *repository,
                                                     size_t *out_count);

/**
 * @brief Get object by index
 * @param repository Repository
 * @param index Object index
 * @return Object or NULL if index out of bounds
 */
NMO_API nmo_object_t *nmo_object_repository_get_by_index(const nmo_object_repository_t *repository,
                                                         size_t index);

/**
 * @brief Remove object from repository
 * @param repository Repository
 * @param id Object ID
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_remove(nmo_object_repository_t *repository, nmo_object_id_t id);

/**
 * @brief Clear all objects
 * @param repository Repository
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_clear(nmo_object_repository_t *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_REPOSITORY_H */
