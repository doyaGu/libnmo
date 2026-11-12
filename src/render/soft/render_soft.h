/**
 * @file render_soft.h
 * @brief The software backend on its own, for a renderer or a test that
 *        drives render_backend.h directly.
 */

#ifndef NMO_RENDER_SOFT_BACKEND_H
#define NMO_RENDER_SOFT_BACKEND_H

#include "../render_backend.h"

/* A software backend; NULL when out of memory. Its destroy op frees it. */
render_backend_t *render_soft_backend_create(void);
/* Its screen as drawn last: RGBA rows top first; NULL before it has one */
const uint8_t *render_soft_backend_screen(render_backend_t *backend, int32_t *out_width,
                                          int32_t *out_height);

#endif /* NMO_RENDER_SOFT_BACKEND_H */
