/**
 * @file nmo_cksceneobject_schemas.h
 * @brief CKSceneObject schema declarations
 */

#ifndef NMO_CKSCENEOBJECT_SCHEMAS_H
#define NMO_CKSCENEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKSceneObject STATE
 * ============================================================================= */

/**
 * @brief CKSceneObject state structure
 */
typedef struct nmo_cksceneobject_state {
    nmo_ckobject_state_t base;  /**< Inherited CKObject state */
    uint8_t *raw_tail;           /**< Unknown/future data */
    size_t raw_tail_size;        /**< Size of unknown data */
} nmo_cksceneobject_state_t;

/* =============================================================================
 * DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSceneObject from chunk
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_cksceneobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Serialize CKSceneObject to chunk
 * 
 * @param in_state State to serialize (input)
 * @param out_chunk Chunk to write to (output)
 * @param arena Arena allocator for error handling
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_cksceneobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cksceneobject_vtable, nmo_register_cksceneobject_type)
#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSCENEOBJECT_SCHEMAS_H */
