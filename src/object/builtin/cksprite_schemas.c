/**
 * @file cksprite_schemas.c
 * @brief CKSprite schema implementation
 *
 * Implements (de)serialization for CKSprite based on reverse-engineered
 * RCKSprite::Load/Save behavior documented in docs/CK2_3D_reverse_notes.md.
 * 
 * Key implementation details:
 * - Calls CK2dEntity deserializer first (parent class)
 * - Two paths: file-backed load (full bitmap) vs chunk-only (lightweight)
 * - Sprite reference (0x80000) short-circuits to clone behavior
 * - Transparency (0x20000), slot (0x10000), save options (0x20000000)
 * - Bitmap payload identifiers passed to CKBitmapData::ReadFromChunk filter
 */

#include "object/builtin/nmo_sprite_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(sprite, nmo_sprite_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sprite_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_sprite_state_t, entity),
                    sizeof(nmo_2dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite_state_t, has_sprite_ref, CKPGUID_BOOL),
    NMO_FIELD_REF(nmo_sprite_state_t, sprite_ref_id),
    NMO_FIELD(nmo_sprite_state_t, has_bitmap_data, CKPGUID_BOOL),
    NMO_FIELD_NAMED("bitmap_data", offsetof(nmo_sprite_state_t, bitmap_data),
                    sizeof(nmo_bitmapdata_t), NMO_GUID_STRUCT_CKBITMAPDATA,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite_state_t, has_transparency, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, is_transparent, CKPGUID_BOOL),
    NMO_FIELD_NAMED("transparent_color", offsetof(nmo_sprite_state_t, transparent_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite_state_t, has_slot, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, current_slot, CKPGUID_UINT32),
    NMO_FIELD(nmo_sprite_state_t, has_save_options, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, save_options, NMO_GUID_ENUM_CK_TEXTURE_SAVEOPTIONS),
    NMO_FIELD_ARRAY(nmo_sprite_state_t, bitmap_properties, CKPGUID_UINT8),
    NMO_FIELD(nmo_sprite_state_t, bitmap_properties_size, CKPGUID_UINT64)
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Copy identifier payload without mutating parser state
 */
static nmo_status_t nmo_sprite_copy_identifier_payload(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    nmo_arena_t *arena,
    uint8_t **out_data,
    size_t *out_size)
{
    if (!chunk || !out_data || !out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite_copy_identifier_payload");
    }

    *out_data = NULL;
    *out_size = 0;

    if (chunk->data.count == 0 || !chunk->data.data) {
        NMO_RETURN_OK();
    }

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t pos = 0;
    while (pos < chunk->data.count && data[pos] != identifier) {
        size_t next_pos = data[pos + 1];
        if (next_pos == 0 || next_pos == pos) {
            break;
        }
        pos = next_pos;
    }

    if (pos >= chunk->data.count || data[pos] != identifier) {
        NMO_RETURN_OK();
    }

    size_t next = data[pos + 1];
    if (next == 0 || next > chunk->data.count) {
        next = chunk->data.count;
    }

    if (next <= pos + 2) {
        NMO_RETURN_OK();
    }

    size_t dwords = next - (pos + 2);
    size_t bytes = dwords * sizeof(uint32_t);
    uint8_t *payload = (uint8_t *)nmo_arena_alloc(arena, bytes, 1);
    if (!payload) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate sprite bitmap payload");
    }

    memcpy(payload, &data[pos + 2], bytes);
    *out_data = payload;
    *out_size = bytes;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_sprite_copy_bitmapdata(
    nmo_arena_t *arena,
    nmo_bitmapdata_t *dst,
    const nmo_bitmapdata_t *src);
static nmo_status_t nmo_sprite_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_sprite_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sprite_remap_dependencies");
    }

    nmo_sprite_state_t *state = (nmo_sprite_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_2dentity_remap_dependencies(&state->entity, NULL, context));

    if (state->has_sprite_ref) {
        if (state->sprite_ref_id == 0) {
            state->has_sprite_ref = false;
        } else if (repo != NULL) {
            nmo_object_t *src_obj = nmo_object_repository_find_by_id(repo, state->sprite_ref_id);
            if (src_obj && src_obj->state &&
                nmo_object_get_class_id(src_obj) == NMO_CID_SPRITE) {
                const nmo_sprite_state_t *src = (const nmo_sprite_state_t *)src_obj->state;

                state->has_transparency = src->has_transparency;
                state->is_transparent = src->is_transparent;
                state->transparent_color = src->transparent_color;
                state->has_slot = src->has_slot;
                state->current_slot = src->current_slot;
                state->has_save_options = src->has_save_options;
                state->save_options = src->save_options;
                state->has_bitmap_data = false;
                state->bitmap_properties = NULL;
                state->bitmap_properties_size = 0;
            }

            state->has_sprite_ref = false;
            state->sprite_ref_id = 0;
        }
    }

    if (!state->has_bitmap_data) {
        state->bitmap_data.pixel_data = NULL;
        state->bitmap_data.pixel_data_size = 0;
        state->bitmap_data.palette_data = NULL;
        state->bitmap_data.palette_size = 0;
        state->bitmap_data.system_copy_data = NULL;
        state->bitmap_data.system_copy_size = 0;
        state->bitmap_data.video_backup_data = NULL;
        state->bitmap_data.video_backup_size = 0;
        state->bitmap_data.pixels_data = NULL;
        state->bitmap_data.pixels_size = 0;
        state->bitmap_data.raw_chunk_data = NULL;
        state->bitmap_data.raw_chunk_size = 0;
    }

    if (!state->has_transparency) {
        state->is_transparent = false;
    }

    if (!state->has_slot) {
        state->current_slot = 0;
    }

    if (!state->has_save_options) {
        state->bitmap_properties = NULL;
        state->bitmap_properties_size = 0;
    } else if (state->bitmap_properties_size > 0 && state->bitmap_properties == NULL) {
        state->bitmap_properties_size = 0;
    }

    return nmo_sprite_validate(state, NULL, NULL);
}

