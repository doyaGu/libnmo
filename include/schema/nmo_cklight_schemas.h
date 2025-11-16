/**
 * @file nmo_cklight_schemas.h
 * @brief CKLight schema definitions header
 * 
 * Based on reverse engineering analysis from CK2_3D.dll:
 * - RCKLight::Load at 0x1001B50E (678 bytes)
 * - RCKLight::Save at 0x1001B389 (389 bytes)
 * - CKLightData structure (104 bytes)
 * 
 * See docs/CK2_3D_reverse_notes_extended.md for detailed analysis.
 */

#ifndef NMO_CKLIGHT_SCHEMAS_H
#define NMO_CKLIGHT_SCHEMAS_H

#include "schema/nmo_ck3dentity_schemas.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief Light types (VXLIGHT_TYPE from Virtools)
 */
typedef enum nmo_vx_light_type {
    NMO_LIGHT_POINT = 1,        ///< Point light (omnidirectional)
    NMO_LIGHT_SPOT = 2,         ///< Spotlight (cone with inner/outer angles)
    NMO_LIGHT_DIRECTIONAL = 3   ///< Directional light (parallel rays)
} nmo_vx_light_type_t;

/**
 * @brief VxColor structure (RGBA float)
 * 
 * Each component is a float in range [0.0, 1.0]
 */
typedef struct nmo_vx_color {
    float r;  ///< Red component
    float g;  ///< Green component
    float b;  ///< Blue component
    float a;  ///< Alpha component
} nmo_vx_color_t;

/**
 * @brief CKLightData structure (104 bytes at 0x1A8 in RCKLight)
 * 
 * Stores all lighting parameters. Serialized with identifier 0x400000.
 */
typedef struct nmo_ck_light_data {
    // Light type (4 bytes at 0x00)
    nmo_vx_light_type_t type;
    
    // Colors (48 bytes: 3 × VxColor at 0x04-0x33)
    nmo_vx_color_t diffuse;     ///< Diffuse color (main light color)
    nmo_vx_color_t specular;    ///< Specular highlight color
    nmo_vx_color_t ambient;     ///< Ambient contribution
    
    // Position and direction (24 bytes: 2 × VxVector at 0x34-0x4B)
    float position[3];          ///< Light position (x, y, z)
    float direction[3];         ///< Light direction (nx, ny, nz)
    
    // Attenuation parameters (20 bytes at 0x4C-0x5F)
    float range;                ///< Maximum light distance
    float falloff;              ///< Falloff exponent
    float attenuation0;         ///< Constant attenuation
    float attenuation1;         ///< Linear attenuation
    float attenuation2;         ///< Quadratic attenuation
    
    // Spotlight parameters (8 bytes at 0x60-0x67, only for VX_LIGHTSPOT)
    float inner_spot_cone;      ///< Inner cone angle (radians)
    float outer_spot_cone;      ///< Outer cone angle (radians)
} nmo_ck_light_data_t;

/**
 * @brief RCKLight state structure
 * 
 * Represents the deserialized state of an RCKLight object.
 * Size: RCK3dEntity (424B) + CKLightData (104B) + flags (4B) + power (4B) = 536 bytes
 */
typedef struct nmo_cklight_state {
    nmo_ck3dentity_state_t entity;  ///< Parent CK3dEntity state
    
    // Light data (104 bytes)
    nmo_ck_light_data_t light_data;
    
    // Flags (4 bytes at 0x210)
    uint32_t flags;                  ///< Light flags (active, specular, etc.)
    
    // Power multiplier (4 bytes at 0x214)
    float light_power;               ///< Intensity multiplier (default 1.0)
} nmo_cklight_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_cklight_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state);

typedef nmo_result_t (*nmo_cklight_serialize_fn)(
    const nmo_cklight_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_cklight_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Register CKLight state schema
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_cklight_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get CKLight deserialize function
 * @return Function pointer for deserialization
 */
NMO_API nmo_cklight_deserialize_fn nmo_get_cklight_deserialize(void);

/**
 * @brief Get CKLight serialize function
 * @return Function pointer for serialization
 */
NMO_API nmo_cklight_serialize_fn nmo_get_cklight_serialize(void);

/**
 * @brief Get CKLight finish_loading function
 * @return Function pointer for finish loading
 */
NMO_API nmo_cklight_finish_loading_fn nmo_get_cklight_finish_loading(void);

/**
 * @brief Helper: Convert ARGB DWORD to VxColor
 * 
 * @param argb Packed ARGB color (0xAARRGGBB)
 * @param out_color Output VxColor structure
 */
NMO_API void nmo_vx_color_from_argb(uint32_t argb, nmo_vx_color_t *out_color);

/**
 * @brief Helper: Convert VxColor to ARGB DWORD
 * 
 * @param color Input VxColor structure
 * @return Packed ARGB color (0xAARRGGBB)
 */
NMO_API uint32_t nmo_vx_color_to_argb(const nmo_vx_color_t *color);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLIGHT_SCHEMAS_H */
