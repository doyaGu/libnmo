/**
 * @file nmo_image.h
 * @brief Image descriptor types and helpers for Virtools bitmap serialization.
 */

#ifndef NMO_IMAGE_H
#define NMO_IMAGE_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Supported pixel formats.
 */
typedef enum nmo_pixel_format {
    NMO_PIXEL_FORMAT_UNKNOWN         = 0,

    /* Standard RGB / ARGB formats (19 kinds) */
    NMO_PIXEL_FORMAT_32_ARGB8888     = 1,
    NMO_PIXEL_FORMAT_32_RGB888       = 2,
    NMO_PIXEL_FORMAT_24_RGB888       = 3,
    NMO_PIXEL_FORMAT_16_RGB565       = 4,
    NMO_PIXEL_FORMAT_16_RGB555       = 5,
    NMO_PIXEL_FORMAT_16_ARGB1555     = 6,
    NMO_PIXEL_FORMAT_16_ARGB4444     = 7,
    NMO_PIXEL_FORMAT_8_RGB332        = 8,
    NMO_PIXEL_FORMAT_8_ARGB2222      = 9,

    /* BGR variants */
    NMO_PIXEL_FORMAT_32_ABGR8888     = 10,
    NMO_PIXEL_FORMAT_32_RGBA8888     = 11,
    NMO_PIXEL_FORMAT_32_BGRA8888     = 12,
    NMO_PIXEL_FORMAT_32_BGR888       = 13,
    NMO_PIXEL_FORMAT_24_BGR888       = 14,
    NMO_PIXEL_FORMAT_16_BGR565       = 15,
    NMO_PIXEL_FORMAT_16_BGR555       = 16,
    NMO_PIXEL_FORMAT_16_ABGR1555     = 17,
    NMO_PIXEL_FORMAT_16_ABGR4444     = 18,

    /* DXT compressed formats */
    NMO_PIXEL_FORMAT_DXT1            = 19,
    NMO_PIXEL_FORMAT_DXT2            = 20,
    NMO_PIXEL_FORMAT_DXT3            = 21,
    NMO_PIXEL_FORMAT_DXT4            = 22,
    NMO_PIXEL_FORMAT_DXT5            = 23,

    /* Bump map formats */
    NMO_PIXEL_FORMAT_16_V8U8         = 24,
    NMO_PIXEL_FORMAT_32_V16U16       = 25,
    NMO_PIXEL_FORMAT_16_L6V5U5       = 26,
    NMO_PIXEL_FORMAT_32_X8L8V8U8     = 27,

    NMO_PIXEL_FORMAT_COUNT           = 28
} nmo_pixel_format_t;

/**
 * @brief Virtools-compatible image descriptor (VxImageDescEx analogue).
 */
typedef struct nmo_image_desc {
    nmo_pixel_format_t format;
    int32_t width;
    int32_t height;
    int32_t bits_per_pixel;
    union {
        int32_t bytes_per_line;   /**< Pitch for uncompressed formats */
        int32_t total_image_size; /**< Total compressed payload size */
    };

    /* Bit masks for channel extraction */
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;

    /* Optional palette data */
    uint16_t bytes_per_color_entry;
    uint16_t color_map_entries;
    uint8_t *color_map_data;

    /* Pixel payload pointer */
    uint8_t *image_data;
} nmo_image_desc_t;

/**
 * @brief Encoded bitmap container formats.
 */
typedef enum nmo_bitmap_format {
    NMO_BITMAP_FORMAT_RAW = 0,
    NMO_BITMAP_FORMAT_BMP,
    NMO_BITMAP_FORMAT_JPG,
    NMO_BITMAP_FORMAT_PNG,
    NMO_BITMAP_FORMAT_TGA,
    NMO_BITMAP_FORMAT_HDR,
    NMO_BITMAP_FORMAT_PSD,
    NMO_BITMAP_FORMAT_GIF,
    NMO_BITMAP_FORMAT_PIC,
    NMO_BITMAP_FORMAT_PNM,
    NMO_BITMAP_FORMAT_COUNT,
} nmo_bitmap_format_t;

/**
 * @brief Encoding preferences for bitmap export.
 */
typedef struct nmo_bitmap_properties {
    nmo_bitmap_format_t format;
    int quality;
    int compression_level;
    bool save_alpha;
    const char *extension;
} nmo_bitmap_properties_t;

/**
 * @brief Shift information for mask-based channel extraction.
 */
typedef struct nmo_mask_shifts {
    uint32_t red_shift_lsb;
    uint32_t red_shift_msb;
    uint32_t green_shift_lsb;
    uint32_t green_shift_msb;
    uint32_t blue_shift_lsb;
    uint32_t blue_shift_msb;
    uint32_t alpha_shift_lsb;
    uint32_t alpha_shift_msb;
} nmo_mask_shifts_t;

