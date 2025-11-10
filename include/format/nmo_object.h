/**
 * @file nmo_object.h
 * @brief Object metadata and handling
 */

#ifndef NMO_OBJECT_H
#define NMO_OBJECT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NMO object metadata
 */
typedef struct nmo_object nmo_object_t;

/**
 * @brief Object properties
 */
typedef struct {
    uint32_t id;             /* Object ID */
    uint32_t manager_id;     /* Manager ID */
    uint32_t flags;          /* Object flags */
    uint64_t data_offset;    /* Offset in file */
    uint32_t data_size;      /* Size of data */
} nmo_object_props_t;

/**
 * Create object
 * @param id Object ID
 * @param manager_id Manager ID
 * @return Object or NULL on error
 */
NMO_API nmo_object_t* nmo_object_create(uint32_t id, uint32_t manager_id);

/**
 * Destroy object
 * @param object Object to destroy
 */
NMO_API void nmo_object_destroy(nmo_object_t* object);

/**
 * Get object properties
 * @param object Object
 * @param out_props Output properties
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_get_props(const nmo_object_t* object, nmo_object_props_t* out_props);

/**
 * Set object properties
 * @param object Object
 * @param props Properties to set
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_set_props(nmo_object_t* object, const nmo_object_props_t* props);

/**
 * Get object ID
 * @param object Object
 * @return Object ID
 */
NMO_API uint32_t nmo_object_get_id(const nmo_object_t* object);

/**
 * Get manager ID
 * @param object Object
 * @return Manager ID
 */
NMO_API uint32_t nmo_object_get_manager_id(const nmo_object_t* object);

/**
 * Get object data size
 * @param object Object
 * @return Data size in bytes
 */
NMO_API uint32_t nmo_object_get_data_size(const nmo_object_t* object);

/**
 * Set object data size
 * @param object Object
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_set_data_size(nmo_object_t* object, uint32_t size);

/**
 * Get object data offset
 * @param object Object
 * @return Data offset in file
 */
NMO_API uint64_t nmo_object_get_data_offset(const nmo_object_t* object);

/**
 * Set object data offset
 * @param object Object
 * @param offset Data offset
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_object_set_data_offset(nmo_object_t* object, uint64_t offset);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_H */
