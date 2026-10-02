/**
 * @file cktexture_schemas.c
 * @brief CKTexture schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKTexture (ClassID 31) deserialization, serialization,
 * and runtime dependency hooks.
 *
 * Reference: docs/CK2_3D_reverse_notes.md lines 341-348
 */

#include "object/builtin/nmo_texture_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
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
#include "core/nmo_arena_array.h"
#include "format/nmo_image.h"
#include "format/nmo_stb_adapter.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

static void nmo_texture_dispose_base_arrays(nmo_texture_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->base.scripts);
    nmo_array_dispose(&state->base.attributes);
    nmo_array_dispose(&state->base.legacy_attributes);
}

static nmo_status_t nmo_texture_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* One name of the slot_filenames list, as a record of its own. */
typedef struct nmo_texture_slot_filename {
    char *text;
} nmo_texture_slot_filename_t;

static const nmo_object_state_member_t nmo_texture_slot_filename_members[] = {
    NMO_STATE_STRING(nmo_texture_slot_filename_t, text)
};

static const nmo_object_state_layout_t nmo_texture_slot_filename_layout = {
    .size = sizeof(nmo_texture_slot_filename_t),
    .members = nmo_texture_slot_filename_members,
    .member_count = sizeof(nmo_texture_slot_filename_members) /
        sizeof(nmo_texture_slot_filename_members[0]),
};

static const nmo_object_state_member_t nmo_texture_reader_slot_members[] = {
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, format_type),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, extension),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, reader_guid),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, data_size),
    NMO_STATE_COUNTED(nmo_texture_reader_slot_t, data, data_size, uint8_t),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, alpha_count),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, alpha_value),
    NMO_STATE_VALUE(nmo_texture_reader_slot_t, alpha_plane_size),
    NMO_STATE_COUNTED(nmo_texture_reader_slot_t, alpha_plane, alpha_plane_size,
                      uint8_t)
};

static const nmo_object_state_layout_t nmo_texture_reader_slot_layout = {
    .size = sizeof(nmo_texture_reader_slot_t),
    .members = nmo_texture_reader_slot_members,
    .member_count = sizeof(nmo_texture_reader_slot_members) /
        sizeof(nmo_texture_reader_slot_members[0]),
};

static const nmo_object_state_member_t nmo_texture_raw_slot_members[] = {
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, bits_per_pixel),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, width),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, height),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, alpha_mask),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, red_mask),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, green_mask),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, blue_mask),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, compression),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, blue_size),
    NMO_STATE_COUNTED(nmo_texture_raw_slot_t, blue_data, blue_size, uint8_t),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, green_size),
    NMO_STATE_COUNTED(nmo_texture_raw_slot_t, green_data, green_size, uint8_t),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, red_size),
    NMO_STATE_COUNTED(nmo_texture_raw_slot_t, red_data, red_size, uint8_t),
    NMO_STATE_VALUE(nmo_texture_raw_slot_t, alpha_size),
    NMO_STATE_COUNTED(nmo_texture_raw_slot_t, alpha_data, alpha_size, uint8_t)
};

static const nmo_object_state_layout_t nmo_texture_raw_slot_layout = {
    .size = sizeof(nmo_texture_raw_slot_t),
    .members = nmo_texture_raw_slot_members,
    .member_count = sizeof(nmo_texture_raw_slot_members) /
        sizeof(nmo_texture_raw_slot_members[0]),
};

static const nmo_object_state_member_t nmo_texture_bitmap2_slot_members[] = {
    NMO_STATE_VALUE(nmo_texture_bitmap2_slot_t, unused_int),
    NMO_STATE_VALUE(nmo_texture_bitmap2_slot_t, buffer_size),
    NMO_STATE_COUNTED(nmo_texture_bitmap2_slot_t, buffer, buffer_size, uint8_t)
};

static const nmo_object_state_layout_t nmo_texture_bitmap2_slot_layout = {
    .size = sizeof(nmo_texture_bitmap2_slot_t),
    .members = nmo_texture_bitmap2_slot_members,
    .member_count = sizeof(nmo_texture_bitmap2_slot_members) /
        sizeof(nmo_texture_bitmap2_slot_members[0]),
};

/* The bitmap lanes: the pointer of a lane the bitmap does not use is NULL while
 * slot_count is not 0. */
static const nmo_object_state_member_t nmo_bitmap_slots_members[] = {
    NMO_STATE_VALUE(nmo_bitmap_slots_t, kind),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, slot_count),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, reader_width),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, reader_height),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, reader_bpp),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_bitmap_slots_t, reader_slots,
                                       slot_count, nmo_texture_reader_slot_layout),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_bitmap_slots_t, raw_slots,
                                       slot_count, nmo_texture_raw_slot_layout),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_bitmap_slots_t, bitmap2_slots,
                                       slot_count, nmo_texture_bitmap2_slot_layout),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, has_slot_filenames),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_bitmap_slots_t, slot_filenames,
                                       slot_count, nmo_texture_slot_filename_layout),
    NMO_STATE_VALUE(nmo_bitmap_slots_t, has_movie_filename),
    NMO_STATE_STRING(nmo_bitmap_slots_t, movie_filename)
};

static const nmo_object_state_layout_t nmo_bitmap_slots_layout = {
    .size = sizeof(nmo_bitmap_slots_t),
    .members = nmo_bitmap_slots_members,
    .member_count = sizeof(nmo_bitmap_slots_members) /
        sizeof(nmo_bitmap_slots_members[0]),
};

static const nmo_object_state_member_t nmo_texture_members[] = {
    NMO_STATE_VALUE(nmo_texture_state_t, has_movie_filename),
    NMO_STATE_STRING(nmo_texture_state_t, movie_filename),
    NMO_STATE_VALUE(nmo_texture_state_t, has_slot_filenames),
    NMO_STATE_VALUE(nmo_texture_state_t, slot_count),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_texture_state_t, slot_filenames,
                                       slot_count, nmo_texture_slot_filename_layout),
    NMO_STATE_VALUE(nmo_texture_state_t, reader_width),
    NMO_STATE_VALUE(nmo_texture_state_t, reader_height),
    NMO_STATE_VALUE(nmo_texture_state_t, reader_bpp),
    NMO_STATE_VALUE(nmo_texture_state_t, bitmap_kind),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_texture_state_t, reader_slots,
                                       slot_count, nmo_texture_reader_slot_layout),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_texture_state_t, raw_slots,
                                       slot_count, nmo_texture_raw_slot_layout),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_texture_state_t, bitmap2_slots,
                                       slot_count, nmo_texture_bitmap2_slot_layout),
    NMO_STATE_VALUE(nmo_texture_state_t, has_pick_threshold),
    NMO_STATE_VALUE(nmo_texture_state_t, pick_threshold),
    NMO_STATE_VALUE(nmo_texture_state_t, has_oldtexonly),
    NMO_STATE_VALUE(nmo_texture_state_t, uses_texonly_identifier),
    NMO_STATE_VALUE(nmo_texture_state_t, packed_unknown_bits),
    NMO_STATE_VALUE(nmo_texture_state_t, mipmap_level),
    NMO_STATE_VALUE(nmo_texture_state_t, save_options),
    NMO_STATE_VALUE(nmo_texture_state_t, is_transparent),
    NMO_STATE_VALUE(nmo_texture_state_t, is_cubemap),
    NMO_STATE_VALUE(nmo_texture_state_t, has_desired_video_format),
    NMO_STATE_VALUE(nmo_texture_state_t, desired_video_format),
    NMO_STATE_VALUE(nmo_texture_state_t, has_transparent_color),
    NMO_STATE_VALUE(nmo_texture_state_t, transparent_color),
    NMO_STATE_VALUE(nmo_texture_state_t, has_current_slot),
    NMO_STATE_VALUE(nmo_texture_state_t, current_slot),
    NMO_STATE_VALUE(nmo_texture_state_t, has_legacy_video_format),
    NMO_STATE_VALUE(nmo_texture_state_t, legacy_use_mipmap),
    NMO_STATE_VALUE(nmo_texture_state_t, legacy_video_format_size),
    NMO_STATE_BYTES(nmo_texture_state_t, legacy_video_format_data,
                    legacy_video_format_size),
    NMO_STATE_VALUE(nmo_texture_state_t, has_legacy_save_format),
    NMO_STATE_VALUE(nmo_texture_state_t, has_save_format),
    NMO_STATE_VALUE(nmo_texture_state_t, save_format_size),
    NMO_STATE_BYTES(nmo_texture_state_t, save_format_data, save_format_size),
    NMO_STATE_VALUE(nmo_texture_state_t, has_user_mipmaps),
    NMO_STATE_VALUE(nmo_texture_state_t, user_mipmap_count),
    NMO_STATE_COUNTED_RECORDS_OPTIONAL(nmo_texture_state_t, user_mipmaps,
                                       user_mipmap_count, nmo_texture_raw_slot_layout)
};

