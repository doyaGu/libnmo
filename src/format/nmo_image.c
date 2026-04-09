/**
 * @file nmo_image.c
 * @brief Image descriptor helpers and mask utilities.
 */

#include "format/nmo_image.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_utils.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

static void analyze_mask(uint32_t mask,
                         uint32_t *out_lsb_pos,
                         uint32_t *out_bit_count) {
    if (mask == 0) {
        *out_lsb_pos = 0;
        *out_bit_count = 0;
        return;
    }

    uint32_t lsb = 0;
    while (((mask >> lsb) & 1u) == 0u) {
        lsb++;
    }

    uint32_t count = 0;
    uint32_t shifted = mask >> lsb;
    while ((shifted & 1u) != 0u) {
        count++;
        shifted >>= 1u;
    }

    *out_lsb_pos = lsb;
    *out_bit_count = count;
}

void nmo_image_calculate_mask_shifts(uint32_t red_mask,
                                     uint32_t green_mask,
                                     uint32_t blue_mask,
                                     uint32_t alpha_mask,
                                     nmo_mask_shifts_t *out_shifts) {
    uint32_t red_bits;
    uint32_t green_bits;
    uint32_t blue_bits;
    uint32_t alpha_bits;

    analyze_mask(red_mask, &out_shifts->red_shift_lsb, &red_bits);
    analyze_mask(green_mask, &out_shifts->green_shift_lsb, &green_bits);
    analyze_mask(blue_mask, &out_shifts->blue_shift_lsb, &blue_bits);
    analyze_mask(alpha_mask, &out_shifts->alpha_shift_lsb, &alpha_bits);

    out_shifts->red_shift_msb = (red_bits > 0u) ? (8u - red_bits) : 0u;
    out_shifts->green_shift_msb = (green_bits > 0u) ? (8u - green_bits) : 0u;
    out_shifts->blue_shift_msb = (blue_bits > 0u) ? (8u - blue_bits) : 0u;
    out_shifts->alpha_shift_msb = (alpha_bits > 0u) ? (8u - alpha_bits) : 0u;
}

uint8_t nmo_image_extract_channel(uint32_t pixel,
                                  uint32_t mask,
                                  const nmo_mask_shifts_t *shifts,
                                  int channel_index) {
    if (mask == 0u) {
        return (channel_index == 3) ? 0xFFu : 0u;
    }

    uint32_t shift_lsb = 0;
    uint32_t shift_msb = 0;

    switch (channel_index) {
        case 0:
            shift_lsb = shifts->red_shift_lsb;
            shift_msb = shifts->red_shift_msb;
            break;
        case 1:
            shift_lsb = shifts->green_shift_lsb;
            shift_msb = shifts->green_shift_msb;
            break;
        case 2:
            shift_lsb = shifts->blue_shift_lsb;
            shift_msb = shifts->blue_shift_msb;
            break;
        case 3:
            shift_lsb = shifts->alpha_shift_lsb;
            shift_msb = shifts->alpha_shift_msb;
            break;
        default:
            return 0u;
    }

    uint32_t value = (pixel & mask) >> shift_lsb;
    value <<= shift_msb;
    return (uint8_t)value;
}

void nmo_image_desc_init_argb32(nmo_image_desc_t *desc,
                                int width,
                                int height) {
    memset(desc, 0, sizeof(*desc));
    desc->format = NMO_PIXEL_FORMAT_32_ARGB8888;
    desc->width = width;
    desc->height = height;
    desc->bits_per_pixel = 32;
    desc->bytes_per_line = width * 4;
    desc->alpha_mask = 0xFF000000u;
    desc->red_mask = 0x00FF0000u;
    desc->green_mask = 0x0000FF00u;
    desc->blue_mask = 0x000000FFu;
}

int32_t nmo_image_calc_bytes_per_line(int32_t width,
                                      int32_t bits_per_pixel) {
    if (width <= 0 || bits_per_pixel <= 0) {
        return 0;
    }

    int64_t bits = (int64_t)width * (int64_t)bits_per_pixel;
    int64_t bytes = (bits + 7) / 8;
    bytes = (int64_t)nmo_align_dword((size_t)bytes);
    if (bytes > INT32_MAX) {
        return 0;
    }
    return (int32_t)bytes;
}

