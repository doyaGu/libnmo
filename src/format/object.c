/**
 * @file object.c
 * @brief Object metadata and handling implementation
 */

#include "format/nmo_object.h"

/**
 * Create object
 */
nmo_object_t* nmo_object_create(uint32_t id, uint32_t manager_id) {
    (void)id;
    (void)manager_id;
    return NULL;
}

/**
 * Destroy object
 */
void nmo_object_destroy(nmo_object_t* object) {
    (void)object;
}

/**
 * Get object properties
 */
nmo_result_t nmo_object_get_props(const nmo_object_t* object, nmo_object_props_t* out_props) {
    (void)object;
    if (out_props != NULL) {
        out_props->id = 0;
        out_props->manager_id = 0;
        out_props->flags = 0;
        out_props->data_offset = 0;
        out_props->data_size = 0;
    }
    return nmo_result_ok();
}

/**
 * Set object properties
 */
nmo_result_t nmo_object_set_props(nmo_object_t* object, const nmo_object_props_t* props) {
    (void)object;
    (void)props;
    return nmo_result_ok();
}

/**
 * Get object ID
 */
uint32_t nmo_object_get_id(const nmo_object_t* object) {
    (void)object;
    return 0;
}

/**
 * Get manager ID
 */
uint32_t nmo_object_get_manager_id(const nmo_object_t* object) {
    (void)object;
    return 0;
}

/**
 * Get object data size
 */
uint32_t nmo_object_get_data_size(const nmo_object_t* object) {
    (void)object;
    return 0;
}

/**
 * Set object data size
 */
nmo_result_t nmo_object_set_data_size(nmo_object_t* object, uint32_t size) {
    (void)object;
    (void)size;
    return nmo_result_ok();
}

/**
 * Get object data offset
 */
uint64_t nmo_object_get_data_offset(const nmo_object_t* object) {
    (void)object;
    return 0;
}

/**
 * Set object data offset
 */
nmo_result_t nmo_object_set_data_offset(nmo_object_t* object, uint64_t offset) {
    (void)object;
    (void)offset;
    return nmo_result_ok();
}