/* RCKTexture::Save always writes the packed state block, and a texture that
 * never had one loads with CKTEXTURE_USEGLOBAL. */
static void nmo_texture_set_defaults(void *instance)
{
    nmo_texture_state_t *state = instance;
    state->has_oldtexonly = 1;
    state->save_options = NMO_CKTEXTURE_USEGLOBAL;
}

static const nmo_object_state_layout_t nmo_texture_layout = {
    .size = sizeof(nmo_texture_state_t),
    .base_vtable = &nmo_beobject_vtable,
    .base_size = sizeof(nmo_beobject_state_t),
    .members = nmo_texture_members,
    .member_count = sizeof(nmo_texture_members) / sizeof(nmo_texture_members[0]),
    .set_defaults = nmo_texture_set_defaults,
    .validate = nmo_texture_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(texture, nmo_texture_layout)

static bool nmo_texture_oldtex_layout_is_representable(
    const nmo_texture_state_t *state)
{
    const uint32_t field_count =
        (state->has_transparent_color ? 1u : 0u) +
        (state->has_current_slot ? 1u : 0u) +
        (state->has_desired_video_format ? 1u : 0u);
    if (field_count == 0u) return true;
    if (field_count == 3u) return true;
    if (field_count == 2u) {
        const bool expects_transparent =
            state->slot_count <= 1u || !state->has_desired_video_format;
        const bool expects_current = state->slot_count > 1u;
        return state->has_transparent_color == expects_transparent &&
            state->has_current_slot == expects_current;
    }
    if (state->has_desired_video_format) {
        return !state->has_transparent_color && !state->has_current_slot;
    }
    if (state->slot_count <= 1u) {
        return state->has_transparent_color && !state->has_current_slot;
    }
    return !state->has_transparent_color && state->has_current_slot;
}

/* RCKTexture::Load ignores what follows the fields it reads in a section. */
static nmo_status_t nmo_texture_require_identifier_end(
    const nmo_chunk_t *chunk)
{
    (void)chunk;
    return NMO_OK;
}

static nmo_status_t nmo_texture_validate_array_count(
    const nmo_chunk_t *chunk,
    int32_t count,
    size_t element_size,
    size_t fixed_dwords,
    size_t minimum_dwords_per_element,
    const char *label)
{
    if (count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Texture %s count cannot be negative", label);
    }

    const size_t item_count = (size_t)count;
    if (element_size != 0 && item_count > SIZE_MAX / element_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Texture %s allocation size overflow", label);
    }
    if (minimum_dwords_per_element != 0 &&
        item_count > (SIZE_MAX - fixed_dwords) / minimum_dwords_per_element) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Texture %s DWORD count overflow", label);
    }

    const size_t required_dwords =
        fixed_dwords + item_count * minimum_dwords_per_element;
    if (required_dwords > nmo_chunk_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Texture %s count exceeds remaining DWORDs", label);
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_texture_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_texture_state_t, base),
                    sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Movie filename */
    NMO_FIELD(nmo_texture_state_t, has_movie_filename, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_texture_state_t, movie_filename, CKPGUID_STRING),
    /* Slot info */
    NMO_FIELD(nmo_texture_state_t, has_slot_filenames, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, slot_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_texture_state_t, slot_filenames, slot_count, 1, CKPGUID_STRING),
    /* Reader dimensions */
    NMO_FIELD(nmo_texture_state_t, reader_width, CKPGUID_INT),
    NMO_FIELD(nmo_texture_state_t, reader_height, CKPGUID_INT),
    NMO_FIELD(nmo_texture_state_t, reader_bpp, CKPGUID_INT),
    /* Bitmap kind and data */
    NMO_FIELD(nmo_texture_state_t, bitmap_kind, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_texture_state_t, reader_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_texture_state_t, raw_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_texture_state_t, bitmap2_slots, CKPGUID_POINTER),
    /* Pick threshold */
    NMO_FIELD(nmo_texture_state_t, has_pick_threshold, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, pick_threshold, CKPGUID_INT),
    /* Packed flags */
    NMO_FIELD(nmo_texture_state_t, has_oldtexonly, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, uses_texonly_identifier, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, packed_unknown_bits, CKPGUID_UINT32),
    NMO_FIELD(nmo_texture_state_t, mipmap_level, CKPGUID_UINT8),
    NMO_FIELD(nmo_texture_state_t, save_options, NMO_GUID_ENUM_CK_TEXTURE_SAVEOPTIONS),
    NMO_FIELD(nmo_texture_state_t, is_transparent, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, is_cubemap, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, has_desired_video_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, desired_video_format, NMO_GUID_ENUM_VX_PIXELFORMAT),
    NMO_FIELD(nmo_texture_state_t, has_transparent_color, CKPGUID_BOOL),
    NMO_FIELD_FULL(nmo_texture_state_t, transparent_color, CKPGUID_COLOR,
                   NMO_FIELD_OPTIONAL, 0),
    NMO_FIELD(nmo_texture_state_t, has_current_slot, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, current_slot, CKPGUID_INT),
    NMO_FIELD(nmo_texture_state_t, has_legacy_video_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, legacy_use_mipmap, CKPGUID_INT),
    NMO_FIELD_OPT(nmo_texture_state_t, legacy_video_format_data, CKPGUID_POINTER),
    NMO_FIELD(nmo_texture_state_t, legacy_video_format_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_texture_state_t, has_legacy_save_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, has_save_format, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_texture_state_t, save_format_data, CKPGUID_POINTER),
    NMO_FIELD(nmo_texture_state_t, save_format_size, CKPGUID_UINT64),
    /* User mipmaps */
    NMO_FIELD(nmo_texture_state_t, has_user_mipmaps, CKPGUID_BOOL),
    NMO_FIELD(nmo_texture_state_t, user_mipmap_count, CKPGUID_UINT32)
};

static size_t nmo_texture_identifier_payload_size(nmo_chunk_t *chunk) {
    if (!chunk || !chunk->parser_state || chunk->data.count == 0) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    if (!state) {
        return 0;
    }

    size_t id_pos = state->prev_identifier_pos;
    if (id_pos + 1 >= chunk->data.count) {
        return 0;
    }

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    uint32_t next_pos = data[id_pos + 1];
    size_t end_pos =
        next_pos != 0 && next_pos <= chunk->data.count
            ? (size_t)next_pos : chunk->data.count;
    if (end_pos < id_pos + 2) {
        return 0;
    }

    return (end_pos - (id_pos + 2)) * sizeof(uint32_t);
}

