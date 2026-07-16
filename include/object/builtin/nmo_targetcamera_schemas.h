/**
 * @file nmo_targetcamera_schemas.h
 * @brief CKTargetCamera schema definitions
 */

#ifndef NMO_CKTARGETCAMERA_SCHEMAS_H
#define NMO_CKTARGETCAMERA_SCHEMAS_H

#include "object/builtin/nmo_camera_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ref.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKTargetCamera state
 */
typedef struct nmo_targetcamera_state {
    nmo_camera_state_t base;
    uint8_t has_target;
    nmo_ref_t target;
} nmo_targetcamera_state_t;

NMO_API nmo_status_t nmo_targetcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_targetcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_targetcamera_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_targetcamera_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_targetcamera_vtable, nmo_register_targetcamera_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETCAMERA_SCHEMAS_H */
