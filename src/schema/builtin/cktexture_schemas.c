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

#include "schema/nmo_cktexture_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
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
 * @brief Validate texture format
 */
static bool is_valid_texture_format(const nmo_texture_format_t *format) {
    if (!format) return false;
    
    /* Check dimensions (must be > 0, typically power of 2) */
    if (format->width == 0 || format->height == 0) return false;
    if (format->width > 16384 || format->height > 16384) return false;
    
    /* Check bits per pixel (common values: 8, 16, 24, 32) */
    if (format->bits_per_pixel != 8 && format->bits_per_pixel != 16 &&
        format->bits_per_pixel != 24 && format->bits_per_pixel != 32) {
        return false;
    }
    
    /* Check image size matches dimensions */
    uint32_t expected_size = format->bytes_per_line * format->height;
    if (format->image_size > 0 && format->image_size < expected_size) {
        return false;
    }
    
    return true;
}

/* ========================================================================
 * Deserialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Deserialize identifier 0x00040000 (texture format)
 */
static nmo_result_t deserialize_texture_format(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *state
) {
    nmo_result_t result;
    
    /* Read width and height */
    result = nmo_chunk_read_dword(chunk, &state->format.width);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read texture width"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_dword(chunk, &state->format.height);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read texture height"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read bits per pixel */
    result = nmo_chunk_read_dword(chunk, &state->format.bits_per_pixel);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read bits per pixel"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read bytes per line (stride) */
    result = nmo_chunk_read_dword(chunk, &state->format.bytes_per_line);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read bytes per line"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read image size */
    result = nmo_chunk_read_dword(chunk, &state->format.image_size);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read image size"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Read color masks */
    result = nmo_chunk_read_dword(chunk, &state->format.red_mask);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_read_dword(chunk, &state->format.green_mask);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_read_dword(chunk, &state->format.blue_mask);
    NMO_RETURN_IF_ERROR(result);
    
    result = nmo_chunk_read_dword(chunk, &state->format.alpha_mask);
    NMO_RETURN_IF_ERROR(result);
    
    state->has_format = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x00200000 (palette data)
 */
static nmo_result_t deserialize_palette(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *state
) {
    nmo_result_t result;
    
    /* Read palette size */
    result = nmo_chunk_read_dword(chunk, &state->palette_size);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read palette size"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Allocate palette */
    if (state->palette_size > 0) {
        state->palette = (uint32_t *)nmo_arena_alloc(
            arena, state->palette_size * sizeof(uint32_t), alignof(uint32_t)
        );
        if (!state->palette) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to allocate palette"
            ));
        }
        
        /* Read palette entries */
        for (uint32_t i = 0; i < state->palette_size; i++) {
            result = nmo_chunk_read_dword(chunk, &state->palette[i]);
            if (result.code != NMO_OK) {
                nmo_error_t *err = NMO_ERROR(
                    arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Failed to read palette entry"
                );
                nmo_error_add_cause(err, result.error);
                return nmo_result_error(err);
            }
        }
    }
    
    state->has_palette = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x10000000 (pixel data)
 */
static nmo_result_t deserialize_pixel_data(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *state
) {
    nmo_result_t result;
    
    /* Read pixel data size */
    result = nmo_chunk_read_dword(chunk, &state->pixel_data_size);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read pixel data size"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Allocate and read pixel data */
    if (state->pixel_data_size > 0) {
        state->pixel_data = (uint8_t *)nmo_arena_alloc(
            arena, state->pixel_data_size, 1
        );
        if (!state->pixel_data) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to allocate pixel data"
            ));
        }
        
        /* Read pixel buffer */
        size_t bytes_read = nmo_chunk_read_and_fill_buffer(
            chunk, state->pixel_data, state->pixel_data_size
        );
        if (bytes_read != state->pixel_data_size) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                "Incomplete pixel data read"
            ));
        }
    }
    
    state->has_pixel_data = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x00800000 (video memory backup)
 */
static nmo_result_t deserialize_video_backup(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *state
) {
    nmo_result_t result;
    
    /* Read video backup size */
    result = nmo_chunk_read_dword(chunk, &state->video_backup_size);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(
            arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Failed to read video backup size"
        );
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    /* Allocate and read video backup */
    if (state->video_backup_size > 0) {
        state->video_backup = (uint8_t *)nmo_arena_alloc(
            arena, state->video_backup_size, 1
        );
        if (!state->video_backup) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to allocate video backup"
            ));
        }
        
        size_t bytes_read = nmo_chunk_read_and_fill_buffer(
            chunk, state->video_backup, state->video_backup_size
        );
        if (bytes_read != state->video_backup_size) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                "Incomplete video backup read"
            ));
        }
    }
    
    state->has_video_backup = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x00400000 (file path)
 */
static nmo_result_t deserialize_file_path(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *state
) {
    char *file_path = NULL;
    size_t len = nmo_chunk_read_string(chunk, &file_path);
    (void)len;  /* String length not needed */
    
    state->file_path = file_path;
    state->has_file_path = (file_path != NULL);
    
    return nmo_result_ok();
}

/**
 * @brief Main deserialization function (modern format v5+)
 *
 * Identifier Processing:
 * - 0x00040000: Texture format (required)
 * - 0x10000000: Pixel data (required)
 * - 0x00200000: Palette (optional)
 * - 0x00800000: Video backup (optional)
 * - 0x00400000: File path (optional)
 */
