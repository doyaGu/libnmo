/**
 * @file nmo_cktargetcamera_schemas.h
 * @brief CKTargetCamera schema definitions
 */

#ifndef NMO_CKTARGETCAMERA_SCHEMAS_H
#define NMO_CKTARGETCAMERA_SCHEMAS_H

#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/**
 * @brief CKTargetCamera state
 */
typedef struct nmo_cktargetcamera_state {
    nmo_ckcamera_state_t base;
    uint8_t has_target;
    nmo_object_id_t target_id;
} nmo_cktargetcamera_state_t;

NMO_API nmo_result_t nmo_cktargetcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_cktargetcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cktargetcamera_vtable, nmo_register_cktargetcamera_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETCAMERA_SCHEMAS_H */
