/**
 * @file ckparameterout_schemas.c
 * @brief CKParameterOut schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterOut.
 *
 * Based on official Virtools SDK (reference/src/CKParameterOut.cpp:120-160).
 */

#include "object/nmo_ckparameterout_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

#define CK_STATESAVE_PARAMETEROUT_DESTINATIONS 0x00000020

/**
 * @brief Deserialize CKParameterOut state from chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
static nmo_result_t nmo_ckparameterout_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameterout_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameterout_state_t));

    /* Read destinations if present */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS).code == NMO_OK) {
        int32_t count;
        nmo_result_t result = nmo_chunk_read_int(chunk, &count);
        if (result.code == NMO_OK && count > 0) {
            out_state->destination_count = (uint32_t)count;
            out_state->destination_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));

            if (out_state->destination_ids) {
                for (int32_t i = 0; i < count; i++) {
                    nmo_chunk_read_object_id(chunk, &out_state->destination_ids[i]);
                }
            }
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterOut state to chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
static nmo_result_t nmo_ckparameterout_serialize(
    const nmo_ckparameterout_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write destinations if any */
    if (in_state->destination_count > 0 && in_state->destination_ids) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->destination_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->destination_count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->destination_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - Accessors
 * ============================================================================= */

nmo_ckparameterout_deserialize_fn nmo_get_ckparameterout_deserialize(void)
{
    return nmo_ckparameterout_deserialize;
}

nmo_ckparameterout_serialize_fn nmo_get_ckparameterout_serialize(void)
{
    return nmo_ckparameterout_serialize;
}
