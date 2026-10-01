/**
 * @file nmo_texture_schemas.h
 * @brief CKTexture schema definitions for Virtools texture objects
 * @author libnmo
 * @date 2025
 *
 * Schema for CKTexture (ClassID 31), inherits from CKBeObject (ClassID 2).
 * Represents texture/image data with mipmaps and video format information.
 *
 * Serialization identifiers: see CK_STATESAVE_TEX* in nmo_statesave_ids.h. The
 * bitmaps are in 0x00100000 (reader-encoded), 0x00020000 (raw planes) or the
 * obsolete 0x00004000; the packed state is in 0x002FF000 (0x00FFF000 in later
 * engines); 0x00200000 is the pick threshold and 0x00400000 the user mipmaps.
 * Before data version 5 the mipmap flag and the save format sit in 0x00040000
 * and 0x00080000.
 *
 * CKTexture wraps image data with mipmap levels and rasterizer context.
 */

#ifndef NMO_CKTEXTURE_SCHEMAS_H
#define NMO_CKTEXTURE_SCHEMAS_H

#include "nmo_types.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
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
#define NMO_CKTEXTURE_USEGLOBAL                0x00000003  /**< Use global texture settings */
#define NMO_CKTEXTURE_INCLUDEORIGINALFILE      0x00000004  /**< Embed original file */

/** Bitmap data flags (CKBITMAPDATA_FLAGS) */
#define NMO_CKBMPDATA_INVALID                  0x00000001  /**< Invalid bitmap */
#define NMO_CKBMPDATA_TRANSPARENT              0x00000002  /**< Uses the transparent color */
#define NMO_CKBMPDATA_FORCERESTORE             0x00000004  /**< Force restore */
#define NMO_CKBMPDATA_CUBEMAP                  0x00000010  /**< Cubemap texture */

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
typedef struct nmo_texture_state {
    nmo_beobject_state_t base;

    /* Movie / filenames */
    uint8_t has_movie_filename;
    char *movie_filename;
    uint8_t has_slot_filenames;
    uint32_t slot_count;
    char **slot_filenames;

    /* Reader bitmap dimensions (CK_STATESAVE_TEXREADER) */
    int32_t reader_width;
    int32_t reader_height;
    int32_t reader_bpp;

    /* Bitmap payloads */
    CKTEXTURE_BITMAP_KIND bitmap_kind;
    nmo_texture_reader_slot_t *reader_slots;
    nmo_texture_raw_slot_t *raw_slots;
    nmo_texture_bitmap2_slot_t *bitmap2_slots;

    /* Pick threshold */
    uint8_t has_pick_threshold;
    int32_t pick_threshold;

    /* Packed flags (CK_STATESAVE_OLDTEXONLY, or CK_STATESAVE_TEXONLY with the
     * same layout when uses_texonly_identifier is set) */
    uint8_t has_oldtexonly;
    uint8_t uses_texonly_identifier;
    /* Flag bits the engine ignores; kept so they are written back */
    uint32_t packed_unknown_bits;
    uint8_t mipmap_level;   /* mipmap level count, 0xFF for automatic */
    uint32_t save_options;
    uint8_t is_transparent;
    uint8_t is_cubemap;
    uint8_t has_desired_video_format;
    uint32_t desired_video_format;
    uint8_t has_transparent_color;
    uint32_t transparent_color;
    uint8_t has_current_slot;
    int32_t current_slot;

    /* Layout of files with data_version < 5. CK_STATESAVE_TEXVIDEOFORMAT holds
     * the mipmap flag and an optional size-prefixed image descriptor (the tail,
     * kept with its length and DWORD padding verbatim); CK_STATESAVE_TEXSAVEFORMAT
     * holds the save options and the properties buffer. */
    uint8_t has_legacy_video_format;
    int32_t legacy_use_mipmap;
    void *legacy_video_format_data;
    size_t legacy_video_format_size;
    uint8_t has_legacy_save_format;

    /* Save format and user mipmaps */
    uint8_t has_save_format;
    void *save_format_data;
    size_t save_format_size;
    uint8_t has_user_mipmaps;
    uint32_t user_mipmap_count;
    nmo_texture_raw_slot_t *user_mipmaps;
} nmo_texture_state_t;

/* ========================================================================
 * Public API Functions
 * ======================================================================== */

NMO_API nmo_status_t nmo_texture_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_texture_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_texture_vtable, nmo_register_texture_type)

NMO_API nmo_status_t nmo_texture_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_texture_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Replace a texture's bitmap data with new RGBA pixels.
 *
 * Encodes the pixels as PNG and updates the texture state's reader slot.
 * The caller must decode the source image (e.g., via stbi_load) beforehand.
 *
 * @param state       Mutable texture state
 * @param arena       Arena for encoded data allocation
 * @param rgba_pixels RGBA pixel data (4 bytes per pixel, row-major)
 * @param width       Image width
 * @param height      Image height
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_texture_replace_bitmap(
    nmo_texture_state_t *state,
    nmo_arena_t *arena,
    const void *rgba_pixels,
    uint32_t width,
    uint32_t height);

/**
 * @brief Locate the encoded image inside a legacy bitmap2 slot.
 *
 * CKStateChunk::ReadBitmap2 selects the image reader from a five-byte tag in
 * front of the image ("CKTGA", "CKJPG", "CKDIB", "CKBMP", "CKTIF", "CKGIF",
 * "CKPCX") and gives that reader the bytes after the tag. A buffer that starts
 * with none of them is a whole TGA image.
 *
 * @param slot      Bitmap2 slot
 * @param out_data  Receives the start of the encoded image
 * @param out_size  Receives its size in bytes
 * @param out_ext   Receives the file extension of the reader ("tga", "jpg",
 *                  "bmp", "tif", "gif" or "pcx"); may be NULL
 */
NMO_API void nmo_texture_bitmap2_image(
    const nmo_texture_bitmap2_slot_t *slot,
    const uint8_t **out_data,
    size_t *out_size,
    const char **out_ext);

/**
 * @brief Desired video format as the engine uses it after a load.
 *
 * RCKTexture::Load ends with "a value above _32_X8L8V8U8 becomes
 * _16_ARGB1555". The state keeps the value the file holds (and writes it
 * back); this returns the one the engine would work with.
 */
NMO_API uint32_t nmo_texture_effective_desired_video_format(
    const nmo_texture_state_t *state);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKTEXTURE_SCHEMAS_H */