static nmo_status_t nmo_texture_read_reader_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_texture_reader_slot_t *slot)
{
    (void)arena;
    memset(slot, 0, sizeof(*slot));

    uint32_t format_type = 0;
    nmo_status_t result = nmo_chunk_read_dword(chunk, &format_type);
    if (result != NMO_OK) return result;
    slot->format_type = format_type;

    if (format_type == 0) {
        NMO_RETURN_OK();
    }

    uint32_t extension = 0;
    result = nmo_chunk_read_dword(chunk, &extension);
    if (result != NMO_OK) return result;
    slot->extension = extension;

    result = nmo_chunk_read_guid(chunk, &slot->reader_guid);
    if (result != NMO_OK) return result;

    void *data = NULL;
    size_t size = 0;
    result = nmo_chunk_read_buffer(chunk, &data, &size);
    if (result != NMO_OK) return result;
    slot->data = (uint8_t *)data;
    slot->data_size = (uint32_t)size;

    if (format_type == 2) {
        int32_t distinct = 0;
        result = nmo_chunk_read_int(chunk, &distinct);
        if (result != NMO_OK) return result;
        slot->alpha_count = (uint32_t)distinct;
        if (distinct == 1) {
            int32_t alpha_value = 0;
            result = nmo_chunk_read_int(chunk, &alpha_value);
            if (result != NMO_OK) return result;
            slot->alpha_value = (uint32_t)alpha_value;
        } else {
            /* Any other count, 0 included, is followed by the alpha plane. */
            void *alpha_plane = NULL;
            size_t alpha_size = 0;
            result = nmo_chunk_read_buffer(chunk, &alpha_plane, &alpha_size);
            if (result != NMO_OK) return result;
            slot->alpha_plane = (uint8_t *)alpha_plane;
            slot->alpha_plane_size = (uint32_t)alpha_size;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_write_reader_slot(
    nmo_chunk_t *chunk,
    const nmo_texture_reader_slot_t *slot)
{
    nmo_status_t result = nmo_chunk_write_dword(chunk, slot->format_type);
    if (result != NMO_OK) return result;

    if (slot->format_type == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_chunk_write_buffer_no_size(chunk, &slot->extension, sizeof(uint32_t));
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_guid(chunk, slot->reader_guid);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_buffer(chunk, slot->data, slot->data_size);
    if (result != NMO_OK) return result;

    if (slot->format_type == 2) {
        result = nmo_chunk_write_int(chunk, (int32_t)slot->alpha_count);
        if (result != NMO_OK) return result;
        if (slot->alpha_count == 1) {
            result = nmo_chunk_write_int(chunk, (int32_t)slot->alpha_value);
            if (result != NMO_OK) return result;
        } else {
            result = nmo_chunk_write_buffer(chunk, slot->alpha_plane, slot->alpha_plane_size);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_read_raw_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_texture_raw_slot_t *slot)
{
    (void)arena;
    memset(slot, 0, sizeof(*slot));

    int32_t bpp = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &bpp);
    if (result != NMO_OK) return result;
    slot->bits_per_pixel = bpp;

    if (bpp == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_chunk_read_int(chunk, &slot->width);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_int(chunk, &slot->height);
    if (result != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &slot->alpha_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_dword(chunk, &slot->red_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_dword(chunk, &slot->green_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_dword(chunk, &slot->blue_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_dword(chunk, &slot->compression);
    if (result != NMO_OK) return result;

    void *buffer = NULL;
    size_t size = 0;
    result = nmo_chunk_read_buffer(chunk, &buffer, &size);
    if (result != NMO_OK) return result;
    slot->blue_data = (uint8_t *)buffer;
    slot->blue_size = (uint32_t)size;

    buffer = NULL;
    size = 0;
    result = nmo_chunk_read_buffer(chunk, &buffer, &size);
    if (result != NMO_OK) return result;
    slot->green_data = (uint8_t *)buffer;
    slot->green_size = (uint32_t)size;

    buffer = NULL;
    size = 0;
    result = nmo_chunk_read_buffer(chunk, &buffer, &size);
    if (result != NMO_OK) return result;
    slot->red_data = (uint8_t *)buffer;
    slot->red_size = (uint32_t)size;

    buffer = NULL;
    size = 0;
    result = nmo_chunk_read_buffer(chunk, &buffer, &size);
    if (result != NMO_OK) return result;
    slot->alpha_data = (uint8_t *)buffer;
    slot->alpha_size = (uint32_t)size;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_write_raw_slot(
    nmo_chunk_t *chunk,
    const nmo_texture_raw_slot_t *slot)
{
    nmo_status_t result = nmo_chunk_write_int(chunk, slot->bits_per_pixel);
    if (result != NMO_OK) return result;

    if (slot->bits_per_pixel == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_chunk_write_int(chunk, slot->width);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_int(chunk, slot->height);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(chunk, slot->alpha_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(chunk, slot->red_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(chunk, slot->green_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(chunk, slot->blue_mask);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(chunk, slot->compression);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_buffer(chunk, slot->blue_data, slot->blue_size);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_buffer(chunk, slot->green_data, slot->green_size);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_buffer(chunk, slot->red_data, slot->red_size);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_buffer(chunk, slot->alpha_data, slot->alpha_size);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_read_bitmap2_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_texture_bitmap2_slot_t *slot)
{
    (void)arena;
    memset(slot, 0, sizeof(*slot));

    int32_t header = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &header);
    if (result != NMO_OK) return result;
    slot->unused_int = header;

    void *buffer = NULL;
    size_t size = 0;
    result = nmo_chunk_read_buffer(chunk, &buffer, &size);
    if (result != NMO_OK) return result;
    slot->buffer = (uint8_t *)buffer;
    slot->buffer_size = (uint32_t)size;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_write_bitmap2_slot(
    nmo_chunk_t *chunk,
    const nmo_texture_bitmap2_slot_t *slot)
{
    nmo_status_t result = nmo_chunk_write_int(chunk, slot->unused_int);
    if (result != NMO_OK) return result;
    return nmo_chunk_write_buffer(chunk, slot->buffer, slot->buffer_size);
}

/* Gives the bitmap slot array the new length; slots past the old length are
 * empty, as SetSlotCount leaves them. */
static nmo_status_t nmo_bitmap_resize_slots(
    nmo_bitmap_slots_t *state,
    nmo_arena_t *arena,
    uint32_t new_count)
{
    void **slots = NULL;
    size_t slot_size = 0;
    size_t slot_align = 0;
    switch (state->kind) {
    case CKTEXTURE_BITMAP_READER:
        slots = (void **)&state->reader_slots;
        slot_size = sizeof(nmo_texture_reader_slot_t);
        slot_align = _Alignof(nmo_texture_reader_slot_t);
        break;
    case CKTEXTURE_BITMAP_RAW:
        slots = (void **)&state->raw_slots;
        slot_size = sizeof(nmo_texture_raw_slot_t);
        slot_align = _Alignof(nmo_texture_raw_slot_t);
        break;
    case CKTEXTURE_BITMAP_BITMAP2:
        slots = (void **)&state->bitmap2_slots;
        slot_size = sizeof(nmo_texture_bitmap2_slot_t);
        slot_align = _Alignof(nmo_texture_bitmap2_slot_t);
        break;
    default:
        break;
    }

    if (slots != NULL && new_count > 0u) {
        uint8_t *resized = (uint8_t *)nmo_arena_alloc(
            arena, slot_size * (size_t)new_count, slot_align);
        if (resized == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate texture slots");
        }
        memset(resized, 0, slot_size * (size_t)new_count);
        uint32_t kept = state->slot_count < new_count ? state->slot_count : new_count;
        if (*slots != NULL && kept > 0u) {
            memcpy(resized, *slots, slot_size * (size_t)kept);
        }
        *slots = resized;
    } else if (slots != NULL) {
        *slots = NULL;
    }
    state->slot_count = new_count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_bitmap_read_slot_filenames(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_bitmap_slots_t *state)
{
    int32_t count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) return result;
    NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
        chunk, count, sizeof(char *), 0, 1, "filename"));

    /* The engine calls SetSlotCount(count) here, which adds empty slots or
       drops the surplus ones. */
    if (state->slot_count != (uint32_t)count) {
        NMO_RETURN_IF_ERROR(nmo_bitmap_resize_slots(
            state, arena, (uint32_t)count));
    }
    state->has_slot_filenames = 1;
    if (count == 0) {
        NMO_RETURN_OK();
    }

    char **names = (char **)nmo_arena_alloc(arena, sizeof(char *) * (size_t)count, _Alignof(char *));
    if (!names) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate slot filenames");
    }

    for (int32_t i = 0; i < count; ++i) {
        char *name = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &name, NULL));
        names[i] = name;
    }

    state->slot_filenames = names;

    NMO_RETURN_OK();
}

static uint32_t nmo_texture_pixel_format_from_desc(const nmo_image_desc_t *desc) {
    if (!desc) return UNKNOWN_PF;

    if (desc->format >= NMO_PIXEL_FORMAT_DXT1 && desc->format <= NMO_PIXEL_FORMAT_32_X8L8V8U8) {
        return (uint32_t)desc->format;
    }

    const uint32_t bpp = (uint32_t)desc->bits_per_pixel;
    const uint32_t r = desc->red_mask;
    const uint32_t g = desc->green_mask;
    const uint32_t b = desc->blue_mask;
    const uint32_t a = desc->alpha_mask;

    if (bpp == 32) {
        if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF && a == 0xFF000000)
            return _32_ARGB8888;
        if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF && a == 0x00000000)
            return _32_RGB888;
        if (r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000 && a == 0xFF000000)
            return _32_ABGR8888;
        if (r == 0xFF000000 && g == 0x00FF0000 && b == 0x0000FF00 && a == 0x000000FF)
            return _32_RGBA8888;
        if (r == 0x0000FF00 && g == 0x00FF0000 && b == 0xFF000000 && a == 0x000000FF)
            return _32_BGRA8888;
        if (r == 0x0000FF00 && g == 0x00FF0000 && b == 0xFF000000 && a == 0x00000000)
            return _32_BGR888;
        if (r == 0x0000FFFF && g == 0xFFFF0000 && b == 0x00000000)
            return _32_V16U16;
        if (r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000 && a == 0xFF000000)
            return _32_X8L8V8U8;
    }

    if (bpp == 24) {
        if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF)
            return _24_RGB888;
        if (r == 0x0000FF00 && g == 0x00FF0000 && b == 0xFF000000)
            return _24_BGR888;
    }

    if (bpp == 16) {
        if (r == 0xF800 && g == 0x07E0 && b == 0x001F)
            return _16_RGB565;
        if (r == 0x7C00 && g == 0x03E0 && b == 0x001F && a == 0x8000)
            return _16_ARGB1555;
        if (r == 0x7C00 && g == 0x03E0 && b == 0x001F && a == 0x0000)
            return _16_RGB555;
        if (r == 0x0F00 && g == 0x00F0 && b == 0x000F && a == 0xF000)
            return _16_ARGB4444;
        if (r == 0x001F && g == 0x07E0 && b == 0xF800)
            return _16_BGR565;
        if (r == 0x001F && g == 0x03E0 && b == 0x7C00 && a == 0x8000)
            return _16_ABGR1555;
        if (r == 0x001F && g == 0x03E0 && b == 0x7C00 && a == 0x0000)
            return _16_BGR555;
        if (r == 0x000F && g == 0x00F0 && b == 0x0F00 && a == 0xF000)
            return _16_ABGR4444;
        if (r == 0x00FF && g == 0xFF00 && b == 0x0000)
            return _16_V8U8;
        if (r == 0x001F && g == 0x07E0 && b == 0xF800)
            return _16_L6V5U5;
    }

    if (bpp == 8) {
        if (r == 0xE0 && g == 0x1C && b == 0x03)
            return _8_RGB332;
        if (r == 0xC0 && g == 0x30 && b == 0x0C && a == 0x03)
            return _8_ARGB2222;
    }

    return UNKNOWN_PF;
}

static uint32_t nmo_texture_legacy_format_from_data(
    const void *data,
    size_t size)
{
    if (data == NULL || size < 36u) return UNKNOWN_PF;

    nmo_image_desc_t desc = {0};
    const uint8_t *bytes = data;
    memcpy(&desc.width, bytes, sizeof(desc.width));
    memcpy(&desc.height, bytes + 4u, sizeof(desc.height));
    memcpy(&desc.bytes_per_line, bytes + 8u, sizeof(desc.bytes_per_line));
    memcpy(&desc.bits_per_pixel, bytes + 12u, sizeof(desc.bits_per_pixel));
    memcpy(&desc.red_mask, bytes + 16u, sizeof(desc.red_mask));
    memcpy(&desc.green_mask, bytes + 20u, sizeof(desc.green_mask));
    memcpy(&desc.blue_mask, bytes + 24u, sizeof(desc.blue_mask));
    memcpy(&desc.alpha_mask, bytes + 28u, sizeof(desc.alpha_mask));
    return nmo_texture_pixel_format_from_desc(&desc);
}

static nmo_status_t nmo_texture_legacy_tail_format(
    const void *tail,
    size_t tail_size,
    uint32_t *out_format)
{
    if (out_format == NULL) return NMO_ERR_INVALID_ARGUMENT;
    *out_format = UNKNOWN_PF;
    if (tail_size == 0u) return NMO_OK;
    if (tail == NULL || tail_size < sizeof(uint32_t) ||
        (tail_size & 3u) != 0u) {
        return NMO_ERR_INVALID_FORMAT;
    }

    uint32_t buffer_size = 0;
    memcpy(&buffer_size, tail, sizeof(buffer_size));
    const size_t buffer_dwords = ((size_t)buffer_size + 3u) / 4u;
    if (buffer_dwords > (tail_size - sizeof(uint32_t)) / sizeof(uint32_t)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    *out_format = nmo_texture_legacy_format_from_data(
        (const uint8_t *)tail + sizeof(uint32_t), buffer_size);
    return NMO_OK;
}

static nmo_status_t nmo_texture_read_legacy_mipmap_tail(
    nmo_texture_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    size_t tail_size)
{
    if (tail_size == 0u) return NMO_OK;
    if ((tail_size & 3u) != 0u || tail_size < sizeof(uint32_t)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    void *tail = nmo_arena_alloc(arena, tail_size, _Alignof(uint32_t));
    if (tail == NULL) return NMO_ERR_NOMEM;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_and_fill_buffer_nosize_checked(
        chunk, tail, tail_size));

    state->legacy_video_format_data = tail;
    state->legacy_video_format_size = tail_size;
    uint32_t format = UNKNOWN_PF;
    NMO_RETURN_IF_ERROR(nmo_texture_legacy_tail_format(
        tail, tail_size, &format));
    if (format != UNKNOWN_PF) {
        state->has_desired_video_format = 1;
        state->desired_video_format = format;
    }
    return NMO_OK;
}

static bool nmo_texture_seek_found(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    nmo_status_t *out_result)
{
    size_t section_dwords = 0u;
    *out_result = nmo_chunk_seek_identifier_with_size(
        chunk, identifier, &section_dwords);
    if (*out_result == NMO_OK && section_dwords == 0u) {
        *out_result = NMO_ERR_TRUNCATED_CHUNK;
    }
    return *out_result == NMO_OK;
}


/* =============================================================================
 * BITMAP SLOTS (shared with CKSprite)
 * ============================================================================= */

static const nmo_bitmap_slot_ids_t nmo_texture_slot_ids = {
    .movie = CK_STATESAVE_TEXAVIFILENAME,
    .reader = CK_STATESAVE_TEXREADER,
    .raw = CK_STATESAVE_TEXCOMPRESSED,
    .filenames = CK_STATESAVE_TEXFILENAMES,
    .bitmap2 = CK_STATESAVE_TEXBITMAPS,
};

static nmo_bitmap_slots_t nmo_texture_bitmap_slots(const nmo_texture_state_t *state)
{
    nmo_bitmap_slots_t slots = {
        .kind = state->bitmap_kind,
        .slot_count = state->slot_count,
        .reader_width = state->reader_width,
        .reader_height = state->reader_height,
        .reader_bpp = state->reader_bpp,
        .reader_slots = state->reader_slots,
        .raw_slots = state->raw_slots,
        .bitmap2_slots = state->bitmap2_slots,
        .has_slot_filenames = state->has_slot_filenames,
        .slot_filenames = state->slot_filenames,
        .has_movie_filename = state->has_movie_filename,
        .movie_filename = state->movie_filename,
    };
    return slots;
}

static void nmo_texture_store_bitmap_slots(
    nmo_texture_state_t *state, const nmo_bitmap_slots_t *slots)
{
    state->bitmap_kind = slots->kind;
    state->slot_count = slots->slot_count;
    state->reader_width = slots->reader_width;
    state->reader_height = slots->reader_height;
    state->reader_bpp = slots->reader_bpp;
    state->reader_slots = slots->reader_slots;
    state->raw_slots = slots->raw_slots;
    state->bitmap2_slots = slots->bitmap2_slots;
    state->has_slot_filenames = slots->has_slot_filenames;
    state->slot_filenames = slots->slot_filenames;
    state->has_movie_filename = slots->has_movie_filename;
    state->movie_filename = slots->movie_filename;
}

static nmo_status_t nmo_texture_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_texture_state_t *out_state = (nmo_texture_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_texture_deserialize");
    }

    {
        nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }
    nmo_status_t seek_result = NMO_OK;

    {
        /* The bitmap sections are the same as a sprite's; the classes differ in
           the identifiers. */
        nmo_bitmap_slots_t slots = nmo_texture_bitmap_slots(out_state);
        NMO_RETURN_IF_ERROR(nmo_bitmap_slots_read(
            chunk, arena, &nmo_texture_slot_ids, &slots));
        nmo_texture_store_bitmap_slots(out_state, &slots);
    }

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    /* RCKTexture::Load reads the pick threshold only from data version 5. */
    if (data_version >= 5 && nmo_texture_seek_found(
            chunk, CK_STATESAVE_PICKTHRESHOLD, &seek_result)) {
        if (nmo_texture_identifier_payload_size(chunk) < sizeof(uint32_t)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        int32_t threshold = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &threshold));
        out_state->pick_threshold = threshold;
        out_state->has_pick_threshold = 1;
    } else if (data_version >= 5 && seek_result != NMO_ERR_NOT_FOUND) {
        return seek_result;
    }
    if (data_version < 5) {
        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXTRANSPARENT, &seek_result)) {
            if (nmo_texture_identifier_payload_size(chunk) < 2u * sizeof(uint32_t)) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            uint32_t color = 0;
            uint32_t transparency = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &transparency));
            out_state->transparent_color = color;
            out_state->has_transparent_color = 1;
            out_state->is_transparent = (transparency != 0);
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXCURRENTIMAGE, &seek_result)) {
            if (nmo_texture_identifier_payload_size(chunk) < sizeof(uint32_t)) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            int32_t slot = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &slot));
            out_state->current_slot = slot;
            out_state->has_current_slot = 1;
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        /* RCKTexture::Load for data_version < 5: 0x40000 holds the mipmap flag
           and, when longer than that, a size-prefixed image descriptor that
           gives the desired video format. */
        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXVIDEOFORMAT, &seek_result)) {
            size_t payload = nmo_texture_identifier_payload_size(chunk);
            if (payload < sizeof(int32_t)) return NMO_ERR_TRUNCATED_CHUNK;
            int32_t use_mipmap = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &use_mipmap));
            out_state->has_legacy_video_format = 1;
            out_state->legacy_use_mipmap = use_mipmap;
            out_state->mipmap_level = use_mipmap != 0 ? UINT8_MAX : 0u;
            NMO_RETURN_IF_ERROR(nmo_texture_read_legacy_mipmap_tail(
                out_state, chunk, arena, payload - sizeof(int32_t)));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        /* 0x80000: save options and, past data_version 6, the properties buffer. */
        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXSAVEFORMAT, &seek_result)) {
            const size_t payload = nmo_texture_identifier_payload_size(chunk);
            if (payload < 2u * sizeof(uint32_t)) return NMO_ERR_TRUNCATED_CHUNK;
            uint32_t save_options = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &save_options));
            out_state->save_options = save_options;
            out_state->has_legacy_save_format = 1;

            void *format = NULL;
            size_t size = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_buffer(chunk, &format, &size));
            if (payload < 2u * sizeof(uint32_t) + ((size + 3u) & ~(size_t)3u)) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            out_state->save_format_data = format;
            out_state->save_format_size = size;
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        NMO_RETURN_OK();
    }

    /* Files written by later engines store the same packed state under
       CK_STATESAVE_TEXONLY. */
    /* Load takes a block only when it holds at least the flags dword, and
       it reads the flags and the fields that follow positionally without
       comparing the block size to them. */
    size_t texonly_dwords = 0;
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_OLDTEXONLY, &texonly_dwords);
    if (seek_result == NMO_ERR_NOT_FOUND) {
        seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_TEXONLY, &texonly_dwords);
        out_state->uses_texonly_identifier = seek_result == NMO_OK;
    }
    if (seek_result == NMO_OK && texonly_dwords > 0u) {
        size_t payload = texonly_dwords * sizeof(uint32_t);
        uint32_t dword = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &dword));

        out_state->has_oldtexonly = 1;
        out_state->packed_unknown_bits = dword & ~UINT32_C(0x00FF07FF);
        out_state->mipmap_level = (uint8_t)(dword & 0xFF);
        out_state->save_options = (dword >> 16) & 0xFF;
        out_state->is_transparent = (dword & 0x100) != 0;
        out_state->is_cubemap = (dword & 0x400) != 0;
        const bool has_desired_flag = (dword & 0x200) != 0;

        payload -= sizeof(uint32_t);

        if (payload == 3 * sizeof(uint32_t)) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
            out_state->has_transparent_color = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
            out_state->has_current_slot = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
            out_state->has_desired_video_format = 1;
        } else if (payload == 2 * sizeof(uint32_t)) {
            if (out_state->slot_count <= 1 || !has_desired_flag) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
                out_state->has_transparent_color = 1;
            }
            if (out_state->slot_count > 1) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
                out_state->has_current_slot = 1;
            }
            if (has_desired_flag) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
                out_state->has_desired_video_format = 1;
            }
        } else if (payload == sizeof(uint32_t)) {
            if (has_desired_flag) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
                out_state->has_desired_video_format = 1;
            } else if (out_state->slot_count <= 1) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
                out_state->has_transparent_color = 1;
            } else {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
                out_state->has_current_slot = 1;
            }
        }
        /* Any other size carries nothing the engine reads. */
        NMO_RETURN_IF_ERROR(nmo_chunk_skip(
            chunk, nmo_chunk_identifier_remaining_dwords(chunk)));
    } else if (seek_result != NMO_OK && seek_result != NMO_ERR_NOT_FOUND) {
        return seek_result;
    }

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_USERMIPMAP, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_raw_slot_t), 0, 1, "mipmap"));
        out_state->has_user_mipmaps = 1;
        out_state->user_mipmap_count = (uint32_t)count;
        if (count > 0) {
            nmo_texture_raw_slot_t *mips = (nmo_texture_raw_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_texture_raw_slot_t) * (size_t)count, _Alignof(nmo_texture_raw_slot_t));
            if (!mips) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate mipmap slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_texture_read_raw_slot(chunk, arena, &mips[i]);
                if (result != NMO_OK) return result;
            }
            out_state->user_mipmaps = mips;
        }
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_TEXSAVEFORMAT, &seek_result)) {
        const size_t payload = nmo_texture_identifier_payload_size(chunk);
        void *format = NULL;
        size_t size = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_buffer(chunk, &format, &size));
        if (payload < sizeof(uint32_t) + ((size + 3u) & ~(size_t)3u)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        out_state->has_save_format = 1;
        out_state->save_format_data = format;
        out_state->save_format_size = size;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_texture_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_texture_state_t *out_state = (nmo_texture_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_texture_state_t decoded = {0};
    /* The CKBitmapData constructor's value, for a texture without a packed state block. */
    decoded.save_options = NMO_CKTEXTURE_USEGLOBAL;
    if (out_state->base.scripts.allocator.alloc != NULL) {
        decoded.base.scripts.allocator = out_state->base.scripts.allocator;
    }
    if (out_state->base.attributes.allocator.alloc != NULL) {
        decoded.base.attributes.allocator = out_state->base.attributes.allocator;
    }
    if (out_state->base.legacy_attributes.allocator.alloc != NULL) {
        decoded.base.legacy_attributes.allocator =
            out_state->base.legacy_attributes.allocator;
    }

    nmo_status_t result = nmo_texture_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_texture_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_texture_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_texture_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_texture_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->base, NULL, context));
    if (s->slot_count > INT32_MAX || s->user_mipmap_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture slot count exceeds serialized range");
    }
    if (!s->has_movie_filename && s->movie_filename != NULL) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture movie filename is present without its section");
    }
    if (s->has_slot_filenames) {
        NMO_VALIDATE_COUNT(s->slot_filenames, s->slot_count, "slot_filenames");
    } else if (s->slot_filenames != NULL) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture slot filenames are present without their section");
    }
    if (s->bitmap_kind == CKTEXTURE_BITMAP_READER) {
        NMO_VALIDATE_COUNT(s->reader_slots, s->slot_count, "reader_slots");
    } else if (s->bitmap_kind == CKTEXTURE_BITMAP_RAW) {
        NMO_VALIDATE_COUNT(s->raw_slots, s->slot_count, "raw_slots");
    } else if (s->bitmap_kind == CKTEXTURE_BITMAP_BITMAP2) {
        NMO_VALIDATE_COUNT(s->bitmap2_slots, s->slot_count, "bitmap2_slots");
    } else if (s->bitmap_kind != CKTEXTURE_BITMAP_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Unknown texture bitmap kind");
    }
    if (s->reader_slots != NULL) {
        for (uint32_t i = 0; i < s->slot_count; ++i) {
            NMO_VALIDATE_BYTES(s->reader_slots[i].data,
                               s->reader_slots[i].data_size,
                               "reader_slots.data");
            NMO_VALIDATE_BYTES(s->reader_slots[i].alpha_plane,
                               s->reader_slots[i].alpha_plane_size,
                               "reader_slots.alpha_plane");
        }
    }
    if (s->raw_slots != NULL) {
        for (uint32_t i = 0; i < s->slot_count; ++i) {
            NMO_VALIDATE_BYTES(s->raw_slots[i].blue_data,
                               s->raw_slots[i].blue_size,
                               "raw_slots.blue_data");
            NMO_VALIDATE_BYTES(s->raw_slots[i].green_data,
                               s->raw_slots[i].green_size,
                               "raw_slots.green_data");
            NMO_VALIDATE_BYTES(s->raw_slots[i].red_data,
                               s->raw_slots[i].red_size,
                               "raw_slots.red_data");
            NMO_VALIDATE_BYTES(s->raw_slots[i].alpha_data,
                               s->raw_slots[i].alpha_size,
                               "raw_slots.alpha_data");
        }
    }
    if (s->bitmap2_slots != NULL) {
        for (uint32_t i = 0; i < s->slot_count; ++i) {
            NMO_VALIDATE_BYTES(s->bitmap2_slots[i].buffer,
                               s->bitmap2_slots[i].buffer_size,
                               "bitmap2_slots.buffer");
        }
    }
    if (s->save_format_size > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture save format exceeds serialized range");
    }
    if (s->legacy_video_format_size > UINT32_MAX ||
        (s->legacy_video_format_size & 3u) != 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture legacy video format tail is not representable");
    }
    if (!s->has_pick_threshold && s->pick_threshold != 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture pick threshold is present without its section");
    }
    if (s->has_oldtexonly &&
        ((s->has_desired_video_format &&
                s->desired_video_format == UNKNOWN_PF) ||
               (!s->has_desired_video_format &&
                s->desired_video_format != UNKNOWN_PF) ||
               (!s->has_transparent_color &&
                s->transparent_color != 0u) ||
               (!s->has_current_slot && s->current_slot != 0) ||
               !nmo_texture_oldtex_layout_is_representable(s))) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture OLDTEXONLY fields cannot be serialized losslessly");
    }
    if (!s->has_legacy_video_format &&
        (s->legacy_use_mipmap != 0 ||
         s->legacy_video_format_size != 0u)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture legacy video format data is present without its section");
    }
    if (!s->has_save_format && !s->has_legacy_save_format &&
        s->save_format_size != 0u) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture save format is present without its section");
    }
    if (!s->has_user_mipmaps && s->user_mipmap_count != 0u) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture user mipmaps are present without their section");
    }
    NMO_VALIDATE_BYTES(
        s->legacy_video_format_data, s->legacy_video_format_size,
        "legacy_video_format_data");
    if (s->has_legacy_video_format) {
        uint32_t legacy_format = UNKNOWN_PF;
        const nmo_status_t legacy_result = nmo_texture_legacy_tail_format(
            s->legacy_video_format_data,
            s->legacy_video_format_size,
            &legacy_format);
        if (legacy_result != NMO_OK) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture legacy video format tail is malformed");
        }
        if ((legacy_format != UNKNOWN_PF) !=
                (s->has_desired_video_format != 0) ||
            (legacy_format != UNKNOWN_PF &&
             legacy_format != s->desired_video_format)) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture legacy video format does not match its raw layout");
        }
        const uint8_t legacy_level =
            s->legacy_use_mipmap != 0 ? UINT8_MAX : 0u;
        if (s->mipmap_level != legacy_level) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture legacy mipmap flag does not match mipmap level");
        }
    }
    NMO_VALIDATE_BYTES(s->save_format_data, s->save_format_size, "save_format_data");
    NMO_VALIDATE_COUNT(s->user_mipmaps, s->user_mipmap_count, "user_mipmaps");
    for (uint32_t i = 0; i < s->user_mipmap_count; ++i) {
        NMO_VALIDATE_BYTES(s->user_mipmaps[i].blue_data,
                           s->user_mipmaps[i].blue_size,
                           "user_mipmaps.blue_data");
        NMO_VALIDATE_BYTES(s->user_mipmaps[i].green_data,
                           s->user_mipmaps[i].green_size,
                           "user_mipmaps.green_data");
        NMO_VALIDATE_BYTES(s->user_mipmaps[i].red_data,
                           s->user_mipmaps[i].red_size,
                           "user_mipmaps.red_data");
        NMO_VALIDATE_BYTES(s->user_mipmaps[i].alpha_data,
                           s->user_mipmaps[i].alpha_size,
                           "user_mipmaps.alpha_data");
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_serialize_internal(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_texture_state_t *state = (const nmo_texture_state_t *)instance;
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = nmo_object_serialize_is_file(chunk, context);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_texture_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_texture_validate(state, type, context));
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool has_legacy_layout =
        state->has_legacy_video_format ||
        state->has_legacy_save_format ||
        (!state->has_oldtexonly &&
         (state->has_transparent_color || state->is_transparent ||
          state->has_current_slot));
    const bool legacy_file_layout = is_file && data_version < 5u &&
        (data_version != 0u || has_legacy_layout);
    if (data_version == 0u && !legacy_file_layout) {
        chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    nmo_texture_state_t packed_layout = *state;
    if (packed_layout.transparent_color != 0u) {
        packed_layout.has_transparent_color = 1;
    }
    if (packed_layout.current_slot != 0) {
        packed_layout.has_current_slot = 1;
    }
    const bool has_packed_state =
        packed_layout.mipmap_level != 0u ||
        packed_layout.save_options != 0u ||
        packed_layout.is_transparent || packed_layout.is_cubemap ||
        packed_layout.has_desired_video_format ||
        packed_layout.has_transparent_color ||
        packed_layout.has_current_slot;
    const bool write_oldtexonly = state->has_oldtexonly || has_packed_state;

    if ((state->has_desired_video_format &&
            state->desired_video_format == UNKNOWN_PF) ||
        (!state->has_desired_video_format &&
            state->desired_video_format != UNKNOWN_PF)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture desired video format presence is inconsistent");
    }
    if ((state->bitmap_kind != CKTEXTURE_BITMAP_READER &&
         state->reader_slots != NULL) ||
        (state->bitmap_kind != CKTEXTURE_BITMAP_RAW &&
         state->raw_slots != NULL) ||
        (state->bitmap_kind != CKTEXTURE_BITMAP_BITMAP2 &&
         state->bitmap2_slots != NULL)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Texture contains inactive bitmap slot storage");
    }
    if (legacy_file_layout) {
        if (state->has_user_mipmaps) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture modern user mipmaps cannot be written to a legacy file");
        }
        if (state->is_cubemap ||
            (state->mipmap_level != 0u && state->mipmap_level != UINT8_MAX) ||
            (state->has_desired_video_format &&
             !state->has_legacy_video_format)) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture state cannot be represented by the legacy layout");
        }
    } else {
        if (state->has_legacy_video_format) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture legacy video format data cannot be written to the modern layout");
        }
        if (state->save_options > UINT8_MAX ||
            (write_oldtexonly &&
             !nmo_texture_oldtex_layout_is_representable(&packed_layout))) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture packed state cannot be represented by OLDTEXONLY");
        }
    }

    {
        nmo_status_t result = nmo_beobject_serialize(&state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_OLDTEXONLY) == 0) {
        NMO_RETURN_OK();
    }

    {
        const nmo_bitmap_slots_t slots = nmo_texture_bitmap_slots(state);
        NMO_RETURN_IF_ERROR(nmo_bitmap_slots_write(
            chunk, &nmo_texture_slot_ids, &slots));
    }

    if (state->has_pick_threshold) {
        if (legacy_file_layout) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Texture pick threshold cannot be written to a legacy file");
        }
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PICKTHRESHOLD);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->pick_threshold));
    }

    if (legacy_file_layout) {
        const bool write_transparent =
            state->has_transparent_color ||
            state->transparent_color != 0u || state->is_transparent;
        if (write_transparent) {
            nmo_status_t result = nmo_chunk_write_identifier(
                chunk, CK_STATESAVE_TEXTRANSPARENT);
            if (result != NMO_OK) return result;
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->transparent_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->is_transparent ? 1u : 0u));
        }

        if (state->has_current_slot || state->current_slot != 0) {
            nmo_status_t result = nmo_chunk_write_identifier(
                chunk, CK_STATESAVE_TEXCURRENTIMAGE);
            if (result != NMO_OK) return result;
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
                chunk, state->current_slot));
        }

        if (state->has_legacy_video_format || state->mipmap_level != 0u) {
            nmo_status_t result = nmo_chunk_write_identifier(
                chunk, CK_STATESAVE_TEXVIDEOFORMAT);
            if (result != NMO_OK) return result;
            const int32_t use_mipmap = state->has_legacy_video_format
                ? state->legacy_use_mipmap : 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, use_mipmap));
            result = nmo_chunk_write_buffer_no_size(
                chunk, state->legacy_video_format_data,
                state->legacy_video_format_size);
            if (result != NMO_OK) return result;
        }

        if (state->has_legacy_save_format || state->has_save_format ||
            state->save_options != 0u || state->save_format_size != 0u) {
            nmo_status_t result = nmo_chunk_write_identifier(
                chunk, CK_STATESAVE_TEXSAVEFORMAT);
            if (result != NMO_OK) return result;
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->save_options));
            result = nmo_chunk_write_buffer(
                chunk, state->save_format_data, state->save_format_size);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_OK();
    }

    if (write_oldtexonly) {
        nmo_status_t result = nmo_chunk_write_identifier(
            chunk, state->uses_texonly_identifier
                       ? CK_STATESAVE_TEXONLY : CK_STATESAVE_OLDTEXONLY);
        if (result != NMO_OK) return result;

        uint32_t dword = (uint32_t)(packed_layout.mipmap_level & 0xFF);
        dword |= ((uint32_t)(packed_layout.save_options & 0xFF) << 16);
        if (packed_layout.is_transparent) dword |= 0x100;
        if (packed_layout.is_cubemap) dword |= 0x400;
        if (packed_layout.has_desired_video_format) dword |= 0x200;
        dword |= packed_layout.packed_unknown_bits;

        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, dword));
        if (packed_layout.has_transparent_color) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, packed_layout.transparent_color));
        }

        if (packed_layout.has_current_slot) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, packed_layout.current_slot));
        }

        if (packed_layout.has_desired_video_format) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, packed_layout.desired_video_format));
        }
    }

    if (state->has_save_format || state->has_legacy_save_format) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXSAVEFORMAT);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_buffer(chunk, state->save_format_data, state->save_format_size);
        if (result != NMO_OK) return result;
    }

    if (state->has_user_mipmaps) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_USERMIPMAP);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)state->user_mipmap_count));
        for (uint32_t i = 0; i < state->user_mipmap_count; ++i) {
            result = nmo_texture_write_raw_slot(chunk, &state->user_mipmaps[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

 
NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_texture)

nmo_status_t nmo_texture_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_texture_remap_dependencies");
    }

    nmo_texture_state_t *state = (nmo_texture_state_t *)instance;

    if (state->slot_count > 0) {
        if (state->bitmap_kind == CKTEXTURE_BITMAP_READER && state->reader_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing reader slots");
        }
        if (state->bitmap_kind == CKTEXTURE_BITMAP_RAW && state->raw_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing raw slots");
        }
        if (state->bitmap_kind == CKTEXTURE_BITMAP_BITMAP2 && state->bitmap2_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing bitmap2 slots");
        }
    }
    return nmo_texture_validate(state, type, context);
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_texture)

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_texture_vtable = {
    .prepare_dependencies = nmo_texture_prepare_dependencies,
    .remap_dependencies = nmo_texture_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_texture_create,
        nmo_texture_destroy,
        nmo_texture_serialize,
        nmo_texture_deserialize,
        nmo_texture_copy,
        nmo_texture_validate,
        nmo_texture_equals,
        nmo_texture_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_texture_type,
    CKPGUID_TEXTURE,
    "CKTexture",
    NMO_CID_TEXTURE,
    CKPGUID_BEOBJECT,
    nmo_texture_state_t,
    &nmo_texture_vtable,
    nmo_texture_fields)

