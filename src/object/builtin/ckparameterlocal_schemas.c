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
    NMO_FIELD_REF(nmo_parameterlocal_state_t, owner_id),
    NMO_FIELD(nmo_parameterlocal_state_t, is_myself, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterlocal_state_t, is_setting, CKPGUID_UINT8)
};

static bool nmo_parameterlocal_is_valid_owner(
    void *context,
    nmo_object_id_t object_id)
{
    if (object_id == 0) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_deserialize_context_get_type_registry(context);
    nmo_object_repository_t *repo = (nmo_object_repository_t *)
        nmo_deserialize_context_get_repository(context);

    if (repo == NULL || registry == NULL) {
        return true;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (obj == NULL) {
        return false;
    }

    const uint32_t class_id = (uint32_t)nmo_object_get_class_id(obj);
    return nmo_type_registry_is_class_derived_from(
        registry, class_id, (uint32_t)NMO_CID_BEHAVIOR) ||
        nmo_type_registry_is_class_derived_from(
            registry, class_id, (uint32_t)NMO_CID_PARAMETEROPERATION);
}

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
        nmo_object_id_t owner_id = 0;
        nmo_chunk_read_object_id(chunk, &owner_id);
        if (nmo_parameterlocal_is_valid_owner(context, owner_id)) {
            out_state->owner_id = owner_id;
        } else {
            out_state->owner_id = 0;
        }
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
        in_state->owner_id != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_OWNER);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->owner_id);
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

nmo_status_t nmo_parameterlocal_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_finish_loading");
    }

    nmo_parameterlocal_state_t *state = (nmo_parameterlocal_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_object_finish_loading(&state->base.base, arena, repository));
    NMO_RETURN_IF_ERROR(nmo_parameter_finish_loading(&state->base, arena, repository));

    if (state->owner_id != 0 && repo &&
        nmo_object_repository_find_by_id(repo, state->owner_id) == NULL) {
        state->owner_id = 0;
    }

    state->is_myself = state->is_myself ? 1 : 0;
    state->is_setting = state->is_setting ? 1 : 0;

    if (state->is_myself) {
        state->base.mode = CKPARAM_MODE_NONE;
        state->base.has_state = false;
        state->base.object_id = 0;
        state->base.manager_guid = NMO_GUID_NULL;
        state->base.manager_value = 0;
        state->base.subchunk = NULL;
        nmo_array_clear(&state->base.buffer_data);
    }

    return nmo_object_default_validate(state, NULL, NULL);
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS(
    parameterlocal,
    nmo_parameterlocal_state_t,
    nmo_parameterlocal_serialize,
    nmo_parameterlocal_deserialize,
    nmo_parameterlocal_finish_loading,
    nmo_parameterlocal_fields,
    CKPGUID_PARAMETERLOCAL,
    "CKParameterLocal",
    NMO_CID_PARAMETERLOCAL,
    CKPGUID_PARAMETER
)


