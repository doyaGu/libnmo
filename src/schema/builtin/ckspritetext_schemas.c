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

#include "schema/nmo_ckspritetext_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

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

/**
 * @brief Duplicate a string using arena allocation
 */
static char *arena_strdup(nmo_arena_t *arena, const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *copy = (char *)nmo_arena_alloc(arena, len + 1, 1);
    if (!copy) return NULL;
    
    memcpy(copy, str, len + 1);
    return copy;
}

/**
 * @brief Check if a charset value is valid (Windows LOGFONT standard)
 */
static bool is_valid_charset(int32_t charset) {
    switch (charset) {
        case NMO_FONT_CHARSET_ANSI:
        case NMO_FONT_CHARSET_DEFAULT:
        case NMO_FONT_CHARSET_SYMBOL:
        case NMO_FONT_CHARSET_SHIFTJIS:
        case NMO_FONT_CHARSET_HANGEUL:
        case NMO_FONT_CHARSET_GB2312:
        case NMO_FONT_CHARSET_CHINESEBIG5:
        case NMO_FONT_CHARSET_OEM:
        case NMO_FONT_CHARSET_JOHAB:
        case NMO_FONT_CHARSET_HEBREW:
        case NMO_FONT_CHARSET_ARABIC:
        case NMO_FONT_CHARSET_GREEK:
        case NMO_FONT_CHARSET_TURKISH:
        case NMO_FONT_CHARSET_VIETNAMESE:
        case NMO_FONT_CHARSET_THAI:
        case NMO_FONT_CHARSET_EASTEUROPE:
        case NMO_FONT_CHARSET_RUSSIAN:
        case NMO_FONT_CHARSET_BALTIC:
            return true;
        default:
            return false;
    }
}

/* ========================================================================
 * Deserialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Deserialize identifier 0x01000000 (text string)
 */
