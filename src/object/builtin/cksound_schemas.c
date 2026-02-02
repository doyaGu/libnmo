/**
 * @file cksound_schemas.c
 * @brief CKSound/CKWaveSound/CKMidiSound schema implementation
 */

#include "object/nmo_cksound_schemas.h"
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
#define CK_STATESAVE_SOUNDFILENAME     0x00001000u
#define CK_STATESAVE_WAVSOUNDFILE      0x00100000u
#define CK_STATESAVE_WAVSOUNDDATA2     0x00400000u
#define CK_STATESAVE_WAVSOUNDDURATION  0x00800000u
#define CK_STATESAVE_MIDISOUNDFILE     0x00100000u

/* =============================================================================
 * CKSound
 * ============================================================================= */

nmo_result_t nmo_cksound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksound_state_t *out_state = (nmo_cksound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SOUNDFILENAME).code == NMO_OK) {
        nmo_result_t result = nmo_chunk_read_dword(chunk, &out_state->save_options);
        if (result.code != NMO_OK) {
            return result;
        }
        (void)nmo_chunk_read_string(chunk, &out_state->file_name);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_cksound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksound_state_t *in_state = (const nmo_cksound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_serialize"));
    }

    nmo_result_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SOUNDFILENAME);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_string(out_chunk, in_state->file_name ? in_state->file_name : "");
}

/* =============================================================================
 * CKWaveSound
 * ============================================================================= */

nmo_result_t nmo_ckwavesound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckwavesound_state_t *out_state = (nmo_ckwavesound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_cksound_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDFILE).code == NMO_OK) {
        out_state->has_wave_file_name = 1;
        (void)nmo_chunk_read_string(chunk, &out_state->wave_file_name);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDDURATION).code == NMO_OK) {
        out_state->has_duration = 1;
        nmo_chunk_read_int(chunk, &out_state->duration);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDDATA2).code == NMO_OK) {
        out_state->has_data2 = 1;
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        if (data_version >= 3) {
            nmo_chunk_read_dword(chunk, &out_state->state_flags);
            nmo_chunk_read_float(chunk, &out_state->priority);
            nmo_chunk_read_float(chunk, &out_state->gain);
            nmo_chunk_read_float(chunk, &out_state->pan);
            nmo_chunk_read_float(chunk, &out_state->pitch);

            /* Reserved floats */
            {
                float reserved = 0.0f;
                nmo_chunk_read_float(chunk, &reserved);
                nmo_chunk_read_float(chunk, &reserved);
                nmo_chunk_read_float(chunk, &reserved);
            }

            /* Cone fields */
            nmo_chunk_read_float(chunk, &out_state->cone_in_angle);
            nmo_chunk_read_float(chunk, &out_state->cone_out_angle);
            nmo_chunk_read_float(chunk, &out_state->cone_out_gain);

            nmo_chunk_read_float(chunk, &out_state->min_distance);
            nmo_chunk_read_float(chunk, &out_state->max_distance);
            nmo_chunk_read_dword(chunk, &out_state->distance_behavior);

            nmo_chunk_read_object_id(chunk, &out_state->attached_object_id);
            (void)nmo_chunk_read_and_fill_buffer(chunk, &out_state->position,
                sizeof(out_state->position));
            (void)nmo_chunk_read_and_fill_buffer(chunk, &out_state->direction,
                sizeof(out_state->direction));

            /* Reserved */
            {
                uint32_t reserved = 0;
                nmo_chunk_read_dword(chunk, &reserved);
            }
        } else if (data_version >= 2) {
            /* Legacy layout (CK2 data version 2) */
            nmo_chunk_read_dword(chunk, &out_state->state_flags);
            nmo_chunk_read_float(chunk, &out_state->priority);

            {
                uint32_t reserved = 0;
                nmo_chunk_read_dword(chunk, &reserved);
                nmo_chunk_read_dword(chunk, &reserved);
            }

            nmo_chunk_read_float(chunk, &out_state->gain);
            nmo_chunk_read_float(chunk, &out_state->pan);
            nmo_chunk_read_float(chunk, &out_state->pitch);

            {
                float reserved = 0.0f;
                nmo_chunk_read_float(chunk, &reserved);
            }

            /* Optional 3D block (not present for background sounds) */
            if (chunk && chunk->data.count > nmo_chunk_get_position(chunk)) {
                size_t remaining = chunk->data.count - nmo_chunk_get_position(chunk);
                if (remaining >= 16) {
                    float reserved = 0.0f;
                    nmo_chunk_read_float(chunk, &reserved);
                    nmo_chunk_read_float(chunk, &reserved);

                    nmo_chunk_read_float(chunk, &out_state->cone_in_angle);
                    nmo_chunk_read_float(chunk, &out_state->cone_out_angle);
                    nmo_chunk_read_float(chunk, &out_state->cone_out_gain);

                    nmo_chunk_read_float(chunk, &out_state->min_distance);
                    nmo_chunk_read_float(chunk, &out_state->max_distance);
                    nmo_chunk_read_dword(chunk, &out_state->distance_behavior);

                    nmo_chunk_read_object_id(chunk, &out_state->attached_object_id);
                    (void)nmo_chunk_read_and_fill_buffer(chunk, &out_state->position,
                        sizeof(out_state->position));
                    (void)nmo_chunk_read_and_fill_buffer(chunk, &out_state->direction,
                        sizeof(out_state->direction));

                    {
                        int32_t reserved_int = 0;
                        nmo_chunk_read_int(chunk, &reserved_int);
                    }
                }
            }
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckwavesound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckwavesound_state_t *in_state = (const nmo_ckwavesound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_serialize"));
    }

    nmo_result_t result = nmo_cksound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (in_state->has_duration) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDURATION);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->duration);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_data2) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDATA2);
        if (result.code != NMO_OK) return result;

        nmo_chunk_write_dword(out_chunk, in_state->state_flags);
        nmo_chunk_write_float(out_chunk, in_state->priority);
        nmo_chunk_write_float(out_chunk, in_state->gain);
        nmo_chunk_write_float(out_chunk, in_state->pan);
        nmo_chunk_write_float(out_chunk, in_state->pitch);

        nmo_chunk_write_float(out_chunk, 0.0f);
        nmo_chunk_write_float(out_chunk, 0.0f);
        nmo_chunk_write_float(out_chunk, 0.0f);

        nmo_chunk_write_float(out_chunk, in_state->cone_in_angle);
        nmo_chunk_write_float(out_chunk, in_state->cone_out_angle);
        nmo_chunk_write_float(out_chunk, in_state->cone_out_gain);

        nmo_chunk_write_float(out_chunk, in_state->min_distance);
        nmo_chunk_write_float(out_chunk, in_state->max_distance);
        nmo_chunk_write_dword(out_chunk, in_state->distance_behavior);

        nmo_chunk_write_object_id(out_chunk, in_state->attached_object_id);
        nmo_chunk_write_buffer(out_chunk, &in_state->position, sizeof(in_state->position));
        nmo_chunk_write_buffer(out_chunk, &in_state->direction, sizeof(in_state->direction));

        nmo_chunk_write_dword(out_chunk, 0);
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKMidiSound
 * ============================================================================= */

