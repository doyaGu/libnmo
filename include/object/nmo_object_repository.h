/**
 * @file nmo_object_repository.h
 * @brief Object repository for storing and managing objects in memory
 */

#ifndef NMO_OBJECT_REPOSITORY_H
#define NMO_OBJECT_REPOSITORY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_allocator.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_index nmo_object_index_t;

/*
 * Repository identity and membership operations are Tier 1, while mutation
 * observer plumbing and scratch-result helpers remain advanced Tier 2 APIs.
 */
#define NMO_OBJECT_REPOSITORY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_OBJECT_REPOSITORY_IDENTITY_API_TIER NMO_API_TIER_STABLE_CONSUMER
#define NMO_OBJECT_REPOSITORY_MUTATION_OBSERVER_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_OBJECT_REPOSITORY_SCRATCH_RESULT_API_TIER NMO_API_TIER_ADVANCED_C

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

typedef enum nmo_object_repository_mutation_flags {
    NMO_OBJECT_REPOSITORY_MUTATION_MEMBERSHIP = 1u << 0,
    NMO_OBJECT_REPOSITORY_MUTATION_NAMES      = 1u << 1,
    NMO_OBJECT_REPOSITORY_MUTATION_TYPE_GUID  = 1u << 2,
    NMO_OBJECT_REPOSITORY_MUTATION_ALL        = (1u << 0) | (1u << 1) | (1u << 2)
} nmo_object_repository_mutation_flags_t;

typedef void (*nmo_object_repository_mutation_fn)(
    nmo_object_repository_t *repository,
    uint32_t flags,
    void *user_data);

/**
 * @brief Create object repository
 * @param allocator Allocator for repository-owned allocations (NULL for default)
 * @return Repository or NULL on error
 * @ownership owned
 */
NMO_API nmo_object_repository_t *nmo_object_repository_create(const nmo_allocator_t *allocator);

/**
 * @brief Destroy object repository
 * @param repository Repository to destroy
 */
NMO_API void nmo_object_repository_destroy(nmo_object_repository_t *repository);

/**
 * Intern an unresolved serialized object ID as a repository-unique runtime
 * token. Tokens never alias real repository object IDs.
 */
NMO_API nmo_status_t nmo_object_repository_intern_unresolved_ref(
    nmo_object_repository_t *repository,
    nmo_object_id_t raw_id,
    nmo_object_id_t *out_token);

/** Resolve an unresolved-reference token back to its serialized raw ID. */
NMO_API bool nmo_object_repository_get_unresolved_ref_raw(
    const nmo_object_repository_t *repository,
    nmo_object_id_t token,
    nmo_object_id_t *out_raw_id);

/**
 * @brief Attach or detach an object index for incremental maintenance
 * @param repository Repository
 * @param index Object index to notify (NULL to detach)
 */
NMO_API void nmo_object_repository_set_index(
    nmo_object_repository_t *repository,
    nmo_object_index_t *index);

/**
 * @brief Add a mutation observer for retained external caches
 * @param repository Repository
 * @param observer Observer callback
 * @param user_data User data passed to observer
 * @note Observers must not add or remove mutation observers from inside the
 *       callback.
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_repository_add_mutation_observer(
    nmo_object_repository_t *repository,
    nmo_object_repository_mutation_fn observer,
    void *user_data);

/**
 * @brief Remove a previously added mutation observer
 * @param repository Repository
 * @param observer Observer callback
 * @param user_data Observer user data
 */
NMO_API void nmo_object_repository_remove_mutation_observer(
    nmo_object_repository_t *repository,
    nmo_object_repository_mutation_fn observer,
    void *user_data);

/**
 * @brief Add object to repository
 * @param repository Repository
 * @param object In/out object pointer (move ownership into repository)
 * @note On success, repository takes ownership and sets *object = NULL.
 *       On failure, *object is unchanged and caller retains ownership.
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_repository_add(nmo_object_repository_t *repository, nmo_object_t **object);

/**
 * @brief Find object by ID
 * @ownership borrowed (owned by repository)
 * @return Object or NULL if not found
 */
NMO_API nmo_object_t *nmo_object_repository_find_by_id(const nmo_object_repository_t *repository,
                                                       nmo_object_id_t id);

/**
 * @brief Find object by name
 * @ownership borrowed (owned by repository)
 * @return Object or NULL if not found
 */
NMO_API nmo_object_t *nmo_object_repository_find_by_name(const nmo_object_repository_t *repository,
                                                         const char *name);

/**
 * @brief Find object by file ID (original CK_ID stored in file)
 * @ownership borrowed (owned by repository)
 * @return Object or NULL if not found or file_id is 0
 */
NMO_API nmo_object_t *nmo_object_repository_find_by_file_id(const nmo_object_repository_t *repository,
                                                             nmo_object_id_t file_id);

/**
 * @brief Find objects by class
 * @param repository Repository
 * @param class_id Class ID
 * @param out_count Output count of found objects
 * @return Array of objects or NULL (caller must not free).
 *         The returned array is owned by the repository and is valid until the
 *         next call to nmo_object_repository_find_by_class(), any repository
 *         membership mutation, or repository destruction.
 *         Ordinary consumers should prefer nmo_object_iter_count_class() /
 *         nmo_object_iter_at_class() instead of depending on this scratch array.
 * @note Returned array is repository-owned; do not free.
 * @ownership borrowed (repository scratch storage; invalidated after next class query or mutation)
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
 *         next call to nmo_object_repository_get_all(), any repository
 *         membership mutation, or repository destruction.
 *         Returns NULL on error or if repository is empty.
 *         Ordinary consumers should prefer nmo_object_iter_count() /
 *         nmo_object_iter_at() instead of depending on this scratch array.
 * @note Returned array is repository-owned; do not free.
 * @ownership borrowed (repository scratch storage; invalidated after next get_all or mutation)
 */
NMO_API nmo_object_t **nmo_object_repository_get_all(nmo_object_repository_t *repository,
                                                     size_t *out_count);

/**
 * @brief Get object by index
 * @ownership borrowed (owned by repository)
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
NMO_API nmo_status_t nmo_object_repository_remove(nmo_object_repository_t *repository, nmo_object_id_t id);

/**
 * @brief Remove object from repository without destroying it
 * @param repository Repository
 * @param id Object ID
 * @param out_object Output pointer receiving removed object (caller owns on success)
 * @note On success, object is detached from repository/index/name tables and
 *       ownership transfers to caller. Caller must eventually destroy it.
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_repository_take(nmo_object_repository_t *repository,
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
NMO_API nmo_status_t nmo_object_repository_rename(nmo_object_repository_t *repository,
                                                  nmo_object_id_t id,
                                                  const char *new_name);

/**
 * @brief Set the type GUID of an object already in the repository
 *
 * Safely updates attached indexes and notifies mutation observers. Use this
 * for repository-owned objects instead of nmo_object_set_type_guid().
 *
 * @param repository Repository
 * @param id         Object ID
 * @param type_guid  New type GUID
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_repository_set_type_guid(nmo_object_repository_t *repository,
                                                        nmo_object_id_t id,
                                                        nmo_guid_t type_guid);

/**
 * @brief Clear all objects
 * @param repository Repository
 * @note All contained objects are destroyed.
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_repository_clear(nmo_object_repository_t *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_REPOSITORY_H */
