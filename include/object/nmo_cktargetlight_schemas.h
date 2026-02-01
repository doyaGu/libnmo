/**
 * @file nmo_cktargetlight_schemas.h
 * @brief CKTargetLight schema definitions
 */

#ifndef NMO_CKTARGETLIGHT_SCHEMAS_H
#define NMO_CKTARGETLIGHT_SCHEMAS_H

#include "object/nmo_cklight_schemas.h"
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
 * @brief CKTargetLight state
 */
typedef struct nmo_cktargetlight_state {
    nmo_cklight_state_t base;
    uint8_t has_target;
    nmo_object_id_t target_id;
} nmo_cktargetlight_state_t;

NMO_API nmo_result_t nmo_register_cktargetlight_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_cktargetlight_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktargetlight_state_t *out_state);

NMO_API nmo_result_t nmo_cktargetlight_serialize(
    const nmo_cktargetlight_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETLIGHT_SCHEMAS_H */
