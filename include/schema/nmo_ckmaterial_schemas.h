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
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "schema/nmo_schema_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    float ambient_r;       /**< Ambient red component (0.0-1.0) */
    float ambient_g;       /**< Ambient green component (0.0-1.0) */
    float ambient_b;       /**< Ambient blue component (0.0-1.0) */
    float ambient_a;       /**< Ambient alpha component (0.0-1.0) */
    
    float diffuse_r;       /**< Diffuse red component (0.0-1.0) */
    float diffuse_g;       /**< Diffuse green component (0.0-1.0) */
    float diffuse_b;       /**< Diffuse blue component (0.0-1.0) */
    float diffuse_a;       /**< Diffuse alpha component (0.0-1.0) */
    
    float specular_r;      /**< Specular red component (0.0-1.0) */
    float specular_g;      /**< Specular green component (0.0-1.0) */
    float specular_b;      /**< Specular blue component (0.0-1.0) */
    float specular_a;      /**< Specular alpha component (0.0-1.0) */
    
    float emissive_r;      /**< Emissive red component (0.0-1.0) */
    float emissive_g;      /**< Emissive green component (0.0-1.0) */
    float emissive_b;      /**< Emissive blue component (0.0-1.0) */
    float emissive_a;      /**< Emissive alpha component (0.0-1.0) */
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
    /* Colors (64 bytes) */
    nmo_material_colors_t colors;
    
    /* Specular power (4 bytes) */
    float specular_power;                /**< Specular highlight power (0 = disabled, default 0) */
    
    /* Texture references (up to 4 textures, 16 bytes) */
    nmo_object_id_t texture_ids[4];      /**< Texture object IDs (0 = none) */
    uint32_t texture_count;              /**< Number of active textures */
    
    /* Texture settings (40 bytes) */
    nmo_texture_blend_mode_t texture_blend_mode;      /**< Texture blend mode */
    nmo_texture_filter_mode_t texture_min_mode;       /**< Minification filter */
    nmo_texture_filter_mode_t texture_mag_mode;       /**< Magnification filter */
    nmo_texture_address_mode_t texture_address_mode;  /**< Address mode */
    uint32_t texture_border_color;                    /**< Border color (ARGB) */
    
    /* Rendering modes (16 bytes) */
    nmo_shade_mode_t shade_mode;         /**< Shading mode */
    nmo_fill_mode_t fill_mode;           /**< Fill mode */
    
    /* Alpha testing (8 bytes) */
    bool alpha_test_enabled;             /**< Enable alpha testing */
    nmo_alpha_func_t alpha_func;         /**< Alpha comparison function */
    uint8_t alpha_ref;                   /**< Alpha reference value (0-255) */
    
    /* Blending (12 bytes) */
    bool blend_enabled;                  /**< Enable blending */
    nmo_blend_factor_t src_blend;        /**< Source blend factor */
    nmo_blend_factor_t dest_blend;       /**< Destination blend factor */
    
    /* Z-buffer control (4 bytes) */
    bool zwrite_enabled;                 /**< Enable Z-buffer writes (default true) */
    bool ztest_enabled;                  /**< Enable Z-buffer testing (default true) */
    
    /* Two-sided rendering (4 bytes) */
    bool two_sided;                      /**< Render both sides (default false) */
    
    /* Presence flags (4 bytes) */
    bool has_colors;                     /**< Colors data present */
    bool has_textures;                   /**< Texture data present */
    bool has_rendering_settings;         /**< Rendering settings present */
} nmo_ck_material_state_t;

/* ========================================================================
 * Serialization Identifiers
 * ======================================================================== */

/* Material identifiers (values TBD based on reverse engineering) */
#define NMO_CKMATERIAL_IDENTIFIER_COLORS        0x00001000
#define NMO_CKMATERIAL_IDENTIFIER_TEXTURES      0x00002000
#define NMO_CKMATERIAL_IDENTIFIER_RENDERING     0x00004000
#define NMO_CKMATERIAL_IDENTIFIER_EXTENDED      0x00008000

/* ========================================================================
 * Function Typedefs
 * ======================================================================== */

/**
 * @brief Deserialize CKMaterial from chunk (modern format)
 */
typedef nmo_result_t (*nmo_ckmaterial_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_material_state_t *out_state
);

/**
 * @brief Serialize CKMaterial to chunk (modern format)
 */
typedef nmo_result_t (*nmo_ckmaterial_serialize_fn)(
    const nmo_ck_material_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
);

/**
 * @brief Finish loading callback for CKMaterial
 */
typedef nmo_result_t (*nmo_ckmaterial_finish_loading_fn)(
    nmo_ck_material_state_t *state,
    void *context,
    nmo_arena_t *arena
);

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Register CKMaterial schemas with the schema system
 *
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result_t nmo_register_ckmaterial_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMATERIAL_SCHEMAS_H */
