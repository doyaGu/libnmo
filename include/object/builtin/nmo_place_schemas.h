/**
 * @file nmo_place_schemas.h
 * @brief CKPlace schema definitions
 */

#ifndef NMO_CKPLACE_SCHEMAS_H
#define NMO_CKPLACE_SCHEMAS_H

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_struct_defs.h"
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
 * @brief CKPlace state
 */
typedef struct nmo_place_state {
    nmo_3dentity_state_t base;

    uint8_t has_camera;
    nmo_object_id_t camera_id;

    uint8_t has_level;
    nmo_object_id_t level_id;

    nmo_array_t portals;       /**< Portal entries (nmo_place_portal_entry_t) */
    nmo_array_t reference_ids; /**< Reference IDs (nmo_object_id_t) */
} nmo_place_state_t;

NMO_API nmo_status_t nmo_place_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_place_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_place_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_place_vtable, nmo_register_place_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPLACE_SCHEMAS_H */
