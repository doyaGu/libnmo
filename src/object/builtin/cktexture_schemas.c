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

NMO_DEFINE_OBJECT_LIFECYCLE(
    texture,
    nmo_texture_state_t,
    do {
        nmo_status_t result = nmo_beobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_texture_dispose_base_arrays(state))

static nmo_status_t nmo_texture_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static size_t nmo_texture_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
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
    if (required_dwords > nmo_texture_identifier_remaining_dwords(chunk)) {
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
        } else if (distinct > 1) {
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
        } else if (slot->alpha_count > 1) {
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
    slot->header_size = header;

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
    nmo_status_t result = nmo_chunk_write_int(chunk, slot->header_size);
    if (result != NMO_OK) return result;
    return nmo_chunk_write_buffer(chunk, slot->buffer, slot->buffer_size);
}

static nmo_status_t nmo_texture_read_slot_filenames(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_texture_state_t *state)
{
    int32_t count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) return result;
    NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
        chunk, count, sizeof(char *), 0, 1, "filename"));

    if (count == 0) {
        NMO_RETURN_OK();
    }
    if (state->slot_count != 0 && state->slot_count != (uint32_t)count) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture filename count does not match bitmap slots");
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

    state->slot_count = (uint32_t)count;
    state->slot_filenames = names;
    state->has_slot_filenames = 1;

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

static nmo_status_t nmo_texture_apply_legacy_format(
    nmo_texture_state_t *state,
    nmo_chunk_t *chunk,
    size_t payload,
    size_t *out_consumed)
{
    if (state == NULL || chunk == NULL || out_consumed == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_consumed = 0;
    if (payload <= sizeof(uint32_t)) return NMO_OK;

    size_t buffer_size = payload - sizeof(uint32_t);
    if (buffer_size < 40u) return NMO_OK;

    nmo_image_desc_t desc = {0};
    int32_t width = 0;
    int32_t height = 0;
    int32_t bytes_per_line = 0;
    int32_t bits_per_pixel = 0;
    uint32_t red_mask = 0;
    uint32_t green_mask = 0;
    uint32_t blue_mask = 0;
    uint32_t alpha_mask = 0;
    int16_t bytes_per_entry = 0;
    int16_t color_map_entries = 0;

    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &width));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &height));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &bytes_per_line));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &bits_per_pixel));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &red_mask));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &green_mask));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &blue_mask));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &alpha_mask));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_word(chunk, (uint16_t *)&bytes_per_entry));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_word(chunk, (uint16_t *)&color_map_entries));

    desc.width = width;
    desc.height = height;
    desc.bits_per_pixel = bits_per_pixel;
    desc.bytes_per_line = bytes_per_line;
    desc.red_mask = red_mask;
    desc.green_mask = green_mask;
    desc.blue_mask = blue_mask;
    desc.alpha_mask = alpha_mask;

    state->desired_video_format = nmo_texture_pixel_format_from_desc(&desc);
    state->has_desired_video_format = (state->desired_video_format != UNKNOWN_PF);
    *out_consumed = 40u;
    return NMO_OK;
}

