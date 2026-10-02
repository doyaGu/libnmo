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
#include "object/nmo_deserialize_context.h"
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
#include <stdint.h>
#include <string.h>

static nmo_status_t nmo_sprite_bitmap_copy(
    nmo_arena_t *arena, void *dst, const void *src, const void *src_owner)
{
    (void)src_owner;
    return nmo_bitmap_slots_copy(arena, dst, src);
}

static bool nmo_sprite_bitmap_equals(
    const void *a, const void *b, const void *a_owner, const void *b_owner)
{
    (void)a_owner;
    (void)b_owner;
    return nmo_bitmap_slots_equals(a, b);
}

static uint32_t nmo_sprite_bitmap_hash(
    uint32_t hash, const void *value, const void *owner)
{
    (void)owner;
    return nmo_bitmap_slots_hash(hash, value);
}

static const nmo_object_state_custom_ops_t nmo_sprite_bitmap_ops = {
    .copy = nmo_sprite_bitmap_copy,
    .equals = nmo_sprite_bitmap_equals,
    .hash = nmo_sprite_bitmap_hash,
};

/* RCKSprite::RCKSprite resets the source rectangle to nothing. */
static void nmo_sprite_set_defaults(void *instance)
{
    nmo_sprite_state_t *state = instance;
    state->sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->entity.source_rect.left = 0.0f;
    state->entity.source_rect.top = 0.0f;
    state->entity.source_rect.right = 0.0f;
    state->entity.source_rect.bottom = 0.0f;
}

static const nmo_object_state_member_t nmo_sprite_members[] = {
    NMO_STATE_VALUE(nmo_sprite_state_t, has_sprite_ref),
    NMO_STATE_VALUE(nmo_sprite_state_t, sprite_ref),
    NMO_STATE_VALUE(nmo_sprite_state_t, has_bitmap_data),
    NMO_STATE_CUSTOM(nmo_sprite_state_t, bitmap, nmo_sprite_bitmap_ops),
    NMO_STATE_VALUE(nmo_sprite_state_t, has_transparency),
    NMO_STATE_VALUE(nmo_sprite_state_t, is_transparent),
    NMO_STATE_VALUE(nmo_sprite_state_t, transparent_color),
    NMO_STATE_VALUE(nmo_sprite_state_t, has_slot),
    NMO_STATE_VALUE(nmo_sprite_state_t, current_slot),
    NMO_STATE_VALUE(nmo_sprite_state_t, has_video_format),
    NMO_STATE_VALUE(nmo_sprite_state_t, video_format),
    NMO_STATE_VALUE(nmo_sprite_state_t, has_save_options),
    NMO_STATE_VALUE(nmo_sprite_state_t, save_options),
    NMO_STATE_VALUE(nmo_sprite_state_t, bitmap_properties_size),
    NMO_STATE_BYTES(nmo_sprite_state_t, bitmap_properties, bitmap_properties_size)
};

