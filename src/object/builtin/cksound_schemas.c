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
    NMO_FIELD_REF_VALUE(nmo_wavesound_state_t, attached_object),
    NMO_FIELD_NAMED("position", offsetof(nmo_wavesound_state_t, position),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("direction", offsetof(nmo_wavesound_state_t, direction),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("version2_reserved_words",
                    offsetof(nmo_wavesound_state_t, version2_reserved_words),
                    sizeof(((nmo_wavesound_state_t *)0)->version2_reserved_words),
                    CKPGUID_VOIDBUF, 0, 0),
    NMO_FIELD_NAMED("modern_reserved_words",
                    offsetof(nmo_wavesound_state_t, modern_reserved_words),
                    sizeof(((nmo_wavesound_state_t *)0)->modern_reserved_words),
                    CKPGUID_VOIDBUF, 0, 0),
    NMO_FIELD_NAMED("legacy_data2_words",
                    offsetof(nmo_wavesound_state_t, legacy_data2_words),
                    sizeof(((nmo_wavesound_state_t *)0)->legacy_data2_words),
                    CKPGUID_VOIDBUF, 0, 0)
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

    size_t section_dwords = 0u;
    nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SOUNDFILENAME, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        uint32_t save_options = CKSOUND_USEGLOBAL;
        char *file_name = NULL;
        result = nmo_chunk_read_dword(chunk, &save_options);
        if (result != NMO_OK) {
            return result;
        }
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
            chunk, &file_name, NULL));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            /* The engine ignores the rest of a section; the residue merge keeps it. */
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(
                chunk, section_end - nmo_chunk_get_position(chunk)));
        }
        out_state->save_options = save_options;
        out_state->file_name = file_name;
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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_sound)

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
    memset(out_state->legacy_data2_words, 0,
           sizeof(out_state->legacy_data2_words));

    size_t section_dwords = 0u;
    nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_WAVSOUNDFILE, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        char *wave_file_name = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
            chunk, &wave_file_name, NULL));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            /* The engine ignores the rest of a section; the residue merge keeps it. */
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(
                chunk, section_end - nmo_chunk_get_position(chunk)));
        }
        out_state->has_wave_file_name = 1;
        out_state->wave_file_name = wave_file_name;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_WAVSOUNDDURATION, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t duration = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &duration));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            /* The engine ignores the rest of a section; the residue merge keeps it. */
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(
                chunk, section_end - nmo_chunk_get_position(chunk)));
        }
        out_state->has_duration = 1;
        out_state->duration = duration;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        nmo_wavesound_state_t data = *out_state;
        data.has_data2 = 1;
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        if (data_version >= 3) {
            if (section_dwords < 24u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.state_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.priority));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.gain));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pan));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pitch));

            for (size_t i = 0; i < 3u; ++i) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                    chunk, &data.modern_reserved_words[i]));
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

            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &data.modern_reserved_words[3]));
        } else if (data_version >= 2) {
            /* Legacy layout (CK2 data version 2) */
            if (section_dwords < 8u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &data.state_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.priority));

            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &data.version2_reserved_words[0]));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &data.version2_reserved_words[1]));

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.gain));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pan));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &data.pitch));

            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &data.version2_reserved_words[2]));

            /* Optional 3D block (not present for background sounds) */
            if ((data.state_flags & CK_WAVESOUND_ALLTYPE) !=
                CK_WAVESOUND_BACKGROUND) {
                if (section_dwords < 26u) {
                    return NMO_ERR_TRUNCATED_CHUNK;
                }
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                    chunk, &data.version2_reserved_words[3]));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                    chunk, &data.version2_reserved_words[4]));

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

                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                    chunk, &data.version2_reserved_words[5]));
            }
        } else {
            if (section_dwords < 20u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            for (size_t i = 0; i < 20u; ++i) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                    chunk, &data.legacy_data2_words[i]));
            }
            if (data.legacy_data2_words[8] != 0u) {
                data.state_flags |= CK_WAVESOUND_LOOPED;
            } else {
                data.state_flags &= ~CK_WAVESOUND_LOOPED;
            }
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            /* The engine ignores the rest of a section; the residue merge keeps it. */
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(
                chunk, section_end - nmo_chunk_get_position(chunk)));
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

