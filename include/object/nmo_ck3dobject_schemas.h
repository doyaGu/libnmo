/**
 * @file nmo_ck3dobject_schemas.h
 * @brief CK3dObject schema definitions header
 */

#ifndef NMO_CK3DOBJECT_SCHEMAS_H
#define NMO_CK3DOBJECT_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/**
 * @brief CK3dObject state structure
 * 
 * Represents the deserialized state of a CK3dObject. In CKRenderEngine,
 * CK3dObject does not add any serialized fields beyond CK3dEntity.
 */
typedef struct nmo_ck3dobject_state {
    nmo_ck3dentity_state_t entity;  ///< Parent CK3dEntity state
} nmo_ck3dobject_state_t;

NMO_API nmo_status_t nmo_ck3dobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ck3dobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ck3dobject_vtable, nmo_register_ck3dobject_type)

NMO_API nmo_status_t nmo_ck3dobject_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK3DOBJECT_SCHEMAS_H */
