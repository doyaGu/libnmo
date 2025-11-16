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

#include "schema/nmo_cksprite_schemas.h"
#include "schema/nmo_ck2dentity_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Read bitmap data from chunk (simplified)
 * 
 * Full implementation would handle palette, pixel formats, compression, etc.
 * For now, preserve as raw buffer for round-trip.
 */
static nmo_result_t read_bitmap_data(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbitmapdata_t *bitmap)
{
    /* Bitmap data reading is complex (CKBitmapData::ReadFromChunk in SDK)
     * For Phase 5, preserve entire bitmap section as raw data */
    
    /* Try to read dimensions if present (identifier 0x20000 from Save) */
    nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, 0x20000);
    if (seek_result.code == NMO_OK) {
        nmo_result_t result = nmo_chunk_read_dword(chunk, &bitmap->width);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_dword(chunk, &bitmap->height);
        if (result.code != NMO_OK) return result;
    }
    
    /* Preserve entire bitmap chunk section as raw data */
    size_t end_pos = nmo_chunk_get_position(chunk);
    size_t chunk_size = nmo_chunk_get_data_size(chunk);
    
    if (end_pos < chunk_size) {
        size_t remaining = chunk_size - end_pos;
        bitmap->raw_data = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (bitmap->raw_data) {
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk,
                bitmap->raw_data, remaining);
            if (bytes_read == remaining) {
                bitmap->raw_data_size = remaining;
            } else {
                bitmap->raw_data = NULL;
                bitmap->raw_data_size = 0;
            }
        }
    }
    
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
        /* No sprite reference - read embedded bitmap payload */
        out_state->has_sprite_ref = false;
        out_state->has_bitmap_data = true;
        
        /* Read bitmap data (identifiers: 0x200000, 0x10000000, 0x800000, 0x400000, 0x40000) */
        result = read_bitmap_data(chunk, arena, &out_state->bitmap_data);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read bitmap data"));
        }
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
        
        /* Read CKBitmapProperties blob (v7+, size varies) */
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        if (data_version > 6) {
            /* Read properties blob (size not known, read until next identifier) */
            size_t current_pos = nmo_chunk_get_position(chunk);
            size_t chunk_size = nmo_chunk_get_data_size(chunk);
            if (current_pos < chunk_size) {
                /* For simplicity, read remaining data as properties blob */
                size_t remaining = chunk_size - current_pos;
                out_state->bitmap_properties = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
                if (out_state->bitmap_properties) {
                    size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk,
                        out_state->bitmap_properties, remaining);
                    if (bytes_read == remaining) {
                        out_state->bitmap_properties_size = remaining;
                    }
                }
            }
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
    
    /* Detect file-backed vs chunk-only by checking for bitmap identifiers
     * File-backed: has dimensions (0x20000 from Save) or bitmap payloads
     * Chunk-only: only lightweight state identifiers */
    
    /* For now, always use file-backed path (more complete) */
    /* TODO: Add heuristic to detect chunk-only case */
    result = deserialize_file_backed(chunk, arena, out_state);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Preserve remaining data as raw tail for round-trip */
    size_t current_pos = nmo_chunk_get_position(chunk);
    size_t chunk_size = nmo_chunk_get_data_size(chunk);
    
    if (current_pos < chunk_size) {
        size_t remaining = chunk_size - current_pos;
        out_state->raw_tail = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (out_state->raw_tail) {
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk,
                out_state->raw_tail, remaining);
            if (bytes_read == remaining) {
                out_state->raw_tail_size = remaining;
            } else {
                out_state->raw_tail = NULL;
                out_state->raw_tail_size = 0;
            }
        }
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
    const nmo_cksprite_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    if (!state || !chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite_serialize"));
    }
    
    /* Serialize parent CK2dEntity data */
    nmo_result_t result = nmo_ck2dentity_serialize(&state->entity, chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Write sprite reference (identifier 0x80000) if present */
    if (state->has_sprite_ref) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITE_CHUNK_SPRITE_REF);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(chunk, state->sprite_ref_id);
        if (result.code != NMO_OK) return result;
    } else if (state->has_bitmap_data) {
        /* Write bitmap data (simplified - preserve raw data for round-trip) */
        if (state->bitmap_data.raw_data && state->bitmap_data.raw_data_size > 0) {
            result = nmo_chunk_write_buffer_no_size(chunk, state->bitmap_data.raw_data,
                state->bitmap_data.raw_data_size);
            if (result.code != NMO_OK) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Failed to write bitmap data"));
            }
        }
    }
    
    /* Write transparency (identifier 0x20000) */
    if (state->has_transparency) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITE_CHUNK_TRANSPARENCY);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(chunk, state->transparent_color);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(chunk, state->is_transparent ? 1 : 0);
        if (result.code != NMO_OK) return result;
    }
    
    /* Write current slot (identifier 0x10000) */
    if (state->has_slot) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITE_CHUNK_SLOT);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(chunk, state->current_slot);
        if (result.code != NMO_OK) return result;
    }
    
    /* Write save options (identifier 0x20000000) */
    if (state->has_save_options) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITE_CHUNK_SAVE_OPTIONS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(chunk, state->save_options);
        if (result.code != NMO_OK) return result;
        
        /* Write bitmap properties blob (v7+) */
        if (state->bitmap_properties && state->bitmap_properties_size > 0) {
            result = nmo_chunk_write_buffer_no_size(chunk, state->bitmap_properties,
                state->bitmap_properties_size);
            if (result.code != NMO_OK) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Failed to write bitmap properties"));
            }
        }
    }
    
    /* Write raw tail if present */
    if (state->raw_tail && state->raw_tail_size > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, state->raw_tail, state->raw_tail_size);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to write raw tail"));
        }
    }
    
    return nmo_result_ok();
}

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
    
    /* TODO: Register schema types when schema builder is ready */
    /* For now, schemas are registered via deserialize/serialize functions */
    
    return nmo_result_ok();
}