static bool nmo_wavesound_has_nonzero_words(
    const uint32_t *words,
    size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (words[i] != 0u) return true;
    }
    return false;
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

    if (nmo_chunk_get_data_version(out_chunk) == 0u &&
        out_chunk->class_id == NMO_CID_WAVESOUND) {
        out_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    const uint32_t data_version =
        nmo_chunk_get_data_version(out_chunk);
    const bool has_version2_reserved = nmo_wavesound_has_nonzero_words(
        in_state->version2_reserved_words, 6u);
    const bool has_modern_reserved = nmo_wavesound_has_nonzero_words(
        in_state->modern_reserved_words, 4u);
    if ((!in_state->has_data2 &&
         (has_version2_reserved || has_modern_reserved)) ||
        (data_version < 2u &&
         (has_version2_reserved || has_modern_reserved)) ||
        (data_version == 2u && has_modern_reserved) ||
        (data_version >= 3u && has_version2_reserved) ||
        (data_version == 2u &&
         (in_state->state_flags & CK_WAVESOUND_ALLTYPE) ==
             CK_WAVESOUND_BACKGROUND &&
         nmo_wavesound_has_nonzero_words(
             &in_state->version2_reserved_words[3], 3u))) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "WaveSound DATA2 reserved words do not fit the requested layout");
    }

    nmo_status_t result = nmo_sound_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

    if (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDONLY) == 0) {
        NMO_RETURN_OK();
    }

    if ((is_file && in_state->has_wave_file_name) ||
        (!is_file && (save_flags & CK_STATESAVE_WAVSOUNDFILE) != 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_WAVSOUNDFILE);
        if (result != NMO_OK) return result;
        /* The engine never writes this section; its loader uses the string as it is. */
        result = nmo_chunk_write_string(out_chunk, in_state->wave_file_name);
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

        if (data_version < 2u) {
            for (size_t i = 0; i < 20u; ++i) {
                const uint32_t word = i == 8u
                    ? ((in_state->state_flags & CK_WAVESOUND_LOOPED) != 0u)
                    : in_state->legacy_data2_words[i];
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, word));
            }
        } else {
            const bool modern_layout = data_version >= 3u;
            const bool write_3d_data = modern_layout ||
                (in_state->state_flags & CK_WAVESOUND_ALLTYPE) !=
                    CK_WAVESOUND_BACKGROUND;
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->state_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->priority));
            if (!modern_layout) {
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, in_state->version2_reserved_words[0]));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, in_state->version2_reserved_words[1]));
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                out_chunk, in_state->gain));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                out_chunk, in_state->pan));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                out_chunk, in_state->pitch));

            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                out_chunk, modern_layout
                    ? in_state->modern_reserved_words[0]
                    : in_state->version2_reserved_words[2]));
            if (modern_layout) {
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, in_state->modern_reserved_words[1]));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, in_state->modern_reserved_words[2]));
            }

            if (write_3d_data) {
                if (!modern_layout) {
                    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                        out_chunk, in_state->version2_reserved_words[3]));
                    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                        out_chunk, in_state->version2_reserved_words[4]));
                }
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                    out_chunk, in_state->cone_in_angle));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                    out_chunk, in_state->cone_out_angle));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                    out_chunk, in_state->cone_out_gain));

                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                    out_chunk, in_state->min_distance));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                    out_chunk, in_state->max_distance));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, in_state->distance_behavior));

                NMO_RETURN_IF_ERROR(nmo_ref_write(
                    out_chunk, &in_state->attached_object));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer(
                    out_chunk, &in_state->position,
                    sizeof(in_state->position)));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer(
                    out_chunk, &in_state->direction,
                    sizeof(in_state->direction)));

                NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                    out_chunk, modern_layout
                        ? in_state->modern_reserved_words[3]
                        : in_state->version2_reserved_words[5]));
            }
        }
    }

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_wavesound)

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

    size_t section_dwords = 0u;
    nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_MIDISOUNDFILE, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        char *midi_file_name = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(
            chunk, &midi_file_name, NULL));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            /* The engine ignores the rest of a section; the residue merge keeps it. */
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(
                chunk, section_end - nmo_chunk_get_position(chunk)));
        }
        out_state->has_midi_file_name = 1;
        out_state->midi_file_name_from_file = 1;
        out_state->midi_file_name = midi_file_name;
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

    /* CKMidiSound::Save does not emit a MIDISOUNDFILE identifier; one that a
       file held stays, since Load reads it. */
    if (in_state->has_midi_file_name && in_state->midi_file_name_from_file) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MIDISOUNDFILE);
        if (result != NMO_OK) return result;
        return nmo_chunk_write_string(out_chunk, in_state->midi_file_name);
    }
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_midisound)

static nmo_status_t nmo_sound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_wavesound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_midisound_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_sound_set_defaults(void *instance)
{
    nmo_sound_state_t *state = instance;
    state->save_options = CKSOUND_USEGLOBAL;
}

static const nmo_object_state_member_t nmo_sound_members[] = {
    NMO_STATE_VALUE(nmo_sound_state_t, save_options),
    NMO_STATE_STRING(nmo_sound_state_t, file_name)
};

