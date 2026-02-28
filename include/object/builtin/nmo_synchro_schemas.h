/**
 * @file nmo_synchro_schemas.h
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#ifndef NMO_CKSYNCHRO_SCHEMAS_H
#define NMO_CKSYNCHRO_SCHEMAS_H

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_array.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKSynchroObject state
 */
typedef struct nmo_synchro_state {
    nmo_object_state_t base;
    int32_t max_waiters;
    nmo_array_t arrived_ids;  /**< Arrived IDs (nmo_object_id_t) */
    nmo_array_t passed_ids;   /**< Passed IDs (nmo_object_id_t) */
} nmo_synchro_state_t;

/**
 * @brief CKStateObject state
 */
typedef struct nmo_state_state {
    nmo_object_state_t base;
    int32_t event_flag;
} nmo_state_state_t;

/**
 * @brief CKCriticalSectionObject state
 */
typedef struct nmo_criticalsection_state {
    nmo_object_state_t base;
    nmo_object_id_t object_in_section_id;
} nmo_criticalsection_state_t;

NMO_API nmo_status_t nmo_synchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_synchro_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_synchro_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_API nmo_status_t nmo_state_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_state_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_criticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_criticalsection_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_synchro_vtable, nmo_register_synchro_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_state_vtable, nmo_register_state_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_criticalsection_vtable, nmo_register_criticalsection_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSYNCHRO_SCHEMAS_H */