/* =============================================================================
 * PUBLIC MUTATION API
 * ============================================================================= */

uint32_t nmo_texture_effective_desired_video_format(
    const nmo_texture_state_t *state)
{
    if (state == NULL) return UNKNOWN_PF;
    /* The state keeps what the file holds; the engine replaces a value above
       _32_X8L8V8U8 once the load is done. */
    return state->desired_video_format > _32_X8L8V8U8
        ? (uint32_t)_16_ARGB1555
        : state->desired_video_format;
}

void nmo_texture_bitmap2_image(
    const nmo_texture_bitmap2_slot_t *slot,
    const uint8_t **out_data,
    size_t *out_size,
    const char **out_ext)
{
    static const struct {
        const char *tag;
        const char *ext;
    } tags[] = {
        {"CKTGA", "tga"}, {"CKJPG", "jpg"}, {"CKDIB", "bmp"}, {"CKBMP", "bmp"},
        {"CKTIF", "tif"}, {"CKGIF", "gif"}, {"CKPCX", "pcx"},
    };
    const uint8_t *data = slot->buffer;
    size_t size = slot->buffer_size;
    const char *ext = "tga";

    if (data != NULL && size >= 5u) {
        for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
            size_t k = 0;
            while (k < 5u && (data[k] | 0x20u) == ((uint8_t)tags[i].tag[k] | 0x20u)) k++;
            if (k == 5u) {
                data += 5;
                size -= 5u;
                ext = tags[i].ext;
                break;
            }
        }
    }
    *out_data = data;
    *out_size = size;
    if (out_ext != NULL) *out_ext = ext;
}

