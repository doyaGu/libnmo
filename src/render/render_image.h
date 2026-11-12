/**
 * @file render_image.h
 * @brief What a texture's RGBA image needs before a device takes it: its
 *        mipmap levels made as CK would want them.
 */

#ifndef NMO_RENDER_IMAGE_H
#define NMO_RENDER_IMAGE_H

#include "engine/nmo_engine_render.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The next mipmap level of an RGBA image: each texel the mean of the up to 2 x 2
 * it covers, its color weighted by their alpha, so that the clear texels of a
 * transparent color lend it nothing (a box filter would darken the edges by them).
 * With `binary` (an image whose alpha is 0 or 255, a transparent color's) the
 * alpha stays so: a texel is there when at least half of what it covers is, as
 * the alpha test would have it, so no partly clear edge widens the shape.
 * Malloc'ed; NULL when out of memory.
 */
uint8_t *render_half_image(const uint8_t *rgba, int width, int height, bool binary, int *out_w,
                           int *out_h);
/* Whether every texel's alpha is 0 or 255 */
bool render_image_is_binary(const uint8_t *rgba, int width, int height);
/* Whether a texel is clear (a transparent color's) */
bool render_image_has_clear_texels(const nmo_engine_image_t *image);

#endif /* NMO_RENDER_IMAGE_H */
