/**
 * @file text.c
 * @brief Sprite texts drawn as VxDrawBitmapText would: the sprite cleared to
 *        nothing, the text in its color with the font CreateFont would pick,
 *        placed by the sprite's alignment.
 *
 * The font is the face's TrueType file from the system's font folders
 * (bold from weight 600, italic from the style), rendered by stb_truetype at
 * the size CreateFont gives a point size at 96 dots an inch. Each different
 * sprite becomes a texture, dropped once no frame has drawn it for a second.
 */

#define GL_SILENCE_DEPRECATION

#include "view_text.h"

/* stb is third-party code: its unused static functions stay out of -Werror */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_KEEP_FRAMES 60

typedef struct text_font {
    char *face;
    bool bold, italic;
    unsigned char *data; /* the file; NULL when none was found */
    stbtt_fontinfo info;
} text_font_t;

typedef struct text_sprite {
    char *text, *font;
    int32_t size, weight;
    bool italic;
    uint32_t align, color;
    int width, height;
    GLuint name;
    uint64_t used;
} text_sprite_t;

struct view_text {
    text_font_t *fonts;
    size_t font_count;
    text_sprite_t *sprites;
    size_t sprite_count, sprite_capacity;
    uint64_t frame;
};

static char *text_copy(const char *s)
{
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, s, length + 1);
    }
    return copy;
}

/* dir/name: exact size */
static char *text_path(const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    char *path = (char *)malloc(a + 1 + b + 1);
    if (path != NULL) {
        memcpy(path, dir, a);
        path[a] = '/';
        memcpy(path + a + 1, name, b + 1);
    }
    return path;
}

static unsigned char *text_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    unsigned char *data = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        data = size > 0 ? (unsigned char *)malloc((size_t)size) : NULL;
        if (data != NULL && (fseek(file, 0, SEEK_SET) != 0 ||
                             fread(data, 1, (size_t)size, file) != (size_t)size)) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    return data;
}

/* The file of `face` in this style: "Face Bold Italic.ttf" and the like, then "Face.ttf" */
static unsigned char *text_find_font(const char *face, bool bold, bool italic)
{
    static const char *const dirs[] = {
        "/System/Library/Fonts/Supplemental", "/System/Library/Fonts", "/Library/Fonts",
        "C:/Windows/Fonts", "/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/TTF",
    };
    const char *style = bold && italic ? " Bold Italic" : bold ? " Bold" : italic ? " Italic" : "";
    size_t face_length = strlen(face), style_length = strlen(style);
    for (int plain = 0; plain < 2; plain++) {
        const char *suffix = plain ? "" : style;
        size_t suffix_length = plain ? 0 : style_length;
        char *name = (char *)malloc(face_length + suffix_length + 5);
        if (name == NULL) {
            return NULL;
        }
        memcpy(name, face, face_length);
        memcpy(name + face_length, suffix, suffix_length);
        memcpy(name + face_length + suffix_length, ".ttf", 5);
        for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
            char *path = text_path(dirs[d], name);
            unsigned char *data = path != NULL ? text_read_file(path) : NULL;
            free(path);
            if (data != NULL) {
                free(name);
                return data;
            }
        }
        free(name);
    }
    return NULL;
}

static const text_font_t *text_font(view_text_t *text, const char *face, bool bold, bool italic)
{
    for (size_t i = 0; i < text->font_count; i++) {
        const text_font_t *font = &text->fonts[i];
        if (font->bold == bold && font->italic == italic && strcmp(font->face, face) == 0) {
            return font->data != NULL ? font : NULL;
        }
    }
    text_font_t *grown =
        (text_font_t *)realloc(text->fonts, (text->font_count + 1) * sizeof(*grown));
    char *copy = grown != NULL ? text_copy(face) : NULL;
    if (grown != NULL) {
        text->fonts = grown;
    }
    if (copy == NULL) {
        return NULL;
    }
    text_font_t *font = &text->fonts[text->font_count++];
    memset(font, 0, sizeof(*font));
    font->face = copy;
    font->bold = bold;
    font->italic = italic;
    font->data = text_find_font(face, bold, italic);
    if (font->data != NULL && !stbtt_InitFont(&font->info, font->data,
                                              stbtt_GetFontOffsetForIndex(font->data, 0))) {
        free(font->data);
        font->data = NULL;
    }
    return font->data != NULL ? font : NULL;
}