static nmo_result_t cktexture_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *out_state
) {
    nmo_result_t result;
    
    /* Initialize with defaults */
    memset(out_state, 0, sizeof(*out_state));
    out_state->needs_mipmap_generation = false;
    
    /* Process identifier 0x00040000: Texture format */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_FORMAT);
    if (result.code == NMO_OK) {
        result = deserialize_texture_format(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x00200000: Palette */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_PALETTE);
    if (result.code == NMO_OK) {
        result = deserialize_palette(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x10000000: Pixel data */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_SYSMEM);
    if (result.code == NMO_OK) {
        result = deserialize_pixel_data(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x00800000: Video memory backup */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_VIDEOMEM);
    if (result.code == NMO_OK) {
        result = deserialize_video_backup(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x00400000: File path */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_FILEPATH);
    if (result.code == NMO_OK) {
        result = deserialize_file_path(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    return nmo_result_ok();
}

/* ========================================================================
 * Serialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Main serialization function (modern format v5+)
 *
 * Identifier Writing:
 * - 0x00040000: Format (written if has_format is true)
 * - 0x10000000: Pixel data (written if has_pixel_data is true)
 * - 0x00200000: Palette (written if has_palette is true)
 * - 0x00800000: Video backup (written if has_video_backup is true)
 * - 0x00400000: File path (written if has_file_path is true)
 */
static nmo_result_t cktexture_serialize_modern(
    const nmo_ck_texture_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
) {
    nmo_result_t result;
    
    /* Write identifier 0x00040000: Texture format */
    if (state->has_format) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_FORMAT);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.width);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.height);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.bits_per_pixel);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.bytes_per_line);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.image_size);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.red_mask);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.green_mask);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.blue_mask);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->format.alpha_mask);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Write identifier 0x00200000: Palette */
    if (state->has_palette && state->palette_size > 0) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_PALETTE);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->palette_size);
        NMO_RETURN_IF_ERROR(result);
        
        for (uint32_t i = 0; i < state->palette_size; i++) {
            result = nmo_chunk_write_dword(chunk, state->palette[i]);
            NMO_RETURN_IF_ERROR(result);
        }
    }
    
    /* Write identifier 0x10000000: Pixel data */
    if (state->has_pixel_data && state->pixel_data_size > 0) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_SYSMEM);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->pixel_data_size);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_buffer(chunk, state->pixel_data, state->pixel_data_size);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Write identifier 0x00800000: Video backup */
    if (state->has_video_backup && state->video_backup_size > 0) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_VIDEOMEM);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->video_backup_size);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_buffer(chunk, state->video_backup, state->video_backup_size);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Write identifier 0x00400000: File path */
    if (state->has_file_path && state->file_path) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKTEXTURE_IDENTIFIER_FILEPATH);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_string(chunk, state->file_path);
        NMO_RETURN_IF_ERROR(result);
    }
    
    return nmo_result_ok();
}

/* ========================================================================
 * Finish Loading Handler
 * ======================================================================== */

/**
 * @brief Finish loading callback for CKTexture objects
 *
 * Post-Deserialization Setup:
 * - Validates texture format
 * - Clears needs_mipmap_generation flag
 */
static nmo_result_t cktexture_finish_loading(
    nmo_ck_texture_state_t *state,
    void *context,
    nmo_arena_t *arena
) {
    (void)context;  /* Unused */
    (void)arena;    /* Unused */
    
    /* Validate format if present */
    if (state->has_format) {
        if (!is_valid_texture_format(&state->format)) {
            return nmo_result_error(NMO_ERROR(
                arena, NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Invalid texture format"
            ));
        }
    }
    
    /* Clear mipmap generation flag */
    state->needs_mipmap_generation = false;
    
    return nmo_result_ok();
}

/* ========================================================================
 * Schema Registration
 * ======================================================================== */

/**
 * @brief Register CKTexture schemas with the schema system
 */
nmo_result_t nmo_register_cktexture_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
) {
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_register_cktexture_schemas"
        ));
    }
    
    /* Get base types */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    
    if (!uint32_type) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Required types not found in registry"
        ));
    }
    
    /* Register CKTexture state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKTextureState",
        sizeof(nmo_ck_texture_state_t),
        alignof(nmo_ck_texture_state_t)
    );
    
    /* Add format fields */
    nmo_builder_add_field_ex(&builder, "width", uint32_type,
                            offsetof(nmo_ck_texture_state_t, format) + offsetof(nmo_texture_format_t, width), 0);
    nmo_builder_add_field_ex(&builder, "height", uint32_type,
                            offsetof(nmo_ck_texture_state_t, format) + offsetof(nmo_texture_format_t, height), 0);
    nmo_builder_add_field_ex(&builder, "bits_per_pixel", uint32_type,
                            offsetof(nmo_ck_texture_state_t, format) + offsetof(nmo_texture_format_t, bits_per_pixel), 0);
    
    /* Add data size fields */
    nmo_builder_add_field_ex(&builder, "pixel_data_size", uint32_type,
                            offsetof(nmo_ck_texture_state_t, pixel_data_size), 0);
    nmo_builder_add_field_ex(&builder, "palette_size", uint32_type,
                            offsetof(nmo_ck_texture_state_t, palette_size), 0);
    
    /* Add flags */
    nmo_builder_add_field_ex(&builder, "save_options", uint32_type,
                            offsetof(nmo_ck_texture_state_t, save_options), 0);
    nmo_builder_add_field_ex(&builder, "flags", uint32_type,
                            offsetof(nmo_ck_texture_state_t, flags), 0);
    
    /* Note: Pointer fields (pixel_data, palette, file_path) are not added to builder
     * as they require special handling during serialization */
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}
