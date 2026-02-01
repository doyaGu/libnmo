/**
 * @file nmo_ckplace_schemas.h
 * @brief CKPlace schema definitions
 */

#ifndef NMO_CKPLACE_SCHEMAS_H
#define NMO_CKPLACE_SCHEMAS_H

#include "object/nmo_ckbeobject_schemas.h"
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
 * @brief CKPlace portal entry
 */
typedef struct nmo_ckplace_portal_entry {
    nmo_object_id_t place_id;
    nmo_object_id_t portal_id;
} nmo_ckplace_portal_entry_t;

/**
 * @brief CKPlace state
 */
typedef struct nmo_ckplace_state {
    nmo_ckbeobject_state_t base;

    uint8_t has_camera;
    nmo_object_id_t camera_id;

    uint8_t has_level;
    nmo_object_id_t level_id;

    uint32_t portal_count;
    nmo_ckplace_portal_entry_t *portals;

    uint32_t reference_count;
    nmo_object_id_t *reference_ids;
} nmo_ckplace_state_t;

NMO_API nmo_result_t nmo_register_ckplace_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckplace_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckplace_state_t *out_state);

NMO_API nmo_result_t nmo_ckplace_serialize(
    const nmo_ckplace_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPLACE_SCHEMAS_H */
