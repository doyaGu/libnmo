/**
 * @file nmo_grid_schemas.h
 * @brief CKGrid schema definitions
 */

#ifndef NMO_CKGRID_SCHEMAS_H
#define NMO_CKGRID_SCHEMAS_H

#include "object/builtin/nmo_3dentity_schemas.h"
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
 * @brief CKGrid state
 */
typedef struct nmo_grid_state {
    nmo_3dentity_state_t base;

    int32_t width;
    int32_t length;
    int32_t priority;
    uint32_t orientation_mode;

    uint8_t has_grid_data;
    uint8_t has_file_flag;
    int32_t file_flag;

    nmo_array_t layer_ids;    /**< Layer IDs (nmo_object_id_t) */
    nmo_array_t layer_chunks; /**< Layer chunks (nmo_chunk_t *) */
} nmo_grid_state_t;

NMO_API nmo_status_t nmo_grid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_grid_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_grid_vtable, nmo_register_grid_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKGRID_SCHEMAS_H */
