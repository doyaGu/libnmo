#include "core/nmo_color.h"

#include <stddef.h>

static float nmo_color_clamp01(float value) {
    /* Treat NaN as 0.0 */
    if (!(value == value)) {
        return 0.0f;
    }
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static uint8_t nmo_color_to_u8(float value) {
    float clamped = nmo_color_clamp01(value);
    int rounded = (int)(clamped * 255.0f + 0.5f);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > 255) {
        return 255;
    }
    return (uint8_t)rounded;
}

void nmo_color_from_argb32(uint32_t argb, nmo_color_t *out_color) {
    if (out_color == NULL) {
        return;
    }

    out_color->a = (float)((argb >> 24) & 0xFFu) / 255.0f;
    out_color->r = (float)((argb >> 16) & 0xFFu) / 255.0f;
    out_color->g = (float)((argb >> 8) & 0xFFu) / 255.0f;
    out_color->b = (float)(argb & 0xFFu) / 255.0f;
}

uint32_t nmo_color_to_argb32(const nmo_color_t *color) {
    if (color == NULL) {
        return 0;
    }

    uint8_t a = nmo_color_to_u8(color->a);
    uint8_t r = nmo_color_to_u8(color->r);
    uint8_t g = nmo_color_to_u8(color->g);
    uint8_t b = nmo_color_to_u8(color->b);

    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t nmo_color_to_argb32_opaque(const nmo_color_t *color) {
    if (color == NULL) {
        return 0xFF000000u;
    }

    uint8_t r = nmo_color_to_u8(color->r);
    uint8_t g = nmo_color_to_u8(color->g);
    uint8_t b = nmo_color_to_u8(color->b);

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