static const nmo_object_state_layout_t nmo_sprite_layout = {
    .size = sizeof(nmo_sprite_state_t),
    .base_vtable = &nmo_2dentity_vtable,
    .base_size = sizeof(nmo_2dentity_state_t),
    .members = nmo_sprite_members,
    .member_count = sizeof(nmo_sprite_members) / sizeof(nmo_sprite_members[0]),
    .set_defaults = nmo_sprite_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(sprite, nmo_sprite_layout)

static void nmo_sprite_dispose_base_arrays(nmo_sprite_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->entity.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sprite_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_sprite_state_t, entity),
                    sizeof(nmo_2dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite_state_t, has_sprite_ref, CKPGUID_BOOL),
    NMO_FIELD_REF_VALUE(nmo_sprite_state_t, sprite_ref),
    NMO_FIELD(nmo_sprite_state_t, has_bitmap_data, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, bitmap.has_movie_filename, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_sprite_state_t, bitmap.movie_filename, CKPGUID_STRING),
    NMO_FIELD(nmo_sprite_state_t, bitmap.has_slot_filenames, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, bitmap.slot_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_sprite_state_t, bitmap.slot_filenames, bitmap.slot_count, 1, CKPGUID_STRING),
    NMO_FIELD(nmo_sprite_state_t, bitmap.reader_width, CKPGUID_INT),
    NMO_FIELD(nmo_sprite_state_t, bitmap.reader_height, CKPGUID_INT),
    NMO_FIELD(nmo_sprite_state_t, bitmap.reader_bpp, CKPGUID_INT),
    NMO_FIELD(nmo_sprite_state_t, bitmap.kind, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_sprite_state_t, bitmap.reader_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_sprite_state_t, bitmap.raw_slots, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_sprite_state_t, bitmap.bitmap2_slots, CKPGUID_POINTER),
    NMO_FIELD(nmo_sprite_state_t, has_transparency, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, is_transparent, CKPGUID_BOOL),
    NMO_FIELD_NAMED("transparent_color", offsetof(nmo_sprite_state_t, transparent_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite_state_t, has_slot, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, current_slot, CKPGUID_UINT32),
    NMO_FIELD(nmo_sprite_state_t, has_video_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, video_format, CKPGUID_UINT32),
    NMO_FIELD(nmo_sprite_state_t, has_save_options, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite_state_t, save_options, NMO_GUID_ENUM_CK_TEXTURE_SAVEOPTIONS),
    NMO_FIELD(nmo_sprite_state_t, bitmap_properties_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_sprite_state_t, bitmap_properties, bitmap_properties_size, 1, CKPGUID_UINT8)
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

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

    NMO_RETURN_IF_ERROR(nmo_2dentity_remap_dependencies(&state->entity, NULL, context));

    /* Keep clone reference and raw bitmap section state intact. */
    return nmo_sprite_validate(state, NULL, NULL);
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_sprite)

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
    state->sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->has_bitmap_data = false;
    memset(&state->bitmap, 0, sizeof(state->bitmap));
    state->bitmap_properties = NULL;
    state->bitmap_properties_size = 0;
    NMO_RETURN_OK();
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
    size_t section_dwords = 0u;
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITESHARED, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        nmo_ref_t sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &sprite_ref);
        if (result != NMO_OK) {
            return result;
        }
        out_state->sprite_ref = sprite_ref;
        out_state->has_sprite_ref = sprite_ref.state != NMO_REF_NONE;
        /* When sprite ref is present, bitmap data is cloned from referenced sprite.
         * No bitmap payload should be present in this chunk. */
        out_state->has_bitmap_data = false;
    } else if (seek_result == NMO_ERR_NOT_FOUND) {
        /* No sprite reference - read embedded bitmap payloads */
        out_state->has_sprite_ref = false;
        out_state->has_bitmap_data = true;

        const nmo_bitmap_slot_ids_t ids = NMO_CKSPRITE_BITMAP_IDS;
        NMO_RETURN_IF_ERROR(nmo_bitmap_slots_read(
            chunk, arena, &ids, &out_state->bitmap));
    } else return seek_result;
    
    /* Read video format (identifier 0x40000000). The Ballance engine neither
       reads nor writes it; files from later engines carry one dword. */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITEVIDEOFORMAT, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        result = nmo_chunk_read_dword(chunk, &out_state->video_format);
        if (result != NMO_OK) return result;
        out_state->has_video_format = true;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    /* Read transparency (identifier 0x20000) */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITETRANSPARENT, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result != NMO_OK) {
            return result;
        }
        /* Read transparency boolean flag */
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result != NMO_OK) {
            return result;
        }
        out_state->is_transparent = (transparent_flag != 0);
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITECURRENTIMAGE, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result != NMO_OK) {
            return result;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read save options (identifier 0x20000000) */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITEFORMAT, &section_dwords);
    if (seek_result == NMO_OK) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        uint32_t save_options = 0u;
        result = nmo_chunk_read_dword(chunk, &save_options);
        if (result != NMO_OK) {
            return result;
        }
        
        /* Read CKBitmapProperties blob (size-prefixed buffer) */
        void *props = NULL;
        size_t props_size = 0;
        result = nmo_chunk_read_buffer(chunk, &props, &props_size);
        if (result != NMO_OK) return result;
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        out_state->has_save_options = true;
        out_state->save_options = save_options;
        if (props && props_size > 0) {
            out_state->bitmap_properties = (uint8_t *)props;
            out_state->bitmap_properties_size = props_size;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
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
    size_t section_dwords = 0u;
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITETRANSPARENT, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_transparency = true;
        result = nmo_chunk_read_dword(chunk, &out_state->transparent_color);
        if (result != NMO_OK) {
            return result;
        }
        uint32_t transparent_flag;
        result = nmo_chunk_read_dword(chunk, &transparent_flag);
        if (result != NMO_OK) {
            return result;
        }
        out_state->is_transparent = (transparent_flag != 0);
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read current slot (identifier 0x10000) */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITECURRENTIMAGE, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_slot = true;
        result = nmo_chunk_read_dword(chunk, &out_state->current_slot);
        if (result != NMO_OK) {
            return result;
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read sprite reference (identifier 0x80000) */
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SPRITESHARED, &section_dwords);
    if (seek_result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        nmo_ref_t sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &sprite_ref);
        if (result != NMO_OK) {
            return result;
        }
        out_state->sprite_ref = sprite_ref;
        out_state->has_sprite_ref = sprite_ref.state != NMO_REF_NONE;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKSprite state from chunk
 * 
 * Dispatches to file-backed or chunk-only deserializer.
 * Detection heuristic: if bitmap payload identifiers are present, use file-backed path.
 */
static nmo_status_t nmo_sprite_deserialize_internal(
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

    out_state->has_sprite_ref = false;
    out_state->sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    const bool is_file = nmo_object_deserialize_is_file(chunk, context);
    if (is_file) {
        result = deserialize_file_backed(chunk, arena, out_state);
    } else {
        result = deserialize_chunk_only(chunk, arena, out_state);
    }
    if (result != NMO_OK) {
        return result;
    }
    if (out_state->has_sprite_ref) {
        nmo_ref_check_class(
            &out_state->sprite_ref,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_SPRITE);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_sprite_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_sprite_state_t *out_state = (nmo_sprite_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_sprite_state_t decoded = {0};
    nmo_beobject_state_t *decoded_beobject = &decoded.entity.base.base;
    const nmo_beobject_state_t *old_beobject = &out_state->entity.base.base;
    if (old_beobject->scripts.allocator.alloc != NULL) {
        decoded_beobject->scripts.allocator = old_beobject->scripts.allocator;
    }
    if (old_beobject->attributes.allocator.alloc != NULL) {
        decoded_beobject->attributes.allocator = old_beobject->attributes.allocator;
    }
    if (old_beobject->legacy_attributes.allocator.alloc != NULL) {
        decoded_beobject->legacy_attributes.allocator =
            old_beobject->legacy_attributes.allocator;
    }

    nmo_status_t result = nmo_sprite_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_sprite_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_sprite_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKSprite SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKSprite state to chunk
 * 
 * Writes sprite data in original format (matches RCKSprite::Save behavior).
 */
static nmo_status_t nmo_sprite_serialize_internal(
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
    
    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

    /* Serialize parent CK2dEntity data */
    nmo_status_t result = nmo_2dentity_serialize(&in_state->entity, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }
    NMO_RETURN_IF_ERROR(nmo_sprite_validate(in_state, type, context));
    if (in_state->has_sprite_ref) {
        if (is_file || (save_flags & CK_STATESAVE_SPRITESHARED) != 0u) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_SPRITESHARED);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->sprite_ref);
            if (result != NMO_OK) return result;
        }
    }
    
    if (is_file) {
        if (!in_state->has_sprite_ref && in_state->has_bitmap_data) {
            const nmo_bitmap_slot_ids_t ids = NMO_CKSPRITE_BITMAP_IDS;
            result = nmo_bitmap_slots_write(out_chunk, &ids, &in_state->bitmap);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_video_format) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_SPRITEVIDEOFORMAT);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->video_format);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_transparency) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_SPRITETRANSPARENT);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(
                out_chunk, in_state->transparent_color);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(
                out_chunk, in_state->is_transparent ? 1 : 0);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_slot) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_SPRITECURRENTIMAGE);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(
                out_chunk, in_state->current_slot);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_save_options) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_SPRITEFORMAT);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->save_options);
            if (result != NMO_OK) return result;

            if (in_state->bitmap_properties &&
                in_state->bitmap_properties_size > 0) {
                result = nmo_chunk_write_buffer(
                    out_chunk, in_state->bitmap_properties,
                    in_state->bitmap_properties_size);
                if (result != NMO_OK) {
                    return result;
                }
            } else {
                result = nmo_chunk_write_buffer(out_chunk, NULL, 0);
                if (result != NMO_OK) {
                    return result;
                }
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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_sprite)

