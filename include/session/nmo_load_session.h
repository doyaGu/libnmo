/**
 * @file nmo_load_session.h
 * @brief Load session for managing object ID remapping during file load
 */

#ifndef NMO_LOAD_SESSION_H
#define NMO_LOAD_SESSION_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;

/**
 * @brief Load session for tracking file ID to runtime ID mapping
 */
typedef struct nmo_load_session nmo_load_session_t;

/**
 * @brief Start load session
 *
 * Begins a load session that tracks object ID remapping from file IDs to runtime IDs.
 * Objects loaded from a file have sequential file IDs (0-based), but runtime IDs may
 * be different if objects already exist in the repository.
 *
 * @param repo Object repository
 * @param max_saved_id Maximum object ID from the file being loaded
 * @return Load session or NULL on error
 */
NMO_API nmo_load_session_t *nmo_load_session_start(nmo_object_repository_t *repo,
                                                 nmo_object_id_t max_saved_id);

/**
 * @brief Register object with file ID
 *
 * Registers an object that was loaded from a file, mapping its file ID to its
 * runtime ID. This mapping is used later to remap object references.
 *
 * @param session Load session
 * @param obj Object to register
 * @param file_id Original ID from the file
 * @return NMO_OK on success
 */
NMO_API int nmo_load_session_register(nmo_load_session_t *session,
                                      nmo_object_t *obj,
                                      nmo_object_id_t file_id);

/**
 * @brief End load session
 *
 * Completes the load session. After this, the ID mappings are finalized
 * and can be used to build an ID remap table.
 *
 * @param session Load session
 * @return NMO_OK on success
 */
NMO_API int nmo_load_session_end(nmo_load_session_t *session);

/**
 * @brief Get object repository
 *
 * @param session Load session
 * @return Object repository
 */
NMO_API nmo_object_repository_t *nmo_load_session_get_repository(
    const nmo_load_session_t *session);

/**
 * @brief Get ID base
 *
 * Returns the base ID that was allocated for this load session.
 * File IDs are offset by this base to get runtime IDs.
 *
 * @param session Load session
 * @return ID base
 */
NMO_API nmo_object_id_t nmo_load_session_get_id_base(const nmo_load_session_t *session);

/**
 * @brief Get max saved ID
 *
 * @param session Load session
 * @return Maximum object ID from file
 */
NMO_API nmo_object_id_t nmo_load_session_get_max_saved_id(const nmo_load_session_t *session);

/**
 * @brief Destroy load session
 *
 * @param session Load session to destroy
 */
NMO_API void nmo_load_session_destroy(nmo_load_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LOAD_SESSION_H */
