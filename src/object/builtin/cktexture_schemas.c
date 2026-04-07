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
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(texture, nmo_texture_state_t)

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
    NMO_FIELD_ARRAY(nmo_texture_state_t, slot_filenames, CKPGUID_STRING),
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
    size_t end_pos = next_pos ? (size_t)next_pos : chunk->data.count;
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
    if (count < 0) count = 0;

    if (count == 0) {
        NMO_RETURN_OK();
    }

    char **names = (char **)nmo_arena_alloc(arena, sizeof(char *) * (size_t)count, _Alignof(char *));
    if (!names) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate slot filenames");
    }

    for (int32_t i = 0; i < count; ++i) {
        char *name = NULL;
        nmo_chunk_read_string(chunk, &name);
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

static size_t nmo_texture_apply_legacy_format(nmo_texture_state_t *state, nmo_chunk_t *chunk, size_t payload) {
    if (!state || !chunk || payload <= sizeof(uint32_t)) {
        return 0;
    }

    size_t buffer_size = payload - sizeof(uint32_t);
    if (buffer_size < 40u) {
        return 0;
    }

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

    nmo_chunk_read_int(chunk, &width);
    nmo_chunk_read_int(chunk, &height);
    nmo_chunk_read_int(chunk, &bytes_per_line);
    nmo_chunk_read_int(chunk, &bits_per_pixel);
    nmo_chunk_read_dword(chunk, &red_mask);
    nmo_chunk_read_dword(chunk, &green_mask);
    nmo_chunk_read_dword(chunk, &blue_mask);
    nmo_chunk_read_dword(chunk, &alpha_mask);
    nmo_chunk_read_word(chunk, (uint16_t *)&bytes_per_entry);
    nmo_chunk_read_word(chunk, (uint16_t *)&color_map_entries);

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
    return 40u;
}

nmo_status_t nmo_texture_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_texture_state_t *out_state = (nmo_texture_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_texture_deserialize");
    }

    {
        nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXREADER) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

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
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXCOMPRESSED) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

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
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXBITMAPS) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXFILENAMES) == NMO_OK) {
        nmo_status_t result = nmo_texture_read_slot_filenames(chunk, arena, out_state);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXAVIFILENAME) == NMO_OK) {
        char *movie = NULL;
        nmo_chunk_read_string(chunk, &movie);
        out_state->movie_filename = movie;
        out_state->has_movie_filename = (movie != NULL);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PICKTHRESHOLD) == NMO_OK) {
        int32_t threshold = 0;
        nmo_chunk_read_int(chunk, &threshold);
        out_state->pick_threshold = threshold;
        out_state->has_pick_threshold = 1;
    }

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXTRANSPARENT) == NMO_OK) {
            uint32_t color = 0;
            uint32_t transparency = 0;
            nmo_chunk_read_dword(chunk, &color);
            nmo_chunk_read_dword(chunk, &transparency);
            out_state->transparent_color = color;
            out_state->has_transparent_color = 1;
            out_state->is_transparent = (transparency != 0);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXCURRENTIMAGE) == NMO_OK) {
            int32_t slot = 0;
            nmo_chunk_read_int(chunk, &slot);
            out_state->current_slot = slot;
            out_state->has_current_slot = 1;
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_USERMIPMAP) == NMO_OK) {
            size_t payload = nmo_texture_identifier_payload_size(chunk);
            int32_t use_mipmap = 0;
            nmo_chunk_read_int(chunk, &use_mipmap);
            if (payload > sizeof(int32_t)) {
                size_t consumed = nmo_texture_apply_legacy_format(out_state, chunk, payload);
                size_t remaining = payload - sizeof(int32_t);
                if (remaining > consumed) {
                    nmo_chunk_skip(chunk, (remaining - consumed + 3) / 4);
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXSYSTEMCACHING) == NMO_OK) {
            uint32_t save_options = 0;
            nmo_chunk_read_dword(chunk, &save_options);
            out_state->save_options = (uint16_t)(save_options & 0xFF);
            out_state->has_save_format = 1;

            void *format = NULL;
            size_t size = 0;
            nmo_chunk_read_buffer(chunk, &format, &size);
            out_state->save_format_data = format;
            out_state->save_format_size = size;
        }

        if (out_state->has_desired_video_format &&
            out_state->desired_video_format > _32_X8L8V8U8) {
            out_state->desired_video_format = _16_ARGB1555;
        }

        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OLDTEXONLY) == NMO_OK) {
        size_t payload = nmo_texture_identifier_payload_size(chunk);
        uint32_t dword = 0;
        nmo_chunk_read_dword(chunk, &dword);

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
            nmo_chunk_read_dword(chunk, &out_state->transparent_color);
            out_state->has_transparent_color = 1;
            nmo_chunk_read_int(chunk, &out_state->current_slot);
            out_state->has_current_slot = 1;
            nmo_chunk_read_dword(chunk, &out_state->desired_video_format);
            out_state->has_desired_video_format = 1;
        } else if (payload == 2 * sizeof(uint32_t)) {
            if (out_state->slot_count <= 1 || !out_state->has_desired_video_format) {
                nmo_chunk_read_dword(chunk, &out_state->transparent_color);
                out_state->has_transparent_color = 1;
            }
            if (out_state->slot_count > 1) {
                nmo_chunk_read_int(chunk, &out_state->current_slot);
                out_state->has_current_slot = 1;
            }
            if (out_state->has_desired_video_format) {
                nmo_chunk_read_dword(chunk, &out_state->desired_video_format);
            }
        } else if (payload == sizeof(uint32_t)) {
            if (out_state->has_desired_video_format) {
                nmo_chunk_read_dword(chunk, &out_state->desired_video_format);
            } else if (out_state->slot_count <= 1) {
                nmo_chunk_read_dword(chunk, &out_state->transparent_color);
                out_state->has_transparent_color = 1;
            } else {
                nmo_chunk_read_int(chunk, &out_state->current_slot);
                out_state->has_current_slot = 1;
            }
        }
    }

    if (out_state->has_desired_video_format &&
        out_state->desired_video_format > _32_X8L8V8U8) {
        out_state->desired_video_format = _16_ARGB1555;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_USERMIPMAP) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;
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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXSAVEFORMAT) == NMO_OK) {
        void *format = NULL;
        size_t size = 0;
        nmo_chunk_read_buffer(chunk, &format, &size);
        out_state->has_save_format = 1;
        out_state->save_format_data = format;
        out_state->save_format_size = size;
    }

    NMO_RETURN_OK();
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

