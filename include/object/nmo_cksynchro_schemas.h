/**
 * @file nmo_cksynchro_schemas.h
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#ifndef NMO_CKSYNCHRO_SCHEMAS_H
#define NMO_CKSYNCHRO_SCHEMAS_H

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

NMO_API nmo_result_t nmo_register_cksynchro_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_cksynchro_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksynchro_state_t *out_state);

NMO_API nmo_result_t nmo_cksynchro_serialize(
    const nmo_cksynchro_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckstate_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckstate_state_t *out_state);

NMO_API nmo_result_t nmo_ckstate_serialize(
    const nmo_ckstate_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckcriticalsection_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcriticalsection_state_t *out_state);

NMO_API nmo_result_t nmo_ckcriticalsection_serialize(
    const nmo_ckcriticalsection_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSYNCHRO_SCHEMAS_H */
