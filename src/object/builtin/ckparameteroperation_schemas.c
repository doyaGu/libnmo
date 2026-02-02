/**
 * @file ckparameteroperation_schemas.c
 * @brief CKParameterOperation schema implementation
 */

#include "object/nmo_ckparameteroperation_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

/* CKDefines2.h identifiers */
#define CK_STATESAVE_OPERATIONINPUTS      0x00000040u
#define CK_STATESAVE_OPERATIONOUTPUT      0x00000080u
#define CK_STATESAVE_OPERATIONOP          0x00000100u
#define CK_STATESAVE_OPERATIONDEFAULTDATA 0x00000200u
#define CK_STATESAVE_OPERATIONNEWDATA     0x00000400u

nmo_result_t nmo_ckparameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameteroperation_state_t *out_state = (nmo_ckparameteroperation_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameteroperation_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_result_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    const int is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    if (is_file) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONNEWDATA).code == NMO_OK) {
            (void)nmo_chunk_read_guid(chunk, &out_state->operation_guid);

            size_t count = 0;
            if (nmo_chunk_read_object_sequence_start(chunk, &count).code == NMO_OK) {
                if (nmo_chunk_get_data_version(chunk) < 5 && count > 3) {
                    nmo_object_id_t dummy = 0;
                    (void)nmo_chunk_read_object_sequence_item(chunk, &dummy);
                    count -= 1;
                }
                if (count >= 1) {
                    (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->in1_id);
                    out_state->has_in1 = 1;
                }
                if (count >= 2) {
                    (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->in2_id);
                    out_state->has_in2 = 1;
                }
                if (count >= 3) {
                    (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->out_id);
                    out_state->has_out = 1;
                }
            }
            return nmo_result_ok();
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP).code == NMO_OK) {
            (void)nmo_chunk_read_guid(chunk, &out_state->operation_guid);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA).code == NMO_OK) {
            out_state->has_owner = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->owner_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT).code == NMO_OK) {
            out_state->has_out = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->out_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS).code == NMO_OK) {
            out_state->has_in1 = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->in1_id);
            out_state->has_in2 = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->in2_id);
        }

        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP).code == NMO_OK) {
        (void)nmo_chunk_read_guid(chunk, &out_state->operation_guid);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA).code == NMO_OK) {
        out_state->has_owner = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->owner_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT).code == NMO_OK) {
        out_state->has_out = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->out_id);
        (void)nmo_chunk_read_sub_chunk(chunk, &out_state->out_chunk);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS).code == NMO_OK) {
        out_state->has_in1 = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->in1_id);
        (void)nmo_chunk_read_sub_chunk(chunk, &out_state->in1_chunk);

        out_state->has_in2 = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->in2_id);
        (void)nmo_chunk_read_sub_chunk(chunk, &out_state->in2_chunk);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckparameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameteroperation_state_t *in_state =
        (const nmo_ckparameteroperation_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameteroperation_serialize"));
    }

    {
        nmo_result_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    const int is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    if (is_file) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONNEWDATA);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_guid(out_chunk, in_state->operation_guid);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, 3);
        if (result.code != NMO_OK) return result;

        (void)nmo_chunk_write_object_sequence_item(out_chunk, in_state->in1_id);
        (void)nmo_chunk_write_object_sequence_item(out_chunk, in_state->in2_id);
        (void)nmo_chunk_write_object_sequence_item(out_chunk, in_state->out_id);

        return nmo_result_ok();
    }

    if (!nmo_guid_is_null(in_state->operation_guid)) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONOP);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_guid(out_chunk, in_state->operation_guid);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_owner) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONDEFAULTDATA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->owner_id);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_in1 || in_state->has_in2) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONINPUTS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->in1_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->in1_chunk);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->in2_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->in2_chunk);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_out) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONOUTPUT);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->out_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->out_chunk);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckparameteroperation,
    nmo_ckparameteroperation_state_t,
    nmo_ckparameteroperation_serialize,
    nmo_ckparameteroperation_deserialize,
    NMO_GUID_CKPARAMETEROPERATION,
    "CKParameterOperation",
    NMO_CID_PARAMETEROPERATION,
    NMO_GUID_CKOBJECT
)

