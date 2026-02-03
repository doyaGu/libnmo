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
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckstatesave_ids.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @defgroup CKTextureSchema CKTexture Schema API
 * @{
 */

/* ========================================================================
 * Constants and Enumerations
 * ======================================================================== */

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
 * @brief Bitmap payload kind stored in CKTexture chunks.
 */
typedef enum nmo_cktexture_bitmap_kind {
    NMO_CKTEXTURE_BITMAP_NONE = 0,
    NMO_CKTEXTURE_BITMAP_READER = 1,
    NMO_CKTEXTURE_BITMAP_RAW = 2,
    NMO_CKTEXTURE_BITMAP_BITMAP2 = 3
} nmo_cktexture_bitmap_kind_t;

/**
 * @brief Reader-compressed bitmap slot payload.
 */
typedef struct nmo_cktexture_reader_slot {
    uint32_t format_type;      /**< 0 = empty, 1 = no alpha plane, 2 = alpha plane */
    uint32_t extension;        /**< CKFileExtension packed into 4 bytes */
    nmo_guid_t reader_guid;    /**< Bitmap reader GUID */
    uint32_t data_size;        /**< Compressed data size */
    uint8_t *data;             /**< Compressed payload */
    uint32_t alpha_count;      /**< Distinct alpha count (format_type == 2) */
    uint32_t alpha_value;      /**< Single alpha value (alpha_count == 1) */
    uint32_t alpha_plane_size; /**< Alpha plane size (alpha_count > 1) */
    uint8_t *alpha_plane;      /**< Alpha plane payload */
} nmo_cktexture_reader_slot_t;

/**
 * @brief Raw bitmap slot payload (WriteRawBitmap layout).
 */
typedef struct nmo_cktexture_raw_slot {
    int32_t bits_per_pixel;
    int32_t width;
    int32_t height;
    uint32_t alpha_mask;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t compression;
    uint32_t blue_size;
    uint8_t *blue_data;
    uint32_t green_size;
    uint8_t *green_data;
    uint32_t red_size;
    uint8_t *red_data;
    uint32_t alpha_size;
    uint8_t *alpha_data;
} nmo_cktexture_raw_slot_t;

/**
 * @brief Legacy bitmap2 slot payload.
 */
typedef struct nmo_cktexture_bitmap2_slot {
    int32_t header_size;
    uint32_t buffer_size;
    uint8_t *buffer;
} nmo_cktexture_bitmap2_slot_t;

/**
 * @brief CKTexture state structure (inherits from CKBeObject)
 *
 * Size: Approximately 200+ bytes (variable based on mipmap count and pixel data)
 *
 * Serialization Format (CK2/CKRenderEngine):
 * - Identifier 0x00001000: Movie file name (optional)
 * - Identifier 0x00100000: Reader-compressed bitmaps (optional)
 * - Identifier 0x00020000: Raw bitmap data (optional)
 * - Identifier 0x00004000: Legacy bitmap2 data (optional)
 * - Identifier 0x00010000: Slot filenames (optional)
 * - Identifier 0x00200000: Pick threshold (optional)
 * - Identifier 0x002FF000: Packed texture flags (oldtexonly)
 * - Identifier 0x00080000: Save format (optional)
 * - Identifier 0x00400000: User mipmaps (optional)
 */
typedef struct nmo_ck_texture_state {
    nmo_ckbeobject_state_t base;

    /* Movie / filenames */
    uint8_t has_movie_filename;
    char *movie_filename;
    uint8_t has_slot_filenames;
    uint32_t slot_count;
    char **slot_filenames;

    /* Bitmap payloads */
    nmo_cktexture_bitmap_kind_t bitmap_kind;
    nmo_cktexture_reader_slot_t *reader_slots;
    nmo_cktexture_raw_slot_t *raw_slots;
    nmo_cktexture_bitmap2_slot_t *bitmap2_slots;

    /* Pick threshold */
    uint8_t has_pick_threshold;
    int32_t pick_threshold;

    /* Packed flags (CK_STATESAVE_OLDTEXONLY) */
    uint8_t has_oldtexonly;
    uint8_t mipmap_level;
    uint16_t save_options;
    uint8_t is_transparent;
    uint8_t is_cubemap;
    uint8_t has_desired_video_format;
    uint32_t desired_video_format;
    uint8_t has_transparent_color;
    uint32_t transparent_color;
    uint8_t has_current_slot;
    int32_t current_slot;

    /* Save format and user mipmaps */
    uint8_t has_save_format;
    void *save_format_data;
    size_t save_format_size;
    uint8_t has_user_mipmaps;
    uint32_t user_mipmap_count;
    nmo_cktexture_raw_slot_t *user_mipmaps;
} nmo_ck_texture_state_t;

/* ========================================================================
 * Public API Functions
 * ======================================================================== */

NMO_API nmo_status_t nmo_cktexture_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_cktexture_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cktexture_vtable, nmo_register_cktexture_type)

NMO_API nmo_status_t nmo_cktexture_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTEXTURE_SCHEMAS_H */
