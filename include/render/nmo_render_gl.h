/**
 * @file nmo_render_gl.h
 * @brief The OpenGL backend of nmo_render: the fixed function pipeline,
 *        set up the way CK2's rasterizer sets up Direct3D 7.
 */

#ifndef NMO_RENDER_GL_H
#define NMO_RENDER_GL_H

#include "render/nmo_render.h"

#define NMO_RENDER_GL_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RENDER_GL_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A renderer drawing with the OpenGL context current now (2.1 with the
 * framebuffer object extension, a depth and a stencil buffer). The context
 * must stay current while the renderer draws, and until it is destroyed.
 */
NMO_API nmo_status_t nmo_render_create_gl(nmo_engine_t *engine, nmo_render_t **out_render);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RENDER_GL_H */
