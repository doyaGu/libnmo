/**
 * @file nmo_header1.h
 * @brief NMO Header1 (object descriptors)
 */

#ifndef NMO_HEADER1_H
#define NMO_HEADER1_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Header1 context (contains object descriptors)
 */
typedef struct nmo_header1 nmo_header1_t;

/**
 * @brief Object descriptor
 */
typedef struct {
    uint32_t id;             /* Object ID */
    uint32_t manager_id;     /* Manager ID */
    uint32_t object_flags;   /* Object flags */
    uint64_t data_offset;    /* Offset in file */
    uint32_t data_size;      /* Size of object data */
} nmo_object_descriptor_t;

/**
 * Create Header1 context
 * @return Header1 context or NULL on error
 */
NMO_API nmo_header1_t* nmo_header1_create(void);

/**
 * Destroy Header1 context
 * @param header1 Header1 context
 */
NMO_API void nmo_header1_destroy(nmo_header1_t* header1);

/**
 * Parse Header1 from IO
 * @param header1 Header1 context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header1_parse(nmo_header1_t* header1, void* io);

/**
 * Write Header1 to IO
 * @param header1 Header1 context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header1_write(const nmo_header1_t* header1, void* io);

/**
 * Add object descriptor
 * @param header1 Header1 context
 * @param descriptor Object descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header1_add_descriptor(nmo_header1_t* header1, const nmo_object_descriptor_t* descriptor);

/**
 * Get object descriptor count
 * @param header1 Header1 context
 * @return Number of descriptors
 */
NMO_API uint32_t nmo_header1_get_descriptor_count(const nmo_header1_t* header1);

/**
 * Get object descriptor by index
 * @param header1 Header1 context
 * @param index Descriptor index
 * @param out_descriptor Output descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header1_get_descriptor(
    const nmo_header1_t* header1, uint32_t index, nmo_object_descriptor_t* out_descriptor);

/**
 * Get object descriptor by ID
 * @param header1 Header1 context
 * @param object_id Object ID
 * @param out_descriptor Output descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header1_get_descriptor_by_id(
    const nmo_header1_t* header1, uint32_t object_id, nmo_object_descriptor_t* out_descriptor);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEADER1_H */
