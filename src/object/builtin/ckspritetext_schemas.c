/**
 * @file ckspritetext_schemas.c
 * @brief CKSpriteText schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKSpriteText (ClassID 29) deserialization, serialization,
 * and finish loading handlers.
 *
 * Reference: docs/CK2_3D_reverse_notes_extended.md lines 470-850
 */

#include "object/nmo_spritetext_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_2dentity_schemas.h"
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

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_spritetext_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_spritetext_state_t, base),
                    sizeof(nmo_2dentity_state_t), CKPGUID_NONE,
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

/**
 * @brief Clamp an integer value to a specified range
 */
static int32_t clamp_int32(int32_t value, int32_t min_val, int32_t max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

static void ckspritetext_init_defaults(
    nmo_spritetext_state_t *state,
    nmo_arena_t *arena)
{
    state->text_content = nmo_arena_strdup(arena, "");
    state->font.font_name = nmo_arena_strdup(arena, "Arial");
    state->font.size = 12;
    state->font.weight = NMO_FONT_WEIGHT_NORMAL;
    state->font.italic = 0;
    state->font.underline = 0;
    state->font_color = 0xFFFFFFFF;
    state->background_color = 0x00000000;
    state->needs_redraw = true;
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
    size_t len = nmo_chunk_read_string(chunk, &text_str);
    (void)len;  /* String length not needed */
    
    state->text_content = text_str ? text_str : nmo_arena_strdup(arena, "");
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    spritetext,
    nmo_spritetext_state_t,
    nmo_spritetext_serialize,
    nmo_spritetext_deserialize,
    nmo_spritetext_fields,
    CKPGUID_SPRITETEXT,
    "CKSpriteText",
    NMO_CID_SPRITETEXT,
    CKPGUID_SPRITE
)

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
    size_t len = nmo_chunk_read_string(chunk, &font_name);
    (void)len;  /* String length not needed */
    
    state->font.font_name = font_name ? font_name : nmo_arena_strdup(arena, "Arial");
    
    /* Read font size */
    result = nmo_chunk_read_int(chunk, &state->font.size);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font size (identifier 0x02000000)");
    }
    
    /* Read font weight */
    result = nmo_chunk_read_int(chunk, &state->font.weight);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font weight (identifier 0x02000000)");
    }
    
    /* Read italic flag */
    result = nmo_chunk_read_int(chunk, &state->font.italic);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read italic flag (identifier 0x02000000)");
    }

    /* Read underline flag */
    result = nmo_chunk_read_int(chunk, &state->font.underline);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read underline flag (identifier 0x02000000)");
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
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font color (identifier 0x04000000)");
    }
    
    /* Read background color */
    result = nmo_chunk_read_dword(chunk, &state->background_color);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read background color (identifier 0x04000000)");
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
    
    /* Initialize text/font defaults (base handled separately) */
    ckspritetext_init_defaults(out_state, arena);
    
    /* Process identifier 0x01000000: Text string */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETEXT);
    if (result == NMO_OK) {
        result = deserialize_text_content(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Default to empty string */
        out_state->text_content = nmo_arena_strdup(arena, "");
    }
    
    /* Process identifier 0x02000000: Font properties */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITEFONT);
    if (result == NMO_OK) {
        result = deserialize_font_properties(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Default font */
        out_state->font.font_name = nmo_arena_strdup(arena, "Arial");
        out_state->font.size = 12;
        out_state->font.weight = NMO_FONT_WEIGHT_NORMAL;
        out_state->font.italic = 0;
        out_state->font.underline = 0;
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
    
    /* Validate font name before serialization */
    if (!state->font.font_name || state->font.font_name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "CKSpriteText: Cannot serialize with NULL or empty font name");
    }
    
    /* Write identifier 0x01000000: Text string */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITETEXT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->text_content ? state->text_content : "");
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
            "Failed to write text string (identifier 0x01000000)");
    }
    
    /* Write identifier 0x02000000: Font properties */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITEFONT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->font.font_name);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
            "Failed to write font name (identifier 0x02000000)");
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
static nmo_status_t ckspritetext_finish_loading(
    nmo_spritetext_state_t *state,
    void *context,
    nmo_arena_t *arena
) {
    (void)context;  /* Unused */
    
    /* Validate and normalize font name */
    if (!state->font.font_name || state->font.font_name[0] == '\0') {
        state->font.font_name = nmo_arena_strdup(arena, "Arial");
        if (!state->font.font_name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to allocate fallback font name");
        }
    }
    
    /* Clamp font size to reasonable range [6, 128] */
    if (state->font.size < 6 || state->font.size > 128) {
        state->font.size = clamp_int32(state->font.size, 6, 128);
    }
    
    /* Clamp font weight to standard range [100, 900] */
    if (state->font.weight < 100 || state->font.weight > 900) {
        state->font.weight = clamp_int32(state->font.weight, 100, 900);
    }
    
    /* Normalize italic flag to 0 or 1 */
    if (state->font.italic != 0 && state->font.italic != 1) {
        state->font.italic = state->font.italic ? 1 : 0;
    }
    
    /* Normalize underline flag to 0 or 1 */
    if (state->font.underline != 0 && state->font.underline != 1) {
        state->font.underline = state->font.underline ? 1 : 0;
    }

    /* Clear redraw flag */
    state->needs_redraw = false;
    
    NMO_RETURN_OK();
}

/* ========================================================================
 * CKSpriteText Deserialization / Serialization
 * ======================================================================== */

nmo_status_t nmo_spritetext_deserialize(
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

    nmo_status_t result = nmo_2dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    result = ckspritetext_deserialize_modern(chunk, arena, out_state);
    if (result != NMO_OK) {
        return result;
    }

    return ckspritetext_finish_loading(out_state, NULL, arena);
}

nmo_status_t nmo_spritetext_serialize(
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

    nmo_status_t result = nmo_2dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    return ckspritetext_serialize_modern(in_state, out_chunk, arena);
}


