/**
 * @file interface.c
 * @brief Ports of the font, text and push button building blocks.
 *
 * Each function does what the block's function in Interface.dll does. The
 * font manager keeps fonts by name, numbered from 1 (CKFontManager's
 * CreateTextureFont, GetFont, DeleteFont), each character's place in its
 * texture as CKTextureFont finds it. 2D Text lays its text out as
 * CKTextureFont::DrawCKText does when its 2D entity draws: a quad a
 * character in the render frame. System fonts are kept but do not draw. PushButton tests the mouse against the 2D entity the
 * host says is under it, and 2D Picking asks the host's picker.
 */

#include "virtools.h"

#include "object/builtin/nmo_texture_schemas.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Fonts
 * ============================================================================ */

/* CKTextureFont's properties (Set Font Properties) and text options (2D Text) */
#define FONT_GRADIENT 1
#define FONT_SHADOW 2
#define FONT_DISABLEFILTER 8
#define TEXT_SCREEN 1
#define TEXT_BACKGROUND 2
#define TEXT_CLIP 4
#define TEXT_WORDWRAP 32
#define TEXT_JUSTIFIED 64
#define TEXT_MULTIPLE 256 /* draw once each time On is activated */
#define TEXT_SCREENCLIP 2048
#define VALIGN_TOP 4
#define VALIGN_BOTTOM 8
#define HALIGN_LEFT 1
#define HALIGN_RIGHT 2

/* Where a character is in a font's texture (CharacterTextureCoordinates), in UVs */
typedef struct eng_glyph {
    float ustart, vstart;
    float uwidth, uprewidth, upostwidth;
    float vwidth;
} eng_glyph_t;

/* A CKTextureFont: its characters in its texture, and how Set Font Properties draws them */
typedef struct eng_font {
    char *name;
    nmo_object_id_t texture; /* the texture it reads, 0 for a system font's own */
    bool created;            /* SpacingProperties CREATED: it draws */
    float char_number[2];
    int32_t first_character;
    nmo_rect_t zone;         /* the part of the texture the characters are in, in pixels */
    float space_percentage;
    eng_glyph_t glyphs[256];
    float leading[2], scale[2], italic;
    uint32_t start_color, end_color, shadow_color;
    float shadow_offset[2], shadow_scale[2];
    nmo_object_id_t material;
    uint32_t properties;     /* FONT_GRADIENT, FONT_SHADOW, FONT_LIGHTING, FONT_DISABLEFILTER */
} eng_font_t;

/*
 * Text a 2D Text block draws this frame: on its 2D entity after the entity
 * (a temporary post render callback), or with Multiple, as it was when the
 * block ran (the font manager's text list, drawn after the sprites).
 */
typedef struct eng_text {
    int32_t node;
    nmo_object_id_t entity;
    bool multiple;
    int32_t font, align, flags;
    char *string;
    nmo_rect_t zone;
    nmo_object_id_t material;
} eng_text_t;

/* A CKSpriteText a Text Display block made: what it shows and where */
typedef struct eng_sprite_text {
    int32_t node;
    bool shown;
    float x, y, width, height;
    char *text; /* NULL until it is drawn the first time */
    char *font; /* the face name tdFontInfo holds */
    int32_t size, weight, style, align;
    uint32_t color; /* as the text shows: CK passes ARGB where GDI takes ABGR */
} eng_sprite_text_t;

/* CKFontManager: the fonts by number; the texts 2D Text draws this frame; Text Display's sprite texts */
typedef struct font_manager {
    ENG_VEC(eng_font_t) fonts;
    ENG_VEC(eng_text_t) texts;
    ENG_VEC(eng_sprite_text_t) sprite_texts;
} font_manager_t;

static const eng_manager_desc_t font_manager;

static font_manager_t *fonts_of(const nmo_engine_t *engine)
{
    return (font_manager_t *)eng_manager_state(engine, &font_manager);
}

int32_t nmo_engine_font_count(const nmo_engine_t *engine)
{
    const font_manager_t *fm = fonts_of(engine);
    return fm != NULL ? (int32_t)fm->fonts.count : 0;
}

const char *nmo_engine_font_name(const nmo_engine_t *engine, int32_t font)
{
    const font_manager_t *fm = fonts_of(engine);
    if (fm == NULL || font <= 0 || (size_t)font > fm->fonts.count) {
        return NULL;
    }
    return fm->fonts.data[font - 1].name;
}

/* CKFontManager::GetFont: a font that was created */
static eng_font_t *font_get(nmo_engine_t *engine, int32_t index)
{
    font_manager_t *fm = fonts_of(engine);
    if (index <= 0 || (size_t)index > fm->fonts.count) {
        return NULL;
    }
    eng_font_t *font = &fm->fonts.data[index - 1];
    return font->created ? font : NULL;
}

/* Whether a font texture's pixel is part of a character: alpha, or not the transparent color */
static bool font_pixel_set(const nmo_engine_image_t *image, int x, int y, bool alpha,
                           uint32_t transparent)
{
    if (x < 0 || y < 0 || x >= image->width || y >= image->height) {
        return false;
    }
    const uint8_t *p = &image->rgba[((size_t)y * (size_t)image->width + (size_t)x) * 4];
    if (alpha) {
        return p[3] != 0;
    }
    uint32_t rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return rgb != (transparent & 0xFFFFFFu);
}

/*
 * CKTextureFont::CreateFromTexture: each character's cell, narrowed to its
 * set pixels for a proportional font (an empty one is a space, the space
 * percentage of a cell wide).
 */
