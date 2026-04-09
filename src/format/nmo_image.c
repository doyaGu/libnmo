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
