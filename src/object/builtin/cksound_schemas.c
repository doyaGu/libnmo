/**
 * @file cksound_schemas.c
 * @brief CKSound/CKWaveSound/CKMidiSound schema implementation
 */

#include "object/nmo_cksound_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cksound, nmo_cksound_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckwavesound, nmo_ckwavesound_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckmidisound, nmo_ckmidisound_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_cksound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_cksound_state_t, base),
                    sizeof(nmo_ckbeobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_cksound_state_t, save_options, NMO_GUID_ENUM_CK_SOUND_SAVEOPTIONS),
    NMO_FIELD_OPT(nmo_cksound_state_t, file_name, CKPGUID_STRING)
};

static const nmo_type_field_t nmo_ckwavesound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckwavesound_state_t, base),
                    sizeof(nmo_cksound_state_t), CKPGUID_SOUND,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckwavesound_state_t, has_wave_file_name, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_ckwavesound_state_t, wave_file_name, CKPGUID_STRING),
    NMO_FIELD(nmo_ckwavesound_state_t, has_duration, CKPGUID_BOOL),
    NMO_FIELD(nmo_ckwavesound_state_t, duration, CKPGUID_INT),
    NMO_FIELD(nmo_ckwavesound_state_t, has_data2, CKPGUID_BOOL),
    NMO_FIELD(nmo_ckwavesound_state_t, state_flags, NMO_GUID_ENUM_CK_WAVESOUND_STATE),
    NMO_FIELD(nmo_ckwavesound_state_t, priority, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, gain, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, pan, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, pitch, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, cone_in_angle, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, cone_out_angle, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, cone_out_gain, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, min_distance, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, max_distance, CKPGUID_FLOAT),
    NMO_FIELD(nmo_ckwavesound_state_t, distance_behavior, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_ckwavesound_state_t, attached_object_id),
    NMO_FIELD_NAMED("position", offsetof(nmo_ckwavesound_state_t, position),
                    sizeof(nmo_vx_vector3_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("direction", offsetof(nmo_ckwavesound_state_t, direction),
                    sizeof(nmo_vx_vector3_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0)
};

static const nmo_type_field_t nmo_ckmidisound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckmidisound_state_t, base),
                    sizeof(nmo_cksound_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckmidisound_state_t, has_midi_file_name, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_ckmidisound_state_t, midi_file_name, CKPGUID_STRING)
};

/* =============================================================================
 * CKSound
 * ============================================================================= */

nmo_status_t nmo_cksound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksound_state_t *out_state = (nmo_cksound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_deserialize");
    }

    nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SOUNDFILENAME) == NMO_OK) {
        nmo_status_t result = nmo_chunk_read_dword(chunk, &out_state->save_options);
        if (result != NMO_OK) {
            return result;
        }
        (void)nmo_chunk_read_string(chunk, &out_state->file_name);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_cksound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksound_state_t *in_state = (const nmo_cksound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksound_serialize");
    }

    nmo_status_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SOUNDFILENAME);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_string(out_chunk, in_state->file_name ? in_state->file_name : "");
}

/* =============================================================================
 * CKWaveSound
 * ============================================================================= */

nmo_status_t nmo_ckwavesound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckwavesound_state_t *out_state = (nmo_ckwavesound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_deserialize");
    }

    nmo_status_t result = nmo_cksound_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDFILE) == NMO_OK) {
        out_state->has_wave_file_name = 1;
        (void)nmo_chunk_read_string(chunk, &out_state->wave_file_name);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDDURATION) == NMO_OK) {
        out_state->has_duration = 1;
        nmo_chunk_read_int(chunk, &out_state->duration);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_WAVSOUNDDATA2) == NMO_OK) {
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

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckwavesound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckwavesound_state_t *in_state = (const nmo_ckwavesound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckwavesound_serialize");
    }

    nmo_status_t result = nmo_cksound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (in_state->has_duration) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDURATION);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->duration);
        if (result != NMO_OK) return result;
    }

    if (in_state->has_data2) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDATA2);
        if (result != NMO_OK) return result;

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

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKMidiSound
 * ============================================================================= */

nmo_status_t nmo_ckmidisound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckmidisound_state_t *out_state = (nmo_ckmidisound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_deserialize");
    }

    nmo_status_t result = nmo_cksound_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MIDISOUNDFILE) == NMO_OK) {
        out_state->has_midi_file_name = 1;
        (void)nmo_chunk_read_string(chunk, &out_state->midi_file_name);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckmidisound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckmidisound_state_t *in_state = (const nmo_ckmidisound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmidisound_serialize");
    }

    nmo_status_t result = nmo_cksound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* CKMidiSound::Save does not emit a MIDISOUNDFILE identifier */
    NMO_RETURN_OK();
}

static nmo_status_t cksound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_cksound_state_t *s = src;
    nmo_cksound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_string(arena, &d->file_name, s->file_name);
}

static nmo_status_t cksound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

static nmo_status_t ckwavesound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckwavesound_state_t *s = src;
    nmo_ckwavesound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
    return nmo_object_copy_string(arena, &d->wave_file_name, s->wave_file_name);
}

static nmo_status_t ckwavesound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

static nmo_status_t ckmidisound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckmidisound_state_t *s = src;
    nmo_ckmidisound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
    return nmo_object_copy_string(arena, &d->midi_file_name, s->midi_file_name);
}

static nmo_status_t ckmidisound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    cksound,
    nmo_cksound_state_t,
    nmo_cksound_serialize,
    nmo_cksound_deserialize,
    nmo_cksound_fields,
    CKPGUID_SOUND,
    "CKSound",
    NMO_CID_SOUND,
    CKPGUID_BEOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckwavesound,
    nmo_ckwavesound_state_t,
    nmo_ckwavesound_serialize,
    nmo_ckwavesound_deserialize,
    nmo_ckwavesound_fields,
    CKPGUID_WAVESOUND,
    "CKWaveSound",
    NMO_CID_WAVESOUND,
    CKPGUID_SOUND
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckmidisound,
    nmo_ckmidisound_state_t,
    nmo_ckmidisound_serialize,
    nmo_ckmidisound_deserialize,
    nmo_ckmidisound_fields,
    CKPGUID_MIDISOUND,
    "CKMidiSound",
    NMO_CID_MIDISOUND,
    CKPGUID_SOUND
)


