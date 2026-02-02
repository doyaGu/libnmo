/**
 * @file nmo_cksynchro_schemas.h
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#ifndef NMO_CKSYNCHRO_SCHEMAS_H
#define NMO_CKSYNCHRO_SCHEMAS_H

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
 * @brief CKSynchroObject state
 */
typedef struct nmo_cksynchro_state {
    nmo_ckobject_state_t base;
    int32_t max_waiters;
    nmo_object_id_t *arrived_ids;
    uint32_t arrived_count;
    nmo_object_id_t *passed_ids;
    uint32_t passed_count;
} nmo_cksynchro_state_t;

/**
 * @brief CKStateObject state
 */
typedef struct nmo_ckstate_state {
    nmo_ckobject_state_t base;
    int32_t event_flag;
} nmo_ckstate_state_t;

/**
 * @brief CKCriticalSectionObject state
 */
typedef struct nmo_ckcriticalsection_state {
    nmo_ckobject_state_t base;
    nmo_object_id_t object_in_section_id;
} nmo_ckcriticalsection_state_t;

NMO_API nmo_result_t nmo_cksynchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_cksynchro_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckstate_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckstate_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckcriticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckcriticalsection_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cksynchro_vtable, nmo_register_cksynchro_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_ckstate_vtable, nmo_register_ckstate_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_ckcriticalsection_vtable, nmo_register_ckcriticalsection_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSYNCHRO_SCHEMAS_H */
