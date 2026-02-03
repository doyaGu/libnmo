/**
 * @file nmo_ckmaterial_schemas.h
 * @brief CKMaterial schema definitions
 * @author libnmo
 * @date 2025
 *
 * Schema for CKMaterial (ClassID 30) - Color and texture settings for objects.
 * Defines material properties including colors (ambient, diffuse, specular, emissive),
 * texture references, blend modes, filter settings, and rendering options.
 *
 * Reference: reference/include/CKMaterial.h
 */

#ifndef NMO_CKMATERIAL_SCHEMAS_H
#define NMO_CKMATERIAL_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_color.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* ========================================================================
 * Material State Structure
 * ======================================================================== */

/**
 * @brief Material color properties
 *
 * Four color components for material lighting:
 * - Ambient: Base color in ambient lighting (default 0.3, 0.3, 0.3, 1.0)
 * - Diffuse: Color for diffuse lighting (default 0.7, 0.7, 0.7, 1.0)
 * - Specular: Highlight color (default 0.5, 0.5, 0.5, 1.0)
 * - Emissive: Self-illumination color (default 0, 0, 0, 1.0)
 */
typedef struct nmo_material_colors {
    nmo_color_t ambient;   /**< Ambient color (default 0.3, 0.3, 0.3, 1.0) */
    nmo_color_t diffuse;   /**< Diffuse color (default 0.7, 0.7, 0.7, 1.0) */
    nmo_color_t specular;  /**< Specular color (default 0.5, 0.5, 0.5, 1.0) */
    nmo_color_t emissive;  /**< Emissive color (default 0, 0, 0, 1.0) */
} nmo_material_colors_t;

/**
 * @brief Texture blend modes
 *
 * Controls how texture and face color are mixed.
 */
typedef enum nmo_texture_blend_mode {
    NMO_TEXBLEND_DECAL = 1,              /**< Texture replaces color */
    NMO_TEXBLEND_MODULATE = 2,           /**< Texture * color */
    NMO_TEXBLEND_DECALALPHA = 3,         /**< Texture alpha controls blend */
    NMO_TEXBLEND_MODULATEALPHA = 4,      /**< Texture * color with alpha */
    NMO_TEXBLEND_DECALMASK = 5,          /**< Texture with mask */
    NMO_TEXBLEND_MODULATEMASK = 6,       /**< Modulate with mask */
    NMO_TEXBLEND_COPY = 7,               /**< Direct copy */
    NMO_TEXBLEND_ADD = 8,                /**< Additive blending */
    NMO_TEXBLEND_DOTPRODUCT3 = 9,        /**< Dot product (bump mapping) */
    NMO_TEXBLEND_MAX = 10                /**< Maximum value */
} nmo_texture_blend_mode_t;

/**
 * @brief Texture filter modes
 *
 * Controls texture filtering when magnified or minified.
 */
typedef enum nmo_texture_filter_mode {
    NMO_TEXFILTER_NEAREST = 1,           /**< Nearest neighbor (point sampling) */
    NMO_TEXFILTER_LINEAR = 2,            /**< Bilinear filtering */
    NMO_TEXFILTER_MIPNEAREST = 3,        /**< Mipmap nearest */
    NMO_TEXFILTER_MIPLINEAR = 4,         /**< Mipmap linear (trilinear) */
    NMO_TEXFILTER_LINEARMIPNEAREST = 5,  /**< Linear with mipmap nearest */
    NMO_TEXFILTER_LINEARMIPLINEAR = 6,   /**< Trilinear filtering */
    NMO_TEXFILTER_ANISOTROPIC = 7        /**< Anisotropic filtering */
} nmo_texture_filter_mode_t;

/**
 * @brief Texture address modes
 *
 * Controls how texture coordinates outside 0..1 are interpreted.
 */
