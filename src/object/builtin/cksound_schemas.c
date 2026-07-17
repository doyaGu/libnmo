/**
 * @file cksound_schemas.c
 * @brief CKSound/CKWaveSound/CKMidiSound schema implementation
 */

#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include <string.h>

static nmo_status_t read_exact_sized_buffer(
    nmo_chunk_t *chunk,
    void *buffer,
    size_t expected_size)
{
    size_t actual_size = 0;
    nmo_status_t result = nmo_chunk_read_and_fill_buffer_checked(
        chunk, buffer, expected_size, &actual_size);
    if (result != NMO_OK) return result;
    return actual_size == expected_size ? NMO_OK : NMO_ERR_INVALID_FORMAT;
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    sound,
    nmo_sound_state_t,
    do {
        state->save_options = CKSOUND_USEGLOBAL;
        state->file_name = NULL;
    } while (0),
    ((void)0))

NMO_DEFINE_OBJECT_LIFECYCLE(
    wavesound,
    nmo_wavesound_state_t,
    do {
        state->base.save_options = CKSOUND_USEGLOBAL;
        state->base.file_name = NULL;
        state->attached_object = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    } while (0),
    ((void)0))

static void nmo_sound_dispose_base_arrays(nmo_sound_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->base.scripts);
    nmo_array_dispose(&state->base.attributes);
    nmo_array_dispose(&state->base.legacy_attributes);
}

static void nmo_sound_copy_base_allocators(
    nmo_sound_state_t *dst,
    const nmo_sound_state_t *src)
{
    if (src->base.scripts.allocator.alloc != NULL) {
        dst->base.scripts.allocator = src->base.scripts.allocator;
    }
    if (src->base.attributes.allocator.alloc != NULL) {
        dst->base.attributes.allocator = src->base.attributes.allocator;
    }
    if (src->base.legacy_attributes.allocator.alloc != NULL) {
        dst->base.legacy_attributes.allocator =
            src->base.legacy_attributes.allocator;
    }
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    midisound,
    nmo_midisound_state_t,
    do {
        state->base.save_options = CKSOUND_USEGLOBAL;
        state->base.file_name = NULL;
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_sound_state_t, base),
                    sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sound_state_t, save_options, NMO_GUID_ENUM_CK_SOUND_SAVEOPTIONS),
    NMO_FIELD_OPT(nmo_sound_state_t, file_name, CKPGUID_STRING)
};

static const nmo_type_field_t nmo_wavesound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_wavesound_state_t, base),
                    sizeof(nmo_sound_state_t), CKPGUID_SOUND,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_wavesound_state_t, has_wave_file_name, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_wavesound_state_t, wave_file_name, CKPGUID_STRING),
    NMO_FIELD(nmo_wavesound_state_t, has_duration, CKPGUID_BOOL),
    NMO_FIELD(nmo_wavesound_state_t, duration, CKPGUID_INT),
    NMO_FIELD(nmo_wavesound_state_t, has_data2, CKPGUID_BOOL),
    NMO_FIELD(nmo_wavesound_state_t, state_flags, NMO_GUID_ENUM_CK_WAVESOUND_STATE),
    NMO_FIELD(nmo_wavesound_state_t, priority, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, gain, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, pan, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, pitch, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, cone_in_angle, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, cone_out_angle, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, cone_out_gain, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, min_distance, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, max_distance, CKPGUID_FLOAT),
    NMO_FIELD(nmo_wavesound_state_t, distance_behavior, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_wavesound_state_t, attached_object),
    NMO_FIELD_NAMED("position", offsetof(nmo_wavesound_state_t, position),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("direction", offsetof(nmo_wavesound_state_t, direction),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0)
};

static const nmo_type_field_t nmo_midisound_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_midisound_state_t, base),
                    sizeof(nmo_sound_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_midisound_state_t, has_midi_file_name, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_midisound_state_t, midi_file_name, CKPGUID_STRING)
};

static const char *nmo_sound_basename(const char *path)
{
    if (!path) {
        return NULL;
    }

    const char *last = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return last;
}

/* =============================================================================
 * CKSound
 * ============================================================================= */

