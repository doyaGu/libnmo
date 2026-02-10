/**
 * @file nmo_renderobject_schemas.h
 * @brief Public API for CKRenderObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKRenderObject.
 * CKRenderObject is an abstract base class for renderable objects (2D and 3D entities).
 * 
 * Based on official Virtools SDK (reference/include/CKRenderObject.h):
 * - CKRenderObject is an ABSTRACT class (all methods are pure virtual = 0)
 * - It does NOT override Load/Save - inherits CKBeObject's serialization
 * - No additional data is serialized beyond CKBeObject (scripts/priority/attributes)
 * - Runtime rendering state (callbacks, Z-order) is managed by derived classes
 */

#ifndef NMO_CKRENDEROBJECT_SCHEMAS_H
#define NMO_CKRENDEROBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKRenderObject STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKRenderObject state
 * 
 * CKRenderObject is an abstract base class with no serialized data beyond CKBeObject.
 * This structure is intentionally minimal - all actual data comes from CKBeObject parent.
 * 
 * Runtime data (render callbacks, Z-order, render context membership) is NOT serialized
 * and is managed by concrete derived classes (CK2dEntity, CK3dEntity, etc.)
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class, no Load/Save)
 */
typedef struct nmo_renderobject_state {
    nmo_beobject_state_t base;
} nmo_renderobject_state_t;

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Deserialize CKRenderObject from chunk (public API)
 * 
 * @param chunk Chunk containing CKRenderObject data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_renderobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Serialize CKRenderObject to chunk (public API)
 * 
 * @param chunk Chunk to write to
 * @param state State to serialize
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_renderobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_renderobject_vtable, nmo_register_renderobject_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKRENDEROBJECT_SCHEMAS_H */