static bool nmo_texture_seek_found(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    nmo_status_t *out_result)
{
    *out_result = nmo_chunk_seek_identifier(chunk, identifier);
    return *out_result == NMO_OK;
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

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_TEXREADER, &seek_result)) {
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
        out_state->reader_width = width;
        out_state->reader_height = height;
        out_state->reader_bpp = bpp;

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
            out_state->bitmap_kind = CKTEXTURE_BITMAP_READER;
            out_state->reader_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    else if (nmo_texture_seek_found(
                 chunk, CK_STATESAVE_TEXCOMPRESSED, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_raw_slot_t), 0, 1, "raw slot"));

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
            out_state->bitmap_kind = CKTEXTURE_BITMAP_RAW;
            out_state->raw_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    else if (nmo_texture_seek_found(
                 chunk, CK_STATESAVE_TEXBITMAPS, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_bitmap2_slot_t), 0, 2, "bitmap slot"));

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
            out_state->bitmap_kind = CKTEXTURE_BITMAP_BITMAP2;
            out_state->bitmap2_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_TEXFILENAMES, &seek_result)) {
        nmo_status_t result = nmo_texture_read_slot_filenames(chunk, arena, out_state);
        if (result != NMO_OK) return result;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_TEXAVIFILENAME, &seek_result)) {
        char *movie = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &movie, NULL));
        out_state->movie_filename = movie;
        out_state->has_movie_filename = (movie != NULL);
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_PICKTHRESHOLD, &seek_result)) {
        int32_t threshold = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &threshold));
        out_state->pick_threshold = threshold;
        out_state->has_pick_threshold = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    if (data_version < 5) {
        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXTRANSPARENT, &seek_result)) {
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
            int32_t slot = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &slot));
            out_state->current_slot = slot;
            out_state->has_current_slot = 1;
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_USERMIPMAP, &seek_result)) {
            size_t payload = nmo_texture_identifier_payload_size(chunk);
            int32_t use_mipmap = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &use_mipmap));
            if (payload > sizeof(int32_t)) {
                size_t consumed = 0;
                NMO_RETURN_IF_ERROR(nmo_texture_apply_legacy_format(
                    out_state, chunk, payload, &consumed));
                size_t remaining = payload - sizeof(int32_t);
                if (remaining > consumed) {
                    NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, (remaining - consumed + 3) / 4));
                }
            }
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        if (nmo_texture_seek_found(
                chunk, CK_STATESAVE_TEXSYSTEMCACHING, &seek_result)) {
            uint32_t save_options = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &save_options));
            out_state->save_options = (uint16_t)(save_options & 0xFF);
            out_state->has_save_format = 1;

            void *format = NULL;
            size_t size = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_buffer(chunk, &format, &size));
            out_state->save_format_data = format;
            out_state->save_format_size = size;
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        NMO_RETURN_OK();
    }

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_OLDTEXONLY, &seek_result)) {
        size_t payload = nmo_texture_identifier_payload_size(chunk);
        uint32_t dword = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &dword));

        out_state->has_oldtexonly = 1;
        out_state->mipmap_level = (uint8_t)(dword & 0xFF);
        out_state->save_options = (uint16_t)((dword >> 16) & 0xFF);
        out_state->is_transparent = (dword & 0x100) != 0;
        out_state->is_cubemap = (dword & 0x400) != 0;
        out_state->has_desired_video_format = (dword & 0x200) != 0;

        if (payload >= sizeof(uint32_t)) {
            payload -= sizeof(uint32_t);
        }

        if (payload == 3 * sizeof(uint32_t)) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
            out_state->has_transparent_color = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
            out_state->has_current_slot = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
            out_state->has_desired_video_format = 1;
        } else if (payload == 2 * sizeof(uint32_t)) {
            if (out_state->slot_count <= 1 || !out_state->has_desired_video_format) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
                out_state->has_transparent_color = 1;
            }
            if (out_state->slot_count > 1) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
                out_state->has_current_slot = 1;
            }
            if (out_state->has_desired_video_format) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
            }
        } else if (payload == sizeof(uint32_t)) {
            if (out_state->has_desired_video_format) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->desired_video_format));
            } else if (out_state->slot_count <= 1) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->transparent_color));
                out_state->has_transparent_color = 1;
            } else {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->current_slot));
                out_state->has_current_slot = 1;
            }
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_USERMIPMAP, &seek_result)) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        NMO_RETURN_IF_ERROR(nmo_texture_validate_array_count(
            chunk, count, sizeof(nmo_texture_raw_slot_t), 0, 1, "mipmap"));
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
            out_state->has_user_mipmaps = 1;
            out_state->user_mipmap_count = (uint32_t)count;
            out_state->user_mipmaps = mips;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    if (nmo_texture_seek_found(
            chunk, CK_STATESAVE_TEXSAVEFORMAT, &seek_result)) {
        void *format = NULL;
        size_t size = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_buffer(chunk, &format, &size));
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

