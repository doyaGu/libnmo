/**
 * @file nmo_ckspritetext_schemas.h
 * @brief CKSpriteText schema definitions for Virtools text rendering objects
 * @author libnmo
 * @date 2025
 *
 * Schema for CKSpriteText (ClassID 29), inherits from CKSprite (ClassID 28).
 * Represents 2D text with font properties and colors.
 *
 * Serialization Identifiers:
 * - 0x01000000 (v5+): Text string content
 * - 0x02000000 (v5+): Font properties (name, size, weight, italic, charset)
 * - 0x04000000 (v5+): Text color and background color (ARGB format)
 *
 * Reference: docs/CK2_3D_reverse_notes_extended.md lines 470-850
 * - Load function: 0x10062547
 * - Save function: 0x100621FF
 */

#ifndef NMO_CKSPRITETEXT_SCHEMAS_H
#define NMO_CKSPRITETEXT_SCHEMAS_H

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
 * @defgroup CKSpriteTextSchema CKSpriteText Schema API
 * @{
 */

/* ========================================================================
 * Constants and Enumerations
 * ======================================================================== */

/** Serialization identifiers (from CK2_3D_reverse_notes_extended.md) */
#define NMO_CKSPRITETEXT_IDENTIFIER_TEXT       0x01000000  /**< Text string (v5+) */
#define NMO_CKSPRITETEXT_IDENTIFIER_FONT       0x02000000  /**< Font properties (v5+) */
#define NMO_CKSPRITETEXT_IDENTIFIER_COLOR      0x04000000  /**< Text and background colors (v5+) */

/** Font weight constants (Windows LOGFONT standard) */
#define NMO_FONT_WEIGHT_DONTCARE    0     /**< Don't care or don't know */
#define NMO_FONT_WEIGHT_THIN        100   /**< Thin weight (100) */
#define NMO_FONT_WEIGHT_EXTRALIGHT  200   /**< Extra light (200) */
#define NMO_FONT_WEIGHT_LIGHT       300   /**< Light (300) */
#define NMO_FONT_WEIGHT_NORMAL      400   /**< Normal (regular) (400) */
#define NMO_FONT_WEIGHT_MEDIUM      500   /**< Medium (500) */
#define NMO_FONT_WEIGHT_SEMIBOLD    600   /**< Semi-bold (demi-bold) (600) */
#define NMO_FONT_WEIGHT_BOLD        700   /**< Bold (700) */
#define NMO_FONT_WEIGHT_EXTRABOLD   800   /**< Extra bold (ultra bold) (800) */
#define NMO_FONT_WEIGHT_HEAVY       900   /**< Heavy (black) (900) */

/** Font charset constants (Windows LOGFONT standard) */
#define NMO_FONT_CHARSET_ANSI          0    /**< ANSI character set (Windows CP1252) */
#define NMO_FONT_CHARSET_DEFAULT       1    /**< System default charset */
#define NMO_FONT_CHARSET_SYMBOL        2    /**< Symbol character set */
#define NMO_FONT_CHARSET_SHIFTJIS      128  /**< Japanese Shift-JIS */
#define NMO_FONT_CHARSET_HANGEUL       129  /**< Korean Hangul */
#define NMO_FONT_CHARSET_GB2312        134  /**< Chinese Simplified (GB2312) */
#define NMO_FONT_CHARSET_CHINESEBIG5   136  /**< Chinese Traditional (Big5) */
#define NMO_FONT_CHARSET_OEM           255  /**< OEM character set */
#define NMO_FONT_CHARSET_JOHAB         130  /**< Korean Johab */
#define NMO_FONT_CHARSET_HEBREW        177  /**< Hebrew */
#define NMO_FONT_CHARSET_ARABIC        178  /**< Arabic */
#define NMO_FONT_CHARSET_GREEK         161  /**< Greek */
#define NMO_FONT_CHARSET_TURKISH       162  /**< Turkish */
#define NMO_FONT_CHARSET_VIETNAMESE    163  /**< Vietnamese */
#define NMO_FONT_CHARSET_THAI          222  /**< Thai */
#define NMO_FONT_CHARSET_EASTEUROPE    238  /**< Eastern European */
#define NMO_FONT_CHARSET_RUSSIAN       204  /**< Cyrillic (Russian) */
#define NMO_FONT_CHARSET_BALTIC        186  /**< Baltic */

/* ========================================================================
 * Structure Definitions
 * ======================================================================== */

/**
 * @brief Font properties structure (mirrors VXFONTINFO from Virtools)
 *
 * Matches the serialization format in identifier 0x02000000.
 * All values follow Windows LOGFONT conventions.
 */
typedef struct nmo_font_info {
    char *font_name;         /**< Font family name (e.g., "Arial", "Times New Roman") */
    int32_t size;            /**< Font size in points (typical range: 8-72) */
    int32_t weight;          /**< Font weight (100-900, see NMO_FONT_WEIGHT_* constants) */
    int32_t italic;          /**< Italic flag (0 = normal, 1 = italic) */
    int32_t charset;         /**< Character set (see NMO_FONT_CHARSET_* constants) */
} nmo_font_info_t;

/**
 * @brief CKSpriteText state structure (inherits from CKSprite)
 *
 * Size: Approximately 120 bytes (64 bytes base + 56 bytes specific)
 *
 * Serialization Format:
 * - Identifier 0x01000000: text_content (string)
 * - Identifier 0x02000000: font properties (string + 4x int32)
 * - Identifier 0x04000000: font_color + background_color (2x uint32)
 *
 * Lifecycle:
 * 1. Deserialize: Parse identifiers, store properties
 * 2. FinishLoading: Validate font, prepare text buffer for rendering
 */
typedef struct nmo_ck_spritetext_state {
    /* === Inherited from CKSprite === */
    nmo_object_id_t parent_id;            /**< Parent 2D entity (from CK2dEntity) */
    nmo_object_id_t material_id;          /**< Material reference (from CK2dEntity) */
    int32_t z_order;                      /**< Z-order for layering (from CK2dEntity) */
    uint32_t entity_flags;                /**< Entity flags (from CK2dEntity, identifier 0x10F000) */
    float rect[4];                        /**< Screen rectangle [left, top, right, bottom] */
    float source_rect[4];                 /**< Source texture rectangle (optional) */
    bool has_source_rect;                 /**< Whether source_rect is valid */
    
    /* Sprite-specific (inherited) */
    uint32_t transparent_color;           /**< Transparent color key (ARGB) */
    bool is_transparent;                  /**< Transparency flag */
    int32_t current_slot;                 /**< Current animation slot index */
    uint32_t bitmap_save_options;         /**< Bitmap save options (from 0x20000000) */
    
    /* === CKSpriteText-Specific === */
    
    /** @name Text Content (Identifier 0x01000000) */
    /**@{*/
    char *text_content;                   /**< Text string to display (UTF-8 or ANSI) */
    /**@}*/
    
    /** @name Font Properties (Identifier 0x02000000) */
    /**@{*/
    nmo_font_info_t font;                 /**< Font properties (name, size, weight, italic, charset) */
    /**@}*/
    
    /** @name Colors (Identifier 0x04000000) */
    /**@{*/
    uint32_t font_color;                  /**< Text color (ARGB format, e.g., 0xFFFFFFFF = white) */
    uint32_t background_color;            /**< Background color (ARGB format, e.g., 0x00000000 = transparent) */
    /**@}*/
    
    /* === Internal State === */
    bool needs_redraw;                    /**< Flag set during Load, cleared after rendering setup */
} nmo_ck_spritetext_state_t;

/* ========================================================================
 * Function Pointer Types
 * ======================================================================== */

/**
 * @brief Deserialize function for CKSpriteText objects (modern format v5+)
 *
 * @param[in] chunk State chunk containing serialized data
 * @param[in] arena Arena allocator for temporary memory
 * @param[out] out_state Deserialized CKSpriteText state
 * @return NMO_OK on success, error code on failure
 *
 * Expected Identifiers:
 * - 0x01000000: Text string (optional, defaults to empty)
 * - 0x02000000: Font properties (optional, defaults to system font)
 * - 0x04000000: Colors (optional, defaults to white text on transparent background)
 *
 * Error Conditions:
 * - NMO_ERR_CHUNK_DATA_CORRUPT: Invalid string length or integer format
 * - NMO_ERR_NOMEM: Arena allocation failure
 */
typedef nmo_result_t (*nmo_ckspritetext_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *out_state
);

/**
 * @brief Serialize function for CKSpriteText objects (modern format v5+)
 *
 * @param[in] state CKSpriteText state to serialize
 * @param[in,out] chunk State chunk to write data into
 * @param[in] arena Arena allocator for temporary memory
 * @return NMO_OK on success, error code on failure
 *
 * Written Identifiers:
 * - 0x01000000: Text string (always written, even if empty)
 * - 0x02000000: Font properties (always written)
 * - 0x04000000: Colors (always written)
 *
 * Error Conditions:
 * - NMO_ERR_CHUNK_WRITE_FAILED: Chunk buffer overflow
 * - NMO_ERR_VALIDATION_FAILED: Invalid font name (NULL or empty)
 */
typedef nmo_result_t (*nmo_ckspritetext_serialize_fn)(
    const nmo_ck_spritetext_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena
);

/**
 * @brief Finish loading callback for CKSpriteText objects
 *
 * Performs post-deserialization setup:
 * - Validates font name (sets default if empty)
 * - Clamps font size to reasonable range (6-128 points)
 * - Normalizes font weight (100-900)
 * - Clears needs_redraw flag
 *
 * @param[in,out] state CKSpriteText state to finalize
 * @param[in] context Object context (unused currently)
 * @param[in] arena Arena allocator for temporary memory
 * @return NMO_OK on success, error code on failure
 *
 * Error Conditions:
 * - NMO_ERR_NOMEM: Failed to allocate default font name
 */
typedef nmo_result_t (*nmo_ckspritetext_finish_loading_fn)(
    nmo_ck_spritetext_state_t *state,
    void *context,
    nmo_arena_t *arena
);

/* ========================================================================
 * Public API Functions
 * ======================================================================== */

/**
 * @brief Register CKSpriteText schemas with the schema system
 *
 * Registers ClassID 29 (CKCID_SPRITETEXT) with deserialize, serialize,
 * and finish_loading handlers.
 *
 * @param[in,out] registry Schema registry to register into
 * @param[in] arena Arena for schema allocations
 * @return NMO_OK on success, error code on failure
 *
 * Thread Safety: NOT thread-safe during registration. Call once during initialization.
 */
nmo_result_t nmo_register_ckspritetext_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITETEXT_SCHEMAS_H */