size_t nmo_image_calc_size(const nmo_image_desc_t *desc) {
    if (!desc || desc->height <= 0 || desc->bytes_per_line <= 0) {
        return 0;
    }

    return (size_t)desc->bytes_per_line * (size_t)desc->height;
}

/**
 * Read a little-endian pixel of 1-4 bytes from memory.
 */
static inline uint32_t read_pixel_le(const uint8_t *ptr, int bytes_per_pixel) {
    switch (bytes_per_pixel) {
        case 1: return (uint32_t)ptr[0];
        case 2: return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8);
        case 3: return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) |
                       ((uint32_t)ptr[2] << 16);
        case 4: {
            uint32_t v;
            memcpy(&v, ptr, sizeof(uint32_t));
            return v;
        }
        default: return 0;
    }
}

/**
 * Normalize a channel value extracted from a bit field to 0-255 range.
 * Uses rounding: (value * 255 + max/2) / max, where max = (1 << bits) - 1.
 */
static inline uint8_t normalize_channel(uint32_t raw_pixel,
                                         uint32_t mask,
                                         uint32_t lsb,
                                         uint32_t bit_count) {
    if (mask == 0 || bit_count == 0) {
        return 0;
    }
    uint32_t value = (raw_pixel & mask) >> lsb;
    if (bit_count >= 8) {
        return (uint8_t)(value >> (bit_count - 8));
    }
    uint32_t max = (1u << bit_count) - 1u;
    return (uint8_t)((value * 255u + max / 2u) / max);
}

nmo_status_t nmo_image_decode_interleaved_to_rgba32(
    const nmo_image_desc_t *desc,
    nmo_arena_t *arena,
    uint8_t **out_rgba,
    int *out_width,
    int *out_height)
{
    if (!desc || !arena || !out_rgba || !out_width || !out_height) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Initialize outputs so callers can check on error */
    *out_rgba = NULL;
    *out_width = 0;
    *out_height = 0;

    /* Reject unsupported format categories */
    if (nmo_pixel_format_is_dxt(desc->format)) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    if (nmo_pixel_format_is_bump(desc->format)) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    if (desc->color_map_entries > 0) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    if (desc->width <= 0 || desc->height <= 0 || !desc->image_data) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int bpp = desc->bits_per_pixel;
    if (bpp <= 0 || bpp > 32) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    int bytes_per_pixel = bpp / 8;
    if ((bpp % 8) != 0) {
        bytes_per_pixel += 1;
    }
    if (bytes_per_pixel <= 0 || bytes_per_pixel > 4) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    /* Compute row stride */
    int32_t row_stride = desc->bytes_per_line;
    if (row_stride <= 0) {
        row_stride = nmo_image_calc_bytes_per_line(desc->width, bpp);
    }
    if (row_stride <= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Allocate output buffer */
    size_t pixel_count = (size_t)desc->width * (size_t)desc->height;
    if (pixel_count > SIZE_MAX / 4u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    uint8_t *rgba = (uint8_t *)nmo_arena_alloc(arena, pixel_count * 4, 16);
    if (!rgba) {
        return NMO_ERR_NOMEM;
    }

    /* Compute mask shifts and bit counts */
    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(
        desc->red_mask, desc->green_mask,
        desc->blue_mask, desc->alpha_mask, &shifts);

    /* Extract bit counts from shift info: lsb + bits = lsb + (8 - msb_shift)
     * The msb_shift stored is 8 - bit_count (or 0 if bit_count >= 8). */
    uint32_t r_bits = (shifts.red_shift_msb < 8u)   ? (8u - shifts.red_shift_msb)   : 0u;
    uint32_t g_bits = (shifts.green_shift_msb < 8u)  ? (8u - shifts.green_shift_msb) : 0u;
    uint32_t b_bits = (shifts.blue_shift_msb < 8u)   ? (8u - shifts.blue_shift_msb)  : 0u;
    uint32_t a_bits = (shifts.alpha_shift_msb < 8u)  ? (8u - shifts.alpha_shift_msb) : 0u;

    const bool has_alpha = desc->alpha_mask != 0;

    for (int y = 0; y < desc->height; ++y) {
        const uint8_t *row = desc->image_data + (size_t)y * (size_t)row_stride;
        for (int x = 0; x < desc->width; ++x) {
            const uint8_t *pp = row + (size_t)x * (size_t)bytes_per_pixel;
            uint32_t raw = read_pixel_le(pp, bytes_per_pixel);
            size_t idx = ((size_t)y * (size_t)desc->width + (size_t)x) * 4;

            rgba[idx + 0] = normalize_channel(raw, desc->red_mask,
                                               shifts.red_shift_lsb, r_bits);
            rgba[idx + 1] = normalize_channel(raw, desc->green_mask,
                                               shifts.green_shift_lsb, g_bits);
            rgba[idx + 2] = normalize_channel(raw, desc->blue_mask,
                                               shifts.blue_shift_lsb, b_bits);
            rgba[idx + 3] = has_alpha
                ? normalize_channel(raw, desc->alpha_mask,
                                     shifts.alpha_shift_lsb, a_bits)
                : 0xFFu;
        }
    }

    *out_rgba = rgba;
    *out_width = desc->width;
    *out_height = desc->height;
    return NMO_OK;
}

nmo_status_t nmo_image_reconstruct_pixels(
    const uint8_t *red, const uint8_t *green,
    const uint8_t *blue, const uint8_t *alpha,
    uint32_t red_size, uint32_t green_size,
    uint32_t blue_size, uint32_t alpha_size,
    int width, int height, int bpp,
    nmo_arena_t *arena,
    uint8_t **out_pixels, int *out_channels)
{
    if (!out_pixels || !out_channels || width <= 0 || height <= 0 || !arena) {
        return NMO_ERR_INVALID_FORMAT;
    }

    /* Only handle 32bpp (1 byte per channel) for now */
    if (bpp != 32) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    bool has_red   = (red   && red_size   >= pixel_count);
    bool has_green = (green && green_size >= pixel_count);
    bool has_blue  = (blue  && blue_size  >= pixel_count);
    bool has_alpha = (alpha && alpha_size >= pixel_count);

    if (!has_red && !has_green && !has_blue) {
        return NMO_ERR_INVALID_STATE;
    }

    uint8_t *rgba = (uint8_t *)nmo_arena_alloc(arena, pixel_count * 4, 1);
    if (!rgba) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4 + 0] = has_red   ? red[i]   : 0;
        rgba[i * 4 + 1] = has_green ? green[i]  : 0;
        rgba[i * 4 + 2] = has_blue  ? blue[i]   : 0;
        rgba[i * 4 + 3] = has_alpha ? alpha[i]  : 255;
    }

    *out_pixels = rgba;
    *out_channels = 4;
    return NMO_OK;
}

