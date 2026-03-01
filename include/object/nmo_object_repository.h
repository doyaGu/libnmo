/**
 * @file nmo_object_repository.h
 * @brief Object repository for storing and managing objects in memory
 */

#ifndef NMO_OBJECT_REPOSITORY_H
#define NMO_OBJECT_REPOSITORY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_index nmo_object_index_t;

/**
 * @brief Object repository
 */
/* OWNERSHIP:
 * - owner: caller
 * - allocator: repository allocator (heap)
 * - lifetime: until nmo_object_repository_destroy()
 * - free: nmo_object_repository_destroy()
 * - thread: caller-synchronized
 */
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * @brief Create object repository
 * @param allocator Allocator for repository-owned allocations (NULL for default)
 * @return Repository or NULL on error
 */
NMO_API nmo_object_repository_t *nmo_object_repository_create(const nmo_allocator_t *allocator);

/**
 * @brief Destroy object repository
 * @param repository Repository to destroy
 */
NMO_API void nmo_object_repository_destroy(nmo_object_repository_t *repository);

/**
 * @brief Attach or detach an object index for incremental maintenance
 * @param repository Repository
 * @param index Object index to notify (NULL to detach)
 */
NMO_API void nmo_object_repository_set_index(
    nmo_object_repository_t *repository,
    nmo_object_index_t *index);

/**
 * @brief Add object to repository
 * @param repository Repository
 * @param object In/out object pointer (move ownership into repository)
 * @note On success, repository takes ownership and sets *object = NULL.
 *       On failure, *object is unchanged and caller retains ownership.
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_add(nmo_object_repository_t *repository, nmo_object_t **object);

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
 * @return Array of objects or NULL (caller must not free).
 *         The returned array is owned by the repository and is valid until the
 *         next call to nmo_object_repository_find_by_class() or repository destruction.
 * @note Returned array is repository-owned; do not free.
 */
NMO_API nmo_object_t **nmo_object_repository_find_by_class(nmo_object_repository_t *repository,
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
 *
 * @note This function returns NULL for both empty repositories (count=0) and
 * allocation failures. To distinguish between these cases, check the count:
 * - *out_count == 0: Empty repository (success)
 * - *out_count == 0 and repository has objects: Allocation failure
 * For production use, consider checking nmo_object_repository_get_count() first.
 *
 * @param repository Repository
 * @param out_count Output count (always set, even on error)
 * @return Array of objects (caller must not free).
 *         The returned array is owned by the repository and is valid until the
 *         next call to nmo_object_repository_get_all() or repository destruction.
 *         Returns NULL on error or if repository is empty.
 * @note Returned array is repository-owned; do not free.
 */
NMO_API nmo_object_t **nmo_object_repository_get_all(nmo_object_repository_t *repository,
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
 * @note Removed object is destroyed by the repository.
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_remove(nmo_object_repository_t *repository, nmo_object_id_t id);

/**
 * @brief Remove object from repository without destroying it
 * @param repository Repository
 * @param id Object ID
 * @param out_object Output pointer receiving removed object (caller owns on success)
 * @note On success, object is detached from repository/index/name tables and
 *       ownership transfers to caller. Caller must eventually destroy it.
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_take(nmo_object_repository_t *repository,
                                       nmo_object_id_t id,
                                       nmo_object_t **out_object);

/**
 * @brief Rename an object that is already in the repository
 *
 * Safely updates the name table so the old key is removed before the
 * object's name pointer is freed, then re-inserts with the new name.
 *
 * @warning Do NOT call nmo_object_set_name() directly on repository-owned
 *          objects — that frees the name pointer used as a hash key, causing
 *          a use-after-free in the name table.  Use this function instead.
 *
 * @param repository Repository
 * @param id         Object ID
 * @param new_name   New name (NULL to clear name; empty string removes from table)
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_rename(nmo_object_repository_t *repository,
                                         nmo_object_id_t id,
                                         const char *new_name);

/**
 * @brief Clear all objects
 * @param repository Repository
 * @note All contained objects are destroyed.
 * @return NMO_OK on success
 */
NMO_API int nmo_object_repository_clear(nmo_object_repository_t *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_REPOSITORY_H */
