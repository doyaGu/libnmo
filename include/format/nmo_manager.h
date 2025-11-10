/**
 * @file nmo_manager.h
 * @brief Object manager interface
 */

#ifndef NMO_MANAGER_H
#define NMO_MANAGER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Object manager
 */
typedef struct nmo_manager nmo_manager_t;

/**
 * @brief Manager callbacks
 */
typedef struct {
    /**
     * Initialize manager
     * @param manager Manager instance
     * @return NMO_OK on success
     */
    nmo_result_t (*init)(nmo_manager_t* manager);

    /**
     * Cleanup manager
     * @param manager Manager instance
     */
    void (*cleanup)(nmo_manager_t* manager);

    /**
     * Read object
     * @param manager Manager instance
     * @param object_id Object ID
     * @param buffer Buffer to read into
     * @param size Buffer size
     * @return Number of bytes read or negative on error
     */
    ssize_t (*read)(nmo_manager_t* manager, uint32_t object_id, void* buffer, size_t size);

    /**
     * Write object
     * @param manager Manager instance
     * @param object_id Object ID
     * @param buffer Data to write
     * @param size Data size
     * @return Number of bytes written or negative on error
     */
    ssize_t (*write)(nmo_manager_t* manager, uint32_t object_id, const void* buffer, size_t size);
} nmo_manager_callbacks_t;

/**
 * Create manager
 * @param manager_id Manager ID
 * @param callbacks Manager callbacks
 * @return Manager or NULL on error
 */
NMO_API nmo_manager_t* nmo_manager_create(uint32_t manager_id, const nmo_manager_callbacks_t* callbacks);

/**
 * Destroy manager
 * @param manager Manager to destroy
 */
NMO_API void nmo_manager_destroy(nmo_manager_t* manager);

/**
 * Initialize manager
 * @param manager Manager
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_manager_init(nmo_manager_t* manager);

/**
 * Get manager ID
 * @param manager Manager
 * @return Manager ID
 */
NMO_API uint32_t nmo_manager_get_id(const nmo_manager_t* manager);

/**
 * Read object from manager
 * @param manager Manager
 * @param object_id Object ID
 * @param buffer Buffer to read into
 * @param size Buffer size
 * @return Number of bytes read or negative on error
 */
NMO_API ssize_t nmo_manager_read(nmo_manager_t* manager, uint32_t object_id, void* buffer, size_t size);

/**
 * Write object to manager
 * @param manager Manager
 * @param object_id Object ID
 * @param buffer Data to write
 * @param size Data size
 * @return Number of bytes written or negative on error
 */
NMO_API ssize_t nmo_manager_write(nmo_manager_t* manager, uint32_t object_id, const void* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MANAGER_H */
