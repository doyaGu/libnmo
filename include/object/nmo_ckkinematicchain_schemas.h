/**
 * @file nmo_ckkinematicchain_schemas.h
 * @brief CKKinematicChain schema definitions
 */

#ifndef NMO_CKKINEMATICCHAIN_SCHEMAS_H
#define NMO_CKKINEMATICCHAIN_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
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
 * @brief CKKinematicChain state
 */
typedef struct nmo_ckkinematicchain_state {
    nmo_ckobject_state_t base;

    uint8_t has_chain_data;
    nmo_object_id_t start_effector_id;
    nmo_object_id_t end_effector_id;
} nmo_ckkinematicchain_state_t;

NMO_API nmo_result_t nmo_ckkinematicchain_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckkinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckkinematicchain_vtable, nmo_register_ckkinematicchain_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKKINEMATICCHAIN_SCHEMAS_H */
