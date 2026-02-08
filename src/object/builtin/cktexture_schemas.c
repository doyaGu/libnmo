/**
 * @file cktexture_schemas.c
 * @brief CKTexture schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKTexture (ClassID 31) deserialization, serialization,
 * and finish loading handlers.
 *
 * Reference: docs/CK2_3D_reverse_notes.md lines 341-348
 */

#include "object/nmo_cktexture_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cktexture, nmo_cktexture_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_cktexture_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_cktexture_state_t, base),
                    sizeof(nmo_ckbeobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Movie filename */
    NMO_FIELD(nmo_cktexture_state_t, has_movie_filename, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_cktexture_state_t, movie_filename, CKPGUID_STRING),
    /* Slot info */
    NMO_FIELD(nmo_cktexture_state_t, has_slot_filenames, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, slot_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_cktexture_state_t, slot_filenames, CKPGUID_STRING),
    /* Bitmap kind and data */
    NMO_FIELD(nmo_cktexture_state_t, bitmap_kind, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_cktexture_state_t, reader_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_cktexture_state_t, raw_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_cktexture_state_t, bitmap2_slots, CKPGUID_POINTER),
    /* Pick threshold */
    NMO_FIELD(nmo_cktexture_state_t, has_pick_threshold, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, pick_threshold, CKPGUID_INT),
    /* Packed flags */
    NMO_FIELD(nmo_cktexture_state_t, has_oldtexonly, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, mipmap_level, CKPGUID_UINT8),
    NMO_FIELD(nmo_cktexture_state_t, save_options, NMO_GUID_ENUM_CK_TEXTURE_SAVEOPTIONS),
    NMO_FIELD(nmo_cktexture_state_t, is_transparent, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, is_cubemap, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, has_desired_video_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, desired_video_format, NMO_GUID_ENUM_VX_PIXELFORMAT),
    NMO_FIELD(nmo_cktexture_state_t, has_transparent_color, CKPGUID_BOOL),
    NMO_FIELD_FULL(nmo_cktexture_state_t, transparent_color, CKPGUID_COLOR,
                   NMO_FIELD_OPTIONAL, 0),
    NMO_FIELD(nmo_cktexture_state_t, has_current_slot, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, current_slot, CKPGUID_INT),
    /* User mipmaps */
    NMO_FIELD(nmo_cktexture_state_t, has_user_mipmaps, CKPGUID_BOOL),
    NMO_FIELD(nmo_cktexture_state_t, user_mipmap_count, CKPGUID_UINT32)
};

static size_t nmo_cktexture_identifier_payload_size(nmo_chunk_t *chunk) {
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

static nmo_status_t nmo_cktexture_read_reader_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktexture_reader_slot_t *slot)
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

static nmo_status_t nmo_cktexture_write_reader_slot(
    nmo_chunk_t *chunk,
    const nmo_cktexture_reader_slot_t *slot)
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

static nmo_status_t nmo_cktexture_read_raw_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktexture_raw_slot_t *slot)
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

static nmo_status_t nmo_cktexture_write_raw_slot(
    nmo_chunk_t *chunk,
    const nmo_cktexture_raw_slot_t *slot)
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

static nmo_status_t nmo_cktexture_read_bitmap2_slot(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktexture_bitmap2_slot_t *slot)
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

static nmo_status_t nmo_cktexture_write_bitmap2_slot(
    nmo_chunk_t *chunk,
    const nmo_cktexture_bitmap2_slot_t *slot)
{
    nmo_status_t result = nmo_chunk_write_int(chunk, slot->header_size);
    if (result != NMO_OK) return result;
    return nmo_chunk_write_buffer(chunk, slot->buffer, slot->buffer_size);
}

static nmo_status_t nmo_cktexture_read_slot_filenames(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktexture_state_t *state)
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

