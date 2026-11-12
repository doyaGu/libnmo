/**
 * @file render_text.h
 * @brief Sprite texts (CKSpriteText) rasterized with a TrueType font
 *        through stb_truetype, kept while frames draw them.
 */

#ifndef NMO_RENDER_TEXT_H
#define NMO_RENDER_TEXT_H

#include "render/nmo_render.h"
#include "engine/nmo_engine_render.h"

#include <stdint.h>

typedef struct render_text render_text_t;

/* A sprite text's pixels, and what a backend made of them */
typedef struct render_text_sprite {
    int width, height;
    unsigned char *rgba;       /* RGBA, rows top first */
    uintptr_t handle;          /* the backend's image of it; 0 until it makes one */
} render_text_sprite_t;

/* A backend's image of a sprite goes */
typedef void (*render_text_release_fn)(void *user_data, uintptr_t handle);

render_text_t *render_text_open(void);
void render_text_close(render_text_t *text, render_text_release_fn release, void *user_data);
/* Where fonts come from; NULL for the system's font folders */
void render_text_set_font_loader(render_text_t *text, nmo_render_font_loader_fn loader,
                                 void *user_data);
/*
 * The sprite of a sprite text draw: its pixels, made the first time a frame
 * draws it; NULL when its font is nowhere. Valid until the next call.
 */
render_text_sprite_t *render_text_sprite(render_text_t *text, const nmo_engine_draw_t *draw);
/* The end of a frame: the sprites no frame drew for a while go. */
void render_text_end_frame(render_text_t *text, render_text_release_fn release, void *user_data);

/* render_font.c: the face's file in the system's font folders */
unsigned char *render_font_system(void *user_data, const char *face, bool bold, bool italic);

#endif /* NMO_RENDER_TEXT_H */