nmo_status_t nmo_sprite_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_sprite_validate(instance, type, context);
}

static nmo_status_t nmo_sprite_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sprite_pre_delete");
    }
    nmo_sprite_state_t *state = (nmo_sprite_state_t *)instance;
    state->has_sprite_ref = false;
    state->sprite_ref_id = 0;
    state->has_bitmap_data = false;
    state->bitmap_properties = NULL;
    state->bitmap_properties_size = 0;
    NMO_RETURN_OK();
}

static void nmo_sprite_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* =============================================================================
 * CKSprite DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSprite state from chunk (file-backed load)
 * 
 * File-backed path reads full bitmap payload or clones from sprite reference.
 * Used when loading from .nmo files with CKFile context.
 */
static nmo_status_t deserialize_file_backed(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_sprite_state_t *out_state)
{
    nmo_status_t result;
    nmo_status_t seek_result;
    
    /* Check for sprite reference (identifier 0x80000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITESHARED);
    if (seek_result == NMO_OK) {
        out_state->has_sprite_ref = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->sprite_ref_id);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read sprite reference ID");
        }
        /* When sprite ref is present, bitmap data is cloned from referenced sprite.
         * No bitmap payload should be present in this chunk. */
        out_state->has_bitmap_data = false;
    } else {
        /* No sprite reference - read embedded bitmap payloads */
        out_state->has_sprite_ref = false;
        out_state->has_bitmap_data = true;

        (void)nmo_sprite_copy_identifier_payload(chunk, NMO_CKSPRITE_BITMAP_PALETTE,
            arena, &out_state->bitmap_data.palette_data, &out_state->bitmap_data.palette_size);
        (void)nmo_sprite_copy_identifier_payload(chunk, NMO_CKSPRITE_BITMAP_SYSTEM_COPY,
            arena, &out_state->bitmap_data.system_copy_data, &out_state->bitmap_data.system_copy_size);
        (void)nmo_sprite_copy_identifier_payload(chunk, NMO_CKSPRITE_BITMAP_VIDEO_BACKUP,
            arena, &out_state->bitmap_data.video_backup_data, &out_state->bitmap_data.video_backup_size);
        (void)nmo_sprite_copy_identifier_payload(chunk, NMO_CKSPRITE_BITMAP_PIXELS,
            arena, &out_state->bitmap_data.pixels_data, &out_state->bitmap_data.pixels_size);
        (void)nmo_sprite_copy_identifier_payload(chunk, NMO_CKSPRITE_BITMAP_RAW,
            arena, &out_state->bitmap_data.raw_chunk_data, &out_state->bitmap_data.raw_chunk_size);
    }
    
    /* Read transparency (identifier 0x20000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETRANSPARENT);
    if (seek_result == NMO_OK) {
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read transparent color");
        }
        /* Read transparency boolean flag */
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read transparency flag");
        }
        out_state->is_transparent = (transparent_flag != 0);
    }
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITECURRENTIMAGE);
    if (seek_result == NMO_OK) {
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read current slot");
        }
    }
    
    /* Read save options (identifier 0x20000000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITEFORMAT);
    if (seek_result == NMO_OK) {
        out_state->has_save_options = true;
        result = nmo_chunk_read_dword(chunk, &out_state->save_options);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read save options");
        }
        
        /* Read CKBitmapProperties blob (size-prefixed buffer) */
        void *props = NULL;
        size_t props_size = 0;
        result = nmo_chunk_read_buffer(chunk, &props, &props_size);
        if (result == NMO_OK && props && props_size > 0) {
            out_state->bitmap_properties = (uint8_t *)props;
            out_state->bitmap_properties_size = props_size;
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKSprite state from chunk (chunk-only load)
 * 
 * Chunk-only path reads lightweight state (no heavy bitmap payload).
 * Used when loading from standalone chunks without CKFile.
 */
static nmo_status_t deserialize_chunk_only(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_sprite_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    nmo_status_t seek_result;
    
    /* Chunk-only load skips bitmap payload, only reads references and state */
    
    /* Read transparency (identifier 0x20000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETRANSPARENT);
    if (seek_result == NMO_OK) {
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read transparent color");
        }
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read transparency flag");
        }
        out_state->is_transparent = (transparent_flag != 0);
    }
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITECURRENTIMAGE);
    if (seek_result == NMO_OK) {
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read current slot");
        }
    }
    
    /* Read sprite reference (identifier 0x80000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITESHARED);
    if (seek_result == NMO_OK) {
        out_state->has_sprite_ref = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->sprite_ref_id);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read sprite reference ID");
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKSprite state from chunk
 * 
 * Dispatches to file-backed or chunk-only deserializer.
 * Detection heuristic: if bitmap payload identifiers are present, use file-backed path.
 */
nmo_status_t nmo_sprite_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sprite_state_t *out_state = (nmo_sprite_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite_deserialize");
    }

    /* First deserialize parent CK2dEntity data */
    nmo_status_t result = nmo_2dentity_deserialize(
        &out_state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* Use chunk option to select file-backed vs chunk-only path */
    if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        result = deserialize_file_backed(chunk, arena, out_state);
    } else {
        result = deserialize_chunk_only(chunk, arena, out_state);
    }
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKSprite SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKSprite state to chunk
 * 
 * Writes sprite data in original format (matches RCKSprite::Save behavior).
 */
nmo_status_t nmo_sprite_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sprite_state_t *in_state = (const nmo_sprite_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite_serialize");
    }
    
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

    /* Serialize parent CK2dEntity data */
    nmo_status_t result = nmo_2dentity_serialize(&in_state->entity, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }
    
    if (is_file) {
        if (in_state->has_bitmap_data) {
            /* Write bitmap payloads in SDK order (no raw chunk) */
            if (in_state->bitmap_data.palette_data && in_state->bitmap_data.palette_size > 0) {
                result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_PALETTE);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                    in_state->bitmap_data.palette_data,
                    in_state->bitmap_data.palette_size);
                if (result != NMO_OK) return result;
            }
            if (in_state->bitmap_data.system_copy_data && in_state->bitmap_data.system_copy_size > 0) {
                result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_SYSTEM_COPY);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                    in_state->bitmap_data.system_copy_data,
                    in_state->bitmap_data.system_copy_size);
                if (result != NMO_OK) return result;
            }
            if (in_state->bitmap_data.video_backup_data && in_state->bitmap_data.video_backup_size > 0) {
                result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_VIDEO_BACKUP);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                    in_state->bitmap_data.video_backup_data,
                    in_state->bitmap_data.video_backup_size);
                if (result != NMO_OK) return result;
            }
            if (in_state->bitmap_data.pixels_data && in_state->bitmap_data.pixels_size > 0) {
                result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_PIXELS);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                    in_state->bitmap_data.pixels_data,
                    in_state->bitmap_data.pixels_size);
                if (result != NMO_OK) return result;
            }
        }

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITETRANSPARENT);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->transparent_color);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->is_transparent ? 1 : 0);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITECURRENTIMAGE);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->current_slot);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITEFORMAT);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
        if (result != NMO_OK) return result;

        if (in_state->bitmap_properties && in_state->bitmap_properties_size > 0) {
            result = nmo_chunk_write_buffer(out_chunk, in_state->bitmap_properties,
                in_state->bitmap_properties_size);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write bitmap properties");
            }
        } else {
            result = nmo_chunk_write_buffer(out_chunk, NULL, 0);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write empty bitmap properties");
            }
        }
    } else {
        if (save_flags & CK_STATESAVE_SPRITETRANSPARENT) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITETRANSPARENT);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->transparent_color);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->is_transparent ? 1 : 0);
            if (result != NMO_OK) return result;
        }
        if (save_flags & CK_STATESAVE_SPRITECURRENTIMAGE) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITECURRENTIMAGE);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->current_slot);
            if (result != NMO_OK) return result;
        }
    }
    
    NMO_RETURN_OK();
}

