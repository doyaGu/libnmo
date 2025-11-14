/**
 * @file nmo_image.c
 * @brief Image descriptor helpers and mask utilities.
 */

#include "format/nmo_image.h"

#include <limits.h>
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
    bytes = (bytes + 3) & ~3;
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
