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
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_enum_defs.h"
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
typedef struct nmo_cktexture_state {
    nmo_ckbeobject_state_t base;

    /* Movie / filenames */
    uint8_t has_movie_filename;
    char *movie_filename;
    uint8_t has_slot_filenames;
    uint32_t slot_count;
    char **slot_filenames;

    /* Bitmap payloads */
    CKTEXTURE_BITMAP_KIND bitmap_kind;
    nmo_texture_reader_slot_t *reader_slots;
    nmo_texture_raw_slot_t *raw_slots;
    nmo_texture_bitmap2_slot_t *bitmap2_slots;

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
    nmo_texture_raw_slot_t *user_mipmaps;
} nmo_cktexture_state_t;

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