static nmo_status_t nmo_sound_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sound_state_t *out_state = (nmo_sound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sound_deserialize");
    }

    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->file_name = NULL;
    out_state->save_options = CKSOUND_USEGLOBAL;

    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_SOUNDFILENAME);
    if (seek_result == NMO_OK) {
        nmo_status_t result = nmo_chunk_read_dword(chunk, &out_state->save_options);
        if (result != NMO_OK) {
            return result;
        }
        NMO_RETURN_IF_ERROR(
            nmo_chunk_read_string_checked(chunk, &out_state->file_name, NULL));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_sound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sound_state_t *out_state = (nmo_sound_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_sound_state_t decoded = {0};
    nmo_sound_copy_base_allocators(&decoded, out_state);
    nmo_status_t result = nmo_sound_deserialize_internal(
        &decoded, chunk, NULL, context);
    if (result != NMO_OK) {
        nmo_sound_dispose_base_arrays(&decoded);
        return result;
    }
    nmo_sound_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_sound_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sound_state_t *in_state = (const nmo_sound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sound_serialize");
    }

    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SOUNDFILENAME);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
    if (result != NMO_OK) return result;

    const char *base_name = nmo_sound_basename(in_state->file_name);
    return nmo_chunk_write_string(out_chunk, base_name);
}

nmo_status_t nmo_sound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_sound_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * CKWaveSound
 * ============================================================================= */

static nmo_status_t nmo_wavesound_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_wavesound_state_t *out_state = (nmo_wavesound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_wavesound_deserialize");
    }

    nmo_status_t result = nmo_sound_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_wave_file_name = 0;
    out_state->wave_file_name = NULL;
    out_state->has_duration = 0;
    out_state->duration = 0;
    out_state->has_data2 = 0;
    out_state->state_flags = 0;
    out_state->priority = 0.0f;
    out_state->gain = 0.0f;
    out_state->pan = 0.0f;
    out_state->pitch = 0.0f;
    out_state->cone_in_angle = 0.0f;
    out_state->cone_out_angle = 0.0f;
    out_state->cone_out_gain = 0.0f;
    out_state->min_distance = 0.0f;
    out_state->max_distance = 0.0f;
    out_state->distance_behavior = 0;
    out_state->attached_object = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->position = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    out_state->direction = (nmo_vector_t){0.0f, 0.0f, 0.0f};

    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_WAVSOUNDFILE);
    if (seek_result == NMO_OK) {
        out_state->has_wave_file_name = 1;
        NMO_RETURN_IF_ERROR(
            nmo_chunk_read_string_checked(chunk, &out_state->wave_file_name, NULL));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_WAVSOUNDDURATION);
    if (seek_result == NMO_OK) {
        out_state->has_duration = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->duration));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_WAVSOUNDDATA2);
    if (seek_result == NMO_OK) {
        nmo_wavesound_state_t data = *out_state;
        data.has_data2 = 1;
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        if (data_version >= 3) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.state_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.priority));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.gain));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pan));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pitch));

            /* Reserved floats */
            {
                float reserved = 0.0f;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));
            }

            /* Cone fields */
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_in_angle));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_out_angle));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_out_gain));

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.min_distance));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.max_distance));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.distance_behavior));

            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &data.attached_object));
            NMO_RETURN_IF_ERROR(read_exact_sized_buffer(
                chunk, &data.position, sizeof(data.position)));
            NMO_RETURN_IF_ERROR(read_exact_sized_buffer(
                chunk, &data.direction, sizeof(data.direction)));

            /* Reserved */
            {
                uint32_t reserved = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &reserved));
            }
        } else if (data_version >= 2) {
            /* Legacy layout (CK2 data version 2) */
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.state_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.priority));

            {
                uint32_t reserved = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &reserved));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &reserved));
            }

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.gain));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pan));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pitch));

            {
                float reserved = 0.0f;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));
            }

            /* Optional 3D block (not present for background sounds) */
            if (chunk && chunk->data.count > nmo_chunk_get_position(chunk)) {
                size_t remaining = chunk->data.count - nmo_chunk_get_position(chunk);
                if (remaining >= 16) {
                    float reserved = 0.0f;
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &reserved));

                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_in_angle));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_out_angle));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.cone_out_gain));

                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.min_distance));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.max_distance));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.distance_behavior));

                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &data.attached_object));
                    NMO_RETURN_IF_ERROR(read_exact_sized_buffer(
                        chunk, &data.position, sizeof(data.position)));
                    NMO_RETURN_IF_ERROR(read_exact_sized_buffer(
                        chunk, &data.direction, sizeof(data.direction)));

                    {
                        int32_t reserved_int = 0;
                        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &reserved_int));
                    }
                }
            }
        }
        nmo_ref_check_class(
            &data.attached_object,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        *out_state = data;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_wavesound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_wavesound_state_t *out_state = (nmo_wavesound_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_wavesound_state_t decoded = {0};
    nmo_sound_copy_base_allocators(&decoded.base, &out_state->base);
    nmo_status_t result = nmo_wavesound_deserialize_internal(
        &decoded, chunk, NULL, context);
    if (result != NMO_OK) {
        nmo_sound_dispose_base_arrays(&decoded.base);
        return result;
    }
    nmo_sound_dispose_base_arrays(&out_state->base);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_wavesound_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_wavesound_state_t *in_state = (const nmo_wavesound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_wavesound_serialize");
    }

    nmo_status_t result = nmo_sound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

    if (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDONLY) == 0) {
        NMO_RETURN_OK();
    }

    if ((is_file && in_state->has_wave_file_name) ||
        (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDFILE) != 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDFILE);
        if (result != NMO_OK) return result;
        const char *base_name = nmo_sound_basename(in_state->wave_file_name);
        result = nmo_chunk_write_string(out_chunk, base_name);
        if (result != NMO_OK) return result;
    }

    if ((is_file && in_state->has_duration) ||
        (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDDURATION) != 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDURATION);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->duration);
        if (result != NMO_OK) return result;
    }

    if ((is_file && in_state->has_data2) ||
        (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDDATA2) != 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDDATA2);
        if (result != NMO_OK) return result;

        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->state_flags));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->priority));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->gain));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->pan));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->pitch));

        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, 0.0f));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, 0.0f));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, 0.0f));

        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->cone_in_angle));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->cone_out_angle));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->cone_out_gain));

        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->min_distance));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->max_distance));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->distance_behavior));

        NMO_RETURN_IF_ERROR(nmo_ref_write(out_chunk, &in_state->attached_object));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer(out_chunk, &in_state->position, sizeof(in_state->position)));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer(out_chunk, &in_state->direction, sizeof(in_state->direction)));

        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, 0));
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_wavesound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_wavesound_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * CKMidiSound
 * ============================================================================= */