static const nmo_object_state_layout_t nmo_sound_layout = {
    .size = sizeof(nmo_sound_state_t),
    .base_vtable = &nmo_beobject_vtable,
    .base_size = sizeof(nmo_beobject_state_t),
    .members = nmo_sound_members,
    .member_count = sizeof(nmo_sound_members) / sizeof(nmo_sound_members[0]),
    .set_defaults = nmo_sound_set_defaults,
    .validate = nmo_sound_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(sound, nmo_sound_layout)

static void nmo_wavesound_set_defaults(void *instance)
{
    nmo_wavesound_state_t *state = instance;
    state->attached_object = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

/* Word 8 of the pre-version-2 DATA2 block holds the loop mode, which the
 * writer derives from state_flags, so it is copied but not compared. */
static const nmo_object_state_member_t nmo_wavesound_members[] = {
    NMO_STATE_VALUE(nmo_wavesound_state_t, has_wave_file_name),
    NMO_STATE_STRING(nmo_wavesound_state_t, wave_file_name),
    NMO_STATE_VALUE(nmo_wavesound_state_t, has_duration),
    NMO_STATE_VALUE(nmo_wavesound_state_t, duration),
    NMO_STATE_VALUE(nmo_wavesound_state_t, has_data2),
    NMO_STATE_VALUE(nmo_wavesound_state_t, state_flags),
    NMO_STATE_VALUE(nmo_wavesound_state_t, priority),
    NMO_STATE_VALUE(nmo_wavesound_state_t, gain),
    NMO_STATE_VALUE(nmo_wavesound_state_t, pan),
    NMO_STATE_VALUE(nmo_wavesound_state_t, pitch),
    NMO_STATE_VALUE(nmo_wavesound_state_t, cone_in_angle),
    NMO_STATE_VALUE(nmo_wavesound_state_t, cone_out_angle),
    NMO_STATE_VALUE(nmo_wavesound_state_t, cone_out_gain),
    NMO_STATE_VALUE(nmo_wavesound_state_t, min_distance),
    NMO_STATE_VALUE(nmo_wavesound_state_t, max_distance),
    NMO_STATE_VALUE(nmo_wavesound_state_t, distance_behavior),
    NMO_STATE_VALUE(nmo_wavesound_state_t, attached_object),
    NMO_STATE_VALUE(nmo_wavesound_state_t, position),
    NMO_STATE_VALUE(nmo_wavesound_state_t, direction),
    NMO_STATE_VALUE(nmo_wavesound_state_t, version2_reserved_words),
    NMO_STATE_VALUE(nmo_wavesound_state_t, modern_reserved_words),
    NMO_STATE_ELEMENTS(nmo_wavesound_state_t, legacy_data2_words, 0, 8),
    NMO_STATE_ELEMENTS_UNCOMPARED(nmo_wavesound_state_t, legacy_data2_words, 8, 1),
    NMO_STATE_ELEMENTS(nmo_wavesound_state_t, legacy_data2_words, 9, 11)
};

static const nmo_object_state_layout_t nmo_wavesound_layout = {
    .size = sizeof(nmo_wavesound_state_t),
    .base_vtable = &nmo_sound_vtable,
    .base_size = sizeof(nmo_sound_state_t),
    .members = nmo_wavesound_members,
    .member_count = sizeof(nmo_wavesound_members) / sizeof(nmo_wavesound_members[0]),
    .set_defaults = nmo_wavesound_set_defaults,
    .validate = nmo_wavesound_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(wavesound, nmo_wavesound_layout)

static const nmo_object_state_member_t nmo_midisound_members[] = {
    NMO_STATE_VALUE(nmo_midisound_state_t, has_midi_file_name),
    NMO_STATE_STRING(nmo_midisound_state_t, midi_file_name),
    NMO_STATE_VALUE(nmo_midisound_state_t, midi_file_name_from_file)
};

static const nmo_object_state_layout_t nmo_midisound_layout = {
    .size = sizeof(nmo_midisound_state_t),
    .base_vtable = &nmo_sound_vtable,
    .base_size = sizeof(nmo_sound_state_t),
    .members = nmo_midisound_members,
    .member_count = sizeof(nmo_midisound_members) / sizeof(nmo_midisound_members[0]),
    .validate = nmo_midisound_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(midisound, nmo_midisound_layout)

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_sound, nmo_sound_state_t, base, nmo_beobject_vtable)

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_sound)

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

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_wavesound, nmo_wavesound_state_t, base, nmo_sound_vtable)

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_wavesound)

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

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_midisound, nmo_midisound_state_t, base, nmo_sound_vtable)

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_midisound)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_sound_vtable = {
    .prepare_dependencies = nmo_sound_prepare_dependencies,
    .remap_dependencies = nmo_sound_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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
