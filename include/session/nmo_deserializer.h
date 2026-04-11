/**
 * @file nmo_deserializer.h
 * @brief Deserializer for managing object ID remapping during file load
 */

#ifndef NMO_DESERIALIZER_H
#define NMO_DESERIALIZER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;

/**
 * @brief Deserializer for tracking file object index to runtime ID mapping
 */
/* OWNERSHIP:
 * - owner: caller
 * - allocator: internal (heap)
 * - lifetime: until nmo_deserializer_destroy()
 * - free: nmo_deserializer_destroy()
 * - thread: caller-synchronized
 */
typedef struct nmo_deserializer nmo_deserializer_t;

/**
 * @brief Start load session
 *
 * Begins a load session that tracks object index remapping from file object table
 * indices (SaveFindObjectIndex, 0-based) to runtime IDs. Runtime IDs may differ
 * if objects already exist in the repository.
 *
 * @param repo Object repository
 * @param max_saved_id Maximum object ID from the file being loaded
 * @return Load session or NULL on error
 * @note Returned session is caller-owned; destroy with nmo_deserializer_destroy().
 * @ownership owned
 */
NMO_API nmo_deserializer_t *nmo_deserializer_start(nmo_object_repository_t *repo,
                                                 nmo_object_id_t max_saved_id);

/**
 * @brief Register object with file object index
 *
 * Registers an object that was loaded from a file, mapping its file object table
 * index (SaveFindObjectIndex) to its runtime ID. This mapping is used later to
 * remap object references in chunks.
 *
 * @param session Load session
 * @param obj Object to register
 * @param file_id File object table index (0-based)
 * @return NMO_OK on success
 */
NMO_API int nmo_deserializer_register(nmo_deserializer_t *session,
                                      nmo_object_t *obj,
                                      nmo_object_id_t file_index);

/**
 * @brief End load session
 *
 * Completes the load session. After this, the ID mappings are finalized
 * and can be used to build an ID remap table.
 *
 * @param session Load session
 * @return NMO_OK on success
 */
NMO_API int nmo_deserializer_end(nmo_deserializer_t *session);

/**
 * @brief Lookup runtime ID for a file object table index.
 *
 * Queries the mapping created via nmo_deserializer_register().
 *
 * @param session Load session
 * @param file_index File object table index (0-based)
 * @param out_runtime_id Output runtime ID
 * @return NMO_OK if found, NMO_ERR_NOT_FOUND if no mapping exists
 */
NMO_API int nmo_deserializer_get_runtime_id(const nmo_deserializer_t *session,
                                           nmo_object_id_t file_index,
                                           nmo_object_id_t *out_runtime_id);

/**
 * @brief Get object repository
 *
 * @param session Load session
 * @return Object repository
 * @note Returned pointer is session-owned; do not free.
 * @ownership borrowed
 */
NMO_API nmo_object_repository_t *nmo_deserializer_get_repository(
    const nmo_deserializer_t *session);

/**
 * @brief Get ID base
 *
 * Returns the base ID that was allocated for this load session.
 * File IDs are offset by this base to get runtime IDs.
 *
 * @param session Load session
 * @return ID base
 */
NMO_API nmo_object_id_t nmo_deserializer_get_id_base(const nmo_deserializer_t *session);

/**
 * @brief Get max saved ID
 *
 * @param session Load session
 * @return Maximum object ID from file
 */
NMO_API nmo_object_id_t nmo_deserializer_get_max_saved_id(const nmo_deserializer_t *session);

/**
 * @brief Destroy load session
 *
 * @param session Load session to destroy
 */
NMO_API void nmo_deserializer_destroy(nmo_deserializer_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DESERIALIZER_H */
