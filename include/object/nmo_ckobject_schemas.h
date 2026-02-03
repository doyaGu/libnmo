/**
 * @file nmo_ckobject_schemas.h
 * @brief Public API for CKObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKObject
 * and its derived classes. Used by the object deserialization pipeline in
 * parser.c Phase 14.
 */

#ifndef NMO_CKOBJECT_SCHEMAS_H
#define NMO_CKOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/* =============================================================================
 * CKObject STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKObject base state
 * 
 * Minimal state for CKObject (visibility flags only).
 * Corresponds to data serialized in CKObject::Load/Save.
 */
typedef struct nmo_ckobject_state {
    uint32_t visibility_flags;  /**< Visibility flags (VISIBLE/HIERARCHICAL) */
} nmo_ckobject_state_t;

/* Visibility flag constants */
#define NMO_CKOBJECT_VISIBLE          0x01  /**< Object is visible */
#define NMO_CKOBJECT_HIERARCHICAL     0x02  /**< Object has hierarchical hide */

/* =============================================================================
 * DESERIALIZATION FUNCTIONS
 * ============================================================================= */

/**
 * @brief Deserialize CKObject state from chunk
 * 
 * Reads CKObject visibility state from chunk using identifier-based reading.
 * Symmetric to CKObject::Load in Virtools SDK.
 * 
 * @param chunk Chunk containing object data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
/**
 * @brief Deserialize CKObject from chunk (implementation)
 */
NMO_API nmo_status_t nmo_ckobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/* =============================================================================
 * SERIALIZATION FUNCTIONS
 * ============================================================================= */

/**
 * @brief Serialize CKObject state to chunk
 * 
 * Writes CKObject visibility state to chunk using identifier-based writing.
 * Symmetric to CKObject::Save in Virtools SDK.
 * 
 * @param in_state  Input state structure to serialize
 * @param out_chunk Output chunk to write to
 * @param arena     Arena for temporary allocations
 * @return Result indicating success or error
 */
/**
 * @brief Serialize CKObject to chunk (implementation)
 */
NMO_API nmo_status_t nmo_ckobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckobject_vtable, nmo_register_ckobject_type)

/* =============================================================================
 * FINISH LOADING FUNCTIONS (Phase 15 - PostLoad equivalent)
 * ============================================================================= */

/**
 * @brief Object-level finish loading function
 * 
 * Called after deserialization to resolve references and initialize runtime state.
 * Equivalent to CKObject::PostLoad() in Virtools SDK.
 * 
 * @param state Deserialized object state
 * @param context Serialization context (nmo_serialize_context_t*)
 * @return Result indicating success or error
 */
/**
 * @brief Finish loading CKObject (base implementation)
 * 
 * Base class implementation does nothing - derived classes override.
 */
NMO_API nmo_status_t nmo_ckobject_finish_loading(
    void *state,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKOBJECT_SCHEMAS_H */
