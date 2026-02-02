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

#define CK_STATESAVE_TCAMERATARGET 0x10000000u

static nmo_result_t nmo_cktargetcamera_deserialize_internal(
    nmo_cktargetcamera_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckcamera_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TCAMERATARGET).code == NMO_OK) {
        out_state->has_target = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->target_id);
    }

    return nmo_result_ok();
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

static nmo_result_t nmo_cktargetcamera_serialize_internal(
    const nmo_cktargetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_serialize"));
    }

    nmo_result_t result = nmo_ckcamera_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (in_state->has_target) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TCAMERATARGET);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_id);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_cktargetcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cktargetcamera_state_t *out_state = (nmo_cktargetcamera_state_t *)instance;
    return nmo_cktargetcamera_deserialize_internal(out_state, chunk, context);
}

nmo_result_t nmo_cktargetcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cktargetcamera_state_t *in_state = (const nmo_cktargetcamera_state_t *)instance;
    return nmo_cktargetcamera_serialize_internal(in_state, out_chunk, context);
}
