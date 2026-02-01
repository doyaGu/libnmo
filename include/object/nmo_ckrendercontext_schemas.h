/**
 * @file nmo_ckrendercontext_schemas.h
 * @brief CKRenderContext schema definitions
 */

#ifndef NMO_CKRENDERCONTEXT_SCHEMAS_H
#define NMO_CKRENDERCONTEXT_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
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
 * @brief CKRenderContext state
 */
typedef struct nmo_ckrendercontext_state {
    nmo_ckobject_state_t base;
} nmo_ckrendercontext_state_t;

NMO_API nmo_result_t nmo_register_ckrendercontext_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckrendercontext_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckrendercontext_state_t *out_state);

NMO_API nmo_result_t nmo_ckrendercontext_serialize(
    const nmo_ckrendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKRENDERCONTEXT_SCHEMAS_H */
