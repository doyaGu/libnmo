/**
 * @file nmo_cktargetcamera_schemas.h
 * @brief CKTargetCamera schema definitions
 */

#ifndef NMO_CKTARGETCAMERA_SCHEMAS_H
#define NMO_CKTARGETCAMERA_SCHEMAS_H

#include "object/nmo_ckcamera_schemas.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief CKTargetCamera state
 */
typedef struct nmo_cktargetcamera_state {
    nmo_ckcamera_state_t base;
    uint8_t has_target;
    nmo_object_id_t target_id;
} nmo_cktargetcamera_state_t;

NMO_API nmo_result_t nmo_register_cktargetcamera_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_cktargetcamera_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktargetcamera_state_t *out_state);

NMO_API nmo_result_t nmo_cktargetcamera_serialize(
    const nmo_cktargetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETCAMERA_SCHEMAS_H */
