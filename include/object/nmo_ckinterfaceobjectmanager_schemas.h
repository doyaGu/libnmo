/**
 * @file nmo_ckinterfaceobjectmanager_schemas.h
 * @brief CKInterfaceObjectManager schema definitions
 */

#ifndef NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H
#define NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKInterfaceObjectManager state
 */
typedef struct nmo_ckinterfaceobjectmanager_state {
    nmo_ckobject_state_t base;

    int32_t chunk_count;
    nmo_chunk_t **chunks;

    nmo_guid_t guid;
} nmo_ckinterfaceobjectmanager_state_t;

NMO_API nmo_status_t nmo_ckinterfaceobjectmanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckinterfaceobjectmanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckinterfaceobjectmanager_vtable, nmo_register_ckinterfaceobjectmanager_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKINTERFACEOBJECTMANAGER_SCHEMAS_H */
