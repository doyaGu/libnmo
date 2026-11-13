/**
 * @file nmo_render_gl3.h
 * @brief The GL3 backend of nmo_render: render_backend.h drawn with one
 *        shader (shared with nmo_render_gpu's, compiled to GLSL) and
 *        buffer objects, on OpenGL 3.3 core, OpenGL ES 3.0, or WebGL 2
 *        (through a build's loader of the current context's functions).
 */

#ifndef NMO_RENDER_GL3_H
#define NMO_RENDER_GL3_H

#include "render/nmo_render.h"

#define NMO_RENDER_GL3_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RENDER_GL3_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/** A function of a GL3/ES3/WebGL2 context, cast to one type */
typedef void (*nmo_render_gl3_function_t)(void);

/**
 * The function of the current context with this name, NULL when it has
 * none: SDL_GL_GetProcAddress and its like.
 */
typedef nmo_render_gl3_function_t (*nmo_render_gl3_loader_t)(const char *name);

/**
 * A renderer drawing with the context current now: OpenGL 3.3 core, OpenGL
 * ES 3.0, or WebGL 2 (through Emscripten). It picks its shader dialect from
 * GL_VERSION (a string starting "OpenGL ES" takes the ES 3.00 shaders, else
 * the 3.30 core ones) and takes every function beyond what every dialect
 * shares from `load`. NMO_ERR_NOT_SUPPORTED when the context is too old.
 * The context must stay current while the renderer draws, and until it is
 * destroyed.
 *
 * Known gaps, none hit by Ballance: no MSAA, no border/mirror-once address
 * mode's own GL state (the shader paints it), POINT and LINE fill draw
 * solid on OpenGL ES and WebGL (no glPolygonMode there), and a render
 * target's unchanged cube faces are redrawn rather than kept.
 */
NMO_API nmo_status_t nmo_render_create_gl3(nmo_engine_t *engine, nmo_render_gl3_loader_t load,
                                           nmo_render_t **out_render);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RENDER_GL3_H */