typedef enum nmo_texture_address_mode {
    NMO_TEXADDR_WRAP = 1,                /**< Repeat texture */
    NMO_TEXADDR_MIRROR = 2,              /**< Mirror texture */
    NMO_TEXADDR_CLAMP = 3,               /**< Clamp to edge */
    NMO_TEXADDR_BORDER = 4               /**< Use border color */
} nmo_texture_address_mode_t;

/**
 * @brief Shade modes
 */
typedef enum nmo_shade_mode {
    NMO_SHADE_FLAT = 1,                  /**< Flat shading */
    NMO_SHADE_GOURAUD = 2,               /**< Gouraud shading (default) */
    NMO_SHADE_PHONG = 3                  /**< Phong shading */
} nmo_shade_mode_t;

/**
 * @brief Fill modes
 */
typedef enum nmo_fill_mode {
    NMO_FILL_POINT = 1,                  /**< Render as points */
    NMO_FILL_WIREFRAME = 2,              /**< Render as wireframe */
    NMO_FILL_SOLID = 3                   /**< Render solid (default) */
} nmo_fill_mode_t;

/**
 * @brief Alpha test comparison functions
 */
typedef enum nmo_alpha_func {
    NMO_ALPHA_NEVER = 1,                 /**< Never pass */
    NMO_ALPHA_LESS = 2,                  /**< Pass if less */
    NMO_ALPHA_EQUAL = 3,                 /**< Pass if equal */
    NMO_ALPHA_LESSEQUAL = 4,             /**< Pass if less or equal */
    NMO_ALPHA_GREATER = 5,               /**< Pass if greater */
    NMO_ALPHA_NOTEQUAL = 6,              /**< Pass if not equal */
    NMO_ALPHA_GREATEREQUAL = 7,          /**< Pass if greater or equal */
    NMO_ALPHA_ALWAYS = 8                 /**< Always pass */
} nmo_alpha_func_t;

/**
 * @brief Source/destination blend factors
 */
typedef enum nmo_blend_factor {
    NMO_BLEND_ZERO = 1,                  /**< (0, 0, 0, 0) */
    NMO_BLEND_ONE = 2,                   /**< (1, 1, 1, 1) */
    NMO_BLEND_SRCCOLOR = 3,              /**< Source color */
    NMO_BLEND_INVSRCCOLOR = 4,           /**< 1 - source color */
    NMO_BLEND_SRCALPHA = 5,              /**< Source alpha */
    NMO_BLEND_INVSRCALPHA = 6,           /**< 1 - source alpha */
    NMO_BLEND_DESTALPHA = 7,             /**< Destination alpha */
    NMO_BLEND_INVDESTALPHA = 8,          /**< 1 - destination alpha */
    NMO_BLEND_DESTCOLOR = 9,             /**< Destination color */
    NMO_BLEND_INVDESTCOLOR = 10,         /**< 1 - destination color */
    NMO_BLEND_SRCALPHASAT = 11           /**< Source alpha saturate */
} nmo_blend_factor_t;

/**
 * @brief CKMaterial state
 *
 * Complete material state for rendering.
 * Size: ~200 bytes
 */
typedef struct nmo_ck_material_state {
    nmo_ckbeobject_state_t base;

    /* Packed colors (ARGB) */
    uint32_t diffuse_color;
    uint32_t ambient_color;
    uint32_t specular_color;
    uint32_t emissive_color;

    /* Specular power */
    float specular_power;

    /* Texture references */
    nmo_object_id_t texture_ids[4];

    /* Packed render settings */
    uint32_t texture_border_color;
    uint32_t packed_modes;
    uint32_t packed_flags;

    /* Effect data */
    uint32_t effect;
    nmo_object_id_t effect_parameter_id;
    uint8_t has_effect;
    uint8_t has_effect_param;
    uint8_t has_additional_textures;
} nmo_ck_material_state_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

NMO_API nmo_status_t nmo_ckmaterial_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckmaterial_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckmaterial_vtable, nmo_register_ckmaterial_type)

NMO_API nmo_status_t nmo_ckmaterial_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMATERIAL_SCHEMAS_H */
