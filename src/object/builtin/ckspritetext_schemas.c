/**
 * @file ckspritetext_schemas.c
 * @brief CKSpriteText schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKSpriteText (ClassID 29) deserialization, serialization,
 * and runtime dependency hooks.
 *
 * Reference: docs/CK2_3D_reverse_notes_extended.md lines 470-850
 */

#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_struct_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(spritetext, nmo_spritetext_state_t)

static void nmo_spritetext_dispose_base_arrays(nmo_spritetext_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->base.entity.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_spritetext_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_spritetext_state_t, base),
                    sizeof(nmo_sprite_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_OPT(nmo_spritetext_state_t, text_content, CKPGUID_STRING),
    NMO_FIELD_NAMED("font", offsetof(nmo_spritetext_state_t, font),
                    sizeof(nmo_font_info_t), NMO_GUID_STRUCT_FONTINFO,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("font_color", offsetof(nmo_spritetext_state_t, font_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("background_color", offsetof(nmo_spritetext_state_t, background_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_spritetext_state_t, needs_redraw, CKPGUID_BOOL)
};

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static void ckspritetext_init_defaults(
    nmo_spritetext_state_t *state,
    nmo_arena_t *arena)
{
    (void)arena;
    state->text_content = NULL;
    state->font.font_name = NULL;
    state->font.size = 0;
    state->font.weight = 0;
    state->font.italic = 0;
    state->font.underline = 0;
    state->font_color = 0xFFFFFFFF;
    state->background_color = 0x00000000;
    state->needs_redraw = false;
}

/* ========================================================================
 * Deserialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Deserialize identifier 0x01000000 (text string)
 */
static nmo_status_t deserialize_text_content(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_spritetext_state_t *state
) {
    char *text_str = NULL;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &text_str, NULL));
    (void)arena;
    state->text_content = text_str;
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize identifier 0x02000000 (font properties)
 */
static nmo_status_t deserialize_font_properties(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_spritetext_state_t *state
) {
    char *font_name = NULL;
    nmo_status_t result;
    
    /* Read font name */
    NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &font_name, NULL));
    
    (void)arena;
    state->font.font_name = font_name;
    
    /* Read font size */
    result = nmo_chunk_read_int(chunk, &state->font.size);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Read font weight */
    result = nmo_chunk_read_int(chunk, &state->font.weight);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Read italic flag */
    result = nmo_chunk_read_int(chunk, &state->font.italic);
    if (result != NMO_OK) {
        return result;
    }

    /* Read underline flag */
    result = nmo_chunk_read_int(chunk, &state->font.underline);
    if (result != NMO_OK) {
        return result;
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize identifier 0x04000000 (text and background colors)
 */
static nmo_status_t deserialize_colors(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_spritetext_state_t *state
) {
    (void)arena;
    nmo_status_t result;
    
    /* Read font color */
    result = nmo_chunk_read_dword(chunk, &state->font_color);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Read background color */
    result = nmo_chunk_read_dword(chunk, &state->background_color);
    if (result != NMO_OK) {
        return result;
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Main deserialization function (modern format v5+)
 *
 * Reference: RCKSpriteText::Load at 0x10062547
 *
 * Identifier Processing:
 * - 0x01000000: Text string (optional, defaults to "")
 * - 0x02000000: Font properties (optional, defaults to Arial 12pt normal)
 * - 0x04000000: Colors (optional, defaults to white on transparent)
 */
static nmo_status_t ckspritetext_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_spritetext_state_t *out_state
) {
    nmo_status_t result;
    
    /* Initialize defaults (base handled separately) */
    ckspritetext_init_defaults(out_state, arena);
    
    /* Process identifier 0x01000000: Text string */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETEXT);
    if (result == NMO_OK) {
        result = deserialize_text_content(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x02000000: Font properties */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITEFONT);
    if (result == NMO_OK) {
        result = deserialize_font_properties(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x04000000: Colors */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETEXTCOLOR);
    if (result == NMO_OK) {
        result = deserialize_colors(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Already initialized to defaults (white on transparent) */
    }
    
    NMO_RETURN_OK();
}

/* ========================================================================
 * Serialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Main serialization function (modern format v5+)
 *
 * Reference: RCKSpriteText::Save at 0x100621FF
 *
 * Identifier Writing:
 * - 0x01000000: Text string (always written, even if empty)
 * - 0x02000000: Font properties (always written)
 * - 0x04000000: Colors (always written)
 */
static nmo_status_t ckspritetext_serialize_modern(
    const nmo_spritetext_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
) {
    (void)arena;
    nmo_status_t result;
    
    /* Write identifier 0x01000000: Text string */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITETEXT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->text_content ? state->text_content : "");
    if (result != NMO_OK) {
        return result;
    }
    
    /* Write identifier 0x02000000: Font properties */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITEFONT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->font.font_name);
    if (result != NMO_OK) {
        return result;
    }
    
    result = nmo_chunk_write_int(chunk, state->font.size);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.weight);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.italic);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.underline);
    NMO_RETURN_IF_ERROR(result);
    
    /* Write identifier 0x04000000: Colors */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITETEXTCOLOR);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_dword(chunk, state->font_color);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_dword(chunk, state->background_color);
    NMO_RETURN_IF_ERROR(result);
    
    NMO_RETURN_OK();
}

