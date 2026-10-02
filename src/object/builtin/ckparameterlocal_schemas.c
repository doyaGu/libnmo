/**
 * @file ckparameterlocal_schemas.c
 * @brief CKParameterLocal schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterLocal.
 *
 * Based on official Virtools SDK (reference/src/CKParameterLocal.cpp:100-140).
 */

#include "object/builtin/nmo_parameterlocal_schemas.h"
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
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterlocal_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterlocal_state_t, base),
                    sizeof(nmo_parameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_VALUE(nmo_parameterlocal_state_t, owner),
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
static nmo_status_t nmo_parameterlocal_deserialize_internal(
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

    /* Check if "myself" parameter */
    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_MYSELF, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords != 0u) return NMO_ERR_INVALID_FORMAT;
        out_state->is_myself = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Check if setting */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords != 0u) return NMO_ERR_INVALID_FORMAT;
        out_state->is_setting = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameterlocal_state_t *out_state =
        (nmo_parameterlocal_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameterlocal_state_t decoded = {0};
    const nmo_allocator_t *allocator =
        out_state->base.buffer_data.allocator.alloc != NULL
            ? &out_state->base.buffer_data.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.base.buffer_data, sizeof(uint8_t), 0, allocator);
    if (result != NMO_OK) return result;
    result = nmo_parameterlocal_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded.base.buffer_data);
        return result;
    }
    nmo_array_dispose(&out_state->base.buffer_data);
    *out_state = decoded;
    return NMO_OK;
}

/**
 * @brief Serialize CKParameterLocal state to chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:119-130
 */
static nmo_status_t nmo_parameterlocal_serialize_internal(
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

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_parameterlocal)

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_parameterlocal)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_parameterlocal_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_parameterlocal_set_defaults(void *instance)
{
    nmo_parameterlocal_state_t *state = instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

static const nmo_object_state_member_t nmo_parameterlocal_members[] = {
    NMO_STATE_VALUE(nmo_parameterlocal_state_t, owner),
    NMO_STATE_VALUE(nmo_parameterlocal_state_t, is_myself),
    NMO_STATE_VALUE(nmo_parameterlocal_state_t, is_setting)
};

static const nmo_object_state_layout_t nmo_parameterlocal_layout = {
    .size = sizeof(nmo_parameterlocal_state_t),
    .base_vtable = &nmo_parameter_vtable,
    .base_size = sizeof(nmo_parameter_state_t),
    .members = nmo_parameterlocal_members,
    .member_count = sizeof(nmo_parameterlocal_members) /
        sizeof(nmo_parameterlocal_members[0]),
    .set_defaults = nmo_parameterlocal_set_defaults,
    .validate = nmo_parameterlocal_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(parameterlocal, nmo_parameterlocal_layout)

static nmo_status_t nmo_parameterlocal_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_parameterlocal_state_t *state =
        (const nmo_parameterlocal_state_t *)instance;
    return nmo_parameter_vtable.validate(&state->base, NULL, context);
}

nmo_type_vtable_t nmo_parameterlocal_vtable = {
    .prepare_dependencies = nmo_parameterlocal_prepare_dependencies,
    .remap_dependencies = nmo_parameterlocal_remap_dependencies,
    .pre_delete = nmo_parameterlocal_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
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






