/**
 * @file nmo_sceneobject_schemas.h
 * @brief CKSceneObject schema declarations
 */

#ifndef NMO_CKSCENEOBJECT_SCHEMAS_H
#define NMO_CKSCENEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/builtin/nmo_object_schemas.h"
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
typedef struct nmo_sceneobject_state {
    nmo_object_state_t base;  /**< Inherited CKObject state */
} nmo_sceneobject_state_t;

/* =============================================================================
 * DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSceneObject from chunk
 * 
 * @param instance State of the class to fill
 * @param chunk    Chunk to read
 * @param type     Type descriptor of the class
 * @param context  Deserialize context (nmo_deserialize_context_t *)
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_sceneobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Serialize CKSceneObject to chunk
 * 
 * @param instance  State of the class to write
 * @param out_chunk Chunk to write to
 * @param type      Type descriptor of the class
 * @param context   Serialize context (nmo_serialize_context_t *)
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_sceneobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sceneobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sceneobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_sceneobject_vtable, nmo_register_sceneobject_type)
#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSCENEOBJECT_SCHEMAS_H */
