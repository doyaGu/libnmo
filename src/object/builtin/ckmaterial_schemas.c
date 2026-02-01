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

#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
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
#if 0

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
#endif /* disabled legacy CKMaterial schema */

/* ========================================================================
 * CKRenderEngine-aligned implementation
 * ======================================================================== */

#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_class_ids.h"

#define CK_STATESAVE_MATDATA 0x00001000u
#define CK_STATESAVE_MATDATA2 0x00002000u
#define CK_STATESAVE_MATDATA3 0x00004000u
#define CK_STATESAVE_MATDATA5 0x00010000u

static uint32_t nmo_pack_color(float r, float g, float b, float a) {
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    if (a < 0.0f) a = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;
    if (a > 1.0f) a = 1.0f;

    const uint32_t rr = (uint32_t)(r * 255.0f + 0.5f);
    const uint32_t gg = (uint32_t)(g * 255.0f + 0.5f);
    const uint32_t bb = (uint32_t)(b * 255.0f + 0.5f);
    const uint32_t aa = (uint32_t)(a * 255.0f + 0.5f);

    return (aa << 24) | (rr << 16) | (gg << 8) | bb;
}

static nmo_result_t nmo_ckmaterial_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmaterial_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckbeobject_deserialize_fn base_deserialize = nmo_get_ckbeobject_deserialize();
    if (base_deserialize) {
        nmo_result_t result = base_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA).code == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);

        if (data_version < 5) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->diffuse_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->ambient_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->specular_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->emissive_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &out_state->specular_power);

            nmo_chunk_read_object_id(chunk, &out_state->texture_ids[0]);

            uint32_t low_flags = 0;
            uint32_t tex_blend = 0;
            uint32_t tex_min = 0;
            uint32_t tex_mag = 0;
            uint32_t src_blend = 0;
            uint32_t dst_blend = 0;
            uint32_t shade_mode = 0;
            uint32_t fill_mode = 0;
            uint32_t tex_address = 0;
            uint32_t zfunc = 0;

            nmo_chunk_read_dword(chunk, &low_flags);
            nmo_chunk_read_dword(chunk, &tex_blend);
            nmo_chunk_read_dword(chunk, &tex_min);
            nmo_chunk_read_dword(chunk, &tex_mag);
            nmo_chunk_read_dword(chunk, &src_blend);
            nmo_chunk_read_dword(chunk, &dst_blend);
            nmo_chunk_read_dword(chunk, &shade_mode);
            nmo_chunk_read_dword(chunk, &fill_mode);
            nmo_chunk_read_dword(chunk, &tex_address);
            nmo_chunk_read_dword(chunk, &out_state->texture_border_color);
            nmo_chunk_read_dword(chunk, &zfunc);

            out_state->packed_modes =
                (tex_blend & 0xF) |
                ((tex_min & 0xF) << 4) |
                ((tex_mag & 0xF) << 8) |
                ((src_blend & 0xF) << 12) |
                ((dst_blend & 0xF) << 16) |
                ((shade_mode & 0xF) << 20) |
                ((fill_mode & 0xF) << 24) |
                ((tex_address & 0xF) << 28);

            out_state->packed_flags =
                (low_flags & 0xFF) |
                ((zfunc & 0x1F) << 8) |
                ((8u & 0x1F) << 16) |
                ((uint32_t)0 << 24);
        } else {
            nmo_chunk_read_dword(chunk, &out_state->diffuse_color);
            nmo_chunk_read_dword(chunk, &out_state->ambient_color);
            nmo_chunk_read_dword(chunk, &out_state->specular_color);
            nmo_chunk_read_dword(chunk, &out_state->emissive_color);
            nmo_chunk_read_float(chunk, &out_state->specular_power);
            nmo_chunk_read_object_id(chunk, &out_state->texture_ids[0]);
            nmo_chunk_read_dword(chunk, &out_state->texture_border_color);
            nmo_chunk_read_dword(chunk, &out_state->packed_modes);
            nmo_chunk_read_dword(chunk, &out_state->packed_flags);
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA2).code == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[1]);
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[2]);
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[3]);
        out_state->has_additional_textures = 1;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA3).code == NMO_OK) {
        nmo_chunk_read_dword(chunk, &out_state->effect);
        out_state->has_effect = 1;
        out_state->has_effect_param = 0;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA5).code == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->effect_parameter_id);
        nmo_chunk_read_dword(chunk, &out_state->effect);
        out_state->has_effect = 1;
        out_state->has_effect_param = 1;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckmaterial_serialize_internal(
    const nmo_ck_material_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    if (!state || !chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmaterial_serialize"));
    }

    nmo_ckbeobject_serialize_fn base_serialize = nmo_get_ckbeobject_serialize();
    if (base_serialize) {
        nmo_result_t result = base_serialize(&state->base, chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA);
    if (result.code != NMO_OK) return result;

    nmo_chunk_write_dword(chunk, state->diffuse_color);
    nmo_chunk_write_dword(chunk, state->ambient_color);
    nmo_chunk_write_dword(chunk, state->specular_color);
    nmo_chunk_write_dword(chunk, state->emissive_color);
    nmo_chunk_write_float(chunk, state->specular_power);
    nmo_chunk_write_object_id(chunk, state->texture_ids[0]);
    nmo_chunk_write_dword(chunk, state->texture_border_color);
    nmo_chunk_write_dword(chunk, state->packed_modes);
    nmo_chunk_write_dword(chunk, state->packed_flags);

    if (state->has_effect) {
        if (state->has_effect_param) {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA5);
            if (result.code != NMO_OK) return result;
            nmo_chunk_write_object_id(chunk, state->effect_parameter_id);
        } else {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA3);
            if (result.code != NMO_OK) return result;
        }
        nmo_chunk_write_dword(chunk, state->effect);
    }

    if (state->has_effect && state->has_additional_textures) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA2);
        if (result.code != NMO_OK) return result;
        nmo_chunk_write_object_id(chunk, state->texture_ids[1]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[2]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[3]);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckmaterial_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckmaterial_deserialize_internal(chunk, arena, (nmo_ck_material_state_t *)out_ptr);
}