static nmo_status_t nmo_midisound_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_midisound_state_t *out_state = (nmo_midisound_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_midisound_deserialize");
    }

    nmo_status_t result = nmo_sound_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_midi_file_name = 0;
    out_state->midi_file_name = NULL;

    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_MIDISOUNDFILE);
    if (seek_result == NMO_OK) {
        out_state->has_midi_file_name = 1;
        NMO_RETURN_IF_ERROR(
            nmo_chunk_read_string_checked(chunk, &out_state->midi_file_name, NULL));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_midisound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_midisound_state_t *out_state = (nmo_midisound_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_midisound_state_t decoded = {0};
    nmo_sound_copy_base_allocators(&decoded.base, &out_state->base);
    nmo_status_t result = nmo_midisound_deserialize_internal(
        &decoded, chunk, NULL, context);
    if (result != NMO_OK) {
        nmo_sound_dispose_base_arrays(&decoded.base);
        return result;
    }
    nmo_sound_dispose_base_arrays(&out_state->base);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_midisound_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_midisound_state_t *in_state = (const nmo_midisound_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_midisound_serialize");
    }

    nmo_status_t result = nmo_sound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* CKMidiSound::Save does not emit a MIDISOUNDFILE identifier */
    NMO_RETURN_OK();
}

nmo_status_t nmo_midisound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_midisound_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_sound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_sound_state_t *s = src;
    nmo_sound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_string(arena, &d->file_name, s->file_name);
}

static nmo_status_t nmo_sound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

nmo_status_t nmo_sound_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_sound_validate(instance, type, context);
}

