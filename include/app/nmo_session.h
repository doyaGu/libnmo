/**
 * @file nmo_session.h
 * @brief High-level session API for loading and saving NMO files
 */

#ifndef NMO_SESSION_H
#define NMO_SESSION_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Session context
 */
typedef struct nmo_session nmo_session_t;

/**
 * @brief Session mode
 */
typedef enum {
    NMO_SESSION_READ = 0,
    NMO_SESSION_WRITE,
    NMO_SESSION_READ_WRITE,
} nmo_session_mode_t;

/**
 * @brief Session options
 */
typedef struct {
    nmo_session_mode_t mode;    /* Session mode */
    int load_all_objects;       /* Load all objects on session start */
    int verify_checksums;       /* Verify checksums */
    int enable_compression;     /* Enable compression */
    int enable_validation;      /* Enable validation */
} nmo_session_options_t;

/**
 * Create session
 * @param file_path File path to load/save
 * @param mode Session mode
 * @return Session or NULL on error
 */
NMO_API nmo_session_t* nmo_session_create(const char* file_path, nmo_session_mode_t mode);

/**
 * Create session with options
 * @param file_path File path to load/save
 * @param options Session options
 * @return Session or NULL on error
 */
NMO_API nmo_session_t* nmo_session_create_with_options(const char* file_path, const nmo_session_options_t* options);

/**
 * Destroy session
 * @param session Session to destroy
 */
NMO_API void nmo_session_destroy(nmo_session_t* session);

/**
 * Open session (for read/existing file)
 * @param session Session
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_session_open(nmo_session_t* session);

/**
 * Create new session (for write)
 * @param session Session
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_session_create_new(nmo_session_t* session);

/**
 * Close session
 * @param session Session
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_session_close(nmo_session_t* session);

/**
 * Get object count
 * @param session Session
 * @return Number of objects
 */
NMO_API uint32_t nmo_session_get_object_count(const nmo_session_t* session);

/**
 * Load object
 * @param session Session
 * @param object_id Object ID
 * @param out_data Output object data (caller must free)
 * @param out_size Output object size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_session_load_object(
    nmo_session_t* session, uint32_t object_id, void** out_data, size_t* out_size);

/**
 * Save object
 * @param session Session
 * @param object_id Object ID
 * @param manager_id Manager ID
 * @param data Object data
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_session_save_object(
    nmo_session_t* session, uint32_t object_id, uint32_t manager_id, const void* data, size_t size);

/**
 * Get session error
 * @param session Session
 * @return Error message or NULL
 */
NMO_API const char* nmo_session_get_error(const nmo_session_t* session);

/**
 * Check if session is active
 * @param session Session
 * @return 1 if active, 0 otherwise
 */
NMO_API int nmo_session_is_active(const nmo_session_t* session);

/**
 * Get session mode
 * @param session Session
 * @return Session mode
 */
NMO_API nmo_session_mode_t nmo_session_get_mode(const nmo_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