static nmo_status_t nmo_texture_copy_reader_slots(
    nmo_arena_t *arena,
    nmo_texture_reader_slot_t **dst,
    const nmo_texture_reader_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_texture_reader_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].data,
                                                  src[i].data, src[i].data_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].alpha_plane,
                                                  src[i].alpha_plane, src[i].alpha_plane_size));
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_copy_raw_slots(
    nmo_arena_t *arena,
    nmo_texture_raw_slot_t **dst,
    const nmo_texture_raw_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_texture_raw_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].blue_data,
                                                  src[i].blue_data, src[i].blue_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].green_data,
                                                  src[i].green_data, src[i].green_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].red_data,
                                                  src[i].red_data, src[i].red_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].alpha_data,
                                                  src[i].alpha_data, src[i].alpha_size));
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_copy_bitmap2_slots(
    nmo_arena_t *arena,
    nmo_texture_bitmap2_slot_t **dst,
    const nmo_texture_bitmap2_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_texture_bitmap2_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].buffer,
                                                  src[i].buffer, src[i].buffer_size));
    }
    NMO_RETURN_OK();
}

static void nmo_texture_release_failed_base_copy(
    nmo_texture_state_t *copied,
    const nmo_texture_state_t *source)
{
#define NMO_TEXTURE_RELEASE_BASE_ARRAY(field) \
    do { \
        if (copied->base.field.data == source->base.field.data) { \
            memset(&copied->base.field, 0, sizeof(copied->base.field)); \
        } else { \
            nmo_array_dispose(&copied->base.field); \
        } \
    } while (0)
    NMO_TEXTURE_RELEASE_BASE_ARRAY(scripts);
    NMO_TEXTURE_RELEASE_BASE_ARRAY(attributes);
    NMO_TEXTURE_RELEASE_BASE_ARRAY(legacy_attributes);
#undef NMO_TEXTURE_RELEASE_BASE_ARRAY
}

