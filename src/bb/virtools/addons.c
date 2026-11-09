/**
 * @file addons.c
 * @brief Ports of the BuildingBlocksAddons1 building blocks.
 *
 * Each function does what the block's function in BuildingBlocksAddons1.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ).
 */

#include "virtools.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Movie Load (MovieLoader): the target texture plays the movie file. */
static int bb_movie_load(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t frames = name != NULL && size > 0 && memchr(name, '\0', size) != NULL
                         ? eng_movie_frame_count(b->engine, name)
                         : -1;
    if (frames < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    texture->movie = true;
    texture->slot_count = frames;
    texture->current_slot = 0;
    eng_movie_close(texture->movie_reader);
    texture->movie_reader = eng_movie_open(b->engine, name);
    eng_texture_changed(b->engine, texture->id);
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_object_id_t id = texture->id;
        nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* An input of Time Settings that has a source: its value; else none (the setting stays) */
static bool time_input(blk_t *b, int pos, void *out, size_t size)
{
    return input_source(b, pos, false) != NULL &&
           nmo_engine_block_get_input_value(b, pos, out, size) == NMO_OK;
}

/*
 * Time Settings (TimeSettings): the time manager's frame and behavior rates,
 * limits, time scale and frame time bounds; an input with no source keeps
 * its setting (the absolute value of each number).
 */
static int bb_time_settings(blk_t *b)
{
    nmo_engine_time_settings_t *t = &b->engine->time_settings;
    int32_t frame = 0, behavior = 0, frame_limit = (int32_t)t->frame_rate_limit;
    int32_t behavior_limit = (int32_t)t->behavior_rate_limit;
    float scale = t->time_scale, min_delta = t->min_delta, max_delta = t->max_delta;
    uint32_t frame_option = t->limit_options & 0xF0u;
    bool frame_given = time_input(b, 0, &frame, sizeof(frame));
    time_input(b, 1, &frame_limit, sizeof(frame_limit));
    time_input(b, 2, &scale, sizeof(scale));
    time_input(b, 4, &behavior_limit, sizeof(behavior_limit));
    time_input(b, 5, &min_delta, sizeof(min_delta));
    time_input(b, 6, &max_delta, sizeof(max_delta));
    if (frame_given) {
        frame_option = frame == 1 ? 0x20u : frame == 2 ? 0x10u : 0x40u; /* free, sync, limit */
        if (frame != 1 && frame != 2 && frame_limit != 0) {
            t->frame_rate_limit = fabsf((float)frame_limit);
        }
    }
    t->time_scale = fabsf(scale);
    uint32_t behavior_option = t->limit_options & 0x0Fu;
    if (time_input(b, 3, &behavior, sizeof(behavior))) {
        behavior_option = behavior == 1 ? 0x1u : 0x4u;
        if (behavior != 1 && behavior_limit != 0) {
            t->behavior_rate_limit = fabsf((float)behavior_limit);
        }
    }
    t->limit_options = frame_option | behavior_option;
    if (fabsf(min_delta) > 0.0f) {
        t->min_delta = fabsf(min_delta);
    }
    if (fabsf(max_delta) > 0.0f) {
        t->max_delta = fabsf(max_delta);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Textures (the DLL's MMX/SSE2 routines, which every CPU since takes)
 * ============================================================================ */

/* The bytes of a surface's pixel `i` (red, green, blue, alpha) */
static uint8_t *texel(eng_surface_t *surface, size_t i)
{
    return &surface->rgba[4u * i];
}

static int32_t clamp_byte(int32_t v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* A texture's image to write, its slot `slot` (-1 the current one); NULL for none */
static eng_surface_t *texture_surface(blk_t *b, nmo_object_id_t id, int32_t slot)
{
    eng_texture_t *texture = id != 0 ? eng_texture(b->engine, id) : NULL;
    eng_surface_t *surface = texture != NULL ? eng_texture_surface(b->engine, texture, slot) : NULL;
    return surface != NULL && surface->rgba != NULL && surface->width > 0 && surface->height > 0 ? surface : NULL;
}

/*
 * Filter Texture: the target's image convolved with the 3x3 matrix (its rows
 * the three inputs, as integers) over Scale (their sum without one), the
 * edges repeated: each weight is (w x 256 / scale) as a 16-bit integer, the
 * sums shifted right 8 and kept in 0-255 (the DLL's MMX path).
 */
static int bb_filter_texture(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t matrix[9];
    for (int r = 0; r < 3; r++) {
        static const float defaults[3][3] = {{1, 2, 1}, {2, 8, 2}, {1, 2, 1}};
        eng_vector_t row = get_in_vector(b, r, defaults[r][0], defaults[r][1], defaults[r][2]);
        matrix[3 * r] = (int32_t)row.x, matrix[3 * r + 1] = (int32_t)row.y, matrix[3 * r + 2] = (int32_t)row.z;
    }
    int32_t scale = get_in_int(b, 3, 0);
    if (scale == 0) {
        for (int i = 0; i < 9; i++) {
            scale += matrix[i];
        }
        scale = scale == 0 ? 1 : scale;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_surface_t *surface = texture_surface(b, target, -1);
    if (surface == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int16_t weight[9];
    for (int i = 0; i < 9; i++) {
        weight[i] = (int16_t)((matrix[i] << 8) / scale);
    }
    int32_t w = surface->width, h = surface->height;
    uint8_t *source = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (source == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    memcpy(source, surface->rgba, (size_t)w * (size_t)h * 4u);
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t sum[4] = {0, 0, 0, 0};
            for (int dy = -1; dy <= 1; dy++) {
                int32_t sy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
                for (int dx = -1; dx <= 1; dx++) {
                    int32_t sx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
                    const uint8_t *p = &source[4u * ((size_t)sy * (size_t)w + (size_t)sx)];
                    int16_t k = weight[3 * (dy + 1) + (dx + 1)];
                    for (int c = 0; c < 4; c++) {
                        sum[c] += (int32_t)p[c] * k;
                    }
                }
            }
            uint8_t *out = texel(surface, (size_t)y * (size_t)w + (size_t)x);
            for (int c = 0; c < 4; c++) {
                out[c] = (uint8_t)clamp_byte(sum[c] >> 8);
            }
        }
    }
    free(source);
    eng_texture_changed(b->engine, target);
    return NMO_ENGINE_BR_OK;
}

/*
 * Blend Textures: the target's image becomes Texture 1's and Texture 2's
 * weighted by 1 - Blend Factor and Blend Factor, each as a 16-bit fraction
 * of 256 (the MMX path); nothing unless the three have as many pixels.
 */
static int bb_blend_textures(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_texture(b->engine, target) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t first = nmo_engine_block_get_input_object(b, 0), second = nmo_engine_block_get_input_object(b, 1);
    float factor = get_in_float(b, 2, 0.5f);
    eng_surface_t *out = texture_surface(b, target, -1);
    eng_surface_t *one = first != 0 && second != 0 ? texture_surface(b, first, -1) : NULL;
    eng_surface_t *two = one != NULL ? texture_surface(b, second, -1) : NULL;
    if (out == NULL || two == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    size_t count = (size_t)out->width * (size_t)out->height;
    if (count != (size_t)one->width * (size_t)one->height || count != (size_t)two->width * (size_t)two->height) {
        return NMO_ENGINE_BR_OK;
    }
    uint16_t keep = (uint16_t)(int64_t)((1.0 - factor) * 256.0), take = (uint16_t)(int64_t)(factor * 256.0);
    for (size_t i = 0; i < count; i++) {
        const uint8_t *p = texel(one, i), *q = texel(two, i);
        uint8_t *o = texel(out, i);
        for (int c = 0; c < 4; c++) {
            uint32_t sum = (uint32_t)(uint16_t)(p[c] * keep) + (uint32_t)(uint16_t)(q[c] * take);
            o[c] = (uint8_t)((sum > 0xFFFFu ? 0xFFFFu : sum) >> 8);
        }
    }
    eng_texture_changed(b->engine, target);
    return NMO_ENGINE_BR_OK;
}

/*
 * Combine Texture: the target's image takes Color Texture's (its current
 * slot) through Color Filter (each channel blended by its filter value; a
 * copy for white), then Alpha Texture's blue channel as its alpha; each only
 * for a texture of the target's size.
 */
static int bb_combine_texture(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_texture(engine, target) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_surface_t *out = texture_surface(b, target, -1);
    float filter[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 1, filter, sizeof(filter));
    nmo_object_id_t color = nmo_engine_block_get_input_object(b, 0);
    eng_texture_t *color_texture = color != 0 ? eng_texture(engine, color) : NULL;
    eng_surface_t *in = color_texture != NULL ? eng_texture_surface(engine, color_texture, color_texture->current_slot) : NULL;
    if (out != NULL && in != NULL && in->rgba != NULL && in->width == out->width && in->height == out->height) {
        size_t count = (size_t)out->width * (size_t)out->height;
        if (filter[0] == 1.0f && filter[1] == 1.0f && filter[2] == 1.0f && filter[3] == 1.0f) {
            memcpy(out->rgba, in->rgba, count * 4u);
        } else {
            for (size_t i = 0; i < count; i++) {
                uint8_t *o = texel(out, i);
                const uint8_t *p = texel(in, i);
                for (int c = 0; c < 4; c++) { /* r, g, b, a: each with its own filter */
                    o[c] = (uint8_t)(int64_t)((double)o[c] * (double)(1.0f - filter[c]) + (double)p[c] * (double)filter[c]);
                }
            }
        }
    }
    nmo_object_id_t alpha = nmo_engine_block_get_input_object(b, 2);
    eng_surface_t *mask = alpha != 0 ? texture_surface(b, alpha, -1) : NULL;
    if (out != NULL && mask != NULL && mask->width == out->width && mask->height == out->height) {
        size_t count = (size_t)out->width * (size_t)out->height;
        for (size_t i = 0; i < count; i++) {
            texel(out, i)[3] = texel(mask, i)[2]; /* the DLL's first byte: blue */
        }
    }
    eng_texture_changed(engine, target);
    return NMO_ENGINE_BR_OK;
}

/* One water pixel: twice the neighbours' average less the older frame, damped, kept in 0-255 */
static uint8_t water_step(int32_t average, int32_t older, int32_t damping)
{
    int32_t v = 2 * average - older;
    v = v < 0 ? 0 : v;
    if (damping >= 0 && damping < 16) {
        v -= v >> damping;
    }
    return (uint8_t)(v > 255 ? 255 : v);
}

/*
 * Water Texture: the target's two slots hold the last two frames of a water
 * surface: the older one becomes twice the current one's neighbour average
 * less itself, less its 2^-Damping part, and is the current slot from then
 * on. As the DLL's SSE2 path computes it: the first and last rows average
 * their four neighbours (the Border Color off the image) to a quarter,
 * the other rows average the rounded means of the vertical and of the
 * horizontal pairs.
 */
static int bb_water_texture(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = eng_texture(engine, target);
    if (texture == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t damping = get_in_int(b, 0, 8);
    float border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 1, border_color, sizeof(border_color));
    int32_t current = texture->current_slot, other = (current - 1) & 1;
    eng_surface_t *now = eng_texture_surface(engine, texture, current);
    eng_surface_t *older = eng_texture_surface(engine, texture, other);
    if (now == NULL || older == NULL || now->rgba == NULL || older->rgba == NULL || now->width != older->width ||
        now->height != older->height || now->width < 2 || now->height < 2) {
        return NMO_ENGINE_BR_OK;
    }
    uint32_t argb = rgbaf_to_color(border_color);
    const int32_t border[4] = {(int32_t)((argb >> 16) & 0xFFu), (int32_t)((argb >> 8) & 0xFFu),
                               (int32_t)(argb & 0xFFu), (int32_t)(argb >> 24)};
    int32_t w = now->width, h = now->height;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            uint8_t *o = texel(older, (size_t)y * (size_t)w + (size_t)x);
            for (int c = 0; c < 4; c++) {
                int32_t left = x > 0 ? texel(now, (size_t)y * (size_t)w + (size_t)(x - 1))[c] : border[c];
                int32_t right = x + 1 < w ? texel(now, (size_t)y * (size_t)w + (size_t)(x + 1))[c] : border[c];
                int32_t up = y > 0 ? texel(now, (size_t)(y - 1) * (size_t)w + (size_t)x)[c] : border[c];
                int32_t down = y + 1 < h ? texel(now, (size_t)(y + 1) * (size_t)w + (size_t)x)[c] : border[c];
                int32_t average = (y == 0 || y == h - 1)
                                      ? (left + right + up + down) >> 2
                                      : (((up + down + 1) >> 1) + ((left + right + 1) >> 1)) >> 1;
                o[c] = water_step(average, o[c], damping);
            }
        }
    }
    texture->current_slot = other;
    eng_texture_changed(engine, target);
    return NMO_ENGINE_BR_OK;
}

/*
 * Copy Buffer to Texture: CKTexture::CopyContext of the render context's
 * Source rectangle into the target's Destination one when they have the same
 * size. The engine has no frame buffer to read (the host renders), so it is
 * an effect.
 */
static int bb_copy_buffer_to_texture(blk_t *b)
{
    if (eng_texture(b->engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Pixel Value: the color of the target's slot at (U, V) (or the U and V
 * inputs; in pixels without Homogeneous), wrapped, mirrored or clamped as
 * Address Mode says (read from the third input either way, as the DLL does);
 * Out Range too for a point off the image.
 */
static int bb_pixel_value(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = eng_texture(engine, target);
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool use_2d = get_local_int(b, 0, 1) != 0, homogeneous = get_local_int(b, 1, 0) != 0;
    int32_t mode = get_in_int(b, 2, VXTEXTURE_ADDRESSWRAP), slot = get_in_int(b, 0, 0);
    float uv[2] = {0.0f, 0.0f};
    if (use_2d) {
        nmo_engine_block_get_input_value(b, 1, uv, sizeof(uv));
    } else {
        uv[0] = get_in_float(b, 1, 0.0f);
        uv[1] = get_in_float(b, 2, 0.0f);
    }
    int32_t width = 0, height = 0;
    eng_bitmap_size(engine, texture, &width, &height);
    double u = uv[0], v = uv[1];
    if (!homogeneous) {
        u = (double)(float)(u / (double)width);
        v = (double)(float)(v / (double)height);
    }
    if (u < 0.0 || v < 0.0 || !(u < 1.0) || !(v < 1.0)) {
        OUT_ON(b, 1);
    }
    double *coords[2] = {&u, &v};
    for (int k = 0; k < 2; k++) {
        double *c = coords[k];
        if (mode == VXTEXTURE_ADDRESSWRAP || mode == VXTEXTURE_ADDRESSMIRROR) {
            int64_t whole = (int64_t)*c;
            *c -= (double)whole;
            if (*c < 0.0) {
                *c += 1.0;
            }
            if (mode == VXTEXTURE_ADDRESSMIRROR && (whole & 1)) {
                *c = 1.0 - *c;
            }
        } else if (mode == VXTEXTURE_ADDRESSCLAMP || mode == VXTEXTURE_ADDRESSBORDER) {
            *c = *c < 0.0 ? 0.0 : *c > 1.0 ? 1.0 : *c;
        }
        *c = (double)(float)*c;
    }
    int32_t x = (int32_t)(int64_t)((double)(width - 1) * u), y = (int32_t)(int64_t)((double)(height - 1) * v);
    eng_surface_t *surface = eng_texture_surface(engine, texture, slot);
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (surface != NULL && surface->rgba != NULL && x >= 0 && y >= 0 && x < surface->width && y < surface->height) {
        const uint8_t *p = texel(surface, (size_t)y * (size_t)surface->width + (size_t)x);
        for (int c = 0; c < 4; c++) {
            color[c] = (float)((double)p[c] * 0.0039215689);
        }
    }
    set_out(b, 0, color, sizeof(color));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Texture Info: the target's movie file, or its first slot's, as a name
 * and extension, and its priority; Exist, or Empty for a texture with no
 * image.
 */
static int bb_get_texture_info(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = eng_texture(engine, target);
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_bitmap_slots_t bitmap;
    const char *path = NULL;
    if (eng_bitmap_view(engine, target, &bitmap)) {
        path = bitmap.has_movie_filename ? bitmap.movie_filename : NULL;
        if (path == NULL && bitmap.has_slot_filenames && bitmap.slot_count > 0) {
            path = bitmap.slot_filenames[0];
        }
    }
    if (texture->files.count > 0 && texture->files.data[0] != NULL && !texture->movie) {
        path = texture->files.data[0];
    }
    const char *name = path != NULL ? path : "";
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '\\' || *c == '/' || *c == ':') {
            name = c + 1;
        }
    }
    nmo_engine_block_set_output_text(b, 0, name);
    nmo_object_t *object = eng_find_object(engine, target);
    const nmo_beobject_state_t *state = object != NULL
        ? (const nmo_beobject_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                          CKPGUID_BEOBJECT)
        : NULL;
    int32_t at = eng_map_get(&engine->object_map, target);
    set_out_int(b, 1, at >= 0 ? engine->objects.data[at].priority : state != NULL ? state->priority : 0);
    int32_t width = 0, height = 0;
    OUT_ON(b, eng_bitmap_size(engine, texture, &width, &height) ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The configuration, the console, the driver and the listener
 * ============================================================================ */

/* VxConfig's key: HKEY_CURRENT_USER\Software\Virtools\UserConfig\<section> */
static char *config_key(blk_t *b)
{
    const char *section = input_string(b, 0);
    section = section != NULL ? section : "";
    size_t length = strlen(section);
    static const char base[] = "Software\\Virtools\\UserConfig\\";
    char *key = (char *)malloc(sizeof(base) + length);
    if (key != NULL) {
        memcpy(key, base, sizeof(base) - 1u);
        memcpy(key + sizeof(base) - 1u, section, length + 1u);
    }
    return key;
}

/* Read Config: entry Entry of Section into Data (its string value); Entry Not Present (and an
   empty Data) when it is missing or longer than 255 characters. */
static int bb_read_config(blk_t *b)
{
    IN_OFF(b, 0);
    char *key = config_key(b);
    const char *entry = input_string(b, 1);
    const char *value = key != NULL ? eng_registry_value(b->engine, key, entry != NULL ? entry : "") : NULL;
    bool ok = value != NULL && strlen(value) < 256u;
    OUT_ON(b, ok ? 0 : 1);
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_engine_block_set_output_text(b, 0, ok ? value : "");
    }
    free(key);
    return NMO_ENGINE_BR_OK;
}

/* Write Config: Data as text into entry Entry of Section. */
static int bb_write_config(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    char *key = config_key(b);
    const char *entry = input_string(b, 1);
    size_t size = nmo_engine_block_input_text(b, 2, NULL, 0);
    char *text = (char *)malloc(size > 0 ? size : 1u);
    if (key != NULL && text != NULL) {
        text[0] = '\0';
        if (size > 0) {
            nmo_engine_block_input_text(b, 2, text, size);
        }
        nmo_engine_set_registry_value(b->engine, key, entry != NULL ? entry : "", text);
    }
    free(text);
    free(key);
    return NMO_ENGINE_BR_OK;
}

/* Output To Console: the string, after "<target name> : " with Output Target? (a beep with
   Beep?). */
static int bb_output_to_console(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *text = input_string(b, 0);
    text = text != NULL ? text : "";
    bool with_target = get_in_int(b, 1, 0) != 0, beep = get_in_int(b, 2, 0) != 0;
    if (!with_target) {
        eng_console(b->engine, text, beep);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    const char *name = target != 0 ? nmo_engine_object_name(b->engine, target) : NULL;
    name = name != NULL ? name : "No Target";
    size_t a = strlen(name), c = strlen(text);
    char *line = (char *)malloc(a + c + 4u);
    if (line != NULL) {
        memcpy(line, name, a);
        memcpy(line + a, " : ", 3);
        memcpy(line + a + 3, text, c + 1u);
        eng_console(b->engine, line, beep);
    }
    free(line);
    return NMO_ENGINE_BR_OK;
}

/* Hardware Level: TL and Hardware: the host renders with hardware transform and lighting. */
static int bb_hardware_level(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Set Listener Properties: the sound manager's distance, Doppler and roll off factors and its
   global gain, each when its input has a value. */
static int bb_set_listener_properties(blk_t *b)
{
    eng_sound_manager_t *manager = eng_sound_manager(b->engine);
    float value = 1.0f;
    if (nmo_engine_block_get_input_value(b, 0, &value, sizeof(value)) == NMO_OK) {
        manager->global_gain = value;
    }
    if (nmo_engine_block_get_input_value(b, 1, &value, sizeof(value)) == NMO_OK) {
        manager->rolloff_factor = value;
    }
    if (nmo_engine_block_get_input_value(b, 2, &value, sizeof(value)) == NMO_OK) {
        manager->doppler_factor = value;
    }
    if (nmo_engine_block_get_input_value(b, 3, &value, sizeof(value)) == NMO_OK) {
        manager->distance_factor = value;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Material Effect: the target material's effect (CKMaterial::SetEffect)
 * and, for a texture generation effect, the generation its parameter input
 * gives. The engine draws one texture a material, so Texture 1-3 are not
 * kept.
 */
static int bb_set_material_effect(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t effect = get_in_int(b, 0, 0);
    material->desc.texgen = 0;
    material->desc.texgen_referential = 0;
    if (effect == 1 /* VXEFFECT_TEXGEN */ && nmo_engine_block_input_parameter_count(b) > 4) {
        material->desc.texgen = get_in_int(b, 4, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Gauge
 * ============================================================================ */

/*
 * Gauge (the DLL's, without the later Pause/Resume): On keeps the target 2D
 * entity's rect; each frame after, a shown entity's rect is that rect cut to
 * Bar Progression toward the Orientation (from the center, the left, right,
 * top or bottom, or a centered line), its source rect cut alike without
 * Stretch (a sprite's in pixels, another's in fractions). Off stops.
 */
static int bb_gauge(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_2d_t *entity = target != 0 ? eng_2d_entity(engine, target) : NULL;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        OUT_ON(b, 0);
        IN_OFF(b, 0);
        if (entity == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        nmo_rect_t kept = (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) ? entity->homogeneous : entity->rect;
        nmo_engine_block_set_local_value(b, 0, &kept, sizeof(kept));
    }
    if (entity == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (!(eng_object_visibility(engine, target) & CK_OBJECT_VISIBLE)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    nmo_rect_t rect = {0};
    nmo_engine_block_get_local_value(b, 0, &rect, sizeof(rect));
    float p = get_in_float(b, 0, 0.0f);
    int32_t orientation = get_in_int(b, 1, 0);
    bool stretch = get_in_int(b, 2, 1) != 0;
    float w = rect.right - rect.left, h = rect.bottom - rect.top, x = 1.0f, y = 1.0f;
    nmo_rect_t dest = rect;
    switch (orientation) {
    case 1: /* center */
        x = y = p;
        dest.left = (1.0f - p) * w * 0.5f + rect.left;
        dest.top = (1.0f - p) * h * 0.5f + rect.top;
        break;
    case 2: x = -p; break; /* left */
    case 4: x = p; break;  /* right */
    case 8: y = -p; break; /* top */
    case 16: y = p; break; /* bottom */
    case 32: /* vertical center */
        y = p;
        dest.top = (1.0f - p) * h * 0.5f + rect.top;
        break;
    case 64: /* horizontal center */
        x = p;
        dest.left = (1.0f - p) * w * 0.5f + rect.left;
        break;
    default:
        break;
    }
    dest.right = x * w + dest.left;
    dest.bottom = y * h + dest.top;
    if (dest.right < dest.left) {
        float t = dest.right;
        dest.right = dest.left;
        dest.left = t;
    }
    if (dest.bottom < dest.top) {
        float t = dest.top;
        dest.top = dest.bottom;
        dest.bottom = t;
    }
    if (stretch) {
        entity->flags &= ~(uint32_t)CK_2DENTITY_USESRCRECT;
    } else {
        float image_w = 1.0f, image_h = 1.0f;
        nmo_object_t *object = eng_find_object(engine, target);
        eng_texture_t *bitmap = object != NULL && nmo_object_get_class_id(object) == NMO_CID_SPRITE
                                    ? eng_texture(engine, target) : NULL;
        int32_t bw = 0, bh = 0;
        if (bitmap != NULL) {
            eng_bitmap_size(engine, bitmap, &bw, &bh);
            image_w = (float)bw, image_h = (float)bh;
        }
        nmo_rect_t source = {0.0f, 0.0f, 0.0f, 0.0f};
        if (orientation & 32) {
            source.top = (1.0f - y) * image_h * 0.5f;
        }
        if (orientation & 64) {
            source.left = (1.0f - x) * image_w * 0.5f;
        }
        if (orientation & 1) {
            source.left += (1.0f - x) * image_w * 0.5f;
            source.top += (1.0f - y) * image_h * 0.5f;
        }
        source.right = fabsf(x) * image_w + source.left;
        source.bottom = fabsf(y) * image_h + source.top;
        entity->flags |= CK_2DENTITY_USESRCRECT;
        entity->source = source;
    }
    if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        eng_2d_set_homogeneous_rect(engine, target, dest, false);
    } else {
        eng_2d_set_rect(engine, target, dest, false);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Replace Rendering (the Addons manager's frame requests)
 * ============================================================================ */

/* A material swapped while an entity (Per Object) or a mesh renders this frame */
typedef struct addons_replace {
    nmo_object_id_t entity, mesh;
    nmo_object_id_t from, to;
} addons_replace_t;

typedef struct addons_state {
    ENG_VEC(addons_replace_t) replaces;
} addons_state_t;

static void addons_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((addons_state_t *)state)->replaces.data);
}

/* The temporary render callbacks of this frame: the draws of the entity (its own render) or of
   the mesh (any entity's) take the swap; gone after the frame */
static void addons_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage, const eng_render_args_t *args)
{
    addons_state_t *addons = (addons_state_t *)state;
    if (stage != ENG_RENDER_SCENE || args->pass) {
        return;
    }
    for (size_t r = 0; r < addons->replaces.count; r++) {
        const addons_replace_t *swap = &addons->replaces.data[r];
        for (size_t d = 0; d < engine->render.draws.count; d++) {
            nmo_engine_draw_t *draw = &engine->render.draws.data[d];
            if (draw->kind != NMO_ENGINE_DRAW_MESH || swap->from == 0 ||
                (swap->entity != 0 ? draw->object != swap->entity : draw->mesh != swap->mesh)) {
                continue;
            }
            draw->replaced_material = swap->from;
            draw->replacing_material = swap->to;
        }
    }
    addons->replaces.count = 0;
}

static const eng_manager_desc_t addons_manager = {
    .name = "Addons1 Render Callbacks",
    .state_size = sizeof(addons_state_t),
    .destroy = addons_destroy,
    .render = addons_render,
};

/*
 * Replace Rendering: while On, each frame the target's current mesh renders
 * In Material's faces with Out Material (Per Object: only when the target
 * renders it; else whoever renders the mesh). Off stops.
 */
static int bb_replace_rendering(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t entity = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, entity);
    if (index < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_t *object = eng_find_object(engine, entity);
    const nmo_3dentity_state_t *state = object != NULL
        ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                          CKPGUID_3DENTITY)
        : NULL;
    nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, entity, &state->current_mesh) : 0;
    addons_state_t *addons = (addons_state_t *)eng_manager_state(engine, &addons_manager);
    addons_replace_t *swap = NULL;
    if (mesh != 0 && addons != NULL && ENG_VEC_PUSH(addons->replaces, swap)) {
        *swap = (addons_replace_t){get_local_int(b, 0, 0) != 0 ? entity : 0, mesh,
                                   nmo_engine_block_get_input_object(b, 0), nmo_engine_block_get_input_object(b, 1)};
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Synchro Recorder and Synchro Player
 * ============================================================================ */

/* Synchro Recorder's inner struct (its local buffer): the array, the time now, the start, when
   the key went down, and whether no key is down */
typedef struct synchro_recorder {
    nmo_object_id_t array;
    float now, start, pressed;
    bool released;
} synchro_recorder_t;

static void synchro_set(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column, float value)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_FLOAT};
    memcpy(&cell.value, &value, sizeof(value));
    nmo_engine_array_set_cell(engine, array, row, column, &cell);
}

/* The end: a row at the time recorded with an amplitude of 0 */
static void synchro_finish(nmo_engine_t *engine, const synchro_recorder_t *rec)
{
    int32_t row = nmo_engine_array_row_count(engine, rec->array);
    if (nmo_engine_array_insert_row(engine, rec->array, -1, NULL) == NMO_OK) {
        synchro_set(engine, rec->array, row, 0, rec->now - rec->start);
        synchro_set(engine, rec->array, row, 1, 0.0f);
    }
}

/*
 * Synchro Recorder: On (re)makes the array named Synchro Array Name (made in
 * the level when missing) with Time, Amplitude and Length columns; each frame
 * the first of the Key inputs held down adds a row (its time since On and an
 * amplitude from Min to Max by the key's rank), and its release sets that
 * row's length; Escape ends the recording. The DLL times it by the real
 * clock: the engine by its frame time.
 */
static int bb_synchro_recorder(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    synchro_recorder_t *rec = (synchro_recorder_t *)nmo_engine_block_state(b, sizeof(synchro_recorder_t));
    if (rec == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float now = (float)engine->time;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        const char *name = input_string(b, 0);
        name = name != NULL ? name : "";
        const char *held = rec->array != 0 ? nmo_engine_object_name(engine, rec->array) : NULL;
        if (rec->array == 0 || held == NULL || strcmp(held, name) != 0) {
            rec->array = eng_object_named(engine, name, NMO_CID_DATAARRAY);
            if (rec->array == 0) {
                rec->array = eng_create_object(engine, NMO_CID_DATAARRAY, name);
            }
        }
        if (rec->array == 0) {
            return NMO_ENGINE_BR_OK;
        }
        nmo_engine_array_clear(engine, rec->array);
        while (nmo_engine_array_column_count(engine, rec->array) > 0) {
            nmo_engine_array_remove_column(engine, rec->array, 0);
        }
        static const char *const names[3] = {"Time", "Amplitude", "Length"};
        for (int c = 0; c < 3; c++) {
            nmo_engine_array_insert_column(engine, rec->array, -1, CKARRAYTYPE_FLOAT, NMO_GUID(0u, 0u));
            nmo_engine_array_set_column_name(engine, rec->array, c, names[c]);
        }
        rec->start = now;
        rec->released = true;
    }
    if (rec->array == 0) {
        return NMO_ENGINE_BR_OK;
    }
    rec->now = now;
    float min = get_in_float(b, 1, 0.0f), max = get_in_float(b, 2, 1.0f);
    if (nmo_engine_is_key_down(engine, 1u)) { /* Escape */
        synchro_finish(engine, rec);
        return NMO_ENGINE_BR_OK;
    }
    int pins = nmo_engine_block_input_parameter_count(b);
    for (int k = 3; k < pins; k++) {
        if (!nmo_engine_is_key_down(engine, (uint32_t)get_in_int(b, k, 0))) {
            continue;
        }
        if (rec->released) {
            rec->released = false;
            rec->pressed = now;
            float amplitude = (float)((double)(max - min) * (double)(k - 2) / (double)(pins - 3) + (double)min);
            int32_t row = nmo_engine_array_row_count(engine, rec->array);
            if (nmo_engine_array_insert_row(engine, rec->array, -1, NULL) == NMO_OK) {
                synchro_set(engine, rec->array, row, 0, now - rec->start);
                synchro_set(engine, rec->array, row, 1, amplitude);
            }
        }
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (!rec->released) {
        rec->released = true;
        synchro_set(engine, rec->array, nmo_engine_array_row_count(engine, rec->array) - 1, 2, now - rec->pressed);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Synchro Recorder's reset: a recording left with a key's amplitude last is ended */
static int cb_synchro_recorder(blk_t *b, uint32_t message)
{
    synchro_recorder_t *rec = (synchro_recorder_t *)nmo_engine_block_state(b, sizeof(synchro_recorder_t));
    if (message != NMO_ENGINE_MSG_RESET || rec == NULL || rec->array == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t rows = nmo_engine_array_row_count(b->engine, rec->array);
    nmo_engine_cell_t cell = {0};
    float amplitude = 0.0f;
    if (rows > 0 && nmo_engine_array_get_cell(b->engine, rec->array, rows - 1, 1, &cell) == NMO_OK) {
        memcpy(&amplitude, &cell.value, sizeof(amplitude));
        if (amplitude != 0.0f) {
            synchro_finish(b->engine, rec);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* Synchro Player's inner struct: when it started and the row it is at */
typedef struct synchro_player {
    float start;
    int32_t line;
} synchro_player_t;

static float array_float(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column)
{
    nmo_engine_cell_t cell = {0};
    float value = 0.0f;
    if (nmo_engine_array_get_cell(engine, array, row, column, &cell) == NMO_OK) {
        memcpy(&value, &cell.value, sizeof(value));
    }
    return value;
}

/*
 * Synchro Player: On starts the array over; each row whose time has come
 * gives its amplitude and length and an Impulse (the row held through its
 * length with Keep Activated During Delay), the last row End. Off stops.
 */
static int bb_synchro_player(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    synchro_player_t *player = (synchro_player_t *)nmo_engine_block_state(b, sizeof(synchro_player_t));
    if (player == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    float now = (float)engine->time;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        player->start = now;
        player->line = 0;
    }
    float time = array_float(engine, array, player->line, 0);
    if (time + player->start <= now) {
        if (player->line == nmo_engine_array_row_count(engine, array) - 1) {
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_OK;
        }
        float amplitude = array_float(engine, array, player->line, 1);
        float delay = array_float(engine, array, player->line, 2);
        if (get_local_int(b, 1, 0) == 0 || delay + player->start + time < now) {
            player->line++;
        }
        set_out_float(b, 0, amplitude);
        set_out_float(b, 1, delay);
        OUT_ON(b, 1);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_addons_blocks[] = {
    {NMO_GUID_INIT(0x798e26a9, 0x34e4169e), "Movie Load", bb_movie_load, NULL, NULL},
    {NMO_GUID_INIT(0x060a0f8c, 0x09203b5f), "Time Settings", bb_time_settings, NULL, NULL},
    {NMO_GUID_INIT(0x52df323c, 0x39dc1211), "Filter Texture", bb_filter_texture, NULL, NULL},
    {NMO_GUID_INIT(0x53ee4292, 0x7d932bb1), "Blend Textures", bb_blend_textures, NULL, NULL},
    {NMO_GUID_INIT(0x84be2329, 0xaed66ce1), "Combine Texture", bb_combine_texture, NULL, NULL},
    {NMO_GUID_INIT(0x3df93ddc, 0x9c89c141), "Water Texture", bb_water_texture, NULL, NULL},
    {NMO_GUID_INIT(0x0ef6070d, 0x67166889), "Copy Buffer to Texture", bb_copy_buffer_to_texture, NULL, NULL},
    {NMO_GUID_INIT(0x473167b6, 0x48f75a1d), "Pixel Value", bb_pixel_value, NULL, NULL},
    {NMO_GUID_INIT(0x250c4d8d, 0x288960d9), "Get Texture Info", bb_get_texture_info, NULL, NULL},
    {NMO_GUID_INIT(0x4a888d94, 0x21ccccfa), "Read Config", bb_read_config, NULL, NULL},
    {NMO_GUID_INIT(0x4a797d94, 0x21ab5cfa), "Write Config", bb_write_config, NULL, NULL},
    {NMO_GUID_INIT(0x18655b3f, 0x68291dc3), "Output To Console", bb_output_to_console, NULL, NULL},
    {NMO_GUID_INIT(0x1b241485, 0x1f572aac), "Hardware Level", bb_hardware_level, NULL, NULL},
    {NMO_GUID_INIT(0xbcfde546, 0xfeefd18b), "Set Listener Properties", bb_set_listener_properties, NULL, NULL},
    {NMO_GUID_INIT(0x3bd73009, 0x7aa01dac), "Set Material Effect", bb_set_material_effect, NULL, NULL},
    {NMO_GUID_INIT(0x6a777fc4, 0x1dc17b23), "Gauge", bb_gauge, NULL, NULL},
    {NMO_GUID_INIT(0x208f0030, 0x652f088e), "Replace Rendering", bb_replace_rendering, NULL, NULL},
    {NMO_GUID_INIT(0x62ea3812, 0x1ed661a6), "Synchro Recorder", bb_synchro_recorder, cb_synchro_recorder, NULL},
    {NMO_GUID_INIT(0x62da29d8, 0x1bbc6234), "Synchro Player", bb_synchro_player, NULL, NULL},
};

nmo_status_t eng_register_addons(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &addons_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_addons_blocks) : status;
}
