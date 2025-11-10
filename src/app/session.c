/**
 * @file session.c
 * @brief High-level session API for loading and saving NMO files implementation
 */

#include "app/nmo_session.h"

/**
 * Create session
 */
nmo_session_t* nmo_session_create(const char* file_path, nmo_session_mode_t mode) {
    (void)file_path;
    (void)mode;
    return NULL;
}

/**
 * Create session with options
 */
nmo_session_t* nmo_session_create_with_options(const char* file_path, const nmo_session_options_t* options) {
    (void)file_path;
    (void)options;
    return NULL;
}

/**
 * Destroy session
 */
void nmo_session_destroy(nmo_session_t* session) {
    (void)session;
}

/**
 * Open session (for read/existing file)
 */
nmo_result_t nmo_session_open(nmo_session_t* session) {
    (void)session;
    return nmo_result_ok();
}

/**
 * Create new session (for write)
 */
nmo_result_t nmo_session_create_new(nmo_session_t* session) {
    (void)session;
    return nmo_result_ok();
}

/**
 * Close session
 */
nmo_result_t nmo_session_close(nmo_session_t* session) {
    (void)session;
    return nmo_result_ok();
}

/**
 * Get object count
 */
uint32_t nmo_session_get_object_count(const nmo_session_t* session) {
    (void)session;
    return 0;
}

/**
 * Load object
 */
nmo_result_t nmo_session_load_object(
    nmo_session_t* session, uint32_t object_id, void** out_data, size_t* out_size) {
    (void)session;
    (void)object_id;
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }
    return nmo_result_ok();
}

/**
 * Save object
 */
nmo_result_t nmo_session_save_object(
    nmo_session_t* session, uint32_t object_id, uint32_t manager_id, const void* data, size_t size) {
    (void)session;
    (void)object_id;
    (void)manager_id;
    (void)data;
    (void)size;
    return nmo_result_ok();
}

/**
 * Get session error
 */
const char* nmo_session_get_error(const nmo_session_t* session) {
    (void)session;
    return NULL;
}

/**
 * Check if session is active
 */
int nmo_session_is_active(const nmo_session_t* session) {
    (void)session;
    return 0;
}

/**
 * Get session mode
 */
nmo_session_mode_t nmo_session_get_mode(const nmo_session_t* session) {
    (void)session;
    return NMO_SESSION_READ;
}