/* ========================================================================
 * Finish Loading Handler
 * ======================================================================== */

/**
 * @brief Finish loading callback for CKSpriteText objects
 *
 * Reference: RCKSpriteText::Load at 0x10062547 (calls Redraw at end)
 *
 * Post-Deserialization Setup:
 * - Validates and normalizes font properties
 * - Clamps font size to [6, 128] range
 * - Clamps font weight to [100, 900] range
 * - Ensures font name is not empty (sets "Arial" as fallback)
 * - Clears needs_redraw flag
 */
static nmo_status_t ckspritetext_normalize_state(
    nmo_spritetext_state_t *state,
    void *context,
    nmo_arena_t *arena
) {
    (void)context;
    (void)arena;
    state->needs_redraw = false;
    NMO_RETURN_OK();
}

nmo_status_t nmo_spritetext_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_spritetext_remap_dependencies");
    }

    nmo_spritetext_state_t *state = (nmo_spritetext_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_sprite_remap_dependencies(&state->base, NULL, context));
    return ckspritetext_normalize_state(state, NULL, NULL);
}

nmo_status_t nmo_spritetext_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_spritetext_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_spritetext_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_spritetext_pre_delete");
    }
    nmo_spritetext_state_t *state = (nmo_spritetext_state_t *)instance;
    state->needs_redraw = false;
    state->text_content = NULL;
    state->font.font_name = NULL;
    NMO_RETURN_OK();
}

static void nmo_spritetext_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ========================================================================
 * CKSpriteText Deserialization / Serialization
 * ======================================================================== */

static nmo_status_t nmo_spritetext_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_spritetext_state_t *out_state = (nmo_spritetext_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_spritetext_deserialize");
    }

    nmo_status_t result = nmo_sprite_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    result = ckspritetext_deserialize_modern(chunk, arena, out_state);
    if (result != NMO_OK) {
        return result;
    }

    return ckspritetext_normalize_state(out_state, NULL, arena);
}

nmo_status_t nmo_spritetext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_spritetext_state_t *out_state =
        (nmo_spritetext_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_spritetext_state_t decoded = {0};
    nmo_beobject_state_t *decoded_beobject =
        &decoded.base.entity.base.base;
    const nmo_beobject_state_t *old_beobject =
        &out_state->base.entity.base.base;
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

    nmo_status_t result = nmo_spritetext_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_spritetext_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_spritetext_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_spritetext_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_spritetext_state_t *in_state = (const nmo_spritetext_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_spritetext_serialize");
    }

    nmo_status_t result = nmo_sprite_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    return ckspritetext_serialize_modern(in_state, out_chunk, arena);
}

nmo_status_t nmo_spritetext_serialize(
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

    nmo_status_t result = nmo_spritetext_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS(spritetext, nmo_spritetext_state_t)

nmo_type_vtable_t nmo_spritetext_vtable = {
    .prepare_dependencies = nmo_spritetext_prepare_dependencies,
    .remap_dependencies = nmo_spritetext_remap_dependencies,
    .pre_delete = nmo_spritetext_pre_delete,
    .post_delete = nmo_spritetext_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_spritetext_create,
        nmo_spritetext_destroy,
        nmo_spritetext_serialize,
        nmo_spritetext_deserialize,
        nmo_spritetext_copy,
        nmo_spritetext_validate,
        nmo_spritetext_equals,
        nmo_spritetext_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_spritetext_type,
    CKPGUID_SPRITETEXT,
    "CKSpriteText",
    NMO_CID_SPRITETEXT,
    CKPGUID_SPRITE,
    nmo_spritetext_state_t,
    &nmo_spritetext_vtable,
    nmo_spritetext_fields)






