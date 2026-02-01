/**
 * @file ckparameterlocal_schemas.c
 * @brief CKParameterLocal schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterLocal.
 *
 * Based on official Virtools SDK (reference/src/CKParameterLocal.cpp:100-140).
 */

#include "object/nmo_ckparameterlocal_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <string.h>

/* =============================================================================
 * CKParameterLocal DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

#define CK_STATESAVE_PARAMETEROUT_MYSELF    0x00000200
#define CK_STATESAVE_PARAMETEROUT_ISSETTING 0x00000400

/**
 * @brief Deserialize CKParameterLocal state from chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:131-145
 */
static nmo_result_t nmo_ckparameterlocal_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameterlocal_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameterlocal_state_t));

    /* Check if "myself" parameter */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_MYSELF).code == NMO_OK) {
        out_state->is_myself = 1;
    }

    /* Check if setting */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING).code == NMO_OK) {
        out_state->is_setting = 1;
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterLocal state to chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:119-130
 */
static nmo_result_t nmo_ckparameterlocal_serialize(
    const nmo_ckparameterlocal_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write "myself" flag if needed */
    if (in_state->is_myself) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_MYSELF);
        if (result.code != NMO_OK) return result;
    }

    /* Write setting flag if needed */
    if (in_state->is_setting) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - Accessors
 * ============================================================================= */

nmo_ckparameterlocal_deserialize_fn nmo_get_ckparameterlocal_deserialize(void)
{
    return nmo_ckparameterlocal_deserialize;
}

nmo_ckparameterlocal_serialize_fn nmo_get_ckparameterlocal_serialize(void)
{
    return nmo_ckparameterlocal_serialize;
}