/* The sprite's pixels: nothing, and the text's coverage in its color */
static unsigned char *text_render(const text_font_t *font, const nmo_engine_draw_t *draw, int width,
                                  int height)
{
    unsigned char *rgba = (unsigned char *)calloc((size_t)width * (size_t)height, 4u);
    if (rgba == NULL) {
        return NULL;
    }
    /* CreateFont's height: a point size at 96 dots an inch, or pixels when negative */
    float pixels = draw->font_size < 0 ? (float)-draw->font_size
                                       : (float)draw->font_size * 96.0f / 72.0f;
    float scale = stbtt_ScaleForMappingEmToPixels(&font->info, pixels);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
    const unsigned char *s = (const unsigned char *)draw->text;
    float line_width = 0.0f;
    for (size_t i = 0; s[i] != '\0'; i++) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, s[i], &advance, &bearing);
        line_width += (float)advance * scale;
        if (s[i + 1] != '\0') {
            line_width += (float)stbtt_GetCodepointKernAdvance(&font->info, s[i], s[i + 1]) * scale;
        }
    }
    float line_height = (float)(ascent - descent) * scale;
    /* DT_CENTER, DT_RIGHT or DT_LEFT; DT_BOTTOM, DT_VCENTER or DT_TOP */
    uint32_t align = draw->text_align;
    float x = (align & (0x1u | 0x40u)) ? ((float)width - line_width) * 0.5f
              : (align & 0x4u)         ? (float)width - line_width
                                       : 0.0f;
    float y = (align & 0x10u) ? (float)height - line_height
              : (align & 0x20u) ? ((float)height - line_height) * 0.5f
                                : 0.0f;
    int baseline = (int)floorf(y + (float)ascent * scale + 0.5f);
    for (size_t i = 0; s[i] != '\0'; i++) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&font->info, s[i], scale, scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        unsigned char *glyph = w > 0 && h > 0 ? (unsigned char *)malloc((size_t)w * (size_t)h) : NULL;
        if (glyph != NULL) {
            stbtt_MakeCodepointBitmap(&font->info, glyph, w, h, w, scale, scale, s[i]);
            int left = (int)floorf(x + 0.5f) + x0, top = baseline + y0;
            for (int gy = 0; gy < h; gy++) {
                for (int gx = 0; gx < w; gx++) {
                    int px = left + gx, py = top + gy;
                    if (px < 0 || py < 0 || px >= width || py >= height) {
                        continue;
                    }
                    unsigned char *p = &rgba[((size_t)py * (size_t)width + (size_t)px) * 4u];
                    unsigned char coverage = glyph[gy * w + gx];
                    if (coverage > p[3]) {
                        p[0] = (unsigned char)(draw->color >> 16);
                        p[1] = (unsigned char)(draw->color >> 8);
                        p[2] = (unsigned char)draw->color;
                        p[3] = coverage;
                    }
                }
            }
            free(glyph);
        }
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, s[i], &advance, &bearing);
        x += (float)advance * scale;
        if (s[i + 1] != '\0') {
            x += (float)stbtt_GetCodepointKernAdvance(&font->info, s[i], s[i + 1]) * scale;
        }
    }
    return rgba;
}

view_text_t *view_text_open(void)
{
    return (view_text_t *)calloc(1, sizeof(view_text_t));
}

static void text_sprite_free(text_sprite_t *sprite)
{
    glDeleteTextures(1, &sprite->name);
    free(sprite->text);
    free(sprite->font);
}

void view_text_close(view_text_t *text)
{
    if (text == NULL) {
        return;
    }
    for (size_t i = 0; i < text->sprite_count; i++) {
        text_sprite_free(&text->sprites[i]);
    }
    free(text->sprites);
    for (size_t i = 0; i < text->font_count; i++) {
        free(text->fonts[i].face);
        free(text->fonts[i].data);
    }
    free(text->fonts);
    free(text);
}

unsigned int view_text_texture(view_text_t *text, const nmo_engine_draw_t *draw)
{
    int width = (int)(draw->rect[2] - draw->rect[0]), height = (int)(draw->rect[3] - draw->rect[1]);
    if (text == NULL || draw->text == NULL || draw->font_name == NULL || width <= 0 || height <= 0) {
        return 0;
    }
    for (size_t i = 0; i < text->sprite_count; i++) {
        text_sprite_t *s = &text->sprites[i];
        if (s->size == draw->font_size && s->weight == draw->font_weight &&
            s->italic == draw->italic && s->align == draw->text_align && s->color == draw->color &&
            s->width == width && s->height == height && strcmp(s->text, draw->text) == 0 &&
            strcmp(s->font, draw->font_name) == 0) {
            s->used = text->frame;
            return s->name;
        }
    }
    const text_font_t *font = text_font(text, draw->font_name, draw->font_weight >= 600, draw->italic);
    unsigned char *rgba = font != NULL ? text_render(font, draw, width, height) : NULL;
    if (rgba == NULL) {
        return 0;
    }
    if (text->sprite_count == text->sprite_capacity) {
        size_t capacity = text->sprite_capacity != 0 ? text->sprite_capacity * 2 : 8;
        text_sprite_t *grown = (text_sprite_t *)realloc(text->sprites, capacity * sizeof(*grown));
        if (grown == NULL) {
            free(rgba);
            return 0;
        }
        text->sprites = grown;
        text->sprite_capacity = capacity;
    }
    text_sprite_t *s = &text->sprites[text->sprite_count];
    memset(s, 0, sizeof(*s));
    s->text = text_copy(draw->text);
    s->font = text_copy(draw->font_name);
    if (s->text == NULL || s->font == NULL) {
        free(s->text);
        free(s->font);
        free(rgba);
        return 0;
    }
    text->sprite_count++;
    s->size = draw->font_size;
    s->weight = draw->font_weight;
    s->italic = draw->italic;
    s->align = draw->text_align;
    s->color = draw->color;
    s->width = width;
    s->height = height;
    s->used = text->frame;
    glGenTextures(1, &s->name);
    glBindTexture(GL_TEXTURE_2D, s->name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return s->name;
}

void view_text_end_frame(view_text_t *text)
{
    if (text == NULL) {
        return;
    }
    size_t kept = 0;
    for (size_t i = 0; i < text->sprite_count; i++) {
        if (text->frame - text->sprites[i].used < TEXT_KEEP_FRAMES) {
            text->sprites[kept++] = text->sprites[i];
        } else {
            text_sprite_free(&text->sprites[i]);
        }
    }
    text->sprite_count = kept;
    text->frame++;
}