static nmo_result_t nmo_ckmaterial_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckmaterial_serialize_internal((const nmo_ck_material_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckmaterial_vtable = {
    .read = nmo_ckmaterial_vtable_read,
    .write = nmo_ckmaterial_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckmaterial_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments"
        ));
    }

    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "float");
    if (!u32_type || !float_type) {
        return nmo_result_error(NMO_ERROR(
            arena, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Required types not found"
        ));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKMaterialState",
        sizeof(nmo_ck_material_state_t),
        alignof(nmo_ck_material_state_t)
    );

    nmo_builder_add_field_ex(&builder, "diffuse_color", u32_type,
                            offsetof(nmo_ck_material_state_t, diffuse_color), 0);
    nmo_builder_add_field_ex(&builder, "ambient_color", u32_type,
                            offsetof(nmo_ck_material_state_t, ambient_color), 0);
    nmo_builder_add_field_ex(&builder, "specular_color", u32_type,
                            offsetof(nmo_ck_material_state_t, specular_color), 0);
    nmo_builder_add_field_ex(&builder, "emissive_color", u32_type,
                            offsetof(nmo_ck_material_state_t, emissive_color), 0);
    nmo_builder_add_field_ex(&builder, "specular_power", float_type,
                            offsetof(nmo_ck_material_state_t, specular_power), 0);
    nmo_builder_add_field_ex(&builder, "texture0_id", u32_type,
                            offsetof(nmo_ck_material_state_t, texture_ids[0]), 0);
    nmo_builder_add_field_ex(&builder, "packed_modes", u32_type,
                            offsetof(nmo_ck_material_state_t, packed_modes), 0);
    nmo_builder_add_field_ex(&builder, "packed_flags", u32_type,
                            offsetof(nmo_ck_material_state_t, packed_flags), 0);

    nmo_builder_set_vtable(&builder, &nmo_ckmaterial_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKMaterialState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_MATERIAL, type);
    }

    return result.code == NMO_OK ? nmo_result_ok() : result;
}