nmo_status_t nmo_sound_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sound_remap_dependencies");
    }

    nmo_sound_state_t *state = (nmo_sound_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_beobject_remap_dependencies(&state->base, NULL, context));

    return nmo_sound_validate(state, NULL, NULL);
}

static nmo_status_t nmo_wavesound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_wavesound_state_t *s = src;
    nmo_wavesound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
    return nmo_object_copy_string(arena, &d->wave_file_name, s->wave_file_name);
}

static nmo_status_t nmo_wavesound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

nmo_status_t nmo_wavesound_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_wavesound_validate(instance, type, context);
}

nmo_status_t nmo_wavesound_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_wavesound_remap_dependencies");
    }

    nmo_wavesound_state_t *state = (nmo_wavesound_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_sound_remap_dependencies(&state->base, NULL, context));

    /* Preserve DATA2 presence and unresolved attachment reference. */
    return nmo_wavesound_validate(state, NULL, NULL);
}

static nmo_status_t nmo_midisound_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_midisound_state_t *s = src;
    nmo_midisound_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
    return nmo_object_copy_string(arena, &d->midi_file_name, s->midi_file_name);
}

static nmo_status_t nmo_midisound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

nmo_status_t nmo_midisound_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_midisound_validate(instance, type, context);
}

nmo_status_t nmo_midisound_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_midisound_remap_dependencies");
    }

    nmo_midisound_state_t *state = (nmo_midisound_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_sound_remap_dependencies(&state->base, NULL, context));

    return nmo_midisound_validate(state, NULL, NULL);
}

static nmo_status_t nmo_sound_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sound_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_sound_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_wavesound_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_wavesound_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_wavesound_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_midisound_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_midisound_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_midisound_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(sound, nmo_sound_state_t)
NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(wavesound, nmo_wavesound_state_t)
NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(midisound, nmo_midisound_state_t)

nmo_type_vtable_t nmo_sound_vtable = {
    .prepare_dependencies = nmo_sound_prepare_dependencies,
    .remap_dependencies = nmo_sound_remap_dependencies,
    .pre_delete = nmo_sound_pre_delete,
    .post_delete = nmo_sound_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_sound_create,
        nmo_sound_destroy,
        nmo_sound_serialize,
        nmo_sound_deserialize,
        nmo_sound_copy,
        nmo_sound_validate,
        nmo_sound_equals,
        nmo_sound_hash)
};

nmo_type_vtable_t nmo_wavesound_vtable = {
    .prepare_dependencies = nmo_wavesound_prepare_dependencies,
    .remap_dependencies = nmo_wavesound_remap_dependencies,
    .pre_delete = nmo_wavesound_pre_delete,
    .post_delete = nmo_wavesound_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_wavesound_create,
        nmo_wavesound_destroy,
        nmo_wavesound_serialize,
        nmo_wavesound_deserialize,
        nmo_wavesound_copy,
        nmo_wavesound_validate,
        nmo_wavesound_equals,
        nmo_wavesound_hash)
};

nmo_type_vtable_t nmo_midisound_vtable = {
    .prepare_dependencies = nmo_midisound_prepare_dependencies,
    .remap_dependencies = nmo_midisound_remap_dependencies,
    .pre_delete = nmo_midisound_pre_delete,
    .post_delete = nmo_midisound_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_midisound_create,
        nmo_midisound_destroy,
        nmo_midisound_serialize,
        nmo_midisound_deserialize,
        nmo_midisound_copy,
        nmo_midisound_validate,
        nmo_midisound_equals,
        nmo_midisound_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_sound_type,
    CKPGUID_SOUND,
    "CKSound",
    NMO_CID_SOUND,
    CKPGUID_BEOBJECT,
    nmo_sound_state_t,
    &nmo_sound_vtable,
    nmo_sound_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_wavesound_type,
    CKPGUID_WAVESOUND,
    "CKWaveSound",
    NMO_CID_WAVESOUND,
    CKPGUID_SOUND,
    nmo_wavesound_state_t,
    &nmo_wavesound_vtable,
    nmo_wavesound_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_midisound_type,
    CKPGUID_MIDISOUND,
    "CKMidiSound",
    NMO_CID_MIDISOUND,
    CKPGUID_SOUND,
    nmo_midisound_state_t,
    &nmo_midisound_vtable,
    nmo_midisound_fields)