static nmo_status_t nmo_texture_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (src == dst) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_texture_validate(src, type, NULL));

    const nmo_texture_state_t *source = src;
    nmo_texture_state_t copied;
    nmo_status_t copy_status = nmo_texture_create(&copied, type, NULL);
    if (copy_status != NMO_OK) return copy_status;

    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    copy_status = nmo_beobject_vtable.copy(
        &source->base, &copied.base, &base_type, arena);
    if (copy_status != NMO_OK) {
        nmo_texture_release_failed_base_copy(&copied, source);
        return copy_status;
    }

    nmo_beobject_state_t copied_base = copied.base;
    copied = *source;
    copied.base = copied_base;
    copied.movie_filename = NULL;
    copied.slot_filenames = NULL;
    copied.reader_slots = NULL;
    copied.raw_slots = NULL;
    copied.bitmap2_slots = NULL;
    copied.save_format_data = NULL;
    copied.user_mipmaps = NULL;

    copy_status = nmo_object_copy_string(
        arena, &copied.movie_filename, source->movie_filename);
    if (copy_status == NMO_OK && source->slot_filenames != NULL) {
        copy_status = nmo_object_copy_string_array(
            arena, &copied.slot_filenames,
            source->slot_filenames, source->slot_count);
    }
    if (copy_status == NMO_OK && source->reader_slots != NULL) {
        copy_status = nmo_texture_copy_reader_slots(
            arena, &copied.reader_slots,
            source->reader_slots, source->slot_count);
    }
    if (copy_status == NMO_OK && source->raw_slots != NULL) {
        copy_status = nmo_texture_copy_raw_slots(
            arena, &copied.raw_slots,
            source->raw_slots, source->slot_count);
    }
    if (copy_status == NMO_OK && source->bitmap2_slots != NULL) {
        copy_status = nmo_texture_copy_bitmap2_slots(
            arena, &copied.bitmap2_slots,
            source->bitmap2_slots, source->slot_count);
    }
    if (copy_status == NMO_OK) {
        copy_status = nmo_object_copy_bytes(
            arena, &copied.save_format_data,
            source->save_format_data, source->save_format_size);
    }
    if (copy_status == NMO_OK && source->user_mipmaps != NULL) {
        copy_status = nmo_texture_copy_raw_slots(
            arena, &copied.user_mipmaps,
            source->user_mipmaps, source->user_mipmap_count);
    }
    if (copy_status != NMO_OK) {
        nmo_texture_dispose_base_arrays(&copied);
        return copy_status;
    }

    nmo_texture_state_t *target = dst;
    nmo_texture_dispose_base_arrays(target);
    *target = copied;
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
    if (s->has_movie_filename && s->movie_filename == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Texture movie filename is marked present but missing");
    }
    if (s->has_slot_filenames) {
        NMO_VALIDATE_COUNT(s->slot_filenames, s->slot_count, "slot_filenames");
    }
    if (s->slot_filenames != NULL) {
        for (uint32_t i = 0; i < s->slot_count; ++i) {
            if (s->slot_filenames[i] == NULL) {
                NMO_RETURN_ERROR(
                    NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                    "Texture slot filename %u is missing", i);
            }
        }
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
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0 ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_texture_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_texture_validate(state, type, context));

    {
        nmo_status_t result = nmo_beobject_serialize(&state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_OLDTEXONLY) == 0) {
        NMO_RETURN_OK();
    }

    if (state->bitmap_kind == CKTEXTURE_BITMAP_READER && state->reader_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXREADER);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)state->slot_count));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->reader_width));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->reader_height));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->reader_bpp));
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_reader_slot(chunk, &state->reader_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == CKTEXTURE_BITMAP_RAW && state->raw_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXCOMPRESSED);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)state->slot_count));
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_raw_slot(chunk, &state->raw_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == CKTEXTURE_BITMAP_BITMAP2 && state->bitmap2_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXBITMAPS);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)state->slot_count));
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_bitmap2_slot(chunk, &state->bitmap2_slots[i]);
            if (result != NMO_OK) return result;
        }
    }

    if (state->has_slot_filenames && state->slot_filenames && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXFILENAMES);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, (int32_t)state->slot_count));
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_string(chunk, state->slot_filenames[i]));
        }
    }

    if (state->has_movie_filename && state->movie_filename) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXAVIFILENAME);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_string(chunk, state->movie_filename));
    }

    if (state->pick_threshold != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PICKTHRESHOLD);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->pick_threshold));
    }

    {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OLDTEXONLY);
        if (result != NMO_OK) return result;

        uint32_t dword = (uint32_t)(state->mipmap_level & 0xFF);
        dword |= ((uint32_t)(state->save_options & 0xFF) << 16);
        if (state->is_transparent) dword |= 0x100;
        if (state->is_cubemap) dword |= 0x400;
        if (state->has_desired_video_format && state->desired_video_format != UNKNOWN_PF) dword |= 0x200;

        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, dword));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, state->transparent_color));

        if (state->slot_count > 1) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(chunk, state->current_slot));
        }

        if (state->has_desired_video_format && state->desired_video_format != UNKNOWN_PF) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, state->desired_video_format));
        }
    }

    if (state->has_save_format && state->save_format_data && state->save_format_size > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXSAVEFORMAT);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_buffer(chunk, state->save_format_data, state->save_format_size);
        if (result != NMO_OK) return result;
    }

    if (state->has_user_mipmaps && state->user_mipmap_count > 0 && state->user_mipmaps) {
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

 
nmo_status_t nmo_texture_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_texture_validate(instance, type, context);
}

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

