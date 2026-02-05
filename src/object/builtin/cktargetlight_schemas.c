/**
 * @file cktargetlight_schemas.c
 * @brief CKTargetLight schema implementation
 */

#include "object/nmo_cktargetlight_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cktargetlight, nmo_cktargetlight_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_cktargetlight_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_cktargetlight_state_t, base),
                    sizeof(nmo_cklight_state_t), NMO_GUID_FIELD_VOID,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_cktargetlight_state_t, has_target, NMO_GUID_FIELD_UINT8),
    NMO_FIELD_REF(nmo_cktargetlight_state_t, target_id)
};

static nmo_status_t nmo_cktargetlight_deserialize_internal(
    nmo_cktargetlight_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetlight_deserialize");
    }

    nmo_status_t result = nmo_cklight_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TLIGHTTARGET) == NMO_OK) {
        out_state->has_target = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->target_id);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    cktargetlight,
    nmo_cktargetlight_state_t,
    nmo_cktargetlight_serialize,
    nmo_cktargetlight_deserialize,
    nmo_cktargetlight_fields,
    NMO_GUID_CKTARGETLIGHT,
    "CKTargetLight",
    NMO_CID_TARGETLIGHT,
    NMO_GUID_CKLIGHT
)

static nmo_status_t nmo_cktargetlight_serialize_internal(
    const nmo_cktargetlight_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetlight_serialize");
    }

    nmo_status_t result = nmo_cklight_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (in_state->has_target) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TLIGHTTARGET);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_cktargetlight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cktargetlight_state_t *out_state = (nmo_cktargetlight_state_t *)instance;
    return nmo_cktargetlight_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_cktargetlight_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cktargetlight_state_t *in_state = (const nmo_cktargetlight_state_t *)instance;
    return nmo_cktargetlight_serialize_internal(in_state, out_chunk, context);
}
