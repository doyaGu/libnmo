/**
 * @file render_gl3.h
 * @brief The GL3 backend on its own, for a test that drives
 *        render_backend.h directly.
 */

#ifndef NMO_RENDER_GL3_BACKEND_H
#define NMO_RENDER_GL3_BACKEND_H

#include "../render_backend.h"

/*
 * A GL3 backend with a hidden window and context of its own (SDL3); NULL
 * when none can be made (no driver, no display). Its destroy op frees the
 * window and context too.
 */
render_backend_t *render_gl3_backend_create_offscreen(void);

#endif /* NMO_RENDER_GL3_BACKEND_H */
