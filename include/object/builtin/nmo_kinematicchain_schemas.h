/**
 * @file nmo_kinematicchain_schemas.h
 * @brief CKKinematicChain schema definitions
 */

#ifndef NMO_CKKINEMATICCHAIN_SCHEMAS_H
#define NMO_CKKINEMATICCHAIN_SCHEMAS_H

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKKinematicChain state
 */
typedef struct nmo_kinematicchain_state {
    nmo_object_state_t base;

    uint8_t has_chain_data;
    nmo_object_id_t start_effector_id;
    nmo_object_id_t end_effector_id;
} nmo_kinematicchain_state_t;

NMO_API nmo_status_t nmo_kinematicchain_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_kinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_kinematicchain_vtable, nmo_register_kinematicchain_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKKINEMATICCHAIN_SCHEMAS_H */
