/**
 * @file cksound_schemas.c
 * @brief CKSound/CKWaveSound/CKMidiSound schema implementation
 */

#include "object/nmo_cksound_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
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
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksound_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckbeobject_deserialize_fn parent_deserialize = nmo_get_ckbeobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
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
    const nmo_cksound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_serialize"));
    }

    nmo_ckbeobject_serialize_fn parent_serialize = nmo_get_ckbeobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SOUNDFILENAME);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_string(out_chunk, in_state->file_name ? in_state->file_name : "");
}

/* =============================================================================
 * CKWaveSound
 * ============================================================================= */

nmo_result_t nmo_ckwavesound_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckwavesound_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_cksound_deserialize(chunk, arena, &out_state->base);
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
    const nmo_ckwavesound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_serialize"));
    }

    nmo_result_t result = nmo_cksound_serialize(&in_state->base, out_chunk, arena);
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
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckmidisound_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_cksound_deserialize(chunk, arena, &out_state->base);
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
    const nmo_ckmidisound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_serialize"));
    }

    nmo_result_t result = nmo_cksound_serialize(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    /* CKMidiSound::Save does not emit a MIDISOUNDFILE identifier */
    return nmo_result_ok();
}

/* =============================================================================
 * Schema registration
 * ============================================================================= */

nmo_result_t nmo_register_cksound_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    (void)registry;
    (void)arena;
    return nmo_result_ok();
}
