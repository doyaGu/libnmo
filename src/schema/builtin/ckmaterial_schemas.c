/**
 * @file ckmaterial_schemas.c
 * @brief CKMaterial schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKMaterial (ClassID 30) deserialization, serialization,
 * and finish loading handlers.
 *
 * Reference: reference/include/CKMaterial.h
 */

#include "schema/nmo_ckmaterial_schemas.h"
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
 * @brief Initialize material with default values
 */
static void initialize_material_defaults(nmo_ck_material_state_t *state) {
    memset(state, 0, sizeof(*state));
    
    /* Default colors */
    state->colors.ambient_r = 0.3f;
    state->colors.ambient_g = 0.3f;
    state->colors.ambient_b = 0.3f;
    state->colors.ambient_a = 1.0f;
    
    state->colors.diffuse_r = 0.7f;
    state->colors.diffuse_g = 0.7f;
    state->colors.diffuse_b = 0.7f;
    state->colors.diffuse_a = 1.0f;
    
    state->colors.specular_r = 0.5f;
    state->colors.specular_g = 0.5f;
    state->colors.specular_b = 0.5f;
    state->colors.specular_a = 1.0f;
    
    state->colors.emissive_r = 0.0f;
    state->colors.emissive_g = 0.0f;
    state->colors.emissive_b = 0.0f;
    state->colors.emissive_a = 1.0f;
    
    /* Default specular power (disabled) */
    state->specular_power = 0.0f;
    
    /* Default texture settings */
    state->texture_blend_mode = NMO_TEXBLEND_MODULATE;
    state->texture_min_mode = NMO_TEXFILTER_LINEAR;
    state->texture_mag_mode = NMO_TEXFILTER_LINEAR;
    state->texture_address_mode = NMO_TEXADDR_WRAP;
    state->texture_border_color = 0xFF000000;
    
    /* Default rendering modes */
    state->shade_mode = NMO_SHADE_GOURAUD;
    state->fill_mode = NMO_FILL_SOLID;
    
    /* Default blending */
    state->blend_enabled = false;
    state->src_blend = NMO_BLEND_ONE;
    state->dest_blend = NMO_BLEND_ZERO;
    
    /* Default alpha testing */
    state->alpha_test_enabled = false;
    state->alpha_func = NMO_ALPHA_ALWAYS;
    state->alpha_ref = 0;
    
    /* Default Z-buffer */
    state->zwrite_enabled = true;
    state->ztest_enabled = true;
    
    /* Default two-sided */
    state->two_sided = false;
}

/* ========================================================================
 * Deserialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Deserialize identifier 0x00001000 (material colors)
 */
static nmo_result_t deserialize_colors(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *state
) {
    nmo_result_t result;
    
    /* Read ambient color (4 floats) */
    result = nmo_chunk_read_float(chunk, &state->colors.ambient_r);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.ambient_g);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.ambient_b);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.ambient_a);
    NMO_RETURN_IF_ERROR(result);
    
    /* Read diffuse color (4 floats) */
    result = nmo_chunk_read_float(chunk, &state->colors.diffuse_r);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.diffuse_g);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.diffuse_b);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.diffuse_a);
    NMO_RETURN_IF_ERROR(result);
    
    /* Read specular color (4 floats) */
    result = nmo_chunk_read_float(chunk, &state->colors.specular_r);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.specular_g);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.specular_b);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.specular_a);
    NMO_RETURN_IF_ERROR(result);
    
    /* Read emissive color (4 floats) */
    result = nmo_chunk_read_float(chunk, &state->colors.emissive_r);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.emissive_g);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.emissive_b);
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_read_float(chunk, &state->colors.emissive_a);
    NMO_RETURN_IF_ERROR(result);
    
    /* Read specular power */
    result = nmo_chunk_read_float(chunk, &state->specular_power);
    NMO_RETURN_IF_ERROR(result);
    
    state->has_colors = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x00002000 (textures)
 */