/* =========================================================================
 * DXT (S3TC) decompression
 * ========================================================================= */

/**
 * Expand an RGB565 color to RGB888 using full-range normalization.
 */
static void dxt_decode_rgb565(uint16_t c, uint8_t out[3]) {
    /* R: bits 15-11 (5 bits), G: bits 10-5 (6 bits), B: bits 4-0 (5 bits) */
    uint32_t r5 = (c >> 11) & 0x1Fu;
    uint32_t g6 = (c >> 5)  & 0x3Fu;
    uint32_t b5 = c & 0x1Fu;
    out[0] = (uint8_t)((r5 * 255u + 15u) / 31u);
    out[1] = (uint8_t)((g6 * 255u + 31u) / 63u);
    out[2] = (uint8_t)((b5 * 255u + 15u) / 31u);
}

/**
 * Decode one DXT1 color block (8 bytes) into RGBA pixels.
 *
 * @param src       Pointer to 8-byte DXT1 block
 * @param dest      Pointer to top-left pixel in output RGBA buffer
 * @param dest_stride Byte stride between output rows (width * 4)
 * @param bw        Number of valid columns in this block (1-4)
 * @param bh        Number of valid rows in this block (1-4)
 * @param has_alpha If true, enable DXT1 transparency (c0 <= c1 mode)
 */
