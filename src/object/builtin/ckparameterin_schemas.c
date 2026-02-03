/**
 * @file ckparameterin_schemas.c
 * @brief CKParameterIn schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterIn.
 *
 * Based on official Virtools SDK (reference/src/CKParameterIn.cpp:140-250).
 */

#include "object/nmo_ckparameterin_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckparameterin, nmo_ckparameterin_state_t)

/* =============================================================================
 * CKParameterIn DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterIn state from chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:177-250
 */
nmo_status_t nmo_ckparameterin_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameterin_state_t *out_state = (nmo_ckparameterin_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version >= 1) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASHARED) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            if (data_version < 5) {
                nmo_object_id_t legacy_id = 0;
                (void)nmo_chunk_read_object_id(chunk, &legacy_id);
            }
            nmo_chunk_read_object_id(chunk, &out_state->source_id);
            out_state->is_shared = 1;
        } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            if (data_version < 5) {
                nmo_object_id_t legacy_id = 0;
                (void)nmo_chunk_read_object_id(chunk, &legacy_id);
            }
            nmo_chunk_read_object_id(chunk, &out_state->source_id);
            out_state->is_shared = 0;
        } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);

            nmo_object_id_t owner_id = 0;
            nmo_object_id_t out_source_id = 0;
            nmo_object_id_t param_id = 0;
            (void)nmo_chunk_read_object_id(chunk, &owner_id);
            (void)nmo_chunk_read_object_id(chunk, &out_source_id);
            (void)nmo_chunk_read_object_id(chunk, &param_id);

            if (out_source_id) {
                out_state->source_id = out_source_id;
                out_state->is_shared = 1;
            } else {
                out_state->source_id = param_id;
                out_state->is_shared = 0;
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED) == NMO_OK) {
            out_state->is_disabled = 1;
        }
    } else {
        /* Legacy path: keep minimal support by scanning known identifiers */
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASHARED) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_chunk_read_object_id(chunk, &out_state->source_id);
            out_state->is_shared = 1;
        }
        if (!out_state->is_shared &&
            nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_chunk_read_object_id(chunk, &out_state->source_id);
        }
    }

    /* Check if disabled */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED) == NMO_OK) {
        out_state->is_disabled = 1;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterIn state to chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:142-162
 */
nmo_status_t nmo_ckparameterin_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameterin_state_t *in_state = (const nmo_ckparameterin_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write identifier based on shared/direct source */
    uint32_t identifier = in_state->is_shared
        ? CK_STATESAVE_PARAMETERIN_DATASHARED
        : CK_STATESAVE_PARAMETERIN_DATASOURCE;

    result = nmo_chunk_write_identifier(out_chunk, identifier);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->source_id);
    if (result != NMO_OK) return result;

    /* Write disabled flag if needed */
    if (in_state->is_disabled) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckparameterin,
    nmo_ckparameterin_state_t,
    nmo_ckparameterin_serialize,
    nmo_ckparameterin_deserialize,
    NMO_GUID_CKPARAMETERIN,
    "CKParameterIn",
    NMO_CID_PARAMETERIN,
    NMO_GUID_CKOBJECT
)

