/**
 * @file ckbehaviorio_schemas.c
 * @brief CKBehaviorIO schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorIO (behavior I/O endpoints).
 * CKBehaviorIO extends CKObject and is a simple class storing only I/O flags.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorIO.cpp:19-48).
 */

#include "object/nmo_ckbehaviorio_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckobject_schemas.h"
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

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckbehaviorio, nmo_ckbehaviorio_state_t)

/* =============================================================================
 * CKBehaviorIO DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorIO state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorIO::Load.
 * Reads I/O flags that determine the endpoint type and characteristics.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:39-48
 * 
 * @param chunk Chunk containing CKBehaviorIO data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckbehaviorio_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckbehaviorio_state_t *out_state = (nmo_ckbehaviorio_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorio_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckbehaviorio_create(out_state, type, context));

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Read I/O flags */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->old_flags);
        if (result != NMO_OK) return result;
        out_state->has_flags = true;
    }
    /* Note: If identifier not found, old_flags remains 0 (valid for older versions) */

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBehaviorIO SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorIO state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorIO::Save.
 * Writes I/O flags that determine the endpoint type.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:19-36
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckbehaviorio_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckbehaviorio_state_t *in_state = (const nmo_ckbehaviorio_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorio_serialize");
    }

    nmo_status_t result;

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* CKBehaviorIO::Save always writes IOFLAGS in file context */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->old_flags);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckbehaviorio,
    nmo_ckbehaviorio_state_t,
    nmo_ckbehaviorio_serialize,
    nmo_ckbehaviorio_deserialize,
    NMO_GUID_CKBEHAVIORIO,
    "CKBehaviorIO",
    NMO_CID_BEHAVIORIO,
    NMO_GUID_CKOBJECT
)

