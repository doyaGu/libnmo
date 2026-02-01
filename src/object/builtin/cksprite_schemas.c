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

#include "object/nmo_cksprite_schemas.h"
#include "object/nmo_ck2dentity_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Copy identifier payload without mutating parser state
 */
static nmo_result_t nmo_sprite_copy_identifier_payload(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    nmo_arena_t *arena,
    uint8_t **out_data,
    size_t *out_size)
{
    if (!chunk || !out_data || !out_size) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite_copy_identifier_payload"));
    }

    *out_data = NULL;
    *out_size = 0;

    if (chunk->data.count == 0 || !chunk->data.data) {
        return nmo_result_ok();
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
        return nmo_result_ok();
    }

    size_t next = data[pos + 1];
    if (next == 0 || next > chunk->data.count) {
        next = chunk->data.count;
    }

    if (next <= pos + 2) {
        return nmo_result_ok();
    }

    size_t dwords = next - (pos + 2);
    size_t bytes = dwords * sizeof(uint32_t);
    uint8_t *payload = (uint8_t *)nmo_arena_alloc(arena, bytes, 1);
    if (!payload) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate sprite bitmap payload"));
    }

    memcpy(payload, &data[pos + 2], bytes);
    *out_data = payload;
    *out_size = bytes;
    return nmo_result_ok();
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
static nmo_result_t deserialize_file_backed(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite_state_t *out_state)
{
    nmo_result_t result;
    nmo_result_t seek_result;
    
    /* Check for sprite reference (identifier 0x80000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_SPRITE_REF);
    if (seek_result.code == NMO_OK) {
        out_state->has_sprite_ref = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->sprite_ref_id);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read sprite reference ID"));
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
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_TRANSPARENCY);
    if (seek_result.code == NMO_OK) {
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read transparent color"));
        }
        /* Read transparency boolean flag */
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read transparency flag"));
        }
        out_state->is_transparent = (transparent_flag != 0);
    }
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_SLOT);
    if (seek_result.code == NMO_OK) {
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read current slot"));
        }
    }
    
    /* Read save options (identifier 0x20000000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_SAVE_OPTIONS);
    if (seek_result.code == NMO_OK) {
        out_state->has_save_options = true;
        result = nmo_chunk_read_dword(chunk, &out_state->save_options);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read save options"));
        }
        
        /* Read CKBitmapProperties blob (size-prefixed buffer) */
        void *props = NULL;
        size_t props_size = 0;
        result = nmo_chunk_read_buffer(chunk, &props, &props_size);
        if (result.code == NMO_OK && props && props_size > 0) {
            out_state->bitmap_properties = (uint8_t *)props;
            out_state->bitmap_properties_size = props_size;
        }
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CKSprite state from chunk (chunk-only load)
 * 
 * Chunk-only path reads lightweight state (no heavy bitmap payload).
 * Used when loading from standalone chunks without CKFile.
 */
static nmo_result_t deserialize_chunk_only(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite_state_t *out_state)
{
    nmo_result_t result;
    nmo_result_t seek_result;
    
    /* Chunk-only load skips bitmap payload, only reads references and state */
    
    /* Read transparency (identifier 0x20000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_TRANSPARENCY);
    if (seek_result.code == NMO_OK) {
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read transparent color"));
        }
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read transparency flag"));
        }
        out_state->is_transparent = (transparent_flag != 0);
    }
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_SLOT);
    if (seek_result.code == NMO_OK) {
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read current slot"));
        }
    }
    
    /* Read sprite reference (identifier 0x80000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITE_CHUNK_SPRITE_REF);
    if (seek_result.code == NMO_OK) {
        out_state->has_sprite_ref = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->sprite_ref_id);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read sprite reference ID"));
        }
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CKSprite state from chunk
 * 
 * Dispatches to file-backed or chunk-only deserializer.
 * Detection heuristic: if bitmap payload identifiers are present, use file-backed path.
 */
nmo_result_t nmo_cksprite_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite_deserialize"));
    }
    
    memset(out_state, 0, sizeof(*out_state));
    
    /* First deserialize parent CK2dEntity data */
    nmo_result_t result = nmo_ck2dentity_deserialize(
        chunk, arena, &out_state->entity);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Use chunk option to select file-backed vs chunk-only path */
    if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        result = deserialize_file_backed(chunk, arena, out_state);
    } else {
        result = deserialize_chunk_only(chunk, arena, out_state);
    }
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKSprite SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKSprite state to chunk
 * 
 * Writes sprite data in original format (matches RCKSprite::Save behavior).
 */