nmo_status_t nmo_cktexture_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cktexture_state_t *out_state = (nmo_cktexture_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktexture_deserialize");
    }

    {
        nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXREADER) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

        if (count > 0) {
            nmo_cktexture_reader_slot_t *slots = (nmo_cktexture_reader_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_cktexture_reader_slot_t) * (size_t)count, _Alignof(nmo_cktexture_reader_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate reader slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_cktexture_read_reader_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            out_state->bitmap_kind = NMO_CKTEXTURE_BITMAP_READER;
            out_state->reader_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXCOMPRESSED) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

        if (count > 0) {
            nmo_cktexture_raw_slot_t *slots = (nmo_cktexture_raw_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_cktexture_raw_slot_t) * (size_t)count, _Alignof(nmo_cktexture_raw_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate raw slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_cktexture_read_raw_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            out_state->bitmap_kind = NMO_CKTEXTURE_BITMAP_RAW;
            out_state->raw_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXBITMAPS) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;

        if (count > 0) {
            nmo_cktexture_bitmap2_slot_t *slots = (nmo_cktexture_bitmap2_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_cktexture_bitmap2_slot_t) * (size_t)count, _Alignof(nmo_cktexture_bitmap2_slot_t));
            if (!slots) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate bitmap2 slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_cktexture_read_bitmap2_slot(chunk, arena, &slots[i]);
                if (result != NMO_OK) return result;
            }
            out_state->bitmap_kind = NMO_CKTEXTURE_BITMAP_BITMAP2;
            out_state->bitmap2_slots = slots;
            out_state->slot_count = (uint32_t)count;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TEXFILENAMES) == NMO_OK) {
        nmo_status_t result = nmo_cktexture_read_slot_filenames(chunk, arena, out_state);
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
            size_t payload = nmo_cktexture_identifier_payload_size(chunk);
            int32_t use_mipmap = 0;
            nmo_chunk_read_int(chunk, &use_mipmap);
            if (payload > sizeof(int32_t)) {
                size_t remaining = payload - sizeof(int32_t);
                nmo_chunk_skip(chunk, (remaining + 3) / 4);
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

        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OLDTEXONLY) == NMO_OK) {
        size_t payload = nmo_cktexture_identifier_payload_size(chunk);
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

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_USERMIPMAP) == NMO_OK) {
        int32_t count = 0;
        nmo_chunk_read_int(chunk, &count);
        if (count < 0) count = 0;
        if (count > 0) {
            nmo_cktexture_raw_slot_t *mips = (nmo_cktexture_raw_slot_t *)nmo_arena_alloc(
                arena, sizeof(nmo_cktexture_raw_slot_t) * (size_t)count, _Alignof(nmo_cktexture_raw_slot_t));
            if (!mips) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate mipmap slots");
            }
            for (int32_t i = 0; i < count; ++i) {
                nmo_status_t result = nmo_cktexture_read_raw_slot(chunk, arena, &mips[i]);
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

static nmo_status_t nmo_cktexture_copy_reader_slots(
    nmo_arena_t *arena,
    nmo_cktexture_reader_slot_t **dst,
    const nmo_cktexture_reader_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_reader_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].data,
                                                  src[i].data, src[i].data_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].alpha_plane,
                                                  src[i].alpha_plane, src[i].alpha_plane_size));
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_cktexture_copy_raw_slots(
    nmo_arena_t *arena,
    nmo_cktexture_raw_slot_t **dst,
    const nmo_cktexture_raw_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_raw_slot_t), count));
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

static nmo_status_t nmo_cktexture_copy_bitmap2_slots(
    nmo_arena_t *arena,
    nmo_cktexture_bitmap2_slot_t **dst,
    const nmo_cktexture_bitmap2_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_bitmap2_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].buffer,
                                                  src[i].buffer, src[i].buffer_size));
    }
    NMO_RETURN_OK();
}

