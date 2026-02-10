/**
 * @file cktargetcamera_schemas.c
 * @brief CKTargetCamera schema implementation
 */

#include "object/nmo_targetcamera_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(targetcamera, nmo_targetcamera_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_targetcamera_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_targetcamera_state_t, base),
                    sizeof(nmo_camera_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_targetcamera_state_t, has_target, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_targetcamera_state_t, target_id)
};

static nmo_status_t nmo_targetcamera_deserialize_internal(
    nmo_targetcamera_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetcamera_deserialize");
    }

    nmo_status_t result = nmo_camera_deserialize(&out_state->base, chunk, NULL, context);
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

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    targetcamera,
    nmo_targetcamera_state_t,
    nmo_targetcamera_serialize,
    nmo_targetcamera_deserialize,
    nmo_targetcamera_fields,
    CKPGUID_TARGETCAMERA,
    "CKTargetCamera",
    NMO_CID_TARGETCAMERA,
    CKPGUID_CAMERA
)
static nmo_status_t nmo_targetcamera_serialize_internal(
    const nmo_targetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetcamera_serialize");
    }

    nmo_status_t result = nmo_camera_serialize(&in_state->base, out_chunk, NULL, context);
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

nmo_status_t nmo_targetcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_targetcamera_state_t *out_state = (nmo_targetcamera_state_t *)instance;
    return nmo_targetcamera_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_targetcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_targetcamera_state_t *in_state = (const nmo_targetcamera_state_t *)instance;
    return nmo_targetcamera_serialize_internal(in_state, out_chunk, context);
}

