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
 * - Optional blocks: 0x10000 (source rect), 0x20000 (z-order), 0x40000 (parent)
 * - Material is stored in identifier 0x200000 (CKCID_2DENTITY only)
 */

#ifndef NMO_CK2DENTITY_SCHEMAS_H
#define NMO_CK2DENTITY_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_math.h"
#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckstatesave_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CK2dEntity STATE STRUCTURES
 * ============================================================================= */

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
    nmo_ckrenderobject_state_t base; /**< Parent CKRenderObject state */
    
    /* Core rectangle fields */
    nmo_rect_t rect;                    /**< Screen-space rectangle */
    bool has_homogeneous_rect;          /**< True if homogeneous coords are used */
    nmo_rect_t homogeneous_rect;        /**< Normalized [0..1] coordinates */
    
    /* Optional fields (presence indicated by flags) */
    bool has_source_rect;               /**< True if source rect is present */
    nmo_rect_t source_rect;             /**< Texture/sprite source rectangle */
    
    bool has_z_order;                   /**< True if z-order is present */
    int32_t z_order;                    /**< Rendering depth */
    
    bool has_parent;                    /**< True if parent reference is present */
    nmo_object_id_t parent_id;          /**< Parent entity ID */
    
    bool has_material;                  /**< True if material identifier is present */
    nmo_object_id_t material_id;        /**< Material reference */
    
    /* Flags (sanitized with 0xFFF8F7FF on load) */
    uint32_t flags;                     /**< Entity flags (visibility, clipping, etc.) */
    
} nmo_ck2dentity_state_t;

/* =============================================================================
 * CHUNK IDENTIFIERS
 * ============================================================================= */

/** Optional block flags (modern format) */
#define NMO_CK2DENTITY_FLAG_SOURCE_RECT  0x10000
#define NMO_CK2DENTITY_FLAG_Z_ORDER      0x20000
#define NMO_CK2DENTITY_FLAG_PARENT       0x40000

/** Homogeneous rect flag (bit in flags field) */
#define NMO_CK2DENTITY_FLAG_HOMOGENEOUS  0x200

/** Flag sanitization mask (applied on load) */
#define NMO_CK2DENTITY_FLAGS_MASK        0xFFF8F7FF

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_ck2dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ck2dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ck2dentity_vtable, nmo_register_ck2dentity_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK2DENTITY_SCHEMAS_H */
