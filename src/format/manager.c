/**
 * @file manager.c
 * @brief Object manager interface implementation
 */

#include "format/nmo_manager.h"

/**
 * Create manager
 */
nmo_manager_t* nmo_manager_create(uint32_t manager_id, const nmo_manager_callbacks_t* callbacks) {
    (void)manager_id;
    (void)callbacks;
    return NULL;
}

/**
 * Destroy manager
 */
void nmo_manager_destroy(nmo_manager_t* manager) {
    (void)manager;
}

/**
 * Initialize manager
 */
nmo_result_t nmo_manager_init(nmo_manager_t* manager) {
    (void)manager;
    return nmo_result_ok();
}

/**
 * Get manager ID
 */
uint32_t nmo_manager_get_id(const nmo_manager_t* manager) {
    (void)manager;
    return 0;
}

/**
 * Read object from manager
 */
ssize_t nmo_manager_read(nmo_manager_t* manager, uint32_t object_id, void* buffer, size_t size) {
    (void)manager;
    (void)object_id;
    (void)buffer;
    (void)size;
    return -1;
}

/**
 * Write object to manager
 */
ssize_t nmo_manager_write(nmo_manager_t* manager, uint32_t object_id, const void* buffer, size_t size) {
    (void)manager;
    (void)object_id;
    (void)buffer;
    (void)size;
    return -1;
}
