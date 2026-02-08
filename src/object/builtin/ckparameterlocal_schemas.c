/**
 * @file ckparameterlocal_schemas.c
 * @brief CKParameterLocal schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterLocal.
 *
 * Based on official Virtools SDK (reference/src/CKParameterLocal.cpp:100-140).
 */

#include "object/nmo_ckparameterlocal_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckparameterlocal, nmo_ckparameterlocal_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckparameterlocal_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckparameterlocal_state_t, base),
                    sizeof(nmo_ckparameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckparameterlocal_state_t, is_myself, CKPGUID_UINT8),
    NMO_FIELD(nmo_ckparameterlocal_state_t, is_setting, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterLocal DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterLocal state from chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:131-145
 */
nmo_status_t nmo_ckparameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameterlocal_state_t *out_state = (nmo_ckparameterlocal_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckparameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

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
nmo_status_t nmo_ckparameterlocal_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameterlocal_state_t *in_state = (const nmo_ckparameterlocal_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Write base state (CKObject when "myself", otherwise CKParameter) */
    if (in_state->is_myself) {
        result = nmo_ckobject_serialize(&in_state->base.base, out_chunk, NULL, context);
    } else {
        result = nmo_ckparameter_serialize(&in_state->base, out_chunk, NULL, context);
    }
    if (result != NMO_OK) return result;

    /* Write "myself" flag if needed */
    if (in_state->is_myself) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_MYSELF);
        if (result != NMO_OK) return result;
    }

    /* Write setting flag if needed */
    if (in_state->is_setting) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    ckparameterlocal,
    nmo_ckparameterlocal_state_t,
    nmo_ckparameterlocal_serialize,
    nmo_ckparameterlocal_deserialize,
    nmo_ckparameterlocal_fields,
    CKPGUID_PARAMETERLOCAL,
    "CKParameterLocal",
    NMO_CID_PARAMETERLOCAL,
    CKPGUID_PARAMETER
)