static nmo_status_t nmo_sprite_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sprite_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_2dentity_vtable.validate(
        &s->entity, NULL, context));
    const bool has_serialized_ref =
        nmo_ref_serialized_id(&s->sprite_ref) != NMO_OBJECT_ID_NONE;
    if ((s->has_sprite_ref != 0) != has_serialized_ref ||
        (s->has_sprite_ref && s->has_bitmap_data)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Sprite reference and bitmap modes are inconsistent");
    }
    NMO_RETURN_IF_ERROR(nmo_bitmap_slots_validate(&s->bitmap));
    NMO_VALIDATE_BYTES(s->bitmap_properties, s->bitmap_properties_size, "bitmap_properties");
    if (!s->has_bitmap_data &&
        (s->bitmap.slot_count != 0u || s->bitmap.kind != CKTEXTURE_BITMAP_NONE ||
         s->bitmap.has_slot_filenames || s->bitmap.has_movie_filename)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Sprite bitmap payload is present while bitmap mode is disabled");
    }
    if (!s->has_save_options && s->bitmap_properties_size != 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Sprite bitmap properties require save options");
    }
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_sprite_vtable = {
    .prepare_dependencies = nmo_sprite_prepare_dependencies,
    .remap_dependencies = nmo_sprite_remap_dependencies,
    .pre_delete = nmo_sprite_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
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