nmo_status_t nmo_texture_replace_bitmap(
    nmo_texture_state_t *state,
    nmo_arena_t *arena,
    const void *rgba_pixels,
    uint32_t width,
    uint32_t height) {
    if (!state || !arena || !rgba_pixels || width == 0 || height == 0)
        return NMO_ERR_INVALID_ARGUMENT;

    /* Encode pixels as PNG */
    size_t encoded_size = 0;
    uint8_t *encoded = nmo_stbi_write_to_memory(
        arena, NMO_BITMAP_FORMAT_PNG,
        (int)width, (int)height, 4,
        (const uint8_t *)rgba_pixels, 0, &encoded_size);
    if (!encoded || encoded_size == 0)
        return NMO_ERR_INTERNAL;

    /* Update dimensions */
    state->reader_width = (int32_t)width;
    state->reader_height = (int32_t)height;
    state->reader_bpp = 32;
    state->save_options = NMO_CKTEXTURE_IMAGEFORMAT;
    state->has_oldtexonly = 1;

    /* The pixels replace the whole image: one slot, no movie, no cube faces
       and nothing that described the old image (mipmaps, save format). */
    state->slot_count = 1;
    state->bitmap_kind = CKTEXTURE_BITMAP_READER;
    state->reader_slots = (nmo_texture_reader_slot_t *)nmo_arena_alloc(
        arena, sizeof(nmo_texture_reader_slot_t), 8);
    if (!state->reader_slots)
        return NMO_ERR_NOMEM;
    memset(state->reader_slots, 0, sizeof(nmo_texture_reader_slot_t));
    state->raw_slots = NULL;
    state->bitmap2_slots = NULL;
    state->has_movie_filename = 0;
    state->movie_filename = NULL;
    state->is_cubemap = 0;
    state->has_current_slot = 0;
    state->current_slot = 0;
    state->has_save_format = 0;
    state->save_format_data = NULL;
    state->save_format_size = 0;
    state->has_user_mipmaps = 0;
    state->user_mipmap_count = 0;
    state->user_mipmaps = NULL;

    /* Write encoded PNG into slot 0 */
    nmo_texture_reader_slot_t *slot = &state->reader_slots[0];
    slot->data = encoded;
    slot->data_size = (uint32_t)encoded_size;
    /* The engine finds the decoder from the reader GUID, else from the
       extension. These are the PNG reader's, as the corpus files store them. */
    slot->format_type = 1;
    slot->extension = 0x00676E70u;   /* "png" */
    slot->reader_guid = (nmo_guid_t){0x02D45C7Bu, 0x4AAC16ECu};
    slot->alpha_count = 0;
    slot->alpha_value = 0;
    slot->alpha_plane = NULL;
    slot->alpha_plane_size = 0;

    return NMO_OK;
}