static nmo_status_t nmo_texture_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_texture_pre_delete");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_texture_serialize(
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

    nmo_status_t result = nmo_texture_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static void nmo_texture_post_delete(
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

static bool nmo_texture_string_equals(const char *lhs, const char *rhs)
{
    if (lhs == rhs) return true;
    return lhs != NULL && rhs != NULL && strcmp(lhs, rhs) == 0;
}

static bool nmo_texture_bytes_equal(
    const void *lhs,
    const void *rhs,
    size_t size)
{
    if (size == 0) return true;
    return lhs != NULL && rhs != NULL && memcmp(lhs, rhs, size) == 0;
}

static bool nmo_texture_reader_slot_equals(
    const nmo_texture_reader_slot_t *lhs,
    const nmo_texture_reader_slot_t *rhs)
{
    return lhs->format_type == rhs->format_type &&
        lhs->extension == rhs->extension &&
        memcmp(&lhs->reader_guid, &rhs->reader_guid,
               sizeof(lhs->reader_guid)) == 0 &&
        lhs->data_size == rhs->data_size &&
        nmo_texture_bytes_equal(lhs->data, rhs->data, lhs->data_size) &&
        lhs->alpha_count == rhs->alpha_count &&
        lhs->alpha_value == rhs->alpha_value &&
        lhs->alpha_plane_size == rhs->alpha_plane_size &&
        nmo_texture_bytes_equal(
            lhs->alpha_plane, rhs->alpha_plane, lhs->alpha_plane_size);
}

static bool nmo_texture_raw_slot_equals(
    const nmo_texture_raw_slot_t *lhs,
    const nmo_texture_raw_slot_t *rhs)
{
    return lhs->bits_per_pixel == rhs->bits_per_pixel &&
        lhs->width == rhs->width &&
        lhs->height == rhs->height &&
        lhs->alpha_mask == rhs->alpha_mask &&
        lhs->red_mask == rhs->red_mask &&
        lhs->green_mask == rhs->green_mask &&
        lhs->blue_mask == rhs->blue_mask &&
        lhs->compression == rhs->compression &&
        lhs->blue_size == rhs->blue_size &&
        nmo_texture_bytes_equal(
            lhs->blue_data, rhs->blue_data, lhs->blue_size) &&
        lhs->green_size == rhs->green_size &&
        nmo_texture_bytes_equal(
            lhs->green_data, rhs->green_data, lhs->green_size) &&
        lhs->red_size == rhs->red_size &&
        nmo_texture_bytes_equal(
            lhs->red_data, rhs->red_data, lhs->red_size) &&
        lhs->alpha_size == rhs->alpha_size &&
        nmo_texture_bytes_equal(
            lhs->alpha_data, rhs->alpha_data, lhs->alpha_size);
}

static bool nmo_texture_bitmap2_slot_equals(
    const nmo_texture_bitmap2_slot_t *lhs,
    const nmo_texture_bitmap2_slot_t *rhs)
{
    return lhs->header_size == rhs->header_size &&
        lhs->buffer_size == rhs->buffer_size &&
        nmo_texture_bytes_equal(
            lhs->buffer, rhs->buffer, lhs->buffer_size);
}

static bool nmo_texture_slot_filenames_equal(
    char *const *lhs,
    char *const *rhs,
    uint32_t count)
{
    if (count == 0 || lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!nmo_texture_string_equals(lhs[i], rhs[i])) return false;
    }
    return true;
}

static bool nmo_texture_reader_slots_equal(
    const nmo_texture_reader_slot_t *lhs,
    const nmo_texture_reader_slot_t *rhs,
    uint32_t count)
{
    if (count == 0 || lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!nmo_texture_reader_slot_equals(&lhs[i], &rhs[i])) return false;
    }
    return true;
}

static bool nmo_texture_raw_slots_equal(
    const nmo_texture_raw_slot_t *lhs,
    const nmo_texture_raw_slot_t *rhs,
    uint32_t count)
{
    if (count == 0 || lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!nmo_texture_raw_slot_equals(&lhs[i], &rhs[i])) return false;
    }
    return true;
}

