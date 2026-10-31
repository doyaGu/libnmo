/**
 * @file view_text.h
 * @brief Sprite texts (CKSpriteText) drawn with a system font through stb_truetype.
 */

#ifndef NMO_VIEW_TEXT_H
#define NMO_VIEW_TEXT_H

#include "engine/nmo_engine_render.h"

typedef struct view_text view_text_t;

view_text_t *view_text_open(void);
void view_text_close(view_text_t *text);
/* The OpenGL texture of a sprite text draw, its sprite's pixels; 0 when its font is nowhere. */
unsigned int view_text_texture(view_text_t *text, const nmo_engine_draw_t *draw);
/* The end of a frame: the textures no frame asked for a while go. */
void view_text_end_frame(view_text_t *text);

#endif /* NMO_VIEW_TEXT_H */
