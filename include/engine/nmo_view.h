/**
 * @file nmo_view.h
 * @brief A window that shows what the engine's render context draws and
 *        feeds it the keyboard and mouse (SDL3 and OpenGL).
 */

#ifndef NMO_VIEW_H
#define NMO_VIEW_H

#include "engine/nmo_engine.h"

#define NMO_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_VIEW_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * The view draws nmo_engine_render_frame() with OpenGL's fixed function
 * pipeline, as CK2's rasterizer would: the frame is the render context's
 * size, scaled to the window. Keys become DirectInput scan codes for the
 * engine, the mouse moves in render context pixels, and the 2D entity under
 * it is the topmost one drawn there.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_view nmo_view_t;

/** What a view draws with */
typedef enum nmo_view_renderer {
    NMO_VIEW_RENDERER_GL = 0,  /* an OpenGL context: nmo_render_gl */
    NMO_VIEW_RENDERER_GPU = 1, /* an SDL GPU device (Metal or Vulkan): nmo_render_gpu */
} nmo_view_renderer_t;

/** Open a window `scale` times the size of the render context, drawn with OpenGL. */
NMO_API nmo_status_t nmo_view_open(nmo_engine_t *engine, const char *title, int scale,
                                   nmo_view_t **out_view);
/** Open a window drawn with `renderer`. */
NMO_API nmo_status_t nmo_view_open_with(nmo_engine_t *engine, const char *title, int scale,
                                        nmo_view_renderer_t renderer, nmo_view_t **out_view);
NMO_API void nmo_view_close(nmo_view_t *view);
/** Pass the window's input to the engine; false once the window is closed. */
NMO_API bool nmo_view_poll(nmo_view_t *view);
/** Draw the engine's frame and show it. */
NMO_API nmo_status_t nmo_view_draw(nmo_view_t *view);
/**
 * Play the engine's voices (nmo_engine_voices) on the default audio
 * device: each starts, follows its gain, pitch and pan, and stops as the
 * engine has it. Nothing plays when there is no device.
 */
NMO_API void nmo_view_play_sounds(nmo_view_t *view);
/**
 * Mipmap every texture but cutouts (`all`: those with clear texels, a
 * transparent color's, keep CK's sampling), or only those CK mipmaps (the
 * default: a texture whose mipmap level is not 0, as Ballance has none).
 */
NMO_API void nmo_view_set_mipmaps(nmo_view_t *view, bool all);
/** Save what the window shows as a PNG file. */
NMO_API nmo_status_t nmo_view_screenshot(nmo_view_t *view, const char *path);
/** Wait `ms` milliseconds (a frame rate limit). */
NMO_API void nmo_view_wait(const nmo_view_t *view, double ms);
/** Milliseconds since the view opened, by the system clock. */
NMO_API double nmo_view_time(const nmo_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VIEW_H */
