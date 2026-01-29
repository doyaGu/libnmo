/**
 * @file nmo_cktexture_schemas.h
 * @brief CKTexture schema definitions for Virtools texture objects
 * @author libnmo
 * @date 2025
 *
 * Schema for CKTexture (ClassID 31), inherits from CKBeObject (ClassID 2).
 * Represents texture/image data with mipmaps and video format information.
 *
 * Serialization Identifiers (from RCKTexture analysis):
 * - 0x00200000: Palette data (for indexed color formats)
 * - 0x10000000: System memory copy flag
 * - 0x00800000: Video memory backup
 * - 0x00400000: Original file path (external texture reference)
 * - 0x00040000: Texture format and dimensions
 *
 * Reference: docs/CK2_3D_reverse_notes.md lines 341-348
 * CKTexture wraps image data with mipmap levels and rasterizer context.
 */

#ifndef NMO_CKTEXTURE_SCHEMAS_H
#define NMO_CKTEXTURE_SCHEMAS_H

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
 * @defgroup CKTextureSchema CKTexture Schema API
 * @{
 */

/* ========================================================================
 * Constants and Enumerations
 * ======================================================================== */

/** Serialization identifiers (from CK2_3D_reverse_notes.md) */
#define NMO_CKTEXTURE_IDENTIFIER_PALETTE       0x00200000  /**< Palette data */
#define NMO_CKTEXTURE_IDENTIFIER_SYSMEM        0x10000000  /**< System memory copy */
#define NMO_CKTEXTURE_IDENTIFIER_VIDEOMEM      0x00800000  /**< Video memory backup */
#define NMO_CKTEXTURE_IDENTIFIER_FILEPATH      0x00400000  /**< Original file path */
#define NMO_CKTEXTURE_IDENTIFIER_FORMAT        0x00040000  /**< Texture format/dimensions */

/** Bitmap save options (CK_BITMAP_SAVEOPTIONS) */
#define NMO_CKTEXTURE_RAWDATA                  0x00000000  /**< Raw pixel data */
#define NMO_CKTEXTURE_EXTERNAL                 0x00000001  /**< External file reference */
#define NMO_CKTEXTURE_IMAGEFORMAT              0x00000002  /**< Compressed format (JPEG/PNG) */
#define NMO_CKTEXTURE_USEGLOBAL                0x00000004  /**< Use global texture settings */
#define NMO_CKTEXTURE_INCLUDEORIGINALFILE      0x00000008  /**< Embed original file */

/** Bitmap data flags (CKBMPDATA_FLAGS) */
#define NMO_CKBMPDATA_FREEVIDEOMEMORY          0x00000001  /**< Free video memory */
#define NMO_CKBMPDATA_INVALID                  0x00000002  /**< Invalid bitmap */
#define NMO_CKBMPDATA_CUBEMAP                  0x00000004  /**< Cubemap texture */
#define NMO_CKBMPDATA_FORCERESTORE             0x00000008  /**< Force restore */
#define NMO_CKBMPDATA_DYNAMIC                  0x00000010  /**< Dynamic texture */
#define NMO_CKBMPDATA_HASPALETTE               0x00000020  /**< Has palette data */

/* ========================================================================
 * Structure Definitions
 * ======================================================================== */

/**
 * @brief Texture format and dimensions
 *
 * Describes the texture's pixel format, size, and video memory requirements.
 */
typedef struct nmo_texture_format {
    uint32_t width;              /**< Texture width in pixels */
    uint32_t height;             /**< Texture height in pixels */
    uint32_t bits_per_pixel;     /**< Bits per pixel (8, 16, 24, 32) */
    uint32_t bytes_per_line;     /**< Bytes per scanline (stride) */
    uint32_t image_size;         /**< Total image size in bytes */
    uint32_t red_mask;           /**< Red channel bitmask */
    uint32_t green_mask;         /**< Green channel bitmask */
    uint32_t blue_mask;          /**< Blue channel bitmask */
    uint32_t alpha_mask;         /**< Alpha channel bitmask */
} nmo_texture_format_t;

/**
 * @brief Mipmap level data
 *
 * Each mipmap level contains a progressively smaller version of the texture.
 */
typedef struct nmo_mipmap_level {
    uint32_t width;              /**< Mipmap width */
    uint32_t height;             /**< Mipmap height */
    uint32_t size;               /**< Data size in bytes */
    uint8_t *data;               /**< Pixel data (arena-allocated) */
} nmo_mipmap_level_t;

/**
 * @brief CKTexture state structure (inherits from CKBeObject)
 *
 * Size: Approximately 200+ bytes (variable based on mipmap count and pixel data)
 *
 * Serialization Format:
 * - Identifier 0x00040000: Format (width, height, bpp, masks)
 * - Identifier 0x00200000: Palette data (optional, for indexed formats)
 * - Identifier 0x10000000: System memory pixel data
 * - Identifier 0x00800000: Video memory backup (optional)
 * - Identifier 0x00400000: Original file path (optional, for external refs)
 *
 * Lifecycle:
 * 1. Deserialize: Parse identifiers, load pixel data and format
 * 2. FinishLoading: Validate format, generate mipmaps if needed
 */
typedef struct nmo_ck_texture_state {
    /* === Inherited from CKBeObject === */
    // CKBeObject state would be here (name, flags, etc.)
    // For now, keeping it simple without explicit inheritance
    
    /* === CKTexture-Specific === */
    
    /** @name Texture Format (Identifier 0x00040000) */
    /**@{*/
    bool has_format;                    /**< True if format data is present */
    nmo_texture_format_t format;        /**< Texture format and dimensions */
    /**@}*/
    
    /** @name Palette Data (Identifier 0x00200000) */
    /**@{*/
    bool has_palette;                   /**< True if palette is present */
    uint32_t palette_size;              /**< Number of palette entries */
    uint32_t *palette;                  /**< Palette entries (ARGB, arena-allocated) */
    /**@}*/
    
    /** @name Pixel Data (Identifier 0x10000000) */
    /**@{*/
    bool has_pixel_data;                /**< True if pixel data is present */
    uint32_t pixel_data_size;           /**< Size of pixel data in bytes */
    uint8_t *pixel_data;                /**< Raw pixel data (arena-allocated) */
    /**@}*/
    
    /** @name Video Memory Backup (Identifier 0x00800000) */
    /**@{*/
    bool has_video_backup;              /**< True if video backup is present */
    uint32_t video_backup_size;         /**< Size of video backup in bytes */
    uint8_t *video_backup;              /**< Video memory backup (arena-allocated) */
    /**@}*/
    
    /** @name External File Reference (Identifier 0x00400000) */
    /**@{*/
    bool has_file_path;                 /**< True if external file path is present */
    char *file_path;                    /**< Original file path (arena-allocated) */
    /**@}*/
    
    /** @name Mipmap Data */
    /**@{*/
    uint32_t mipmap_count;              /**< Number of mipmap levels */
    nmo_mipmap_level_t *mipmaps;        /**< Mipmap levels (arena-allocated) */
    /**@}*/
    
    /** @name Save Options */
    /**@{*/
    uint32_t save_options;              /**< Bitmap save options (CK_BITMAP_SAVEOPTIONS) */
    uint32_t flags;                     /**< Bitmap flags (CKBMPDATA_FLAGS) */
    /**@}*/
    
    /* === Internal State === */
    bool needs_mipmap_generation;       /**< Flag set during Load, cleared after mipmap gen */
} nmo_ck_texture_state_t;

/* ========================================================================
 * Function Pointer Types
 * ======================================================================== */

/**
 * @brief Deserialize function for CKTexture objects (modern format v5+)
 *
 * @param[in] chunk State chunk containing serialized data
 * @param[in] arena Arena allocator for temporary memory
 * @param[out] out_state Deserialized CKTexture state
 * @return NMO_OK on success, error code on failure
 *
 * Expected Identifiers:
 * - 0x00040000: Format (required)
 * - 0x10000000: Pixel data (required)
 * - 0x00200000: Palette (optional, for indexed formats)
 * - 0x00800000: Video backup (optional)
 * - 0x00400000: File path (optional, for external refs)
 *
 * Error Conditions:
 * - NMO_ERR_INVALID_FORMAT: Missing required format or pixel data
 * - NMO_ERR_NOMEM: Arena allocation failure
 */
typedef nmo_result_t (*nmo_cktexture_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *out_state
);

/**
 * @brief Serialize function for CKTexture objects (modern format v5+)
 *
 * @param[in] state CKTexture state to serialize
 * @param[in,out] chunk State chunk to write data into
 * @param[in] arena Arena allocator for temporary memory
 * @return NMO_OK on success, error code on failure
 *
 * Written Identifiers:
 * - 0x00040000: Format (always written if has_format is true)
 * - 0x10000000: Pixel data (always written if has_pixel_data is true)
 * - 0x00200000: Palette (written if has_palette is true)
 * - 0x00800000: Video backup (written if has_video_backup is true)
 * - 0x00400000: File path (written if has_file_path is true)
 *
 * Error Conditions:
 * - NMO_ERR_CANT_WRITE_FILE: Chunk buffer overflow
 * - NMO_ERR_VALIDATION_FAILED: Invalid format or missing required data
 */
typedef nmo_result_t (*nmo_cktexture_serialize_fn)(
    const nmo_ck_texture_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena
);

/**
 * @brief Finish loading callback for CKTexture objects
 *
 * Performs post-deserialization setup:
 * - Validates texture format (dimensions, bpp)
 * - Generates mipmaps if needed
 * - Clears needs_mipmap_generation flag
 *
 * @param[in,out] state CKTexture state to finalize
 * @param[in] context Object context (unused currently)
 * @param[in] arena Arena allocator for temporary memory
 * @return NMO_OK on success, error code on failure
 *
 * Error Conditions:
 * - NMO_ERR_VALIDATION_FAILED: Invalid texture format
 * - NMO_ERR_NOMEM: Failed to allocate mipmap data
 */
typedef nmo_result_t (*nmo_cktexture_finish_loading_fn)(
    nmo_ck_texture_state_t *state,
    void *context,
    nmo_arena_t *arena
);

/* ========================================================================
 * Public API Functions
 * ======================================================================== */

/**
 * @brief Register CKTexture schemas with the schema system
 *
 * Registers ClassID 31 (CKCID_TEXTURE) with deserialize, serialize,
 * and finish_loading handlers.
 *
 * @param[in,out] registry Schema registry to register into
 * @param[in] arena Arena for schema allocations
 * @return NMO_OK on success, error code on failure
 *
 * Thread Safety: NOT thread-safe during registration. Call once during initialization.
 */
nmo_result_t nmo_register_cktexture_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena
);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTEXTURE_SCHEMAS_H */
