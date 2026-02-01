/**
 * @file nmo_ckprogressivemesh_schemas.h
 * @brief CKProgressiveMesh schema definitions
 */

#ifndef NMO_CKPROGRESSIVEMESH_SCHEMAS_H
#define NMO_CKPROGRESSIVEMESH_SCHEMAS_H

#include "object/nmo_ckmesh_schemas.h"
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
 * @brief CKProgressiveMesh state
 */
typedef struct nmo_ckprogressivemesh_state {
    nmo_ck_mesh_state_t base;
} nmo_ckprogressivemesh_state_t;

NMO_API nmo_result_t nmo_register_ckprogressivemesh_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckprogressivemesh_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckprogressivemesh_state_t *out_state);

NMO_API nmo_result_t nmo_ckprogressivemesh_serialize(
    const nmo_ckprogressivemesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPROGRESSIVEMESH_SCHEMAS_H */
