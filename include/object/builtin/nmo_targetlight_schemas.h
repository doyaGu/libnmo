/**
 * @file nmo_targetlight_schemas.h
 * @brief CKTargetLight schema definitions
 */

#ifndef NMO_CKTARGETLIGHT_SCHEMAS_H
#define NMO_CKTARGETLIGHT_SCHEMAS_H

#include "object/builtin/nmo_light_schemas.h"
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
typedef struct nmo_targetlight_state {
    nmo_light_state_t base;
    uint8_t has_target;
    nmo_object_id_t target_id;
} nmo_targetlight_state_t;

NMO_API nmo_status_t nmo_targetlight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_targetlight_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_targetlight_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_targetlight_vtable, nmo_register_targetlight_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTARGETLIGHT_SCHEMAS_H */
