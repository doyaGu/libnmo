/**
 * @file nmo_cklayer_schemas.h
 * @brief CKLayer schema definitions
 */

#ifndef NMO_CKLAYER_SCHEMAS_H
#define NMO_CKLAYER_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief CKLayer state
 */
typedef struct nmo_cklayer_state {
    nmo_ckobject_state_t base;

    nmo_object_id_t grid_id;
    int32_t type;
    int32_t format;
    int32_t version;
    uint32_t color_rgba;
    nmo_guid_t param_guid;
    uint32_t flags;

    uint8_t has_type;
    uint8_t has_version;
    uint8_t has_color;
    uint8_t has_param_guid;

    void *square_data;
    size_t square_data_size;
} nmo_cklayer_state_t;

NMO_API nmo_result_t nmo_register_cklayer_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_cklayer_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklayer_state_t *out_state);

NMO_API nmo_result_t nmo_cklayer_serialize(
    const nmo_cklayer_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLAYER_SCHEMAS_H */
