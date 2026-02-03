/**
 * @file nmo_cktargetlight_schemas.h
 * @brief CKTargetLight schema definitions
 */

#ifndef NMO_CKTARGETLIGHT_SCHEMAS_H
#define NMO_CKTARGETLIGHT_SCHEMAS_H

#include "object/nmo_cklight_schemas.h"
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
 * @brief CKTargetLight state
 */
typedef struct nmo_cktargetlight_state {
    nmo_cklight_state_t base;
    uint8_t has_target;
    nmo_object_id_t target_id;
} nmo_cktargetlight_state_t;

NMO_API nmo_status_t nmo_cktargetlight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_cktargetlight_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cktargetlight_vtable, nmo_register_cktargetlight_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETLIGHT_SCHEMAS_H */