static nmo_result_t deserialize_textures(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *state
) {
    nmo_result_t result;
    
    /* Read texture count */
    result = nmo_chunk_read_dword(chunk, &state->texture_count);
    NMO_RETURN_IF_ERROR(result);
    
    /* Clamp to maximum */
    if (state->texture_count > 4) {
        state->texture_count = 4;
    }
    
    /* Read texture IDs */
    for (uint32_t i = 0; i < state->texture_count; i++) {
        result = nmo_chunk_read_object_id(chunk, &state->texture_ids[i]);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Read texture blend mode */
    uint32_t blend_mode;
    result = nmo_chunk_read_dword(chunk, &blend_mode);
    NMO_RETURN_IF_ERROR(result);
    state->texture_blend_mode = (nmo_texture_blend_mode_t)blend_mode;
    
    /* Read texture filter modes */
    uint32_t min_mode, mag_mode;
    result = nmo_chunk_read_dword(chunk, &min_mode);
    NMO_RETURN_IF_ERROR(result);
    state->texture_min_mode = (nmo_texture_filter_mode_t)min_mode;
    
    result = nmo_chunk_read_dword(chunk, &mag_mode);
    NMO_RETURN_IF_ERROR(result);
    state->texture_mag_mode = (nmo_texture_filter_mode_t)mag_mode;
    
    /* Read texture address mode */
    uint32_t address_mode;
    result = nmo_chunk_read_dword(chunk, &address_mode);
    NMO_RETURN_IF_ERROR(result);
    state->texture_address_mode = (nmo_texture_address_mode_t)address_mode;
    
    /* Read border color */
    result = nmo_chunk_read_dword(chunk, &state->texture_border_color);
    NMO_RETURN_IF_ERROR(result);
    
    state->has_textures = true;
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize identifier 0x00004000 (rendering settings)
 */
static nmo_result_t deserialize_rendering(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *state
) {
    nmo_result_t result;
    uint32_t temp;
    
    /* Read shade mode */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->shade_mode = (nmo_shade_mode_t)temp;
    
    /* Read fill mode */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->fill_mode = (nmo_fill_mode_t)temp;
    
    /* Read alpha testing */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->alpha_test_enabled = (temp != 0);
    
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->alpha_func = (nmo_alpha_func_t)temp;
    
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->alpha_ref = (uint8_t)temp;
    
    /* Read blending */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->blend_enabled = (temp != 0);
    
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->src_blend = (nmo_blend_factor_t)temp;
    
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->dest_blend = (nmo_blend_factor_t)temp;
    
    /* Read Z-buffer control */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->zwrite_enabled = (temp != 0);
    
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->ztest_enabled = (temp != 0);
    
    /* Read two-sided flag */
    result = nmo_chunk_read_dword(chunk, &temp);
    NMO_RETURN_IF_ERROR(result);
    state->two_sided = (temp != 0);
    
    state->has_rendering_settings = true;
    
    return nmo_result_ok();
}

/**
 * @brief Main deserialization function (modern format v5+)
 *
 * Identifier Processing:
 * - 0x00001000: Material colors (ambient, diffuse, specular, emissive, power)
 * - 0x00002000: Textures (IDs, blend mode, filter, address mode)
 * - 0x00004000: Rendering settings (shade/fill modes, alpha, blend, Z-buffer)
 */
static nmo_result_t ckmaterial_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *out_state
) {
    nmo_result_t result;
    
    /* Initialize with defaults */
    initialize_material_defaults(out_state);
    
    /* Process identifier 0x00001000: Colors */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_COLORS);
    if (result.code == NMO_OK) {
        result = deserialize_colors(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x00002000: Textures */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_TEXTURES);
    if (result.code == NMO_OK) {
        result = deserialize_textures(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Process identifier 0x00004000: Rendering settings */
    result = nmo_chunk_seek_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_RENDERING);
    if (result.code == NMO_OK) {
        result = deserialize_rendering(chunk, arena, out_state);
        NMO_RETURN_IF_ERROR(result);
    }
    
    return nmo_result_ok();
}

/* ========================================================================
 * Serialization (Modern Format v5+)
 * ======================================================================== */

/**
 * @brief Main serialization function (modern format v5+)
 */
static nmo_result_t ckmaterial_serialize_modern(
    const nmo_ck_material_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
) {
    nmo_result_t result;
    
    /* Write identifier 0x00001000: Colors */
    if (state->has_colors) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_COLORS);
        NMO_RETURN_IF_ERROR(result);
        
        /* Write ambient */
        result = nmo_chunk_write_float(chunk, state->colors.ambient_r);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.ambient_g);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.ambient_b);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.ambient_a);
        NMO_RETURN_IF_ERROR(result);
        
        /* Write diffuse */
        result = nmo_chunk_write_float(chunk, state->colors.diffuse_r);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.diffuse_g);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.diffuse_b);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.diffuse_a);
        NMO_RETURN_IF_ERROR(result);
        
        /* Write specular */
        result = nmo_chunk_write_float(chunk, state->colors.specular_r);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.specular_g);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.specular_b);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.specular_a);
        NMO_RETURN_IF_ERROR(result);
        
        /* Write emissive */
        result = nmo_chunk_write_float(chunk, state->colors.emissive_r);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.emissive_g);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.emissive_b);
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_float(chunk, state->colors.emissive_a);
        NMO_RETURN_IF_ERROR(result);
        
        /* Write specular power */
        result = nmo_chunk_write_float(chunk, state->specular_power);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Write identifier 0x00002000: Textures */
    if (state->has_textures && state->texture_count > 0) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_TEXTURES);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->texture_count);
        NMO_RETURN_IF_ERROR(result);
        
        for (uint32_t i = 0; i < state->texture_count; i++) {
            result = nmo_chunk_write_object_id(chunk, state->texture_ids[i]);
            NMO_RETURN_IF_ERROR(result);
        }
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->texture_blend_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->texture_min_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->texture_mag_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->texture_address_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->texture_border_color);
        NMO_RETURN_IF_ERROR(result);
    }
    
    /* Write identifier 0x00004000: Rendering settings */
    if (state->has_rendering_settings) {
        result = nmo_chunk_write_identifier(chunk, NMO_CKMATERIAL_IDENTIFIER_RENDERING);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->shade_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->fill_mode);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->alpha_test_enabled ? 1 : 0);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->alpha_func);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->alpha_ref);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->blend_enabled ? 1 : 0);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->src_blend);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, (uint32_t)state->dest_blend);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->zwrite_enabled ? 1 : 0);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->ztest_enabled ? 1 : 0);
        NMO_RETURN_IF_ERROR(result);
        
        result = nmo_chunk_write_dword(chunk, state->two_sided ? 1 : 0);
        NMO_RETURN_IF_ERROR(result);
    }
    
    return nmo_result_ok();
}

