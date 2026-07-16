/**
 * @file cktargetcamera_schemas.c
 * @brief CKTargetCamera schema implementation
 */

#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_deserialize_context.h"
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
    NMO_FIELD_REF(nmo_targetcamera_state_t, target)
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

    out_state->has_target = 0;
    out_state->target = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TCAMERATARGET) == NMO_OK) {
        nmo_ref_t target = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &target);
        if (result != NMO_OK) {
            return result;
        }
        nmo_ref_check_class(
            &target,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        out_state->target = target;
        out_state->has_target = target.state != NMO_REF_NONE;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_status_t nmo_targetcamera_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_targetcamera_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_targetcamera_remap_dependencies");
    }

    nmo_targetcamera_state_t *state = (nmo_targetcamera_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_camera_remap_dependencies(&state->base, NULL, context));

    /* Preserve target section presence and unresolved ID. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_targetcamera_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_targetcamera_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_targetcamera_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

NMO_DEFINE_OBJECT_STATE_OPS(targetcamera, nmo_targetcamera_state_t)

nmo_type_vtable_t nmo_targetcamera_vtable = {
    .prepare_dependencies = nmo_targetcamera_prepare_dependencies,
    .remap_dependencies = nmo_targetcamera_remap_dependencies,
    .pre_delete = nmo_targetcamera_pre_delete,
    .post_delete = nmo_targetcamera_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_targetcamera_create,
        nmo_targetcamera_destroy,
        nmo_targetcamera_serialize,
        nmo_targetcamera_deserialize,
        nmo_targetcamera_copy,
        nmo_targetcamera_validate,
        nmo_targetcamera_equals,
        nmo_targetcamera_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_targetcamera_type,
    CKPGUID_TARGETCAMERA,
    "CKTargetCamera",
    NMO_CID_TARGETCAMERA,
    CKPGUID_CAMERA,
    nmo_targetcamera_state_t,
    &nmo_targetcamera_vtable,
    nmo_targetcamera_fields)

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

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_TCAMERAONLY) == 0) {
            return NMO_OK;
        }
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TCAMERATARGET);
    if (result != NMO_OK) return result;
    result = nmo_ref_write(out_chunk, &in_state->target);
    if (result != NMO_OK) return result;

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
