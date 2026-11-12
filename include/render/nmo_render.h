/**
 * @file nmo_render.h
 * @brief Draws what the engine's render context would draw, through a
 *        backend chosen at creation (nmo_render_gl.h for OpenGL).
 */

#ifndef NMO_RENDER_H
#define NMO_RENDER_H

#include "engine/nmo_engine.h"

#define NMO_RENDER_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RENDER_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * A renderer draws nmo_engine_render_frame() into a target the host owns (a
 * window's back buffer, say) the way CK2's rasterizer would: the frame is
 * the render context's size, scaled to the target and centered in it. It
 * knows no window system: the host makes the target current, gives its
 * size in pixels, and shows what was drawn.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_render nmo_render_t;

/*
 * The TrueType file of a font face in a style, for the sprite texts
 * (CreateFont's pick); NULL when there is none. The data is malloc'ed: the
 * renderer frees it.
 */
typedef unsigned char *(*nmo_render_font_loader_fn)(void *user_data, const char *face, bool bold,
                                                     bool italic);

/** Free a renderer and what its backend made. */
NMO_API void nmo_render_destroy(nmo_render_t *render);

/**
 * Mipmap every texture but cutouts (`all`: those with clear texels, a
 * transparent color's, keep CK's sampling), or only those CK mipmaps (the
 * default: a texture whose mipmap level is not 0, as Ballance has none).
 */
NMO_API void nmo_render_set_mipmaps(nmo_render_t *render, bool all);

/**
 * Where sprite texts find their fonts. Without a loader (NULL) the system's
 * font folders are searched for "Face Bold Italic.ttf" and the like, then
 * "Face.ttf".
 */
NMO_API void nmo_render_set_font_loader(nmo_render_t *render, nmo_render_font_loader_fn loader,
                                        void *user_data);

/** Draw the engine's frame into a target of `width` x `height` pixels. */
NMO_API nmo_status_t nmo_render_draw(nmo_render_t *render, int width, int height);

/**
 * Tell the engine which 2D entity of the frame drawn last is under the
 * mouse: the topmost one drawn there (Pick2D, for the font manager).
 */
NMO_API void nmo_render_pick(nmo_render_t *render);

/**
 * A point of the target (pixels from its top left) in render context
 * pixels, as the frame drawn last shows there.
 */
NMO_API void nmo_render_target_to_frame(const nmo_render_t *render, float x, float y,
                                        float *out_x, float *out_y);

/** The render context of the frame drawn last (640 x 480 before any): its own size. */
NMO_API void nmo_render_frame_size(const nmo_render_t *render, int *out_width, int *out_height);

/**
 * Draw the frame into a target of `width` x `height` pixels and save the
 * part of it the frame covers as a PNG file. The target is read before the
 * host shows it.
 */
NMO_API nmo_status_t nmo_render_screenshot(nmo_render_t *render, int width, int height,
                                           const char *path);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RENDER_H */
