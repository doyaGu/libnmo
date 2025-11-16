/**
 * @file nmo_ck2dentity_schemas.h
 * @brief Public API for CK2dEntity schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CK2dEntity.
 * CK2dEntity is the base class for all 2D UI elements (sprites, text, etc.).
 * 
 * Based on reverse-engineered RCK2dEntity Load/Save (docs/CK2_3D_reverse_notes.md):
 * - Modern format (v5+): single identifier 0x10F000 with flags + optional blocks
 * - Legacy format (<v5): separate identifiers 0x4000 (flags), 0x8000 (origin), 
 *   0x2000 (size), 0x1000 (source rect), 0x100000 (z-order)
 * - Optional blocks: 0x10000 (source rect), 0x20000 (z-order), 0x40000 (parent),
 *   0x200000 (material, sprites only)
 */

#ifndef NMO_CK2DENTITY_SCHEMAS_H
#define NMO_CK2DENTITY_SCHEMAS_H

#include "nmo_types.h"
#include "schema/nmo_ckrenderobject_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CK2dEntity STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief VxRect - 2D rectangle (x, y, width, height)
 * 
 * Matches the Virtools SDK VxRect structure.
 */
typedef struct nmo_vx_rect {
    float x;
    float y;
    float width;
    float height;
} nmo_vx_rect_t;

/**
 * @brief CK2dEntity state
 * 
 * Represents a 2D UI entity with position, size, and hierarchy.
 * 
 * Reference: RCK2dEntity structure in docs/CK2_3D_reverse_notes.md
 * - m_Rect: screen-space rectangle (absolute coordinates)
 * - m_HomogeneousRect: normalized coordinates [0..1] when flag 0x200 is set
 * - m_SourceRect: texture/sprite source rectangle
 * - m_Flags: visibility, clipping, pickable, etc. (masked with 0xFFF8F7FF on load)
 * - m_ZOrder: rendering depth (higher values render on top)
 * - m_Parent: parent entity for hierarchy (CK_ID reference)
 * - m_Material: material reference (sprites only, identifier 0x200000)
 */
typedef struct nmo_ck2dentity_state {
    nmo_ckrenderobject_state_t render_object; /**< Parent CKRenderObject state */
    
    /* Core rectangle fields */
    nmo_vx_rect_t rect;                 /**< Screen-space rectangle */
    bool has_homogeneous_rect;          /**< True if homogeneous coords are used */
    nmo_vx_rect_t homogeneous_rect;     /**< Normalized [0..1] coordinates */
    
    /* Optional fields (presence indicated by flags) */
    bool has_source_rect;               /**< True if source rect is present */
    nmo_vx_rect_t source_rect;          /**< Texture/sprite source rectangle */
    
    bool has_z_order;                   /**< True if z-order is present */
    uint32_t z_order;                   /**< Rendering depth */
    
    bool has_parent;                    /**< True if parent reference is present */
    nmo_object_id_t parent_id;          /**< Parent entity ID */
    
    bool has_material;                  /**< True if material is present (sprites only) */
    nmo_object_id_t material_id;        /**< Material reference */
    
    /* Flags (sanitized with 0xFFF8F7FF on load) */
    uint32_t flags;                     /**< Entity flags (visibility, clipping, etc.) */
    
    /* Preserve any unknown chunk data for round-trip safety */
    uint8_t *raw_tail;                  /**< Unrecognized trailing data */
    size_t raw_tail_size;               /**< Size of trailing data in bytes */
} nmo_ck2dentity_state_t;

/* =============================================================================
 * CHUNK IDENTIFIERS
 * ============================================================================= */

/** Modern format (v5+) identifier - contains all fields */
#define NMO_CK2DENTITY_CHUNK_MODERN      0x10F000

/** Legacy format (<v5) identifiers */
#define NMO_CK2DENTITY_CHUNK_FLAGS       0x4000
#define NMO_CK2DENTITY_CHUNK_ORIGIN      0x8000
#define NMO_CK2DENTITY_CHUNK_SIZE        0x2000
#define NMO_CK2DENTITY_CHUNK_SOURCE_RECT 0x1000
#define NMO_CK2DENTITY_CHUNK_Z_ORDER     0x100000

/** Optional block identifiers (modern format) */
#define NMO_CK2DENTITY_FLAG_SOURCE_RECT  0x10000
#define NMO_CK2DENTITY_FLAG_Z_ORDER      0x20000
#define NMO_CK2DENTITY_FLAG_PARENT       0x40000
#define NMO_CK2DENTITY_FLAG_MATERIAL     0x200000

/** Homogeneous rect flag (bit in flags field) */
#define NMO_CK2DENTITY_FLAG_HOMOGENEOUS  0x200

/** Flag sanitization mask (applied on load) */
#define NMO_CK2DENTITY_FLAGS_MASK        0xFFF8F7FF

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CK2dEntity deserialize function pointer type
 * 
 * @param chunk Chunk containing CK2dEntity data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ck2dentity_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state);

/**
 * @brief CK2dEntity serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ck2dentity_serialize_fn)(
    nmo_chunk_t *chunk,
    const nmo_ck2dentity_state_t *state);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CK2dEntity schema types
 * 
 * Registers schema types for CK2dEntity state structures.
 * Must be called during initialization before using CK2dEntity schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ck2dentity_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Deserialize CK2dEntity from chunk (public API)
 * 
 * @param chunk Chunk containing CK2dEntity data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ck2dentity_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state);

/**
 * @brief Serialize CK2dEntity to chunk (public API)
 * 
 * @param chunk Chunk to write to
 * @param state State to serialize
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ck2dentity_serialize(
    const nmo_ck2dentity_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK2DENTITY_SCHEMAS_H */
