/**
 * @file nmo_ckrenderobject_schemas.h
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

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

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
typedef struct nmo_ckrenderobject_state {
    /* No additional data beyond CKBeObject - this is an abstract base class */
    
    /* Preserve any unknown chunk data for round-trip safety */
    uint8_t *raw_tail;       /**< Unrecognized trailing data */
    size_t raw_tail_size;    /**< Size of trailing data in bytes */
} nmo_ckrenderobject_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKRenderObject deserialize function pointer type
 * 
 * @param chunk Chunk containing CKRenderObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckrenderobject_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckrenderobject_state_t *out_state);

/**
 * @brief CKRenderObject serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckrenderobject_serialize_fn)(
    const nmo_ckrenderobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKRenderObject schema types
 * 
 * Registers schema types for CKRenderObject state structures.
 * Must be called during initialization before using CKRenderObject schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckrenderobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Deserialize CKRenderObject from chunk (public API)
 * 
 * @param chunk Chunk containing CKRenderObject data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ckrenderobject_deserialize(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckrenderobject_state_t *out_state);

/**
 * @brief Serialize CKRenderObject to chunk (public API)
 * 
 * @param chunk Chunk to write to
 * @param state State to serialize
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ckrenderobject_serialize(
    const nmo_ckrenderobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKRenderObject
 * 
 * @return Deserialize function pointer
 */
NMO_API nmo_ckrenderobject_deserialize_fn nmo_get_ckrenderobject_deserialize(void);

/**
 * @brief Get the serialize function for CKRenderObject
 * 
 * @return Serialize function pointer
 */
NMO_API nmo_ckrenderobject_serialize_fn nmo_get_ckrenderobject_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKRENDEROBJECT_SCHEMAS_H */