static nmo_result_t deserialize_text_content(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *state
) {
    char *text_str = NULL;
    size_t len = nmo_chunk_read_string(chunk, &text_str);
    (void)len;  /* String length not needed */
    
    state->text_content = text_str ? text_str : arena_strdup(arena, "");
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x02000000 (font properties)
 */
static nmo_result_t deserialize_font_properties(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *state
) {
    char *font_name = NULL;
    nmo_result_t result;
    
    /* Read font name */
    size_t len = nmo_chunk_read_string(chunk, &font_name);
    (void)len;  /* String length not needed */
    
    state->font.font_name = font_name ? font_name : arena_strdup(arena, "Arial");
    
    /* Read font size */
    result = nmo_chunk_read_int(chunk, &state->font.size);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font size (identifier 0x02000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read font weight */
    result = nmo_chunk_read_int(chunk, &state->font.weight);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font weight (identifier 0x02000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read italic flag */
    result = nmo_chunk_read_int(chunk, &state->font.italic);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read italic flag (identifier 0x02000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read charset */
    result = nmo_chunk_read_int(chunk, &state->font.charset);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read charset (identifier 0x02000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x04000000 (text and background colors)
 */
static nmo_result_t deserialize_colors(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *state
) {
    nmo_result_t result;
    
    /* Read font color */
    result = nmo_chunk_read_dword(chunk, &state->font_color);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read font color (identifier 0x04000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read background color */
    result = nmo_chunk_read_dword(chunk, &state->background_color);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read background color (identifier 0x04000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    return nmo_result_ok();
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
static nmo_result_t ckspritetext_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *out_state
) {
    nmo_result_t result;
    
    /* Initialize with defaults */
    memset(out_state, 0, sizeof(*out_state));
    out_state->font_color = 0xFFFFFFFF;      /* White, fully opaque */
    out_state->background_color = 0x00000000; /* Black, fully transparent */
    out_state->needs_redraw = true;
    
    /* Process identifier 0x01000000: Text string */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_TEXT);
    if (result.code == NMO_OK) {
        result = deserialize_text_content(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Default to empty string */
        out_state->text_content = arena_strdup(arena, "");
    }
    
    /* Process identifier 0x02000000: Font properties */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_FONT);
    if (result.code == NMO_OK) {
        result = deserialize_font_properties(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Default font */
        out_state->font.font_name = arena_strdup(arena, "Arial");
        out_state->font.size = 12;
        out_state->font.weight = NMO_FONT_WEIGHT_NORMAL;
        out_state->font.italic = 0;
        out_state->font.charset = NMO_FONT_CHARSET_ANSI;
    }
    
    /* Process identifier 0x04000000: Colors */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_COLOR);
    if (result.code == NMO_OK) {
        result = deserialize_colors(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    } else {
        /* Already initialized to defaults (white on transparent) */
    }
    
    return nmo_result_ok();
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
static nmo_result_t ckspritetext_serialize_modern(
    const nmo_ck_spritetext_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
) {
    nmo_result_t result;
    
    /* Validate font name before serialization */
    if (!state->font.font_name || state->font.font_name[0] == '\0') {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "CKSpriteText: Cannot serialize with NULL or empty font name"
        ));
    }
    
    /* Write identifier 0x01000000: Text string */
    result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_TEXT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->text_content ? state->text_content : "");
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
            "Failed to write text string (identifier 0x01000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Write identifier 0x02000000: Font properties */
    result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_FONT);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_string(chunk, state->font.font_name);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
            "Failed to write font name (identifier 0x02000000)"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_write_int(chunk, state->font.size);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.weight);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.italic);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_int(chunk, state->font.charset);
    NMO_RETURN_IF_ERROR(result);
    
    /* Write identifier 0x04000000: Colors */
    result = nmo_chunk_write_identifier(chunk, NMO_CKSPRITETEXT_IDENTIFIER_COLOR);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_dword(chunk, state->font_color);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_write_dword(chunk, state->background_color);
    NMO_RETURN_IF_ERROR(result);
    
    return nmo_result_ok();
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
static nmo_result_t ckspritetext_finish_loading(
    nmo_ck_spritetext_state_t *state,
    void *context,
    nmo_arena_t *arena
) {
    (void)context;  /* Unused */
    
    /* Validate and normalize font name */
    if (!state->font.font_name || state->font.font_name[0] == '\0') {
        state->font.font_name = arena_strdup(arena, "Arial");
        if (!state->font.font_name) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to allocate fallback font name"
            ));
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
    
    /* Validate charset (warn if invalid, but don't fail) */
    if (!is_valid_charset(state->font.charset)) {
        /* Unknown charset, but proceed anyway */
    }
    
    /* Clear redraw flag */
    state->needs_redraw = false;
    
    return nmo_result_ok();
}

/* ========================================================================
 * Schema Registration
 * ======================================================================== */

/**
 * @brief Register CKSpriteText schemas with the schema system
 */
nmo_result_t nmo_register_ckspritetext_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
) {
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_register_ckspritetext_schemas"
        ));
    }
    
    /* Get base types */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *int32_type = nmo_schema_registry_find_by_name(registry, "i32");
    const nmo_schema_type_t *string_type = nmo_schema_registry_find_by_name(registry, "string");
    
    if (!uint32_type || !int32_type || !string_type) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Required types not found in registry"
        ));
    }
    
    /* Register CKSpriteText state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKSpriteTextState",
        sizeof(nmo_ck_spritetext_state_t),
        alignof(nmo_ck_spritetext_state_t)
    );
    
    /* Add text content field */
    nmo_builder_add_field_ex(&builder, "text_content", string_type,
                            offsetof(nmo_ck_spritetext_state_t, text_content), 0);
    
    /* Add font properties fields */
    nmo_builder_add_field_ex(&builder, "font_name", string_type,
                            offsetof(nmo_ck_spritetext_state_t, font) + offsetof(nmo_font_info_t, font_name), 0);
    nmo_builder_add_field_ex(&builder, "font_size", int32_type,
                            offsetof(nmo_ck_spritetext_state_t, font) + offsetof(nmo_font_info_t, size), 0);
    nmo_builder_add_field_ex(&builder, "font_weight", int32_type,
                            offsetof(nmo_ck_spritetext_state_t, font) + offsetof(nmo_font_info_t, weight), 0);
    nmo_builder_add_field_ex(&builder, "font_italic", int32_type,
                            offsetof(nmo_ck_spritetext_state_t, font) + offsetof(nmo_font_info_t, italic), 0);
    nmo_builder_add_field_ex(&builder, "font_charset", int32_type,
                            offsetof(nmo_ck_spritetext_state_t, font) + offsetof(nmo_font_info_t, charset), 0);
    
    /* Add color fields */
    nmo_builder_add_field_ex(&builder, "font_color", uint32_type,
                            offsetof(nmo_ck_spritetext_state_t, font_color), 0);
    nmo_builder_add_field_ex(&builder, "background_color", uint32_type,
                            offsetof(nmo_ck_spritetext_state_t, background_color), 0);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}
