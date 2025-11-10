/**
 * @file nmo_load_session.h
 * @brief Load session for parsing NMO files
 */

#ifndef NMO_LOAD_SESSION_H
#define NMO_LOAD_SESSION_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load session context
 */
typedef struct nmo_load_session nmo_load_session_t;

/**
 * @brief Load session configuration
 */
typedef struct {
    int load_all_objects;    /* Load all objects or on-demand */
    int verify_checksums;    /* Verify checksums during load */
    size_t buffer_size;      /* IO buffer size */
    int decompress;          /* Decompress compressed data */
} nmo_load_session_config_t;

/**
 * Create load session
 * @param file_path Path to NMO file
 * @return Load session or NULL on error
 */
NMO_API nmo_load_session_t* nmo_load_session_create(const char* file_path);

/**
 * Create load session with config
 * @param file_path Path to NMO file
 * @param config Load session configuration
 * @return Load session or NULL on error
 */
NMO_API nmo_load_session_t* nmo_load_session_create_with_config(
    const char* file_path, const nmo_load_session_config_t* config);

/**
 * Destroy load session
 * @param session Load session to destroy
 */
NMO_API void nmo_load_session_destroy(nmo_load_session_t* session);

/**
 * Start load session
 * @param session Load session
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_load_session_start(nmo_load_session_t* session);

/**
 * Get object count in file
 * @param session Load session
 * @return Number of objects
 */
NMO_API uint32_t nmo_load_session_get_object_count(const nmo_load_session_t* session);

/**
 * Load object
 * @param session Load session
 * @param object_id Object ID to load
 * @param out_data Output object data
 * @param out_size Output object size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_load_session_load_object(
    nmo_load_session_t* session, uint32_t object_id, void** out_data, size_t* out_size);

/**
 * Get loaded object repository
 * @param session Load session
 * @return Object repository
 */
NMO_API void* nmo_load_session_get_repository(const nmo_load_session_t* session);

/**
 * Get object list from file
 * @param session Load session
 * @param out_ids Output object ID array (caller must free)
 * @param out_count Output count
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_load_session_get_object_list(
    nmo_load_session_t* session, uint32_t** out_ids, uint32_t* out_count);

/**
 * Finish load session
 * @param session Load session
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_load_session_finish(nmo_load_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LOAD_SESSION_H */