nmo_status_t nmo_bitmap_slots_read(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_bitmap_slot_ids_t *ids,
    nmo_bitmap_slots_t *bitmap)
{
    if (chunk == NULL || arena == NULL || ids == NULL || bitmap == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t seek_result = NMO_OK;

    if (nmo_texture_seek_found(
            chunk, ids->reader, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_reader_slot_t), 3, 1, "reader slot"));

        int32_t width = 0;
        int32_t height = 0;
        int32_t bpp = 0;
        nmo_status_t header_result = nmo_chunk_read_int(chunk, &width);
        if (header_result != NMO_OK) return header_result;
        header_result = nmo_chunk_read_int(chunk, &height);
        if (header_result != NMO_OK) return header_result;
        header_result = nmo_chunk_read_int(chunk, &bpp);
        if (header_result != NMO_OK) return header_result;
        bitmap->reader_width = width;
        bitmap->reader_height = height;
        bitmap->reader_bpp = bpp;
        bitmap->kind = CKTEXTURE_BITMAP_READER;
        bitmap->slot_count = (uint32_t)count;

        if (count > 0) {
            nmo_texture_reader_slot_t *slots = (nmo_texture_reader_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_texture_reader_slot_t) * (size_t)count, _Alignof(nmo_texture_reader_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate reader slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_texture_read_reader_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            bitmap->reader_slots = slots;
        }
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    else if (nmo_texture_seek_found(
                 chunk, ids->raw, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_raw_slot_t), 0, 1, "raw slot"));
        bitmap->kind = CKTEXTURE_BITMAP_RAW;
        bitmap->slot_count = (uint32_t)count;

        if (count > 0) {
            nmo_texture_raw_slot_t *slots = (nmo_texture_raw_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_texture_raw_slot_t) * (size_t)count, _Alignof(nmo_texture_raw_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate raw slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_texture_read_raw_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            bitmap->raw_slots = slots;
        }
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    else if (nmo_texture_seek_found(
                 chunk, ids->bitmap2, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_bitmap2_slot_t), 0, 2, "bitmap slot"));
        bitmap->kind = CKTEXTURE_BITMAP_BITMAP2;
        bitmap->slot_count = (uint32_t)count;

        if (count > 0) {
            nmo_texture_bitmap2_slot_t *slots = (nmo_texture_bitmap2_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_texture_bitmap2_slot_t) * (size_t)count, _Alignof(nmo_texture_bitmap2_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate bitmap2 slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_texture_read_bitmap2_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            bitmap->bitmap2_slots = slots;
        }
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, ids->filenames, &seek_result)) {
        nmo_status_t result = nmo_bitmap_read_slot_filenames(chunk, arena, bitmap);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, ids->movie, &seek_result)) {
        char *movie = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &movie, NULL));
        bitmap->movie_filename = movie;
        bitmap->has_movie_filename = 1;
        NMO_RETURN_IF_ERROR(nmo_texture_require_identifier_end(chunk));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;


    NMO_RETURN_OK();
}

