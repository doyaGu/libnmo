/**
 * @file nmo_color.h
 * @brief RGBA color utilities (core layer)
 */

#ifndef NMO_COLOR_H
#define NMO_COLOR_H

#include "nmo_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGBA color (float components)
 *
 * Components are expected in the range [0, 1].
 */
typedef struct nmo_color {
    float r;
    float g;
    float b;
    float a;
} nmo_color_t;

/**
 * @brief Convert packed ARGB (0xAARRGGBB) into a float color.
 */
NMO_API void nmo_color_from_argb32(uint32_t argb, nmo_color_t *out_color);

/**
 * @brief Convert float color into packed ARGB (0xAARRGGBB).
 *
 * Values are clamped to [0, 1] and rounded to nearest byte.
 */
NMO_API uint32_t nmo_color_to_argb32(const nmo_color_t *color);

/**
 * @brief Convert float color into packed ARGB (0xFFRRGGBB), forcing alpha to 0xFF.
 */
NMO_API uint32_t nmo_color_to_argb32_opaque(const nmo_color_t *color);

#ifdef __cplusplus
}
#endif

#endif /* NMO_COLOR_H */