static nmo_status_t nmo_sprite_copy_bitmapdata(
    nmo_arena_t *arena,
    nmo_bitmapdata_t *dst,
    const nmo_bitmapdata_t *src)
{
    if (src->pixel_data_size > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->pixel_data,
                                                  src->pixel_data, src->pixel_data_size));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->palette_data,
                                              src->palette_data, src->palette_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->system_copy_data,
                                              src->system_copy_data, src->system_copy_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->video_backup_data,
                                              src->video_backup_data, src->video_backup_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->pixels_data,
                                              src->pixels_data, src->pixels_size));
    return nmo_object_copy_bytes(arena, (void **)&dst->raw_chunk_data,
                                 src->raw_chunk_data, src->raw_chunk_size);
}

static nmo_status_t nmo_sprite_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_sprite_state_t *s = src;
    nmo_sprite_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->entity.base.base.script_ids,
                                         &d->entity.base.base.script_ids,
                                         &s->entity.base.base.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->entity.base.base.attribute_parameter_ids,
                                         &d->entity.base.base.attribute_parameter_ids,
                                         &s->entity.base.base.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->entity.base.base.attribute_types,
                                         &d->entity.base.base.attribute_types,
                                         &s->entity.base.base.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->entity.base.base.attribute_chunks,
                                                    &s->entity.base.base.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->entity.base.base.legacy_attributes_raw,
                                         &d->entity.base.base.legacy_attributes_raw,
                                         &s->entity.base.base.legacy_attributes_raw.allocator));

    if (s->has_bitmap_data) {
        NMO_RETURN_IF_ERROR(nmo_sprite_copy_bitmapdata(arena, &d->bitmap_data, &s->bitmap_data));
    } else {
        d->bitmap_data.pixel_data = NULL;
        d->bitmap_data.palette_data = NULL;
        d->bitmap_data.system_copy_data = NULL;
        d->bitmap_data.video_backup_data = NULL;
        d->bitmap_data.pixels_data = NULL;
        d->bitmap_data.raw_chunk_data = NULL;
    }
    if (s->has_save_options) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->bitmap_properties,
                                                  s->bitmap_properties, s->bitmap_properties_size));
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_sprite_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_sprite_state_t *s = instance;
    if (s->has_bitmap_data) {
        NMO_VALIDATE_BYTES(s->bitmap_data.pixel_data, s->bitmap_data.pixel_data_size,
                           "bitmap_data.pixel_data");
        NMO_VALIDATE_BYTES(s->bitmap_data.palette_data, s->bitmap_data.palette_size,
                           "bitmap_data.palette_data");
        NMO_VALIDATE_BYTES(s->bitmap_data.system_copy_data, s->bitmap_data.system_copy_size,
                           "bitmap_data.system_copy_data");
        NMO_VALIDATE_BYTES(s->bitmap_data.video_backup_data, s->bitmap_data.video_backup_size,
                           "bitmap_data.video_backup_data");
        NMO_VALIDATE_BYTES(s->bitmap_data.pixels_data, s->bitmap_data.pixels_size,
                           "bitmap_data.pixels_data");
        NMO_VALIDATE_BYTES(s->bitmap_data.raw_chunk_data, s->bitmap_data.raw_chunk_size,
                           "bitmap_data.raw_chunk_data");
    }
    NMO_VALIDATE_BYTES(s->bitmap_properties, s->bitmap_properties_size, "bitmap_properties");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(sprite, nmo_sprite_state_t)

nmo_type_vtable_t nmo_sprite_vtable = {
    .prepare_dependencies = nmo_sprite_prepare_dependencies,
    .remap_dependencies = nmo_sprite_remap_dependencies,
    .pre_delete = nmo_sprite_pre_delete,
    .post_delete = nmo_sprite_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_sprite_create,
        nmo_sprite_destroy,
        nmo_sprite_serialize,
        nmo_sprite_deserialize,
        nmo_sprite_copy,
        nmo_sprite_validate,
        nmo_sprite_equals,
        nmo_sprite_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_sprite_type,
    CKPGUID_SPRITE,
    "CKSprite",
    NMO_CID_SPRITE,
    CKPGUID_2DENTITY,
    nmo_sprite_state_t,
    &nmo_sprite_vtable,
    nmo_sprite_fields)






