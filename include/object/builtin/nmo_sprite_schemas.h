/**
 * @file nmo_sprite_schemas.h
 * @brief Public API for CKSprite schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKSprite.
 * CKSprite extends CK2dEntity with bitmap data for textured 2D elements.
 * 
 * Based on reverse-engineered RCKSprite Load/Save (docs/CK2_3D_reverse_notes.md):
 * - Inherits CK2dEntity serialization (calls parent Load/Save)
 * - Identifiers:
 *   - 0x80000: sprite reference (clone data from another sprite)
 *   - 0x20000: transparent color + boolean flag
 *   - 0x10000: current slot index
 *   - 0x20000000: save options + CKBitmapProperties (v7+)
 *   - Bitmap payload identifiers: 0x200000, 0x10000000, 0x800000, 0x400000, 0x40000
 */

#ifndef NMO_CKSPRITE_SCHEMAS_H
#define NMO_CKSPRITE_SCHEMAS_H

#include "nmo_types.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_statesave_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKSprite STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKSprite state
 * 
 * Represents a 2D sprite with bitmap texture.
 * 
 * Reference: RCKSprite structure in docs/CK2_3D_reverse_notes.md
 * - Inherits CK2dEntity (position, size, hierarchy)
 * - m_BitmapData: pixel data, palette, video format
 * - Transparency: color key for alpha blending
 * - Slot: animation frame index (for sprite sheets)
 * - Reference: can clone data from another sprite
 */
typedef struct nmo_sprite_state {
    nmo_2dentity_state_t entity;      /**< Parent CK2dEntity state */
    
    /* Sprite reference (optional) */
    bool has_sprite_ref;                /**< True if cloning from another sprite */
    nmo_object_id_t sprite_ref_id;      /**< Sprite to clone from (identifier 0x80000) */
    
    /* Bitmap data (optional, not present if sprite_ref is used) */
    bool has_bitmap_data;               /**< True if bitmap payload is present */
    nmo_bitmapdata_t bitmap_data;     /**< Bitmap pixel data */
    
    /* Transparency (identifier 0x20000) */
    bool has_transparency;              /**< True if transparency is set */
    bool is_transparent;                /**< Transparency enabled flag */
    uint32_t transparent_color;         /**< Color key (ARGB) */
    
    /* Current slot (identifier 0x10000) */
    bool has_slot;                      /**< True if slot is specified */
    uint32_t current_slot;              /**< Animation frame index */
    
    /* Save options (identifier 0x20000000) */
    bool has_save_options;              /**< True if save options are present */
    uint32_t save_options;              /**< Bitmap save flags */
    uint8_t *bitmap_properties;         /**< CKBitmapProperties blob (v7+) */
    size_t bitmap_properties_size;      /**< Size of properties blob */
    
} nmo_sprite_state_t;

/* =============================================================================
 * CHUNK IDENTIFIERS
 * ============================================================================= */

/** Bitmap payload identifiers (passed to CKBitmapData::ReadFromChunk) */
#define NMO_CKSPRITE_BITMAP_PALETTE      0x200000
#define NMO_CKSPRITE_BITMAP_SYSTEM_COPY  0x10000000
#define NMO_CKSPRITE_BITMAP_VIDEO_BACKUP 0x800000
#define NMO_CKSPRITE_BITMAP_PIXELS       0x400000
#define NMO_CKSPRITE_BITMAP_RAW          0x40000

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKSprite deserialize function pointer type
 * 
 * @param chunk Chunk containing CKSprite data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_status_t (*nmo_sprite_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_sprite_state_t *out_state);

/**
 * @brief CKSprite serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_status_t (*nmo_sprite_serialize_fn)(
    const nmo_sprite_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKSprite schema types
 * 
 * Registers schema types for CKSprite state structures.
 * Must be called during initialization before using CKSprite schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
/**
 * @brief Deserialize CKSprite from chunk (public API)
 * 
 * @param chunk Chunk containing CKSprite data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_sprite_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Serialize CKSprite to chunk (public API)
 * 
 * @param chunk Chunk to write to
 * @param state State to serialize
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
NMO_API nmo_status_t nmo_sprite_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sprite_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sprite_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_sprite_vtable, nmo_register_sprite_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITE_SCHEMAS_H */
