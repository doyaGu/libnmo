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
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cksceneobject, nmo_cksceneobject_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_cksceneobject_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_cksceneobject_state_t, base),
                    sizeof(nmo_ckobject_state_t), NMO_GUID_FIELD_VOID,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_ARRAY_NAMED("raw_tail", offsetof(nmo_cksceneobject_state_t, raw_tail),
                          sizeof(uint8_t *), NMO_GUID_FIELD_UINT8,
                          NMO_FIELD_OPTIONAL, 0)
};

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

static nmo_status_t cksceneobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_cksceneobject_state_t *s = src;
    nmo_cksceneobject_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_bytes(arena, (void **)&d->raw_tail,
                                 s->raw_tail, s->raw_tail_size);
}

static nmo_status_t cksceneobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_cksceneobject_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    cksceneobject,
    nmo_cksceneobject_state_t,
    nmo_cksceneobject_serialize,
    nmo_cksceneobject_deserialize,
    nmo_cksceneobject_fields,
    NMO_GUID_CKSCENEOBJECT,
    "CKSceneObject",
    NMO_CID_SCENEOBJECT,
    NMO_GUID_CKOBJECT
)