/* ========================================================================
 * Finish Loading Handler
 * ======================================================================== */

/**
 * @brief Finish loading callback for CKMaterial objects
 *
 * Validates material properties and clamps to valid ranges.
 */
static nmo_result_t ckmaterial_finish_loading(
    nmo_ck_material_state_t *state,
    void *context,
    nmo_arena_t *arena
) {
    (void)context;  /* Unused */
    (void)arena;    /* Unused */
    
    /* Clamp colors to [0, 1] range */
    if (state->has_colors) {
        /* Clamp function */
        #define CLAMP_COLOR(c) ((c) < 0.0f ? 0.0f : ((c) > 1.0f ? 1.0f : (c)))
        
        state->colors.ambient_r = CLAMP_COLOR(state->colors.ambient_r);
        state->colors.ambient_g = CLAMP_COLOR(state->colors.ambient_g);
        state->colors.ambient_b = CLAMP_COLOR(state->colors.ambient_b);
        state->colors.ambient_a = CLAMP_COLOR(state->colors.ambient_a);
        
        state->colors.diffuse_r = CLAMP_COLOR(state->colors.diffuse_r);
        state->colors.diffuse_g = CLAMP_COLOR(state->colors.diffuse_g);
        state->colors.diffuse_b = CLAMP_COLOR(state->colors.diffuse_b);
        state->colors.diffuse_a = CLAMP_COLOR(state->colors.diffuse_a);
        
        state->colors.specular_r = CLAMP_COLOR(state->colors.specular_r);
        state->colors.specular_g = CLAMP_COLOR(state->colors.specular_g);
        state->colors.specular_b = CLAMP_COLOR(state->colors.specular_b);
        state->colors.specular_a = CLAMP_COLOR(state->colors.specular_a);
        
        state->colors.emissive_r = CLAMP_COLOR(state->colors.emissive_r);
        state->colors.emissive_g = CLAMP_COLOR(state->colors.emissive_g);
        state->colors.emissive_b = CLAMP_COLOR(state->colors.emissive_b);
        state->colors.emissive_a = CLAMP_COLOR(state->colors.emissive_a);
        
        #undef CLAMP_COLOR
        
        /* Clamp specular power to non-negative */
        if (state->specular_power < 0.0f) {
            state->specular_power = 0.0f;
        }
    }
    
    return nmo_result_ok();
}

