/**
 * @file nmo_render_soft.h
 * @brief The software backend of nmo_render: frames drawn in memory, with
 *        no graphics device and no window (headless runs, tests, a
 *        reference for the other backends).
 */

#ifndef NMO_RENDER_SOFT_H
#define NMO_RENDER_SOFT_H

#include "render/nmo_render.h"

#define NMO_RENDER_SOFT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RENDER_SOFT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A renderer drawing into memory. nmo_render_draw() draws into a screen of
 * the size it is given, which nmo_render_screenshot() saves and
 * nmo_render_soft_pixels() reads.
 */
NMO_API nmo_status_t nmo_render_create_soft(nmo_engine_t *engine, nmo_render_t **out_render);

/**
 * The screen as drawn last: RGBA rows top first, `*out_width` x
 * `*out_height`; the renderer's until it draws again. NULL before a draw.
 */
NMO_API const uint8_t *nmo_render_soft_pixels(const nmo_render_t *render, int *out_width,
                                              int *out_height);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RENDER_SOFT_H */