/**
 * @brief Check whether a pixel format is DXT compressed.
 */
static inline bool nmo_pixel_format_is_dxt(nmo_pixel_format_t f) {
    return f >= NMO_PIXEL_FORMAT_DXT1 && f <= NMO_PIXEL_FORMAT_DXT5;
}

/**
 * @brief Check whether a pixel format is a bump map format.
 */
static inline bool nmo_pixel_format_is_bump(nmo_pixel_format_t f) {
    return f >= NMO_PIXEL_FORMAT_16_V8U8 && f <= NMO_PIXEL_FORMAT_32_X8L8V8U8;
}

NMO_API void nmo_image_calculate_mask_shifts(
    uint32_t red_mask,
    uint32_t green_mask,
    uint32_t blue_mask,
    uint32_t alpha_mask,
    nmo_mask_shifts_t *out_shifts);

NMO_API uint8_t nmo_image_extract_channel(
    uint32_t pixel,
    uint32_t mask,
    const nmo_mask_shifts_t *shifts,
    int channel_index);

NMO_API void nmo_image_desc_init_argb32(
    nmo_image_desc_t *desc,
    int width,
    int height);

NMO_API int32_t nmo_image_calc_bytes_per_line(
    int32_t width,
    int32_t bits_per_pixel);

NMO_API size_t nmo_image_calc_size(
    const nmo_image_desc_t *desc);

/**
 * @brief Reconstruct interleaved RGBA pixels from separated channel buffers.
 *
 * Handles the common 32bpp case where each channel is a 1-byte-per-pixel
 * buffer. Non-32bpp formats return NMO_ERR_NOT_SUPPORTED.
 *
 * @param red       Red channel buffer (NULL = fill zeros)
 * @param green     Green channel buffer (NULL = fill zeros)
 * @param blue      Blue channel buffer (NULL = fill zeros)
 * @param alpha     Alpha channel buffer (NULL = fill 0xFF)
 * @param red_size  Size of red buffer in bytes
 * @param green_size Size of green buffer in bytes
 * @param blue_size Size of blue buffer in bytes
 * @param alpha_size Size of alpha buffer in bytes
 * @param width     Image width
 * @param height    Image height
 * @param bpp       Bits per pixel (must be 32 for now)
 * @param arena     Arena for output allocation
 * @param out_pixels Output: interleaved RGBA pixel buffer
 * @param out_channels Output: channel count (always 4)
 * @return NMO_OK on success, NMO_ERR_NOT_SUPPORTED for non-32bpp
 */
NMO_API nmo_status_t nmo_image_reconstruct_pixels(
    const uint8_t *red, const uint8_t *green,
    const uint8_t *blue, const uint8_t *alpha,
    uint32_t red_size, uint32_t green_size,
    uint32_t blue_size, uint32_t alpha_size,
    int width, int height, int bpp,
    nmo_arena_t *arena,
    uint8_t **out_pixels, int *out_channels);

/**
 * @brief Decode interleaved (non-planar) pixel data to RGBA32.
 *
 * Handles 8/16/24/32 bpp formats using mask-based channel extraction
 * with full-range normalization. Rejects DXT, bump, and palette-indexed
 * formats.
 *
 * @param desc      Source image descriptor (must have valid image_data)
 * @param arena     Arena for output allocation
 * @param out_rgba  Output: RGBA32 pixel buffer (4 bytes per pixel)
 * @param out_width Output: image width
 * @param out_height Output: image height
 * @return NMO_OK on success, NMO_ERR_NOT_SUPPORTED for unsupported formats
 */
NMO_API nmo_status_t nmo_image_decode_interleaved_to_rgba32(
    const nmo_image_desc_t *desc,
    nmo_arena_t *arena,
    uint8_t **out_rgba,
    int *out_width,
    int *out_height);

/**
 * @brief Decode DXT-compressed pixel data to RGBA32.
 *
 * @param data       Compressed DXT data
 * @param data_size  Size of compressed data in bytes
 * @param width      Image width
 * @param height     Image height
 * @param format     DXT pixel format (DXT1-DXT5)
 * @param arena      Arena for output allocation
 * @param out_rgba   Output: RGBA32 pixel buffer (4 bytes per pixel)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_image_decode_dxt(
    const uint8_t *data, size_t data_size,
    int width, int height,
    nmo_pixel_format_t format,
    nmo_arena_t *arena,
    uint8_t **out_rgba);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IMAGE_H */
