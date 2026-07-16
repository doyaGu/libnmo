/**
 * @file ckparameterlocal_schemas.c
 * @brief CKParameterLocal schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterLocal.
 *
 * Based on official Virtools SDK (reference/src/CKParameterLocal.cpp:100-140).
 */

#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_serialize_context.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(parameterlocal, nmo_parameterlocal_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterlocal_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterlocal_state_t, base),
                    sizeof(nmo_parameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_parameterlocal_state_t, owner),
    NMO_FIELD(nmo_parameterlocal_state_t, is_myself, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterlocal_state_t, is_setting, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterLocal DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterLocal state from chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:131-145
 */
nmo_status_t nmo_parameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameterlocal_state_t *out_state = (nmo_parameterlocal_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_parameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_OWNER) == NMO_OK) {
        nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
        nmo_ref_check_class(
            &owner,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_BEHAVIOR);
        out_state->owner = owner;
    }

    /* Check if "myself" parameter */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_MYSELF) == NMO_OK) {
        out_state->is_myself = 1;
    }

    /* Check if setting */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING) == NMO_OK) {
        out_state->is_setting = 1;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterLocal state to chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:119-130
 */
nmo_status_t nmo_parameterlocal_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameterlocal_state_t *in_state = (const nmo_parameterlocal_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = is_file
        ? CK_STATESAVE_PARAMETEROUT_ALL
        : nmo_serialize_context_get_save_flags(context);
    const bool want_value = is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_VAL) != 0);

    /* Write base state (CKObject when "myself", otherwise CKParameter unless value is skipped) */
    if (in_state->is_myself || !want_value) {
        result = nmo_object_serialize(&in_state->base.base, out_chunk, NULL, context);
    } else {
        result = nmo_parameter_serialize(&in_state->base, out_chunk, NULL, context);
    }
    if (result != NMO_OK) return result;

    if (!is_file && save_flags == 0) {
        return NMO_OK;
    }

    if (!is_file &&
        (save_flags & CK_STATESAVE_PARAMETEROUT_OWNER) != 0 &&
        nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_OWNER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->owner);
        if (result != NMO_OK) return result;
    }

    /* Write "myself" flag if needed */
    if (in_state->is_myself &&
        (is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_MYSELF) != 0))) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_MYSELF);
        if (result != NMO_OK) return result;
    }

    /* Write setting flag if needed */
    if (in_state->is_setting &&
        (is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_ISSETTING) != 0))) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_remap_dependencies");
    }

    nmo_parameterlocal_state_t *state = (nmo_parameterlocal_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base.base, NULL, context));
    NMO_RETURN_IF_ERROR(nmo_parameter_remap_dependencies(&state->base, NULL, context));

    /* Preserve owner and payload fields; normalization is explicit. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameterlocal_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_pre_delete");
    }
    nmo_parameterlocal_state_t *state =
        (nmo_parameterlocal_state_t *)instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static void nmo_parameterlocal_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS(parameterlocal, nmo_parameterlocal_state_t)

nmo_type_vtable_t nmo_parameterlocal_vtable = {
    .prepare_dependencies = nmo_parameterlocal_prepare_dependencies,
    .remap_dependencies = nmo_parameterlocal_remap_dependencies,
    .pre_delete = nmo_parameterlocal_pre_delete,
    .post_delete = nmo_parameterlocal_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameterlocal_create,
        nmo_parameterlocal_destroy,
        nmo_parameterlocal_serialize,
        nmo_parameterlocal_deserialize,
        nmo_parameterlocal_copy,
        nmo_parameterlocal_validate,
        nmo_parameterlocal_equals,
        nmo_parameterlocal_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameterlocal_type,
    CKPGUID_PARAMETERLOCAL,
    "CKParameterLocal",
    NMO_CID_PARAMETERLOCAL,
    CKPGUID_PARAMETER,
    nmo_parameterlocal_state_t,
    &nmo_parameterlocal_vtable,
    nmo_parameterlocal_fields)






