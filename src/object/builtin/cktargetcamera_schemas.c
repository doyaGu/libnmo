/**
 * @file cktargetcamera_schemas.c
 * @brief CKTargetCamera schema implementation
 */

#include "object/nmo_cktargetcamera_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cktargetcamera, nmo_cktargetcamera_state_t)

static nmo_status_t nmo_cktargetcamera_deserialize_internal(
    nmo_cktargetcamera_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_cktargetcamera_create(out_state, NULL, context));

    nmo_status_t result = nmo_ckcamera_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TCAMERATARGET) == NMO_OK) {
        out_state->has_target = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->target_id);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cktargetcamera,
    nmo_cktargetcamera_state_t,
    nmo_cktargetcamera_serialize,
    nmo_cktargetcamera_deserialize,
    NMO_GUID_CKTARGETCAMERA,
    "CKTargetCamera",
    NMO_CID_TARGETCAMERA,
    NMO_GUID_CKCAMERA
)

static nmo_status_t nmo_cktargetcamera_serialize_internal(
    const nmo_cktargetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_serialize");
    }

    nmo_status_t result = nmo_ckcamera_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (in_state->has_target) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TCAMERATARGET);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_cktargetcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cktargetcamera_state_t *out_state = (nmo_cktargetcamera_state_t *)instance;
    return nmo_cktargetcamera_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_cktargetcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cktargetcamera_state_t *in_state = (const nmo_cktargetcamera_state_t *)instance;
    return nmo_cktargetcamera_serialize_internal(in_state, out_chunk, context);
}