nmo_result_t nmo_cksprite_serialize(
    const nmo_cksprite_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite_serialize"));
    }
    
    /* Serialize parent CK2dEntity data */
    nmo_result_t result = nmo_ck2dentity_serialize(&in_state->entity, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Write sprite reference (identifier 0x80000) if present */
    if (in_state->has_sprite_ref) {
        result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_CHUNK_SPRITE_REF);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->sprite_ref_id);
        if (result.code != NMO_OK) return result;
    } else if (in_state->has_bitmap_data) {
        /* Write bitmap payloads in SDK order */
        if (in_state->bitmap_data.palette_data && in_state->bitmap_data.palette_size > 0) {
            result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_PALETTE);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                in_state->bitmap_data.palette_data,
                in_state->bitmap_data.palette_size);
            if (result.code != NMO_OK) return result;
        }
        if (in_state->bitmap_data.system_copy_data && in_state->bitmap_data.system_copy_size > 0) {
            result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_SYSTEM_COPY);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                in_state->bitmap_data.system_copy_data,
                in_state->bitmap_data.system_copy_size);
            if (result.code != NMO_OK) return result;
        }
        if (in_state->bitmap_data.video_backup_data && in_state->bitmap_data.video_backup_size > 0) {
            result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_VIDEO_BACKUP);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                in_state->bitmap_data.video_backup_data,
                in_state->bitmap_data.video_backup_size);
            if (result.code != NMO_OK) return result;
        }
        if (in_state->bitmap_data.pixels_data && in_state->bitmap_data.pixels_size > 0) {
            result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_PIXELS);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                in_state->bitmap_data.pixels_data,
                in_state->bitmap_data.pixels_size);
            if (result.code != NMO_OK) return result;
        }
        if (in_state->bitmap_data.raw_chunk_data && in_state->bitmap_data.raw_chunk_size > 0) {
            result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_BITMAP_RAW);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                in_state->bitmap_data.raw_chunk_data,
                in_state->bitmap_data.raw_chunk_size);
            if (result.code != NMO_OK) return result;
        }
    }
    
    /* Write transparency (identifier 0x20000) */
    if (in_state->has_transparency) {
        result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_CHUNK_TRANSPARENCY);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->transparent_color);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->is_transparent ? 1 : 0);
        if (result.code != NMO_OK) return result;
    }
    
    /* Write current slot (identifier 0x10000) */
    if (in_state->has_slot) {
        result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_CHUNK_SLOT);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->current_slot);
        if (result.code != NMO_OK) return result;
    }
    
    /* Write save options (identifier 0x20000000) */
    if (in_state->has_save_options) {
        result = nmo_chunk_write_identifier(out_chunk, NMO_CKSPRITE_CHUNK_SAVE_OPTIONS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
        if (result.code != NMO_OK) return result;
        
        /* Write bitmap properties blob (v7+) */
        if (in_state->bitmap_properties && in_state->bitmap_properties_size > 0) {
            result = nmo_chunk_write_buffer(out_chunk, in_state->bitmap_properties,
                in_state->bitmap_properties_size);
            if (result.code != NMO_OK) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Failed to write bitmap properties"));
            }
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_cksprite(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_cksprite_deserialize(chunk, arena, (nmo_cksprite_state_t *)out_ptr);
}

static nmo_result_t vtable_write_cksprite(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_cksprite_serialize((const nmo_cksprite_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cksprite_vtable = {
    .read = vtable_read_cksprite,
    .write = vtable_write_cksprite,
    .validate = NULL
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

nmo_result_t nmo_register_cksprite_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cksprite_schemas"));
    }
    
    /* Register minimal schema with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKSpriteState",
                                                      sizeof(nmo_cksprite_state_t),
                                                      alignof(nmo_cksprite_state_t));
    
    nmo_builder_set_vtable(&builder, &nmo_cksprite_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}