nmo_result_t nmo_ckmidisound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckmidisound_state_t *out_state = (nmo_ckmidisound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_cksound_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MIDISOUNDFILE).code == NMO_OK) {
        out_state->has_midi_file_name = 1;
        (void)nmo_chunk_read_string(chunk, &out_state->midi_file_name);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckmidisound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckmidisound_state_t *in_state = (const nmo_ckmidisound_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_serialize"));
    }

    nmo_result_t result = nmo_cksound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    /* CKMidiSound::Save does not emit a MIDISOUNDFILE identifier */
    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cksound,
    nmo_cksound_state_t,
    nmo_cksound_serialize,
    nmo_cksound_deserialize,
    NMO_GUID_CKSOUND,
    "CKSound",
    NMO_CID_SOUND,
    NMO_GUID_CKBEOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckwavesound,
    nmo_ckwavesound_state_t,
    nmo_ckwavesound_serialize,
    nmo_ckwavesound_deserialize,
    NMO_GUID_CKWAVESOUND,
    "CKWaveSound",
    NMO_CID_WAVESOUND,
    NMO_GUID_CKSOUND
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckmidisound,
    nmo_ckmidisound_state_t,
    nmo_ckmidisound_serialize,
    nmo_ckmidisound_deserialize,
    NMO_GUID_CKMIDISOUND,
    "CKMidiSound",
    NMO_CID_MIDISOUND,
    NMO_GUID_CKSOUND
)