static void decode_dxt1_block(const uint8_t *src,
                               uint8_t *dest,
                               int dest_stride,
                               int bw, int bh,
                               bool has_alpha) {
    uint16_t c0 = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
    uint16_t c1 = (uint16_t)src[2] | ((uint16_t)src[3] << 8);

    uint8_t colors[4][4]; /* [index][R,G,B,A] */

    dxt_decode_rgb565(c0, colors[0]);
    colors[0][3] = 255;

    dxt_decode_rgb565(c1, colors[1]);
    colors[1][3] = 255;

    if (!has_alpha || c0 > c1) {
        /* 4-color mode: 1/3 and 2/3 interpolation */
        for (int ch = 0; ch < 3; ch++) {
            colors[2][ch] = (uint8_t)((2u * colors[0][ch] + colors[1][ch] + 1u) / 3u);
            colors[3][ch] = (uint8_t)((colors[0][ch] + 2u * colors[1][ch] + 1u) / 3u);
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
    } else {
        /* 3-color + transparent mode */
        for (int ch = 0; ch < 3; ch++) {
            colors[2][ch] = (uint8_t)((colors[0][ch] + colors[1][ch]) / 2u);
        }
        colors[2][3] = 255;
        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0;
    }

    uint32_t indices = (uint32_t)src[4] | ((uint32_t)src[5] << 8) |
                       ((uint32_t)src[6] << 16) | ((uint32_t)src[7] << 24);

    for (int y = 0; y < bh; y++) {
        uint8_t *row = dest + y * dest_stride;
        for (int x = 0; x < bw; x++) {
            int bit_pos = (y * 4 + x) * 2;
            int idx = (int)((indices >> bit_pos) & 0x3u);
            row[x * 4 + 0] = colors[idx][0];
            row[x * 4 + 1] = colors[idx][1];
            row[x * 4 + 2] = colors[idx][2];
            row[x * 4 + 3] = colors[idx][3];
        }
    }
}

/**
 * Decode DXT3 explicit 4-bit alpha block (8 bytes) into the alpha channel.
 */
static void decode_dxt3_alpha_block(const uint8_t *src,
                                     uint8_t *dest,
                                     int dest_stride,
                                     int bw, int bh) {
    for (int y = 0; y < bh; y++) {
        uint8_t *row = dest + y * dest_stride;
        /* 2 bytes per row of 4 pixels, each nibble is one alpha */
        uint16_t row_alpha = (uint16_t)src[y * 2] | ((uint16_t)src[y * 2 + 1] << 8);
        for (int x = 0; x < bw; x++) {
            uint32_t a4 = (row_alpha >> (x * 4)) & 0xFu;
            /* Expand 4-bit to 8-bit: a * 255 / 15 = a * 17 */
            row[x * 4 + 3] = (uint8_t)(a4 * 17u);
        }
    }
}

/**
 * Decode DXT5 interpolated alpha block (8 bytes) into the alpha channel.
 */
static void decode_dxt5_alpha_block(const uint8_t *src,
                                     uint8_t *dest,
                                     int dest_stride,
                                     int bw, int bh) {
    uint8_t a0 = src[0];
    uint8_t a1 = src[1];

    /* Build 8-entry alpha palette */
    uint8_t alphas[8];
    alphas[0] = a0;
    alphas[1] = a1;

    if (a0 > a1) {
        /* 8 alpha values: 6 interpolated */
        alphas[2] = (uint8_t)((6u * a0 + 1u * a1 + 3u) / 7u);
        alphas[3] = (uint8_t)((5u * a0 + 2u * a1 + 3u) / 7u);
        alphas[4] = (uint8_t)((4u * a0 + 3u * a1 + 3u) / 7u);
        alphas[5] = (uint8_t)((3u * a0 + 4u * a1 + 3u) / 7u);
        alphas[6] = (uint8_t)((2u * a0 + 5u * a1 + 3u) / 7u);
        alphas[7] = (uint8_t)((1u * a0 + 6u * a1 + 3u) / 7u);
    } else {
        /* 6 alpha values: 4 interpolated + 0 and 255 */
        alphas[2] = (uint8_t)((4u * a0 + 1u * a1 + 2u) / 5u);
        alphas[3] = (uint8_t)((3u * a0 + 2u * a1 + 2u) / 5u);
        alphas[4] = (uint8_t)((2u * a0 + 3u * a1 + 2u) / 5u);
        alphas[5] = (uint8_t)((1u * a0 + 4u * a1 + 2u) / 5u);
        alphas[6] = 0;
        alphas[7] = 255;
    }

    /* Extract 48-bit index field (6 bytes at src+2) into a 64-bit value */
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) {
        bits |= (uint64_t)src[2 + i] << (i * 8);
    }

    for (int y = 0; y < bh; y++) {
        uint8_t *row = dest + y * dest_stride;
        for (int x = 0; x < bw; x++) {
            int bit_pos = (y * 4 + x) * 3;
            int idx = (int)((bits >> bit_pos) & 0x7u);
            row[x * 4 + 3] = alphas[idx];
        }
    }
}