static nmo_status_t nmo_texture_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_texture_state_t *s = src;
    nmo_texture_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.script_ids, &d->base.script_ids,
                                        &s->base.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_parameter_ids,
                                        &d->base.attribute_parameter_ids,
                                        &s->base.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_types,
                                        &d->base.attribute_types,
                                        &s->base.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->base.attribute_chunks,
                                                     &s->base.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.legacy_attributes_raw,
                                        &d->base.legacy_attributes_raw,
                                        &s->base.legacy_attributes_raw.allocator));

    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->movie_filename, s->movie_filename));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string_array(arena, &d->slot_filenames,
                                                     s->slot_filenames, s->slot_count));

    d->reader_slots = NULL;
    d->raw_slots = NULL;
    d->bitmap2_slots = NULL;
    if (s->slot_count > 0) {
        if (s->reader_slots) {
            NMO_RETURN_IF_ERROR(nmo_texture_copy_reader_slots(arena, &d->reader_slots,
                                                                s->reader_slots, s->slot_count));
        }
        if (s->raw_slots) {
            NMO_RETURN_IF_ERROR(nmo_texture_copy_raw_slots(arena, &d->raw_slots,
                                                             s->raw_slots, s->slot_count));
        }
        if (s->bitmap2_slots) {
            NMO_RETURN_IF_ERROR(nmo_texture_copy_bitmap2_slots(arena, &d->bitmap2_slots,
                                                                 s->bitmap2_slots, s->slot_count));
        }
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &d->save_format_data,
                                              s->save_format_data, s->save_format_size));
    d->user_mipmaps = NULL;
    if (s->user_mipmap_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_texture_copy_raw_slots(arena, &d->user_mipmaps,
                                                         s->user_mipmaps, s->user_mipmap_count));
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_texture_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_texture_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->slot_filenames, s->slot_count, "slot_filenames");
    NMO_VALIDATE_BYTES(s->save_format_data, s->save_format_size, "save_format_data");
    NMO_VALIDATE_COUNT(s->user_mipmaps, s->user_mipmap_count, "user_mipmaps");
    NMO_RETURN_OK();
}

