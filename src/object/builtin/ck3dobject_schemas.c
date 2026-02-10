/**
 * @file ck3dobject_schemas.c
 * @brief CK3dObject schema definitions
 *
 * Implements schema for CK3dObject.
 * 
 * Based on CKRenderEngine RCK3dObject:
 * - CK3dObject inherits from CK3dEntity
 * - No additional serialized fields beyond CK3dEntity
 */

#include "object/builtin/nmo_3dobject_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(3dobject, nmo_3dobject_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_3dobject_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_3dobject_state_t, entity),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0)
};

/* =============================================================================
 * CK3dObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dObject state from chunk
 * 
 * Reads CK3dEntity data only (CK3dObject adds no extra serialized fields).
 * 
 * @param chunk Chunk containing CK3dObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_3dobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_3dobject_state_t *out_state = (nmo_3dobject_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dObject deserialize");
    }

    return nmo_3dentity_deserialize(&out_state->entity, chunk, NULL, context);
}

/* =============================================================================
 * CK3dObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK3dObject state to chunk
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_status_t nmo_3dobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_3dobject_state_t *in_state = (const nmo_3dobject_state_t *)instance;
        nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dObject serialize");
    }

    return nmo_3dentity_serialize(&in_state->entity, out_chunk, NULL, context);
}


/**
 * @brief Finish loading CK3dObject
 * 
 * Performs reference resolution for mesh linkage and material setup.
 * 
 * @param state 3D object state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_status_t nmo_3dobject_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    /* Mesh reference resolution would go here */
    (void)instance;
    (void)arena;
    (void)repository;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS(
    3dobject,
    nmo_3dobject_state_t,
    nmo_3dobject_serialize,
    nmo_3dobject_deserialize,
    nmo_3dobject_finish_loading,
    nmo_3dobject_fields,
    CKPGUID_OBJECT3D,
    "CK3dObject",
    NMO_CID_3DOBJECT,
    CKPGUID_3DENTITY
)


