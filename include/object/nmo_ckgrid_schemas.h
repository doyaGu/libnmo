/**
 * @file nmo_ckgrid_schemas.h
 * @brief CKGrid schema definitions
 */

#ifndef NMO_CKGRID_SCHEMAS_H
#define NMO_CKGRID_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
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
 * @brief CKGrid state
 */
typedef struct nmo_ckgrid_state {
    nmo_ck3dentity_state_t base;

    int32_t width;
    int32_t length;
    int32_t priority;
    uint32_t orientation_mode;

    uint8_t has_file_flag;
    int32_t file_flag;

    nmo_object_id_t *layer_ids;
    uint32_t layer_count;

    uint32_t layer_chunk_count;
    nmo_chunk_t **layer_chunks;
} nmo_ckgrid_state_t;

NMO_API nmo_result_t nmo_register_ckgrid_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckgrid_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckgrid_state_t *out_state);

NMO_API nmo_result_t nmo_ckgrid_serialize(
    const nmo_ckgrid_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKGRID_SCHEMAS_H */