static nmo_status_t cktexture_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_cktexture_state_t *s = src;
    nmo_cktexture_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.base.raw_tail,
                                              s->base.base.raw_tail, s->base.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.script_ids,
                                              s->base.script_ids, sizeof(nmo_object_id_t), s->base.script_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_parameter_ids,
                                              s->base.attribute_parameter_ids, sizeof(nmo_object_id_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_types,
                                              s->base.attribute_types, sizeof(uint32_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &d->base.attribute_chunks,
                                                    s->base.attribute_chunks, s->base.attribute_chunk_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.legacy_attributes_raw,
                                              s->base.legacy_attributes_raw, s->base.legacy_attributes_size));

    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->movie_filename, s->movie_filename));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string_array(arena, &d->slot_filenames,
                                                     s->slot_filenames, s->slot_count));

    d->reader_slots = NULL;
    d->raw_slots = NULL;
    d->bitmap2_slots = NULL;
    if (s->slot_count > 0) {
        if (s->reader_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_reader_slots(arena, &d->reader_slots,
                                                                s->reader_slots, s->slot_count));
        }
        if (s->raw_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_raw_slots(arena, &d->raw_slots,
                                                             s->raw_slots, s->slot_count));
        }
        if (s->bitmap2_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_bitmap2_slots(arena, &d->bitmap2_slots,
                                                                 s->bitmap2_slots, s->slot_count));
        }
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &d->save_format_data,
                                              s->save_format_data, s->save_format_size));
    d->user_mipmaps = NULL;
    if (s->user_mipmap_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_cktexture_copy_raw_slots(arena, &d->user_mipmaps,
                                                         s->user_mipmaps, s->user_mipmap_count));
    }
    NMO_RETURN_OK();
}

static nmo_status_t cktexture_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_cktexture_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->slot_filenames, s->slot_count, "slot_filenames");
    NMO_VALIDATE_BYTES(s->save_format_data, s->save_format_size, "save_format_data");
    NMO_VALIDATE_COUNT(s->user_mipmaps, s->user_mipmap_count, "user_mipmaps");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    cktexture,
    nmo_cktexture_state_t,
    nmo_cktexture_serialize,
    nmo_cktexture_deserialize,
    nmo_cktexture_fields,
    CKPGUID_TEXTURE,
    "CKTexture",
    NMO_CID_TEXTURE,
    CKPGUID_BEOBJECT
)

nmo_status_t nmo_cktexture_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cktexture_state_t *state = (const nmo_cktexture_state_t *)instance;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktexture_serialize");
    }

    {
        nmo_status_t result = nmo_ckbeobject_serialize(&state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    if (state->bitmap_kind == NMO_CKTEXTURE_BITMAP_READER && state->reader_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXREADER);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_cktexture_write_reader_slot(chunk, &state->reader_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == NMO_CKTEXTURE_BITMAP_RAW && state->raw_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXCOMPRESSED);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_cktexture_write_raw_slot(chunk, &state->raw_slots[i]);
            if (result != NMO_OK) return result;
        }
    } else if (state->bitmap_kind == NMO_CKTEXTURE_BITMAP_BITMAP2 && state->bitmap2_slots && state->slot_count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXBITMAPS);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, (int32_t)state->slot_count);
        for (uint32_t i = 0; i < state->slot_count; ++i) {
            result = nmo_cktexture_write_bitmap2_slot(chunk, &state->bitmap2_slots[i]);
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

    if (state->has_pick_threshold && state->pick_threshold != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PICKTHRESHOLD);
        if (result != NMO_OK) return result;
        nmo_chunk_write_int(chunk, state->pick_threshold);
    }

    if (state->has_oldtexonly) {
        nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OLDTEXONLY);
        if (result != NMO_OK) return result;

        uint32_t dword = (uint32_t)(state->mipmap_level & 0xFF);
        dword |= ((uint32_t)(state->save_options & 0xFF) << 16);
        if (state->is_transparent) dword |= 0x100;
        if (state->is_cubemap) dword |= 0x400;
        if (state->has_desired_video_format) dword |= 0x200;

        nmo_chunk_write_dword(chunk, dword);

        if (state->has_transparent_color) {
            nmo_chunk_write_dword(chunk, state->transparent_color);
        }

        if (state->has_current_slot && state->slot_count > 1) {
            nmo_chunk_write_int(chunk, state->current_slot);
        }

        if (state->has_desired_video_format) {
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
            result = nmo_cktexture_write_raw_slot(chunk, &state->user_mipmaps[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

 
nmo_status_t nmo_cktexture_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    (void)instance;
    (void)arena;
    (void)repository;
    NMO_RETURN_OK();
}

