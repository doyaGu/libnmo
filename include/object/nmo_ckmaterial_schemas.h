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
#include "object/nmo_object_enum_defs.h"
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
 * Canonical type is VXTEXTURE_BLENDMODE (registered in object enum registry).
 */
typedef VXTEXTURE_BLENDMODE nmo_texture_blend_mode_t;
#define NMO_TEXBLEND_DECAL VXTEXTUREBLEND_DECAL
#define NMO_TEXBLEND_MODULATE VXTEXTUREBLEND_MODULATE
#define NMO_TEXBLEND_DECALALPHA VXTEXTUREBLEND_DECALALPHA
#define NMO_TEXBLEND_MODULATEALPHA VXTEXTUREBLEND_MODULATEALPHA
#define NMO_TEXBLEND_DECALMASK VXTEXTUREBLEND_DECALMASK
#define NMO_TEXBLEND_MODULATEMASK VXTEXTUREBLEND_MODULATEMASK
#define NMO_TEXBLEND_COPY VXTEXTUREBLEND_COPY
#define NMO_TEXBLEND_ADD VXTEXTUREBLEND_ADD
#define NMO_TEXBLEND_DOTPRODUCT3 VXTEXTUREBLEND_DOTPRODUCT3
#define NMO_TEXBLEND_MAX VXTEXTUREBLEND_MAX

/**
 * @brief Texture filter modes
 *
 * Canonical type is VXTEXTURE_FILTERMODE (registered in object enum registry).
 */
typedef VXTEXTURE_FILTERMODE nmo_texture_filter_mode_t;
#define NMO_TEXFILTER_NEAREST VXTEXTUREFILTER_NEAREST
#define NMO_TEXFILTER_LINEAR VXTEXTUREFILTER_LINEAR
#define NMO_TEXFILTER_MIPNEAREST VXTEXTUREFILTER_MIPNEAREST
#define NMO_TEXFILTER_MIPLINEAR VXTEXTUREFILTER_MIPLINEAR
#define NMO_TEXFILTER_LINEARMIPNEAREST VXTEXTUREFILTER_LINEARMIPNEAREST
#define NMO_TEXFILTER_LINEARMIPLINEAR VXTEXTUREFILTER_LINEARMIPLINEAR
#define NMO_TEXFILTER_ANISOTROPIC VXTEXTUREFILTER_ANISOTROPIC

/**
 * @brief Texture address modes
 *
 * Canonical type is VXTEXTURE_ADDRESSMODE (registered in object enum registry).
 */
typedef VXTEXTURE_ADDRESSMODE nmo_texture_address_mode_t;
#define NMO_TEXADDR_WRAP VXTEXTURE_ADDRESSWRAP
#define NMO_TEXADDR_MIRROR VXTEXTURE_ADDRESSMIRROR
#define NMO_TEXADDR_CLAMP VXTEXTURE_ADDRESSCLAMP
#define NMO_TEXADDR_BORDER VXTEXTURE_ADDRESSBORDER

/**
 * @brief Shade modes
 *
 * Canonical type is VXSHADE_MODE (registered in object enum registry).
 */
typedef VXSHADE_MODE nmo_shade_mode_t;
#define NMO_SHADE_FLAT VXSHADE_FLAT
#define NMO_SHADE_GOURAUD VXSHADE_GOURAUD
#define NMO_SHADE_PHONG VXSHADE_PHONG

/**
 * @brief Fill modes
 *
 * Canonical type is VXFILL_MODE (registered in object enum registry).
 */
typedef VXFILL_MODE nmo_fill_mode_t;
#define NMO_FILL_POINT VXFILL_POINT
#define NMO_FILL_WIREFRAME VXFILL_WIREFRAME
#define NMO_FILL_SOLID VXFILL_SOLID

/**
 * @brief Alpha test comparison functions
 *
 * Canonical type is VXCMPFUNC (registered in object enum registry).
 */
typedef VXCMPFUNC nmo_alpha_func_t;
#define NMO_ALPHA_NEVER VXCMP_NEVER
#define NMO_ALPHA_LESS VXCMP_LESS
#define NMO_ALPHA_EQUAL VXCMP_EQUAL
#define NMO_ALPHA_LESSEQUAL VXCMP_LESSEQUAL
#define NMO_ALPHA_GREATER VXCMP_GREATER
#define NMO_ALPHA_NOTEQUAL VXCMP_NOTEQUAL
#define NMO_ALPHA_GREATEREQUAL VXCMP_GREATEREQUAL
#define NMO_ALPHA_ALWAYS VXCMP_ALWAYS

/**
 * @brief Source/destination blend factors
 *
 * Canonical type is VXBLEND_MODE (registered in object enum registry).
 */
typedef VXBLEND_MODE nmo_blend_factor_t;
#define NMO_BLEND_ZERO VXBLEND_ZERO
#define NMO_BLEND_ONE VXBLEND_ONE
#define NMO_BLEND_SRCCOLOR VXBLEND_SRCCOLOR
#define NMO_BLEND_INVSRCCOLOR VXBLEND_INVSRCCOLOR
#define NMO_BLEND_SRCALPHA VXBLEND_SRCALPHA
#define NMO_BLEND_INVSRCALPHA VXBLEND_INVSRCALPHA
#define NMO_BLEND_DESTALPHA VXBLEND_DESTALPHA
#define NMO_BLEND_INVDESTALPHA VXBLEND_INVDESTALPHA
#define NMO_BLEND_DESTCOLOR VXBLEND_DESTCOLOR
#define NMO_BLEND_INVDESTCOLOR VXBLEND_INVDESTCOLOR
#define NMO_BLEND_SRCALPHASAT VXBLEND_SRCALPHASAT

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