static void font_create_from_texture(nmo_engine_t *engine, eng_font_t *font, bool fixed)
{
    nmo_engine_image_t image;
    if (!nmo_engine_texture_image(engine, font->texture, &image) || font->char_number[0] <= 0.0f ||
        font->char_number[1] <= 0.0f || font->first_character > 255) {
        return;
    }
    if (font->first_character < 0) {
        font->first_character = 0;
    }
    float tw = (float)image.width, th = (float)image.height;
    if ((int)(font->zone.right - font->zone.left) == 0 || (int)(font->zone.bottom - font->zone.top) == 0) {
        font->zone.left = font->zone.top = 0.0f;
        font->zone.right = tw;
        font->zone.bottom = th;
    }
    int chars_x = (int)ceilf(font->char_number[0]);
    int chars_y = (int)ceilf(font->char_number[1]);
    float ustep = (font->zone.right - font->zone.left) / (tw * (float)chars_x);
    float vstep = (font->zone.bottom - font->zone.top) / (th * (float)chars_y);
    float ustart = font->zone.left / tw, vstart = font->zone.top / th;
    for (int k = 0; k < 256; k++) {
        eng_glyph_t glyph = {ustart, vstart, ustep, 0.0f, 0.0f, vstep};
        font->glyphs[k] = glyph;
    }
    /* The font texture's own transparency: its transparent color, else its alpha */
    nmo_object_t *object = eng_find_object(engine, font->texture);
    const nmo_texture_state_t *state =
        object != NULL ? (const nmo_texture_state_t *)nmo_object_get_state(object) : NULL;
    bool alpha = state == NULL || !state->is_transparent;
    uint32_t transparent = state != NULL && state->has_transparent_color ? state->transparent_color : 0;
    float upixel = 1.0f / tw;
    int xwidth = (int)(tw * ustep), ywidth = (int)(th * vstep);
    int c = font->first_character;
    float v = vstart;
    int ypixel = (int)font->zone.top;
    for (int i = 0; i < chars_y && c < 256; i++) {
        float u = ustart;
        int xpixel = (int)font->zone.left;
        for (int j = 0; j < chars_x && c < 256; j++, c++, u += ustep, xpixel += xwidth) {
            eng_glyph_t *glyph = &font->glyphs[c];
            glyph->ustart = u;
            glyph->vstart = v;
            if (fixed) {
                continue;
            }
            int k = 0;
            for (; k < xwidth; k++) {
                int y = 0;
                while (y < ywidth && !font_pixel_set(&image, xpixel + k, ypixel + y, alpha, transparent)) {
                    y++;
                }
                if (y < ywidth) {
                    break;
                }
            }
            if (k == xwidth) {
                glyph->uwidth *= font->space_percentage;
                continue;
            }
            glyph->ustart += (float)k * upixel;
            glyph->uwidth -= (float)k * upixel;
            for (k = 0; k < xwidth; k++) {
                int y = 0;
                while (y < ywidth &&
                       !font_pixel_set(&image, xpixel + xwidth - 1 - k, ypixel + y, alpha, transparent)) {
                    y++;
                }
                if (y < ywidth) {
                    break;
                }
            }
            glyph->uwidth -= (float)k * upixel;
            if (glyph->uwidth < 0.0f) {
                glyph->uwidth = 0.0f;
            }
        }
        v += vstep;
        ypixel += ywidth;
    }
    if (font->first_character != 0) {
        font->glyphs[0].vwidth = vstep;
    }
    font->created = true;
}

/*
 * CKFontManager::CreateTextureFont: a font of that name is replaced where it
 * is. A system font (no texture) is kept by name but does not draw.
 */
static int32_t font_create(nmo_engine_t *engine, const char *name, nmo_object_id_t texture,
                           const nmo_rect_t *zone, const float char_number[2], bool fixed,
                           int32_t first_character, float space)
{
    font_manager_t *fm = fonts_of(engine);
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, name, length + 1);
    eng_font_t *font = NULL;
    for (size_t i = 0; i < fm->fonts.count && font == NULL; i++) {
        if (strcmp(fm->fonts.data[i].name, name) == 0) {
            font = &fm->fonts.data[i];
            free(font->name);
            memset(font, 0, sizeof(*font));
        }
    }
    if (font == NULL && !ENG_VEC_PUSH(fm->fonts, font)) {
        free(copy);
        return -1;
    }
    int32_t index = (int32_t)(font - fm->fonts.data) + 1;
    font->name = copy;
    font->texture = texture;
    font->scale[0] = font->scale[1] = 1.0f;
    font->shadow_offset[0] = font->shadow_offset[1] = 4.0f;
    font->shadow_scale[0] = font->shadow_scale[1] = 1.0f;
    font->start_color = 0xFFFFFFFFu;
    font->end_color = 0xFF000000u;
    font->shadow_color = 0x80000000u;
    if (texture != 0 && zone != NULL) {
        font->zone = *zone;
        font->char_number[0] = char_number[0];
        font->char_number[1] = char_number[1];
        font->first_character = first_character;
        font->space_percentage = space < 0.0f ? 0.0f : space;
        font_create_from_texture(engine, font, fixed);
    }
    return index;
}

/* CKFontManager::DeleteFont: the fonts after it move down one. */
static void font_delete(nmo_engine_t *engine, int32_t index)
{
    font_manager_t *fm = fonts_of(engine);
    if (index <= 0 || (size_t)index > fm->fonts.count) {
        return;
    }
    free(fm->fonts.data[index - 1].name);
    memmove(&fm->fonts.data[index - 1], &fm->fonts.data[index],
            (fm->fonts.count - (size_t)index) * sizeof(eng_font_t));
    fm->fonts.count--;
}

