/**
 * @file nmo_cklayer_schemas.h
 * @brief CKLayer schema definitions
 */

#ifndef NMO_CKLAYER_SCHEMAS_H
#define NMO_CKLAYER_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

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

NMO_API nmo_result_t nmo_cklayer_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_cklayer_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cklayer_vtable, nmo_register_cklayer_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLAYER_SCHEMAS_H */