nmo_status_t nmo_texture_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_texture_state_t *state = (const nmo_texture_state_t *)instance;
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_texture_serialize");
    }

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
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        nmo_chunk_write_int(chunk, state->reader_width);
        nmo_chunk_write_int(chunk, state->reader_height);
        nmo_chunk_write_int(chunk, state->reader_bpp);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_reader_slot(chunk, &state->reader_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == CKTEXTURE_BITMAP_RAW && state->raw_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXCOMPRESSED);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_raw_slot(chunk, &state->raw_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == CKTEXTURE_BITMAP_BITMAP2 && state->bitmap2_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXBITMAPS);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_texture_write_bitmap2_slot(chunk, &state->bitmap2_slots[i]);
            if (result != NMO_OK) return result;
        }
    }

    if (state->has_slot_filenames && state->slot_filenames && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXFILENAMES);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            nmo_chunk_write_string(chunk, state->slot_filenames[i]);
        }
    }

    if (state->has_movie_filename && state->movie_filename) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXAVIFILENAME);
        if (result != NMO_OK) return result;
        nmo_chunk_write_string(chunk, state->movie_filename);
    }

    if (state->pick_threshold != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PICKTHRESHOLD);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, state->pick_threshold);
    }

    {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OLDTEXONLY);
        if (result != NMO_OK) return result;

        uint32_t dword = (uint32_t)(state->mipmap_level & 0xFF);
        dword |= ((uint32_t)(state->save_options & 0xFF) << 16);
        if (state->is_transparent) dword |= 0x100;
        if (state->is_cubemap) dword |= 0x400;
        if (state->has_desired_video_format && state->desired_video_format != UNKNOWN_PF) dword |= 0x200;

        nmo_chunk_write_dword(chunk, dword);
        nmo_chunk_write_dword(chunk, state->transparent_color);

        if (state->slot_count > 1) {
            nmo_chunk_write_int(chunk, state->current_slot);
        }

        if (state->has_desired_video_format && state->desired_video_format != UNKNOWN_PF) {
            nmo_chunk_write_dword(chunk, state->desired_video_format);
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
        nmo_chunk_write_int(chunk, (int32_t)state->user_mipmap_count);
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

    if (state->slot_count == 0) {
        state->reader_slots = NULL;
        state->raw_slots = NULL;
        state->bitmap2_slots = NULL;
    } else {
        if (state->bitmap_kind == CKTEXTURE_BITMAP_READER && state->reader_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing reader slots");
        }
        if (state->bitmap_kind == CKTEXTURE_BITMAP_RAW && state->raw_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing raw slots");
        }
        if (state->bitmap_kind == CKTEXTURE_BITMAP_BITMAP2 && state->bitmap2_slots == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing bitmap2 slots");
        }
        if (state->bitmap_kind == CKTEXTURE_BITMAP_NONE) {
            state->reader_slots = NULL;
            state->raw_slots = NULL;
            state->bitmap2_slots = NULL;
            state->slot_count = 0;
        }
    }

    if (state->has_slot_filenames) {
        if (!state->slot_filenames || state->slot_count == 0) {
            state->has_slot_filenames = 0;
        }
    }

    if (state->has_movie_filename && !state->movie_filename) {
        state->has_movie_filename = 0;
    }

    if (state->has_desired_video_format &&
        state->desired_video_format > _32_X8L8V8U8) {
        state->desired_video_format = _16_ARGB1555;
    }

    if (state->has_current_slot) {
        if (state->slot_count == 0 || state->current_slot < 0 ||
            (uint32_t)state->current_slot >= state->slot_count) {
            state->has_current_slot = 0;
            state->current_slot = 0;
        }
    }

    if (state->has_user_mipmaps && state->user_mipmap_count == 0) {
        state->has_user_mipmaps = 0;
        state->user_mipmaps = NULL;
    }

    if (state->has_save_format && (state->save_format_size == 0 || !state->save_format_data)) {
        state->has_save_format = 0;
        state->save_format_data = NULL;
        state->save_format_size = 0;
    }

    return nmo_texture_validate(state, NULL, NULL);
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

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(texture, nmo_texture_state_t)

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
