/**
 * @file load_session.c
 * @brief Load session for parsing NMO files implementation
 */

#include "session/nmo_load_session.h"

/**
 * Create load session
 */
nmo_load_session_t* nmo_load_session_create(const char* file_path) {
    (void)file_path;
    return NULL;
}

/**
 * Create load session with config
 */
nmo_load_session_t* nmo_load_session_create_with_config(
    const char* file_path, const nmo_load_session_config_t* config) {
    (void)file_path;
    (void)config;
    return NULL;
}

/**
 * Destroy load session
 */
void nmo_load_session_destroy(nmo_load_session_t* session) {
    (void)session;
}

/**
 * Start load session
 */
nmo_result_t nmo_load_session_start(nmo_load_session_t* session) {
    (void)session;
    return nmo_result_ok();
}

/**
 * Get object count in file
 */
uint32_t nmo_load_session_get_object_count(const nmo_load_session_t* session) {
    (void)session;
    return 0;
}

/**
 * Load object
 */
nmo_result_t nmo_load_session_load_object(
    nmo_load_session_t* session, uint32_t object_id, void** out_data, size_t* out_size) {
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
 * Get loaded object repository
 */
void* nmo_load_session_get_repository(const nmo_load_session_t* session) {
    (void)session;
    return NULL;
}

/**
 * Get object list from file
 */
nmo_result_t nmo_load_session_get_object_list(
    nmo_load_session_t* session, uint32_t** out_ids, uint32_t* out_count) {
    (void)session;
    if (out_ids != NULL) {
        *out_ids = NULL;
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    return nmo_result_ok();
}

/**
 * Finish load session
 */
nmo_result_t nmo_load_session_finish(nmo_load_session_t* session) {
    (void)session;
    return nmo_result_ok();
}
