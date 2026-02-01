/**
 * @file nmo_ckinterfaceobjectmanager_schemas.h
 * @brief CKInterfaceObjectManager schema definitions
 */

#ifndef NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H
#define NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
#include "core/nmo_guid.h"
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
 * @brief CKInterfaceObjectManager state
 */
typedef struct nmo_ckinterfaceobjectmanager_state {
    nmo_ckobject_state_t base;

    int32_t chunk_count;
    nmo_chunk_t **chunks;

    nmo_guid_t guid;
} nmo_ckinterfaceobjectmanager_state_t;

NMO_API nmo_result_t nmo_register_ckinterfaceobjectmanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckinterfaceobjectmanager_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckinterfaceobjectmanager_state_t *out_state);

NMO_API nmo_result_t nmo_ckinterfaceobjectmanager_serialize(
    const nmo_ckinterfaceobjectmanager_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H */
