/**
 * @file render_image.c
 * @brief Mipmap levels of a texture's image, made as CK would want them.
 */

#include "render_image.h"

#include <stdlib.h>

uint8_t *render_half_image(const uint8_t *rgba, int width, int height, bool binary, int *out_w,
                           int *out_h)
{
    int w = width > 1 ? width / 2 : 1, h = height > 1 ? height / 2 : 1;
    uint8_t *half = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (half == NULL) {
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned sum[4] = {0, 0, 0, 0}, plain[3] = {0, 0, 0}, count = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int sx = x * 2 + dx < width ? x * 2 + dx : width - 1;
                    int sy = y * 2 + dy < height ? y * 2 + dy : height - 1;
                    const uint8_t *p = &rgba[((size_t)sy * (size_t)width + (size_t)sx) * 4u];
                    for (int c = 0; c < 3; c++) {
                        sum[c] += (unsigned)p[c] * p[3];
                        plain[c] += p[c];
                    }
                    sum[3] += p[3];
                    count++;
                }
            }
            uint8_t *q = &half[((size_t)y * (size_t)w + (size_t)x) * 4u];
            for (int c = 0; c < 3; c++) {
                q[c] = (uint8_t)(sum[3] > 0 ? (sum[c] + sum[3] / 2u) / sum[3]
                                            : (plain[c] + count / 2u) / count);
            }
            q[3] = (uint8_t)((sum[3] + count / 2u) / count);
            if (binary) {
                q[3] = q[3] >= 128 ? 255 : 0;
            }
        }
    }
    *out_w = w;
    *out_h = h;
    return half;
}

bool render_image_is_binary(const uint8_t *rgba, int width, int height)
{
    bool binary = true;
    for (size_t i = 0; binary && i < (size_t)width * (size_t)height; i++) {
        binary = rgba[i * 4 + 3] == 0 || rgba[i * 4 + 3] == 255;
    }
    return binary;
}

bool render_image_has_clear_texels(const nmo_engine_image_t *image)
{
    for (size_t i = 0; i < (size_t)image->width * (size_t)image->height; i++) {
        if (image->rgba[i * 4 + 3] == 0) {
            return true;
        }
    }
    return false;
}