/* Create System Font: Success with the font, from a system font made into a texture. */
static int bb_create_system_font(blk_t *b)
{
    IN_OFF(b, 0);
    const char *name = input_string(b, 0);
    if (name == NULL || get_in_int(b, 1, -1) == -1) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t font = font_create(nmo_engine_block_engine(b), name, 0, NULL, NULL, false, 0, 0.0f);
    nmo_engine_block_set_output_value(b, 0, &font, sizeof(font));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Create Font and TT_CreateFontEx: the font of a texture's character grid,
 * proportional unless told otherwise. TT_CreateFontEx then takes each
 * character's coordinates from the rows of its FontCoordinatesData array.
 */
static int bb_create_font(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    const char *name = input_string(b, 0);
    if (name == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 1);
    nmo_engine_image_t image;
    if (texture == 0 || !nmo_engine_texture_image(engine, texture, &image)) {
        image.width = image.height = 0;
    }
    if (texture == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    float char_number[2] = {16.0f, 8.0f};
    nmo_engine_block_get_input_value(b, 2, char_number, sizeof(char_number));
    bool proportional = get_in_int(b, 3, 1) != 0;
    nmo_rect_t zone = {0.0f, 0.0f, (float)image.width, (float)image.height};
    nmo_engine_block_get_input_value(b, 4, &zone, sizeof(zone));
    int32_t first = get_in_int(b, 5, 0);
    bool extended = nmo_engine_block_input_parameter_count(b) > 6;
    float space = extended ? 0.3f : get_local_float(b, 0, 0.3f);
    int32_t font = font_create(engine, name, texture, &zone, char_number, !proportional, first, space);
    nmo_engine_block_set_output_value(b, 0, &font, sizeof(font));
    eng_font_t *created = font > 0 ? &fonts_of(engine)->fonts.data[font - 1] : NULL;
    if (created == NULL || !created->created) {
        return extended ? NMO_ENGINE_BR_PARAMETERERROR : NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t coordinates = extended ? nmo_engine_block_get_input_object(b, 6) : 0;
    int32_t rows = coordinates != 0 ? nmo_engine_array_row_count(engine, coordinates) : 0;
    if (coordinates == 0 || nmo_engine_array_column_count(engine, coordinates) < 6 || rows <= 0) {
        return NMO_ENGINE_BR_OK;
    }
    for (int32_t r = 0; r < rows && r < 256; r++) {
        float *fields[6] = {&created->glyphs[r].ustart, &created->glyphs[r].vstart,
                            &created->glyphs[r].uwidth, &created->glyphs[r].uprewidth,
                            &created->glyphs[r].upostwidth, &created->glyphs[r].vwidth};
        for (int32_t c = 0; c < 6; c++) {
            nmo_engine_cell_t cell;
            /* GetElementValue copies the cell's four bytes */
            if (nmo_engine_array_get_cell(engine, coordinates, r, c, &cell) == NMO_OK &&
                cell.type != 3 && cell.type != 5) {
                memcpy(fields[c], &cell.value, sizeof(float));
            }
        }
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_delete_font(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t font = get_in_int(b, 0, -1);
    if (font != -1) {
        font_delete(nmo_engine_block_engine(b), font);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static uint32_t font_color(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/* Set Font Properties: the spacing, scale, italic, colors, shadow and properties */
static int bb_set_font_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_font_t *font = font_get(b->engine, get_in_int(b, 0, 0));
    if (font == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_block_get_input_value(b, 1, font->leading, sizeof(font->leading));
    nmo_engine_block_get_input_value(b, 2, font->scale, sizeof(font->scale));
    nmo_engine_block_get_input_value(b, 3, &font->italic, sizeof(font->italic));
    float color[4];
    if (nmo_engine_block_get_input_value(b, 4, color, sizeof(color)) == NMO_OK) {
        font->start_color = font_color(color);
    }
    if (nmo_engine_block_get_input_value(b, 5, color, sizeof(color)) == NMO_OK) {
        font->end_color = font_color(color);
    }
    if (nmo_engine_block_get_input_value(b, 6, color, sizeof(color)) == NMO_OK) {
        font->shadow_color = font_color(color);
    }
    float angle = 1.0f;
    if (nmo_engine_block_get_input_value(b, 7, &angle, sizeof(angle)) == NMO_OK) {
        /* Shadow Distance only when input 8 really is it (some files have Shadow Size there) */
        float distance = 4.0f;
        if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 8), CKPGUID_FLOAT)) {
            nmo_engine_block_get_input_value(b, 8, &distance, sizeof(distance));
        }
        font->shadow_offset[0] = -distance * cosf(angle);
        font->shadow_offset[1] = distance * sinf(angle);
    }
    nmo_engine_block_get_input_value(b, 9, font->shadow_scale, sizeof(font->shadow_scale));
    font->material = nmo_engine_block_get_input_object(b, 10);
    font->properties = (uint32_t)get_local_int(b, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Text
 * ============================================================================ */

/* A line of text (LineData): a negative length starts a paragraph */
typedef struct text_line {
    const char *string;
    int len;
    int spaces;
    float width;
} text_line_t;

/* What a font draws a string with (CKTextureFont's per-draw members) */
typedef struct text_draw {
    nmo_engine_t *engine;
    eng_font_t *font;
    float screen[2];   /* m_ScreenExtents */
    float space_size;  /* m_SpaceSize */
    float hleading;    /* m_HLeading */
    float line_width;  /* m_LineWidth */
    nmo_rect_t clip;   /* m_ClippingRect */
    int32_t options;
} text_draw_t;

/* CKTextureFont::BuildStringGeometry (2D): a quad a character */
static void text_draw_string(text_draw_t *t, const char *string, int len, float x, float y,
                             const nmo_rect_t *zone, float scale_x, float scale_y,
                             uint32_t top, uint32_t bottom, bool gradient)
{
    const eng_font_t *font = t->font;
    nmo_rect_t clip = *zone;
    if (t->options & TEXT_SCREENCLIP) {
        clip.left = fmaxf(clip.left, t->clip.left);
        clip.top = fmaxf(clip.top, t->clip.top);
        clip.right = fminf(clip.right, t->clip.right);
        clip.bottom = fminf(clip.bottom, t->clip.bottom);
        if (clip.left >= clip.right || clip.top >= clip.bottom) {
            return;
        }
    }
    bool do_clip = (t->options & TEXT_CLIP) != 0;
    float width = scale_x * t->screen[0];
    float height = scale_y * t->screen[1];
    float inv_width = do_clip ? 1.0f / width : 0.0f;
    nmo_engine_image_t image;
    if (!nmo_engine_texture_image(t->engine, font->texture, &image)) {
        return;
    }
    float texel_w = 0.25f / (float)image.width, texel_h = 0.25f / (float)image.height;
    x = floorf(x + 0.5f);
    y = floorf(y + 0.5f);
    float hlead = (t->options & TEXT_JUSTIFIED) ? t->hleading : floorf(t->hleading + 0.5f);
    float line_height = font->glyphs[0].vwidth * height;
    float start_y = y, end_y = y + line_height, cut_up = 0.0f, cut_down = 1.0f;
    if (do_clip) {
        if (y > clip.bottom || end_y < clip.top || x > clip.right || x + t->line_width < clip.left) {
            return;
        }
        if (start_y < clip.top) {
            cut_up = (clip.top - start_y) / line_height;
            start_y = clip.top;
        }
        if (end_y > clip.bottom) {
            cut_down = 1.0f - (end_y - clip.bottom) / line_height;
            end_y = clip.bottom;
        }
    }
    if (gradient && (cut_up != 0.0f || cut_down != 1.0f)) {
        uint32_t s = top, e = bottom, out_top = 0, out_bottom = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            float sc = (float)((s >> shift) & 0xFF), ec = (float)((e >> shift) & 0xFF);
            float d = ec - sc;
            out_top |= (uint32_t)(sc + d * cut_up + 0.5f) << shift;
            out_bottom |= (uint32_t)(ec - d * (1.0f - cut_down) + 0.5f) << shift;
        }
        top = out_top;
        bottom = out_bottom;
    }
    while (len-- > 0) {
        unsigned char ch = (unsigned char)*string++;
        if (ch == ' ') {
            x += t->space_size;
            continue;
        }
        if (ch == '\b') {
            continue; /* the caret */
        }
        const eng_glyph_t *glyph = &font->glyphs[ch];
        if (glyph->vwidth == 0.0f) {
            continue;
        }
        float start_x = x + glyph->uprewidth * width;
        float end_x = start_x + width * glyph->uwidth;
        float start_u = glyph->ustart, end_u = glyph->ustart + glyph->uwidth;
        if (do_clip) {
            if (start_x > clip.right) {
                break;
            }
            if (end_x < clip.left) {
                x = end_x + hlead + glyph->upostwidth * width;
                continue;
            }
            if (x < clip.left) {
                start_u += (clip.left - start_x) * inv_width;
                start_x = clip.left;
            }
            if (end_x > clip.right) {
                end_u -= (end_x - clip.right) * inv_width;
                end_x = clip.right;
            }
        }
        x = end_x + glyph->upostwidth * width + hlead;
        nmo_engine_draw_t *draw = eng_render_push(t->engine);
        if (draw == NULL) {
            return;
        }
        draw->kind = NMO_ENGINE_DRAW_QUAD;
        draw->rect[0] = floorf(start_x + 0.5f) - 0.25f;
        draw->rect[1] = start_y;
        draw->rect[2] = floorf(end_x + 0.5f) - 0.25f;
        draw->rect[3] = end_y;
        draw->skew = floorf(start_x + font->italic + 0.5f) - floorf(start_x + 0.5f);
        draw->uv[0] = start_u + texel_w;
        draw->uv[1] = glyph->vstart + cut_up * glyph->vwidth + texel_h;
        draw->uv[2] = end_u;
        draw->uv[3] = glyph->vstart + cut_down * glyph->vwidth;
        draw->texture = font->texture;
        draw->color = top;
        draw->color2 = gradient ? bottom : top;
        draw->nearest = (font->properties & FONT_DISABLEFILTER) != 0;
    }
}

/* DrawStringShadowed: the shadow (one color, its own scale) first, when it shows */
static void text_draw_line(text_draw_t *t, const char *string, int len, float x, float y,
                           const nmo_rect_t *zone)
{
    const eng_font_t *font = t->font;
    bool gradient = (font->properties & FONT_GRADIENT) != 0;
    if ((font->properties & FONT_SHADOW) && (font->shadow_color >> 24) != 0) {
        text_draw_string(t, string, len, x + font->shadow_offset[0], y + font->shadow_offset[1],
                         zone, font->shadow_scale[0], font->shadow_scale[1], font->shadow_color,
                         font->shadow_color, false);
    }
    text_draw_string(t, string, len, x, y, zone, font->scale[0], font->scale[1],
                     font->start_color, font->end_color, gradient);
}

/*
 * CKTextureFont::DrawCKText: the string cut into lines (at line feeds, and
 * where words no longer fit with WordWrap or Justified), aligned in the zone
 * less its margins, each line drawn.
 */
static void text_draw(nmo_engine_t *engine, eng_font_t *font, const char *string, int32_t align,
                      nmo_rect_t zone, int32_t options, const float margins[4],
                      const float offset[2], const float indent[2], nmo_object_id_t material,
                      bool version2, const nmo_rect_t *clip, float frame_width, float frame_height,
                      nmo_rect_t *out_extents, int32_t *out_lines)
{
    text_draw_t t = {engine, font, {0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}, options};
    if (clip != NULL) {
        t.clip = *clip;
    }
    const eng_glyph_t *glyphs = font->glyphs;
    zone.left += margins[0];
    zone.top += margins[1];
    zone.right -= margins[2];
    zone.bottom -= margins[3];
    nmo_engine_image_t image;
    if (options & TEXT_SCREEN) {
        t.screen[0] = frame_width;
        t.screen[1] = frame_height;
    } else if (nmo_engine_texture_image(engine, font->texture, &image)) {
        t.screen[0] = (float)image.width;
        t.screen[1] = (float)image.height;
    }
    bool wrap = (options & (TEXT_JUSTIFIED | TEXT_WORDWRAP)) != 0;
    float zone_width = zone.right - zone.left;
    if (wrap && zone_width < (font->zone.right - font->zone.left) /
                                 (font->scale[0] * t.screen[0] * font->char_number[0])) {
        return;
    }
    float width = font->scale[0] * t.screen[0];
    float height = font->scale[1] * t.screen[1];
    float space_advance = glyphs[' '].uprewidth + glyphs[' '].uwidth + glyphs[' '].upostwidth;
    float hlead = version2 ? 0.1f * width * space_advance * font->leading[0] : font->leading[0];
    float space_size = width * space_advance + hlead;
    float indent_x = indent[0] * glyphs[0].uwidth * width;
    float indent_y = indent[1] * glyphs[0].vwidth * height;
    float vertical = glyphs[0].vwidth * height + font->leading[1];

    ENG_VEC(text_line_t) lines = {0};
    const char *last_word = "\n";
    int last_len = 0;
    float last_width = 0.0f, text_width = 0.0f, text_height = 0.0f;
    bool last_was_space = true, paragraph = true;
    while (*string) {
        text_line_t line = {string, 0, 0, paragraph ? indent_x : 0.0f};
        while (*string && *string != '\n') {
            if (*string == ' ') {
                last_was_space = true;
                last_len = line.len;
                last_word = string + 1;
                last_width = line.width;
                line.spaces++;
                line.width += space_size;
            } else {
                if (last_was_space) {
                    last_was_space = false;
                    last_len = line.len - 1;
                    last_word = string;
                    last_width = line.width - space_size;
                }
                const eng_glyph_t *g = &glyphs[(unsigned char)*string];
                line.width += (g->uprewidth + g->uwidth + g->upostwidth) * width + hlead;
            }
            if (wrap && line.width > zone_width) {
                if (last_len > 0) {
                    line.len = last_len;
                    line.width = last_width;
                    line.spaces--;
                    string = last_word;
                } else if (*string != ' ') {
                    const eng_glyph_t *g = &glyphs[(unsigned char)*string];
                    line.width -= (g->uprewidth + g->uwidth + g->upostwidth) * width + hlead;
                    if (last_word == string) {
                        string++;
                    }
                    last_word = string;
                }
                last_was_space = true;
                break;
            }
            line.len++;
            string++;
        }
        if (paragraph) {
            line.len = -line.len;
            paragraph = false;
        }
        if (*string == '\n') {
            paragraph = true;
            if (*last_word == '\n') {
                string++;
            }
        }
        if (line.width > text_width) {
            text_width = line.width;
        }
        if (line.len < 0 && lines.count != 0) {
            text_height += indent_y;
        }
        text_height += vertical;
        text_line_t *slot = NULL;
        if (!ENG_VEC_PUSH(lines, slot)) {
            break;
        }
        *slot = line;
        if (*string && string != last_word && *last_word != '\n') {
            string++;
        }
    }
    float x, y;
    if (align & VALIGN_TOP) {
        y = zone.top;
    } else if (align & VALIGN_BOTTOM) {
        y = zone.bottom - text_height;
    } else {
        y = (zone.bottom + zone.top) * 0.5f - text_height * 0.5f;
    }
    if (align & HALIGN_LEFT) {
        x = zone.left;
    } else if (align & HALIGN_RIGHT) {
        x = zone.right - text_width;
    } else {
        x = (zone.right + zone.left) * 0.5f - text_width * 0.5f;
    }
    nmo_rect_t extents = {x, y, x + text_width, y + text_height};
    if (out_extents != NULL) {
        *out_extents = extents;
    }
    if (out_lines != NULL) {
        *out_lines = (int32_t)lines.count;
    }
    if (align & (VALIGN_TOP | VALIGN_BOTTOM)) {
        y += offset[1];
    }
    if ((options & TEXT_BACKGROUND) && material != 0) {
        nmo_engine_draw_t *draw = eng_render_push(engine);
        eng_material_t *m = eng_material(engine, material);
        if (draw != NULL && m != NULL) {
            draw->kind = NMO_ENGINE_DRAW_QUAD;
            draw->rect[0] = (options & TEXT_JUSTIFIED) ? zone.left : extents.left;
            draw->rect[1] = extents.top;
            draw->rect[2] = (options & TEXT_JUSTIFIED) ? zone.right : extents.right;
            draw->rect[3] = extents.bottom;
            draw->uv[2] = draw->uv[3] = 1.0f;
            draw->material = material;
            draw->texture = m->desc.texture;
            draw->color = draw->color2 = font_color(m->desc.diffuse);
        }
    }
    for (size_t i = 0; i < lines.count; i++) {
        const text_line_t *line = &lines.data[i];
        int len = line->len < 0 ? -line->len : line->len;
        t.space_size = space_size;
        t.hleading = hlead;
        t.line_width = line->width;
        if (options & TEXT_JUSTIFIED) {
            if (line->spaces != 0) {
                t.space_size = space_size - (line->width - zone_width) / (float)line->spaces;
            } else if (len > 1) {
                t.hleading = hlead + (zone_width - line->width) / (float)(len - 1);
            }
            x = zone.left;
        } else if (align & HALIGN_LEFT) {
            x = zone.left + offset[0];
        } else if (align & HALIGN_RIGHT) {
            x = offset[0] + zone.right - line->width;
        } else {
            x = (zone.right + zone.left) * 0.5f - line->width * 0.5f;
        }
        if (line->len < 0) {
            if (align & HALIGN_LEFT) {
                x += indent_x;
            }
            if (i != 0) {
                y += indent_y;
            }
        }
        text_draw_line(&t, line->string, len, x, y, &zone);
        y += vertical;
    }
    free(lines.data);
}

/*
 * Text2DRenderCallback: 2D Text's text on its 2D entity, with the block's
 * parameters as they are when the entity draws, clipped like the entity.
 */
static void text_draw_block(nmo_engine_t *engine, const eng_text_t *text, float width,
                            float height)
{
    nmo_engine_block_t handle = {engine, text->node};
    blk_t *b = &handle;
    eng_2d_t *entity = eng_2d_entity(engine, text->entity);
    if (entity == NULL) {
        return;
    }
    eng_font_t *font = font_get(engine, text->multiple ? text->font : get_in_int(b, 0, 0));
    const char *string = text->multiple ? text->string : input_string(b, 1);
    if (font == NULL || string == NULL) {
        return;
    }
    int32_t align = text->multiple ? text->align : get_in_int(b, 2, 0);
    float margins[4] = {2.0f, 2.0f, 2.0f, 2.0f};
    nmo_engine_block_get_input_value(b, 3, margins, sizeof(margins));
    float offset[2] = {0.0f, 0.0f}, indent[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 4, offset, sizeof(offset));
    nmo_engine_block_get_input_value(b, 5, indent, sizeof(indent));
    int32_t flags = get_local_int(b, 0, 0);
    nmo_rect_t zone = text->multiple ? text->zone : entity->rect;
    nmo_rect_t clip = {0.0f, 0.0f, width, height};
    bool clipped = false;
    if (entity->flags & CK_2DENTITY_CLIPTOCAMERAVIEW) {
        flags |= TEXT_SCREENCLIP;
        clipped = true;
    }
    if ((entity->flags & CK_2DENTITY_CLIPTOPARENT) && entity->parent != 0) {
        eng_2d_t *parent = eng_2d_entity(engine, entity->parent);
        if (parent != NULL) {
            nmo_rect_t rect = parent->extents;
            if (clipped) {
                rect.left = fmaxf(rect.left, clip.left);
                rect.top = fmaxf(rect.top, clip.top);
                rect.right = fminf(rect.right, clip.right);
                rect.bottom = fminf(rect.bottom, clip.bottom);
            }
            clip = rect;
            flags |= TEXT_SCREENCLIP;
        }
    }
    nmo_rect_t extents;
    int32_t lines = 0;
    text_draw(engine, font, string, align, zone, flags, margins, offset, indent,
              nmo_engine_block_get_input_object(b, 6), nmo_engine_block_version(b) >= 0x00020000,
              &clip, width, height, &extents, &lines);
    if (!text->multiple) {
        nmo_engine_block_set_output_value(b, 0, &extents, sizeof(extents));
        nmo_engine_block_set_output_value(b, 1, &lines, sizeof(lines));
    }
}

static void draw_texts(nmo_engine_t *engine, nmo_object_id_t entity, float width, float height)
{
    font_manager_t *fm = fonts_of(engine);
    for (size_t i = 0; i < fm->texts.count; i++) {
        eng_text_t text = fm->texts.data[i];
        if (entity != 0 ? !text.multiple && text.entity == entity : text.multiple) {
            text_draw_block(engine, &text, width, height);
        }
    }
}

/* 2D Text's text this frame: a callback on its entity, or with Multiple, as it is now */
static void text_request(blk_t *b, bool multiple)
{
    nmo_engine_t *engine = b->engine;
    font_manager_t *fm = fonts_of(engine);
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_2d_t *entity = eng_2d_entity(engine, target);
    if (entity == NULL) {
        return;
    }
    if (multiple && !nmo_engine_object_is_visible(engine, target)) {
        return;
    }
    const char *string = multiple ? input_string(b, 1) : NULL;
    if (multiple && string == NULL) {
        return;
    }
    eng_text_t *text = NULL;
    for (size_t i = 0; !multiple && i < fm->texts.count; i++) {
        if (fm->texts.data[i].node == b->node && !fm->texts.data[i].multiple) {
            return; /* AddPostRenderCallBack once */
        }
    }
    if (!ENG_VEC_PUSH(fm->texts, text)) {
        return;
    }
    text->node = b->node;
    text->entity = target;
    text->multiple = multiple;
    if (multiple) {
        size_t length = strlen(string);
        text->string = (char *)malloc(length + 1);
        if (text->string != NULL) {
            memcpy(text->string, string, length + 1);
        }
        text->font = get_in_int(b, 0, 0);
        text->align = get_in_int(b, 2, 0);
        text->zone = entity->rect;
    }
}

/* 2D Text: Off first; On draws from then on, every frame (or once, with Multiple). */
static int bb_text_2d(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        if (get_local_int(b, 0, 0) & TEXT_MULTIPLE) {
            text_request(b, true);
            return NMO_ENGINE_BR_OK;
        }
    }
    text_request(b, false);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* 3D Text: as 2D Text, with On tested before Off. */
static int bb_text_3d(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        if (get_local_int(b, 0, 0) & TEXT_MULTIPLE) {
            return NMO_ENGINE_BR_OK;
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* The block's CKSpriteText, made the first time (the creation callback's) */
static eng_sprite_text_t *text_display_sprite(blk_t *b)
{
    font_manager_t *fm = fonts_of(b->engine);
    for (size_t i = 0; i < fm->sprite_texts.count; i++) {
        if (fm->sprite_texts.data[i].node == b->node) {
            return &fm->sprite_texts.data[i];
        }
    }
    eng_sprite_text_t *sprite = NULL;
    if (!ENG_VEC_PUSH(fm->sprite_texts, sprite)) {
        return NULL;
    }
    memset(sprite, 0, sizeof(*sprite));
    sprite->node = b->node;
    /* Create(Sprite Size), 320 x 32 by default */
    float size[2] = {320.0f, 32.0f};
    nmo_engine_block_get_local_value(b, 4, size, sizeof(size));
    sprite->width = (float)(int)size[0];
    sprite->height = (float)(int)size[1];
    /* tdFontInfo: the face name, the weight and the style; Arial before version 2 */
    const eng_node_t *node = block_node(b);
    const eng_param_t *info = eng_node_param(b->engine, node->first_local, node->local_count, 1);
    const char *face = "Arial";
    sprite->weight = 400;
    if (nmo_engine_block_version(b) >= 0x00020000u && info != NULL && info->data != NULL &&
        info->size >= 64u && memchr(info->data, '\0', 56) != NULL) {
        face = (const char *)info->data;
        memcpy(&sprite->weight, info->data + 56, sizeof(int32_t));
        memcpy(&sprite->style, info->data + 60, sizeof(int32_t));
    }
    sprite->font = eng_text_printf("%s", face);
    return sprite;
}

/* AppendTextDisplayParameter: a string as it is, anything else as its text, each with a space */
static char *text_display_string(blk_t *b)
{
    char *text = eng_text_printf("%s", "");
    int count = nmo_engine_block_input_parameter_count(b);
    for (int i = 4; text != NULL && i < count; i++) {
        eng_param_t *source = input_source(b, i, true);
        if (source == NULL) {
            continue;
        }
        char *part = nmo_guid_equals(source->type, CKPGUID_STRING)
                         ? eng_text_printf("%s", source->data != NULL && source->size > 0 &&
                                                         memchr(source->data, '\0', source->size)
                                                     ? (const char *)source->data
                                                     : "")
                         : eng_param_text(b->engine, source);
        if (part != NULL && part[0] != '\0') {
            char *joined = eng_text_printf("%s%s ", text, part);
            free(text);
            text = joined;
        }
        free(part);
    }
    return text;
}

/*
 * Text Display: its CKSpriteText shown with On (Exit On), hidden with Off
 * (Exit Off, tested first). While on it stays active: the sprite at
 * Offset, and when the text the inputs from the fifth on make changed (or
 * after On), the Color, Alignment and Size of the font tdFontInfo names
 * (local 1) with it. The color reaches GDI with red and blue swapped (the
 * "BUG" the block's source warns of), so the text shows them swapped.
 */
static int bb_text_display(blk_t *b)
{
    eng_sprite_text_t *sprite = text_display_sprite(b);
    if (sprite == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    bool redraw = false;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        sprite->shown = false;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        sprite->shown = true;
        redraw = true;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 1);
    }
    char *text = text_display_string(b);
    float offset[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, offset, sizeof(offset));
    sprite->x = offset[0];
    sprite->y = offset[1];
    if (text != NULL && (redraw || sprite->text == NULL || strcmp(text, sprite->text) != 0)) {
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
        uint32_t channel[4];
        for (int c = 0; c < 4; c++) {
            float v = color[c] < 0.0f ? 0.0f : color[c] > 1.0f ? 1.0f : color[c];
            channel[c] = (uint32_t)(v * 255.0f);
        }
        /* RGBAFTOCOLOR gives ARGB; RGB(c & 0xFF, c >> 8 & 0xFF, c >> 16 & 0xFF) reads it as ABGR */
        sprite->color = 0xFF000000u | channel[2] << 16 | channel[1] << 8 | channel[0];
        sprite->align = get_in_int(b, 2, 2);
        sprite->size = get_in_int(b, 3, 12);
        free(sprite->text);
        sprite->text = text;
        text = NULL;
    }
    free(text);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TextDisplayCallBackObject: the sprite hides when its script stops and shows when it runs */
static int cb_text_display(blk_t *b, uint32_t message)
{
    eng_sprite_text_t *sprite = text_display_sprite(b);
    if (sprite == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_PAUSE ||
        message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        sprite->shown = false;
    } else if ((message == NMO_ENGINE_MSG_ACTIVATESCRIPT || message == NMO_ENGINE_MSG_RESUME) &&
               nmo_engine_behavior_is_active(b->engine, block_node(b)->id) &&
               nmo_engine_parent_script_is_active(b->engine, block_node(b)->id)) {
        sprite->shown = true;
    }
    return NMO_ENGINE_BR_OK;
}

static void draw_sprite_texts(nmo_engine_t *engine)
{
    font_manager_t *fm = fonts_of(engine);
    for (size_t i = 0; i < fm->sprite_texts.count; i++) {
        const eng_sprite_text_t *sprite = &fm->sprite_texts.data[i];
        nmo_engine_draw_t *draw = NULL;
        if (!sprite->shown || sprite->text == NULL || sprite->text[0] == '\0' ||
            !ENG_VEC_PUSH(engine->render.draws, draw)) {
            continue;
        }
        memset(draw, 0, sizeof(*draw));
        draw->kind = NMO_ENGINE_DRAW_SPRITE_TEXT;
        draw->rect[0] = sprite->x;
        draw->rect[1] = sprite->y;
        draw->rect[2] = sprite->x + sprite->width;
        draw->rect[3] = sprite->y + sprite->height;
        draw->color = draw->color2 = sprite->color;
        draw->text = sprite->text;
        draw->font_name = sprite->font;
        draw->font_size = sprite->size;
        draw->font_weight = sprite->weight;
        draw->italic = (sprite->style & 0x1) != 0; /* FONT_ITALIC */
        draw->text_align = (uint32_t)sprite->align;
    }
}

/* ============================================================================
 * PushButton
 * ============================================================================ */

/* The button state (local 0) */
#define BUTTON_RELEASED 0
#define BUTTON_PRESSED 1
#define BUTTON_ROLLOVER 2
#define BUTTON_PUSHED 3
#define BUTTON_DISABLED 4

/* The "Events" setting (local 1): the outputs the button has, in this order */
enum {
    BOM_PRESSED = 1,
    BOM_RELEASED = 2,
    BOM_ACTIVE = 4,
    BOM_ENTERREGION = 8,
    BOM_EXITREGION = 16,
    BOM_INREGION = 32,
};

static int bb_push_button(blk_t *b)
{
    int32_t state = BUTTON_RELEASED;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, state);
    }
    if (nmo_engine_block_input_count(b) > 1 && IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_input_count(b) > 2 && IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        state = BUTTON_DISABLED;
        set_local_int(b, 0, state);
    }
    int32_t old = get_local_int(b, 0, 0);
    if (old & BUTTON_DISABLED) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }

    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (nmo_engine_block_target(b) == nmo_engine_entity_under_mouse(engine)) {
        state |= BUTTON_ROLLOVER;
    }
    if (nmo_engine_is_mouse_button_down(engine, 0)) {
        state |= BUTTON_PRESSED;
    }
    if (state != old) {
        set_local_int(b, 0, state);
    }

    if (nmo_engine_block_version(b) > 0x00020000u) {
        int32_t events = get_local_int(b, 1, 63);
        bool pressed = (state & BUTTON_PRESSED) != 0, over = (state & BUTTON_ROLLOVER) != 0;
        bool was_over = (old & BUTTON_ROLLOVER) != 0;
        const bool fired[6] = {
            !(old & BUTTON_PRESSED) && pressed && over, /* Pressed */
            old == BUTTON_PUSHED && !pressed && over,   /* Released */
            pressed && over,                            /* Active */
            !was_over && over,                          /* Enter Button */
            was_over && !over,                          /* Exit Button */
            over,                                       /* In Button */
        };
        int pos = 0;
        for (int bit = 0; bit < 6; bit++) {
            if ((events & (1 << bit)) == 0) {
                continue;
            }
            if (fired[bit] && pos < nmo_engine_block_output_count(b)) {
                OUT_ON(b, pos);
            }
            pos++;
        }
    } else if (state != old) {
        /* The old button: Released (0) on leaving, Roll Over (2) on entering, Pressed (1) on a click */
        if ((old & BUTTON_ROLLOVER) && !(state & BUTTON_ROLLOVER)) {
            OUT_ON(b, BUTTON_RELEASED);
        }
        if (!(old & BUTTON_ROLLOVER) && (state & BUTTON_ROLLOVER)) {
            OUT_ON(b, BUTTON_ROLLOVER);
        }
        if (old == BUTTON_PUSHED && state == BUTTON_ROLLOVER) {
            OUT_ON(b, BUTTON_PRESSED);
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * 2D Picking: what the render context picks at the mouse (Use Mouse
 * Coordinates?) or at Pos (in screen coordinates unless window relative):
 * True with the 3D entity, the point and normal in world space, the UVs,
 * distance and face, and the sprite; else False and no object. There is no
 * window, so screen and window coordinates are the same.
 */
static int bb_picking_2d(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    IN_OFF(b, 0);
    int32_t x = 0, y = 0;
    if (get_local_int(b, 1, 0) != 0) {
        float mouse[2] = {0.0f, 0.0f};
        nmo_engine_mouse_position(engine, &mouse[0], &mouse[1]);
        x = (int32_t)(int64_t)mouse[0];
        y = (int32_t)(int64_t)mouse[1];
    } else if (get_local_int(b, 0, 0) != 0) {
        float pos[2] = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 0, pos, sizeof(pos));
        x = (int32_t)(int64_t)pos[0];
        y = (int32_t)(int64_t)pos[1];
    } else {
        x = get_in_int(b, 0, 0); /* before version 2: X and Y */
        y = get_in_int(b, 1, 0);
    }
    nmo_engine_pick_t pick;
    memset(&pick, 0, sizeof(pick));
    bool hit = engine->input.pick != NULL && engine->input.pick(engine->input.pick_user_data, x, y, &pick);
    if (!hit || (pick.object == 0 && pick.sprite == 0)) {
        const uint32_t none = 0;
        nmo_engine_block_set_output_value(b, 0, &none, sizeof(none));
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t object = (uint32_t)pick.object;
    nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
    int32_t entity = pick.object != 0 ? eng_entity_index(engine, pick.object) : -1;
    if (entity >= 0) {
        eng_vector_t local = {pick.point[0], pick.point[1], pick.point[2]}, world;
        eng_entity_transform(engine, entity, &world, &local, -1);
        nmo_engine_block_set_output_value(b, 1, &world, sizeof(world));
        eng_vector_t normal = {pick.normal[0], pick.normal[1], pick.normal[2]}, world_normal;
        eng_entity_transform_vector(engine, entity, &world_normal, &normal, -1, false);
        nmo_engine_block_set_output_value(b, 2, &world_normal, sizeof(world_normal));
        nmo_engine_block_set_output_value(b, 3, pick.uv, sizeof(pick.uv));
        nmo_engine_block_set_output_value(b, 4, &pick.distance, sizeof(pick.distance));
        nmo_engine_block_set_output_value(b, 5, &pick.face, sizeof(pick.face));
    }
    uint32_t sprite = (uint32_t)pick.sprite;
    nmo_engine_block_set_output_value(b, 6, &sprite, sizeof(sprite));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_interface_blocks[] = {
    {NMO_GUID_INIT(0x936334fc, 0xf243684f), "Create System Font", bb_create_system_font, NULL, NULL},
    {NMO_GUID_INIT(0x7d644088, 0x40dd0349), "Create Font", bb_create_font, NULL, NULL},
    {NMO_GUID_INIT(0x260e4eb0, 0x0e256b90), "TT_CreateFontEx", bb_create_font, NULL, NULL},
    {NMO_GUID_INIT(0x0153342c, 0xe543674f), "Delete Font", bb_delete_font, NULL, NULL},
    {NMO_GUID_INIT(0xdacfbd61, 0x7a6e65e7), "Set Font Properties", bb_set_font_properties, NULL, NULL},
    {NMO_GUID_INIT(0x055b29fe, 0x662d5ca0), "2D Text", bb_text_2d, NULL, NULL},
    {NMO_GUID_INIT(0xf22d010a, 0xf2cd010a), "Text Display", bb_text_display, cb_text_display, NULL},
    {NMO_GUID_INIT(0x1f5b2fe7, 0x6d355175), "3D Text", bb_text_3d, NULL, NULL},
    {NMO_GUID_INIT(0x761b7e64, 0x2b4c6911), "PushButton", bb_push_button, NULL, NULL},
    {NMO_GUID_INIT(0x1616120f, 0x5b2f48ce), "2D Picking", bb_picking_2d, NULL, NULL},
};

/* The texts drawn last frame are dropped (the temporary callbacks) */
static void font_manager_pre_process(nmo_engine_t *engine, void *state)
{
    (void)engine;
    font_manager_t *fm = (font_manager_t *)state;
    for (size_t i = 0; i < fm->texts.count; i++) {
        free(fm->texts.data[i].string);
    }
    fm->texts.count = 0;
}

static void font_manager_destroy(nmo_engine_t *engine, void *state)
{
    font_manager_t *fm = (font_manager_t *)state;
    font_manager_pre_process(engine, fm);
    free(fm->texts.data);
    for (size_t i = 0; i < fm->fonts.count; i++) {
        free(fm->fonts.data[i].name);
    }
    free(fm->fonts.data);
    for (size_t i = 0; i < fm->sprite_texts.count; i++) {
        free(fm->sprite_texts.data[i].text);
        free(fm->sprite_texts.data[i].font);
    }
    free(fm->sprite_texts.data);
}

/* The font manager's texts after each 2D entity and after the sprites; Text Display's sprite texts */
static void font_manager_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                                const eng_render_args_t *args)
{
    (void)state;
    if (stage == ENG_RENDER_ENTITY_2D) {
        draw_texts(engine, args->entity, args->width, args->height);
    } else if (stage == ENG_RENDER_OVERLAY) {
        draw_sprite_texts(engine);
        draw_texts(engine, 0, args->width, args->height);
    }
}

/* CKFontManager */
static const eng_manager_desc_t font_manager = {
    .name = "Font Manager",
    .state_size = sizeof(font_manager_t),
    .destroy = font_manager_destroy,
    .pre_process = font_manager_pre_process,
    .render = font_manager_render,
};

nmo_status_t eng_register_interface(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &font_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_interface_blocks) : status;
}