nmo_status_t nmo_bitmap_slots_write(
    nmo_chunk_t *chunk,
    const nmo_bitmap_slot_ids_t *ids,
    const nmo_bitmap_slots_t *bitmap)
{
    if (chunk == NULL || ids == NULL || bitmap == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (bitmap->kind == CKTEXTURE_BITMAP_READER) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, ids->reader);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)bitmap->slot_count));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, bitmap->reader_width));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, bitmap->reader_height));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, bitmap->reader_bpp));
        for (uint32_t i = 0; i < bitmap->slot_count; ++i) {
            result = nmo_texture_write_reader_slot(chunk, &bitmap->reader_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (bitmap->kind == CKTEXTURE_BITMAP_RAW) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, ids->raw);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)bitmap->slot_count));
        for (uint32_t i = 0; i < bitmap->slot_count; ++i) {
            result = nmo_texture_write_raw_slot(chunk, &bitmap->raw_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (bitmap->kind == CKTEXTURE_BITMAP_BITMAP2) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, ids->bitmap2);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)bitmap->slot_count));
        for (uint32_t i = 0; i < bitmap->slot_count; ++i) {
            result = nmo_texture_write_bitmap2_slot(chunk, &bitmap->bitmap2_slots[i]);
            if (result != NMO_OK) return result;
        }
    }

    if (bitmap->has_slot_filenames) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, ids->filenames);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)bitmap->slot_count));
        for (uint32_t i = 0; i < bitmap->slot_count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_string(chunk, bitmap->slot_filenames[i]));
        }
    }

    if (bitmap->has_movie_filename) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, ids->movie);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_string(chunk, bitmap->movie_filename));
    }


    NMO_RETURN_OK();
}

