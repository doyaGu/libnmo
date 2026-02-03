/**
 * @file ckparameterout_schemas.c
 * @brief CKParameterOut schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterOut.
 *
 * Based on official Virtools SDK (reference/src/CKParameterOut.cpp:120-160).
 */

#include "object/nmo_ckparameterout_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckparameterout, nmo_ckparameterout_state_t)

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterOut state from chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
nmo_status_t nmo_ckparameterout_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameterout_state_t *out_state = (nmo_ckparameterout_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckparameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Read destinations if present */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS) == NMO_OK) {
        int32_t count;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result == NMO_OK && count > 0) {
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

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterOut state to chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
nmo_status_t nmo_ckparameterout_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameterout_state_t *in_state = (const nmo_ckparameterout_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Write base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckparameter_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write destinations if any */
    if (in_state->destination_count > 0 && in_state->destination_ids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->destination_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->destination_count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->destination_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckparameterout,
    nmo_ckparameterout_state_t,
    nmo_ckparameterout_serialize,
    nmo_ckparameterout_deserialize,
    NMO_GUID_CKPARAMETEROUT,
    "CKParameterOut",
    NMO_CID_PARAMETEROUT,
    NMO_GUID_CKPARAMETER
)