/* ========================================================================
 * Schema Registration
 * ======================================================================== */

/**
 * @brief Register CKMaterial schemas with the schema system
 */
nmo_result_t nmo_register_ckmaterial_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
) {
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments"
        ));
    }
    
    /* Get base types */
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "float");
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    const nmo_schema_type_t *bool_type = nmo_schema_registry_find_by_name(registry, "bool");
    
    if (!float_type || !uint32_type || !bool_type) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Required types not found"
        ));
    }
    
    /* Register CKMaterial state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKMaterialState",
        sizeof(nmo_ck_material_state_t),
        alignof(nmo_ck_material_state_t)
    );
    
    /* Add color fields */
    nmo_builder_add_field_ex(&builder, "ambient_r", float_type,
                            offsetof(nmo_ck_material_state_t, colors) + offsetof(nmo_material_colors_t, ambient_r), 0);
    nmo_builder_add_field_ex(&builder, "diffuse_r", float_type,
                            offsetof(nmo_ck_material_state_t, colors) + offsetof(nmo_material_colors_t, diffuse_r), 0);
    nmo_builder_add_field_ex(&builder, "specular_r", float_type,
                            offsetof(nmo_ck_material_state_t, colors) + offsetof(nmo_material_colors_t, specular_r), 0);
    nmo_builder_add_field_ex(&builder, "emissive_r", float_type,
                            offsetof(nmo_ck_material_state_t, colors) + offsetof(nmo_material_colors_t, emissive_r), 0);
    
    /* Add specular power */
    nmo_builder_add_field_ex(&builder, "specular_power", float_type,
                            offsetof(nmo_ck_material_state_t, specular_power), 0);
    
    /* Add texture count */
    nmo_builder_add_field_ex(&builder, "texture_count", uint32_type,
                            offsetof(nmo_ck_material_state_t, texture_count), 0);
    
    /* Add flags */
    nmo_builder_add_field_ex(&builder, "blend_enabled", bool_type,
                            offsetof(nmo_ck_material_state_t, blend_enabled), 0);
    nmo_builder_add_field_ex(&builder, "alpha_test_enabled", bool_type,
                            offsetof(nmo_ck_material_state_t, alpha_test_enabled), 0);
    nmo_builder_add_field_ex(&builder, "zwrite_enabled", bool_type,
                            offsetof(nmo_ck_material_state_t, zwrite_enabled), 0);
    nmo_builder_add_field_ex(&builder, "two_sided", bool_type,
                            offsetof(nmo_ck_material_state_t, two_sided), 0);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}