nmo_status_t nmo_bitmap_slots_validate(const nmo_bitmap_slots_t *bitmap)
{
    if (bitmap == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (bitmap->slot_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Bitmap slot count exceeds serialized range");
    }
    if (!bitmap->has_movie_filename && bitmap->movie_filename != NULL) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Bitmap movie file name is present without its section");
    }
    if (bitmap->has_slot_filenames) {
        NMO_VALIDATE_COUNT(bitmap->slot_filenames, bitmap->slot_count, "slot_filenames");
    } else if (bitmap->slot_filenames != NULL) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Bitmap slot file names are present without their section");
    }
    if (bitmap->kind == CKTEXTURE_BITMAP_READER) {
        NMO_VALIDATE_COUNT(bitmap->reader_slots, bitmap->slot_count, "reader_slots");
    } else if (bitmap->kind == CKTEXTURE_BITMAP_RAW) {
        NMO_VALIDATE_COUNT(bitmap->raw_slots, bitmap->slot_count, "raw_slots");
    } else if (bitmap->kind == CKTEXTURE_BITMAP_BITMAP2) {
        NMO_VALIDATE_COUNT(bitmap->bitmap2_slots, bitmap->slot_count, "bitmap2_slots");
    } else if (bitmap->kind != CKTEXTURE_BITMAP_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Unknown bitmap kind");
    }
    if ((bitmap->kind != CKTEXTURE_BITMAP_READER && bitmap->reader_slots != NULL) ||
        (bitmap->kind != CKTEXTURE_BITMAP_RAW && bitmap->raw_slots != NULL) ||
        (bitmap->kind != CKTEXTURE_BITMAP_BITMAP2 && bitmap->bitmap2_slots != NULL)) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Bitmap contains inactive slot storage");
    }
    for (uint32_t i = 0; bitmap->reader_slots != NULL && i < bitmap->slot_count; ++i) {
        NMO_VALIDATE_BYTES(bitmap->reader_slots[i].data, bitmap->reader_slots[i].data_size,
                           "reader_slots.data");
        NMO_VALIDATE_BYTES(bitmap->reader_slots[i].alpha_plane,
                           bitmap->reader_slots[i].alpha_plane_size,
                           "reader_slots.alpha_plane");
    }
    for (uint32_t i = 0; bitmap->raw_slots != NULL && i < bitmap->slot_count; ++i) {
        NMO_VALIDATE_BYTES(bitmap->raw_slots[i].blue_data, bitmap->raw_slots[i].blue_size,
                           "raw_slots.blue_data");
        NMO_VALIDATE_BYTES(bitmap->raw_slots[i].green_data, bitmap->raw_slots[i].green_size,
                           "raw_slots.green_data");
        NMO_VALIDATE_BYTES(bitmap->raw_slots[i].red_data, bitmap->raw_slots[i].red_size,
                           "raw_slots.red_data");
        NMO_VALIDATE_BYTES(bitmap->raw_slots[i].alpha_data, bitmap->raw_slots[i].alpha_size,
                           "raw_slots.alpha_data");
    }
    for (uint32_t i = 0; bitmap->bitmap2_slots != NULL && i < bitmap->slot_count; ++i) {
        NMO_VALIDATE_BYTES(bitmap->bitmap2_slots[i].buffer, bitmap->bitmap2_slots[i].buffer_size,
                           "bitmap2_slots.buffer");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_bitmap_slots_copy(
    nmo_arena_t *arena,
    nmo_bitmap_slots_t *dst,
    const nmo_bitmap_slots_t *src)
{
    if (arena == NULL || dst == NULL || src == NULL) return NMO_ERR_INVALID_ARGUMENT;
    return nmo_object_layout_copy(&nmo_bitmap_slots_layout, src, dst, arena);
}

bool nmo_bitmap_slots_equals(
    const nmo_bitmap_slots_t *a,
    const nmo_bitmap_slots_t *b)
{
    return nmo_object_layout_equals(&nmo_bitmap_slots_layout, a, b);
}

uint32_t nmo_bitmap_slots_hash(uint32_t hash, const nmo_bitmap_slots_t *bitmap)
{
    return nmo_object_layout_hash_from(&nmo_bitmap_slots_layout, hash, bitmap);
}