static bool nmo_texture_bitmap2_slots_equal(
    const nmo_texture_bitmap2_slot_t *lhs,
    const nmo_texture_bitmap2_slot_t *rhs,
    uint32_t count)
{
    if (count == 0 || lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!nmo_texture_bitmap2_slot_equals(&lhs[i], &rhs[i])) return false;
    }
    return true;
}

static bool nmo_texture_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_texture_state_t *lhs = a;
    const nmo_texture_state_t *rhs = b;
    return nmo_beobject_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->has_movie_filename == rhs->has_movie_filename &&
        nmo_texture_string_equals(
            lhs->movie_filename, rhs->movie_filename) &&
        lhs->has_slot_filenames == rhs->has_slot_filenames &&
        lhs->slot_count == rhs->slot_count &&
        nmo_texture_slot_filenames_equal(
            lhs->slot_filenames, rhs->slot_filenames, lhs->slot_count) &&
        lhs->reader_width == rhs->reader_width &&
        lhs->reader_height == rhs->reader_height &&
        lhs->reader_bpp == rhs->reader_bpp &&
        lhs->bitmap_kind == rhs->bitmap_kind &&
        nmo_texture_reader_slots_equal(
            lhs->reader_slots, rhs->reader_slots, lhs->slot_count) &&
        nmo_texture_raw_slots_equal(
            lhs->raw_slots, rhs->raw_slots, lhs->slot_count) &&
        nmo_texture_bitmap2_slots_equal(
            lhs->bitmap2_slots, rhs->bitmap2_slots, lhs->slot_count) &&
        lhs->has_pick_threshold == rhs->has_pick_threshold &&
        lhs->pick_threshold == rhs->pick_threshold &&
        lhs->has_oldtexonly == rhs->has_oldtexonly &&
        lhs->mipmap_level == rhs->mipmap_level &&
        lhs->save_options == rhs->save_options &&
        lhs->is_transparent == rhs->is_transparent &&
        lhs->is_cubemap == rhs->is_cubemap &&
        lhs->has_desired_video_format == rhs->has_desired_video_format &&
        lhs->desired_video_format == rhs->desired_video_format &&
        lhs->has_transparent_color == rhs->has_transparent_color &&
        lhs->transparent_color == rhs->transparent_color &&
        lhs->has_current_slot == rhs->has_current_slot &&
        lhs->current_slot == rhs->current_slot &&
        lhs->has_save_format == rhs->has_save_format &&
        lhs->save_format_size == rhs->save_format_size &&
        nmo_texture_bytes_equal(
            lhs->save_format_data, rhs->save_format_data,
            lhs->save_format_size) &&
        lhs->has_user_mipmaps == rhs->has_user_mipmaps &&
        lhs->user_mipmap_count == rhs->user_mipmap_count &&
        nmo_texture_raw_slots_equal(
            lhs->user_mipmaps, rhs->user_mipmaps,
            lhs->user_mipmap_count);
}

static uint32_t nmo_texture_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    if (bytes == NULL) return hash;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_texture_hash_string(uint32_t hash, const char *string)
{
    const uint8_t present = string != NULL;
    hash = nmo_texture_hash_bytes(hash, &present, sizeof(present));
    return present
        ? nmo_texture_hash_bytes(hash, string, strlen(string) + 1u)
        : hash;
}

static uint32_t nmo_texture_hash_buffer(
    uint32_t hash,
    const void *data,
    size_t size)
{
    hash = nmo_texture_hash_bytes(hash, &size, sizeof(size));
    return nmo_texture_hash_bytes(hash, data, size);
}

