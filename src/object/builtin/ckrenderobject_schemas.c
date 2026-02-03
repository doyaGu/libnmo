/**
 * @file ckrenderobject_schemas.c
 * @brief CKRenderObject schema definitions
 *
 * Implements schema for CKRenderObject and its descendants.
 * 
 * Based on official Virtools SDK (reference/include/CKRenderObject.h):
 * - CKRenderObject is an ABSTRACT BASE CLASS (all methods pure virtual)
 * - It does NOT override Load/Save - inherits CKBeObject's behavior
 * - No additional data is serialized to chunks beyond CKBeObject
 * - Runtime rendering data (callbacks, Z-order) is NOT persisted
 * 
 * This schema correctly delegates to CKBeObject deserializer, maintaining
 * the parent chain functionality as required by design.md ��6.4.
 */

#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckrenderobject, nmo_ckrenderobject_state_t)

/* =============================================================================
 * CKRenderObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKRenderObject state from chunk
 * 
 * CKRenderObject is an abstract base class with no Load/Save implementation.
 * This function delegates to CKBeObject deserializer to maintain proper
 * inheritance chain behavior.
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class)
 * No corresponding Load/Save in reference/src/ - uses parent CKBeObject
 * 
 * @param chunk Chunk containing CKRenderObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckrenderobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckrenderobject_state_t *out_state = (nmo_ckrenderobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrenderobject_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckrenderobject_create(out_state, type, context));

    nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKRenderObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKRenderObject state to chunk
 * 
 * CKRenderObject has no additional data beyond CKBeObject.
 * This function delegates to CKBeObject serializer.
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class, no Save)
 * 
 * @param in_state Input state structure
 * @param out_chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckrenderobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckrenderobject_state_t *in_state = (const nmo_ckrenderobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrenderobject_serialize");
    }

    nmo_status_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckrenderobject,
    nmo_ckrenderobject_state_t,
    nmo_ckrenderobject_serialize,
    nmo_ckrenderobject_deserialize,
    NMO_GUID_CKRENDEROBJECT,
    "CKRenderObject",
    NMO_CID_RENDEROBJECT,
    NMO_GUID_CKBEOBJECT
)