nmo_status_t nmo_image_decode_dxt(
    const uint8_t *data, size_t data_size,
    int width, int height,
    nmo_pixel_format_t format,
    nmo_arena_t *arena,
    uint8_t **out_rgba)
{
    if (!data || !arena || !out_rgba) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_rgba = NULL;

    if (width <= 0 || height <= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!nmo_pixel_format_is_dxt(format)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    /* Block size: DXT1 = 8 bytes, DXT2-5 = 16 bytes */
    size_t block_bytes = (format == NMO_PIXEL_FORMAT_DXT1) ? 8u : 16u;

    /* Block counts (round up to multiples of 4) */
    size_t bx = ((size_t)width + 3u) / 4u;
    size_t by = ((size_t)height + 3u) / 4u;

    /* Validate data size */
    size_t required = bx * by * block_bytes;
    if (data_size < required) {
        return NMO_ERR_INVALID_FORMAT;
    }

    /* Check output size doesn't overflow */
    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / 4u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint8_t *rgba = (uint8_t *)nmo_arena_alloc(arena, pixel_count * 4u, 16);
    if (!rgba) {
        return NMO_ERR_NOMEM;
    }

    /* Zero-fill for safety (edges of non-multiple-of-4 images) */
    memset(rgba, 0, pixel_count * 4u);

    int dest_stride = width * 4;
    bool is_premul = (format == NMO_PIXEL_FORMAT_DXT2 ||
                      format == NMO_PIXEL_FORMAT_DXT4);

    const uint8_t *src = data;
    for (size_t block_y = 0; block_y < by; block_y++) {
        for (size_t block_x = 0; block_x < bx; block_x++) {
            /* Clamp block dimensions at image edge */
            int bw = (int)(width - (int)(block_x * 4u));
            if (bw > 4) bw = 4;
            int bh = (int)(height - (int)(block_y * 4u));
            if (bh > 4) bh = 4;

            uint8_t *dest = rgba + (block_y * 4u * (size_t)dest_stride) +
                            (block_x * 4u * 4u);

            switch (format) {
                case NMO_PIXEL_FORMAT_DXT1:
                    decode_dxt1_block(src, dest, dest_stride, bw, bh, true);
                    src += 8;
                    break;

                case NMO_PIXEL_FORMAT_DXT2:
                case NMO_PIXEL_FORMAT_DXT3:
                    /* First 8 bytes: explicit alpha, next 8: DXT1 color (4-color mode) */
                    decode_dxt1_block(src + 8, dest, dest_stride, bw, bh, false);
                    decode_dxt3_alpha_block(src, dest, dest_stride, bw, bh);
                    src += 16;
                    break;

                case NMO_PIXEL_FORMAT_DXT4:
                case NMO_PIXEL_FORMAT_DXT5:
                    /* First 8 bytes: interpolated alpha, next 8: DXT1 color (4-color mode) */
                    decode_dxt1_block(src + 8, dest, dest_stride, bw, bh, false);
                    decode_dxt5_alpha_block(src, dest, dest_stride, bw, bh);
                    src += 16;
                    break;

                default:
                    return NMO_ERR_INVALID_FORMAT;
            }
        }
    }

    /* Convert premultiplied alpha to straight alpha for DXT2/DXT4 */
    if (is_premul) {
        for (size_t i = 0; i < pixel_count; i++) {
            uint8_t a = rgba[i * 4 + 3];
            if (a > 0 && a < 255) {
                for (int ch = 0; ch < 3; ch++) {
                    uint32_t v = (uint32_t)rgba[i * 4 + (size_t)ch] * 255u / (uint32_t)a;
                    rgba[i * 4 + (size_t)ch] = (uint8_t)(v > 255u ? 255u : v);
                }
            } else if (a == 0) {
                rgba[i * 4 + 0] = 0;
                rgba[i * 4 + 1] = 0;
                rgba[i * 4 + 2] = 0;
            }
        }
    }

    *out_rgba = rgba;
    return NMO_OK;
}