static uint32_t nmo_texture_hash_raw_slot(
    uint32_t hash,
    const nmo_texture_raw_slot_t *slot)
{
#define NMO_TEXTURE_HASH_RAW_FIELD(field) \
    hash = nmo_texture_hash_bytes(hash, &slot->field, sizeof(slot->field))
    NMO_TEXTURE_HASH_RAW_FIELD(bits_per_pixel);
    NMO_TEXTURE_HASH_RAW_FIELD(width);
    NMO_TEXTURE_HASH_RAW_FIELD(height);
    NMO_TEXTURE_HASH_RAW_FIELD(alpha_mask);
    NMO_TEXTURE_HASH_RAW_FIELD(red_mask);
    NMO_TEXTURE_HASH_RAW_FIELD(green_mask);
    NMO_TEXTURE_HASH_RAW_FIELD(blue_mask);
    NMO_TEXTURE_HASH_RAW_FIELD(compression);
    hash = nmo_texture_hash_buffer(hash, slot->blue_data, slot->blue_size);
    hash = nmo_texture_hash_buffer(hash, slot->green_data, slot->green_size);
    hash = nmo_texture_hash_buffer(hash, slot->red_data, slot->red_size);
    hash = nmo_texture_hash_buffer(hash, slot->alpha_data, slot->alpha_size);
#undef NMO_TEXTURE_HASH_RAW_FIELD
    return hash;
}

static uint32_t nmo_texture_hash_raw_slots(
    uint32_t hash,
    const nmo_texture_raw_slot_t *slots,
    uint32_t count)
{
    if (count == 0) return hash;
    const uint8_t present = slots != NULL;
    hash = nmo_texture_hash_bytes(hash, &present, sizeof(present));
    if (slots != NULL) {
        for (uint32_t i = 0; i < count; ++i) {
            hash = nmo_texture_hash_raw_slot(hash, &slots[i]);
        }
    }
    return hash;
}

static uint32_t nmo_texture_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_texture_state_t *state = instance;
    uint32_t hash = nmo_beobject_vtable.hash(&state->base);
