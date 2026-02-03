/**
 * @file cksceneobject_schemas.c
 * @brief CKSceneObject schema definitions
 *
 * Implements schema for CKSceneObject and its descendants.
 * 
 * Based on official Virtools SDK (reference/src/CKSceneObject.cpp):
 * - CKSceneObject does NOT override Load/Save - inherits CKObject's behavior
 * - m_Scenes (XBitArray) is runtime-only data managed by CKScene::AddObject/RemoveObject
 * - No additional data is serialized to chunks beyond CKObject's visibility flags
 * 
 * This schema correctly delegates to CKObject deserializer and maintains
 * the parent chain functionality as required by design.md ��6.4.
 */

#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cksceneobject, nmo_cksceneobject_state_t)

/* =============================================================================
 * CKSceneObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSceneObject state from chunk
 * 
 * CKSceneObject doesn't add any chunk data beyond CKObject.
 * This function delegates to CKObject deserializer.
 * 
 * @param chunk Chunk containing CKSceneObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_cksceneobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksceneobject_state_t *out_state = (nmo_cksceneobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksceneobject_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_cksceneobject_create(out_state, type, context));

    /* Deserialize base CKObject state */
    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* CKSceneObject has no additional chunk data */
    /* Scene membership is populated at runtime by CKScene */

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKSceneObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKSceneObject state to chunk
 * 
 * Symmetric write operation for round-trip support.
 * 
 * @param in_state State structure to serialize (input)
 * @param out_chunk Chunk to write to (output)
 * @param arena Arena allocator for error handling
 * @return Result indicating success or error
 */
nmo_status_t nmo_cksceneobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksceneobject_state_t *in_state = (const nmo_cksceneobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksceneobject_serialize");
    }

    /* Serialize base CKObject state */
    nmo_status_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* Write preserved unknown data */
    if (in_state->raw_tail != NULL && in_state->raw_tail_size > 0) {
        nmo_status_t result = nmo_chunk_write_buffer_no_size(
            out_chunk, (const void *)in_state->raw_tail, in_state->raw_tail_size);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cksceneobject,
    nmo_cksceneobject_state_t,
    nmo_cksceneobject_serialize,
    nmo_cksceneobject_deserialize,
    NMO_GUID_CKSCENEOBJECT,
    "CKSceneObject",
    NMO_CID_SCENEOBJECT,
    NMO_GUID_CKOBJECT
)


