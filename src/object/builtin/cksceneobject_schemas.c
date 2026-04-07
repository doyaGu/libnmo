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

#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_sceneobject_schemas.h"
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

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(sceneobject, nmo_sceneobject_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sceneobject_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_sceneobject_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* No additional fields beyond CKObject base */
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
nmo_status_t nmo_sceneobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sceneobject_state_t *out_state = (nmo_sceneobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sceneobject_deserialize");
    }

    /* Deserialize base CKObject state */
    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
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
nmo_status_t nmo_sceneobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sceneobject_state_t *in_state = (const nmo_sceneobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sceneobject_serialize");
    }

    /* Serialize base CKObject state */
    nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_sceneobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)arena;
    return nmo_object_default_copy(src, dst, type, arena);
}

static nmo_status_t nmo_sceneobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    (void)instance;
    NMO_RETURN_OK();
}

nmo_status_t nmo_sceneobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sceneobject_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_sceneobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sceneobject_remap_dependencies");
    }

    nmo_sceneobject_state_t *state = (nmo_sceneobject_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));
    return nmo_sceneobject_validate(state, NULL, NULL);
}

static nmo_status_t nmo_sceneobject_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sceneobject_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_sceneobject_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(sceneobject, nmo_sceneobject_state_t)

nmo_type_vtable_t nmo_sceneobject_vtable = {
    .prepare_dependencies = nmo_sceneobject_prepare_dependencies,
    .remap_dependencies = nmo_sceneobject_remap_dependencies,
    .pre_delete = nmo_sceneobject_pre_delete,
    .post_delete = nmo_sceneobject_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_sceneobject_create,
        nmo_sceneobject_destroy,
        nmo_sceneobject_serialize,
        nmo_sceneobject_deserialize,
        nmo_sceneobject_copy,
        nmo_sceneobject_validate,
        nmo_sceneobject_equals,
        nmo_sceneobject_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_sceneobject_type,
    CKPGUID_SCENEOBJECT,
    "CKSceneObject",
    NMO_CID_SCENEOBJECT,
    CKPGUID_OBJECT,
    nmo_sceneobject_state_t,
    &nmo_sceneobject_vtable,
    nmo_sceneobject_fields)