#define NMO_TEXTURE_HASH_FIELD(field) \
    hash = nmo_texture_hash_bytes(hash, &state->field, sizeof(state->field))
    NMO_TEXTURE_HASH_FIELD(has_movie_filename);
    hash = nmo_texture_hash_string(hash, state->movie_filename);
    NMO_TEXTURE_HASH_FIELD(has_slot_filenames);
    NMO_TEXTURE_HASH_FIELD(slot_count);
    if (state->slot_count > 0) {
        const uint8_t names_present = state->slot_filenames != NULL;
        hash = nmo_texture_hash_bytes(
            hash, &names_present, sizeof(names_present));
        if (state->slot_filenames != NULL) {
            for (uint32_t i = 0; i < state->slot_count; ++i) {
                hash = nmo_texture_hash_string(
                    hash, state->slot_filenames[i]);
            }
        }
    }
    NMO_TEXTURE_HASH_FIELD(reader_width);
    NMO_TEXTURE_HASH_FIELD(reader_height);
    NMO_TEXTURE_HASH_FIELD(reader_bpp);
    NMO_TEXTURE_HASH_FIELD(bitmap_kind);
    if (state->slot_count > 0) {
        const uint8_t reader_present = state->reader_slots != NULL;
        hash = nmo_texture_hash_bytes(
            hash, &reader_present, sizeof(reader_present));
        if (state->reader_slots != NULL) {
            for (uint32_t i = 0; i < state->slot_count; ++i) {
                const nmo_texture_reader_slot_t *slot =
                    &state->reader_slots[i];
#define NMO_TEXTURE_HASH_READER_FIELD(field) \
                hash = nmo_texture_hash_bytes( \
                    hash, &slot->field, sizeof(slot->field))
                NMO_TEXTURE_HASH_READER_FIELD(format_type);
                NMO_TEXTURE_HASH_READER_FIELD(extension);
                NMO_TEXTURE_HASH_READER_FIELD(reader_guid.d1);
                NMO_TEXTURE_HASH_READER_FIELD(reader_guid.d2);
                hash = nmo_texture_hash_buffer(
                    hash, slot->data, slot->data_size);
                NMO_TEXTURE_HASH_READER_FIELD(alpha_count);
                NMO_TEXTURE_HASH_READER_FIELD(alpha_value);
                hash = nmo_texture_hash_buffer(
                    hash, slot->alpha_plane, slot->alpha_plane_size);
#undef NMO_TEXTURE_HASH_READER_FIELD
            }
        }
        hash = nmo_texture_hash_raw_slots(
            hash, state->raw_slots, state->slot_count);
        const uint8_t bitmap2_present = state->bitmap2_slots != NULL;
        hash = nmo_texture_hash_bytes(
            hash, &bitmap2_present, sizeof(bitmap2_present));
        if (state->bitmap2_slots != NULL) {
            for (uint32_t i = 0; i < state->slot_count; ++i) {
                const nmo_texture_bitmap2_slot_t *slot =
                    &state->bitmap2_slots[i];
                hash = nmo_texture_hash_bytes(
                    hash, &slot->header_size, sizeof(slot->header_size));
                hash = nmo_texture_hash_buffer(
                    hash, slot->buffer, slot->buffer_size);
            }
        }
    }
    NMO_TEXTURE_HASH_FIELD(has_pick_threshold);
    NMO_TEXTURE_HASH_FIELD(pick_threshold);
    NMO_TEXTURE_HASH_FIELD(has_oldtexonly);
    NMO_TEXTURE_HASH_FIELD(mipmap_level);
    NMO_TEXTURE_HASH_FIELD(save_options);
    NMO_TEXTURE_HASH_FIELD(is_transparent);
    NMO_TEXTURE_HASH_FIELD(is_cubemap);
    NMO_TEXTURE_HASH_FIELD(has_desired_video_format);
    NMO_TEXTURE_HASH_FIELD(desired_video_format);
    NMO_TEXTURE_HASH_FIELD(has_transparent_color);
    NMO_TEXTURE_HASH_FIELD(transparent_color);
    NMO_TEXTURE_HASH_FIELD(has_current_slot);
    NMO_TEXTURE_HASH_FIELD(current_slot);
    NMO_TEXTURE_HASH_FIELD(has_save_format);
    hash = nmo_texture_hash_buffer(
        hash, state->save_format_data, state->save_format_size);
    NMO_TEXTURE_HASH_FIELD(has_user_mipmaps);
    NMO_TEXTURE_HASH_FIELD(user_mipmap_count);
    hash = nmo_texture_hash_raw_slots(
        hash, state->user_mipmaps, state->user_mipmap_count);
#undef NMO_TEXTURE_HASH_FIELD
    return hash;
}

nmo_type_vtable_t nmo_texture_vtable = {
    .prepare_dependencies = nmo_texture_prepare_dependencies,
    .remap_dependencies = nmo_texture_remap_dependencies,
    .pre_delete = nmo_texture_pre_delete,
    .post_delete = nmo_texture_post_delete,
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

    /* Ensure at least one slot */
    if (state->slot_count == 0)
        state->slot_count = 1;

    /* Switch to reader mode if needed */
    if (state->bitmap_kind != CKTEXTURE_BITMAP_READER || !state->reader_slots) {
        state->bitmap_kind = CKTEXTURE_BITMAP_READER;
        state->reader_slots = (nmo_texture_reader_slot_t *)nmo_arena_alloc(
            arena, state->slot_count * sizeof(nmo_texture_reader_slot_t), 8);
        if (!state->reader_slots)
            return NMO_ERR_NOMEM;
        memset(state->reader_slots, 0,
               state->slot_count * sizeof(nmo_texture_reader_slot_t));
        state->raw_slots = NULL;
        state->bitmap2_slots = NULL;
    }

    /* Write encoded PNG into slot 0 */
    nmo_texture_reader_slot_t *slot = &state->reader_slots[0];
    slot->data = encoded;
    slot->data_size = (uint32_t)encoded_size;
    slot->format_type = 1;
    slot->extension = 0;
    slot->alpha_plane = NULL;
    slot->alpha_plane_size = 0;

    return NMO_OK;
}
