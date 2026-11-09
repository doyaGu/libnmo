/**
 * @file addons.c
 * @brief Ports of the BuildingBlocksAddons1 building blocks.
 *
 * Each function does what the block's function in BuildingBlocksAddons1.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ).
 */

#include "virtools.h"
#include "../../engine/engine_animation.h"
#include "core/nmo_arena.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
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
 * Source rectangle into the target's Destination one (face 0) when they have
 * the same size. The host renders, so the copy goes with the next frame (see
 * nmo_engine_buffer_copy_t).
 */
static int bb_copy_buffer_to_texture(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float src[4] = {0.0f, 0.0f, 0.0f, 0.0f}, dest[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, src, sizeof(src));
    nmo_engine_block_get_input_value(b, 1, dest, sizeof(dest));
    if (eng_texture(b->engine, target) != NULL && dest[2] - dest[0] == src[2] - src[0] &&
        dest[3] - dest[1] == src[3] - src[1]) {
        eng_render_copy_buffer(b->engine, target, 0, src, dest);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
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
    ENG_VEC(int32_t) flares; /* Lens Flare blocks with this frame's post render callback */
    ENG_VEC(int32_t) shadows; /* ShadowStencil blocks with this frame's post render callback */
} addons_state_t;

static void lens_flare_render(nmo_engine_t *engine, int32_t node);
static void shadow_stencil_render(nmo_engine_t *engine, int32_t node);

static void addons_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((addons_state_t *)state)->replaces.data);
    free(((addons_state_t *)state)->flares.data);
    free(((addons_state_t *)state)->shadows.data);
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
    for (size_t f = 0; f < addons->flares.count; f++) {
        lens_flare_render(engine, addons->flares.data[f]);
    }
    addons->flares.count = 0;
    for (size_t i = 0; i < addons->shadows.count; i++) {
        shadow_stencil_render(engine, addons->shadows.data[i]);
    }
    addons->shadows.count = 0;
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


/* ============================================================================
 * Volumetric Fog and Lens Flare
 * ============================================================================ */

/*
 * Volumetric Fog (the DLL's, which computes in the block): each shown object
 * of Group with a mesh gets a channel of Material (added once, blended ONE /
 * INVSRCCOLOR) whose V coordinate is the fog the eye sees through to the
 * vertex: its distance along the eye's axis times FogDensity, for vertices
 * below the eye (on the fog's up side of the target); with the eye inside
 * the fog, less the part above the fog's top. An object all outside the last
 * frame's view is left as it is.
 */
static int bb_volumetric_fog(blk_t *b)
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
    int32_t fog = eng_entity_index(engine, nmo_engine_block_target(b));
    if (fog < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    float density = get_in_float(b, 1, 0.0099999998f);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 2);
    int32_t count = group != 0 ? nmo_engine_group_count(engine, group) : 0;
    if (group == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t camera = eng_entity_index(engine, engine->render.camera);
    for (int32_t g = 0; g < count; g++) {
        nmo_object_id_t id = nmo_engine_group_object(engine, group, g);
        int32_t index = eng_entity_index(engine, id);
        nmo_object_t *object = index >= 0 ? eng_find_object(engine, id) : NULL;
        const nmo_3dentity_state_t *state = object != NULL
            ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                              CKPGUID_3DENTITY)
            : NULL;
        nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
        eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy(engine, mesh) : NULL;
        if (copy == NULL || camera < 0 || !(eng_object_visibility(engine, id) & CK_OBJECT_VISIBLE) ||
            eng_entity_all_outside_frustum(engine, index)) {
            continue;
        }
        int32_t channel = eng_mesh_channel(copy, material);
        if (channel < 0) {
            channel = eng_mesh_add_channel(copy, material);
            if (channel < 0) {
                continue;
            }
            copy->channels.data[channel].source_blend = VXBLEND_ONE;
            copy->channels.data[channel].dest_blend = VXBLEND_INVSRCCOLOR;
        }
        size_t stride = 0;
        int32_t vertices = 0;
        float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &vertices);
        if (uvs == NULL) {
            continue;
        }
        const eng_vector_t axis_y = {0.0f, 1.0f, 0.0f}, axis_z = {0.0f, 0.0f, 1.0f};
        eng_vector_t up, eye_z;
        eng_entity_transform_vector(engine, index, &up, &axis_y, fog, true);
        eng_vector_t eye = eng_entity_position(engine, camera, index), base = eng_entity_position(engine, fog, index);
        float height = (eye.x - base.x) * up.x + (eye.y - base.y) * up.y + (eye.z - base.z) * up.z;
        float height_density = height * density;
        eng_entity_transform_vector(engine, index, &eye_z, &axis_z, camera, true);
        eng_vector_t scale = eng_entity_scale(engine, index, true);
        eye_z = (eng_vector_t){-eye_z.x * scale.x * scale.x, -eye_z.y * scale.y * scale.y, -eye_z.z * scale.z * scale.z};
        for (int32_t v = 0; v < vertices; v++) {
            const nmo_vector_t *p = &copy->vertices[v].position;
            eng_vector_t apart = {eye.x - p->x, eye.y - p->y, eye.z - p->z};
            float depth = apart.x * up.x + apart.y * up.y + apart.z * up.z;
            float along = apart.x * eye_z.x + apart.y * eye_z.y + apart.z * eye_z.z;
            float *uv = (float *)((uint8_t *)uvs + (size_t)v * stride);
            if (!(depth + 0.001f > height)) {
                uv[1] = 0.0f;
            } else if (height < 0.0f) {
                uv[1] = along * density;
            } else {
                uv[1] = along * (density - height_density / depth);
            }
        }
        copy->version = ++engine->render.mesh_version;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* A flare of Lens Flare (the DLL's 64 bytes), from a row of its Flare Array */
typedef struct addons_flare {
    float position;          /* along the line from the eye to the light */
    float rotation;          /* how much it turns with its place on screen */
    float size[2];
    int16_t fade_in, fade_out; /* milliseconds */
    float time;              /* into its fade */
    float cos_limit;         /* shown while the light is within this cosine of the view's axis */
    int32_t state;           /* 0 off, 1 fading in, 2 fading out, 4 shown */
    float color[4];
    float rect[4];           /* its part of the texture in pixels; left = right: all of it */
} addons_flare_t;

static const addons_flare_t addons_flare_default = {1.0f, 0.0f, {1.0f, 1.0f}, 100, 100, 0.0f, 0.0f, 0,
                                                    {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};

static addons_flare_t *flares_of(nmo_engine_t *engine, int32_t node, int32_t *count)
{
    const eng_node_t *block = &engine->nodes.data[node];
    eng_param_t *param = eng_node_param(engine, block->first_local, block->local_count, 0);
    *count = param != NULL && param->data != NULL ? (int32_t)(param->size / sizeof(addons_flare_t)) : 0;
    return *count > 0 ? (addons_flare_t *)param->data : NULL;
}

/* The flares from Flare Array: as many as its rows (all back to their defaults when that
   changes), each from the columns that have the types expected */
static void lens_flare_read(blk_t *b, nmo_object_id_t array)
{
    nmo_engine_t *engine = b->engine;
    const eng_array_t *runtime = eng_find_array(engine, array);
    int32_t rows = nmo_engine_array_row_count(engine, array), count = 0;
    eng_param_t *param = block_local(b, 0);
    if (runtime == NULL || param == NULL || rows < 0) {
        return;
    }
    flares_of(engine, b->node, &count);
    if (count != rows) {
        if (!eng_param_resize(param, (size_t)rows * sizeof(addons_flare_t))) {
            return;
        }
        for (int32_t i = 0; i < rows; i++) {
            memcpy(param->data + (size_t)i * sizeof(addons_flare_t), &addons_flare_default, sizeof(addons_flare_t));
        }
    }
    addons_flare_t *flares = flares_of(engine, b->node, &count);
    for (int32_t i = 0; i < count; i++) {
        addons_flare_t *f = &flares[i];
        nmo_engine_cell_t cell = {0};
        nmo_guid_t types[8] = {{0}};
        for (uint32_t c = 0; c < 8 && c < runtime->column_count; c++) {
            types[c] = runtime->columns[c].type == CKARRAYTYPE_PARAMETER ? runtime->columns[c].param_type : NMO_GUID(0u, 0u);
        }
        if (nmo_guid_equals(types[0], CKPGUID_RECT) && nmo_engine_array_get_cell(engine, array, i, 0, &cell) == NMO_OK &&
            cell.data != NULL && cell.size >= sizeof(f->rect)) {
            memcpy(f->rect, cell.data, sizeof(f->rect));
        }
        if (nmo_engine_array_column_type(engine, array, 1) == CKARRAYTYPE_FLOAT &&
            nmo_engine_array_get_cell(engine, array, i, 1, &cell) == NMO_OK) {
            memcpy(&f->position, &cell.value, sizeof(float));
        }
        if (nmo_guid_equals(types[2], CKPGUID_COLOR) && nmo_engine_array_get_cell(engine, array, i, 2, &cell) == NMO_OK &&
            cell.data != NULL && cell.size >= sizeof(f->color)) {
            memcpy(f->color, cell.data, sizeof(f->color));
        }
        if (nmo_guid_equals(types[3], CKPGUID_2DVECTOR) && nmo_engine_array_get_cell(engine, array, i, 3, &cell) == NMO_OK &&
            cell.data != NULL && cell.size >= sizeof(f->size)) {
            memcpy(f->size, cell.data, sizeof(f->size));
        }
        if (nmo_engine_array_column_type(engine, array, 4) == CKARRAYTYPE_FLOAT &&
            nmo_engine_array_get_cell(engine, array, i, 4, &cell) == NMO_OK) {
            memcpy(&f->rotation, &cell.value, sizeof(float));
        }
        if (nmo_engine_array_column_type(engine, array, 5) == CKARRAYTYPE_INT &&
            nmo_engine_array_get_cell(engine, array, i, 5, &cell) == NMO_OK) {
            f->fade_in = (int16_t)(int32_t)cell.value;
        }
        if (nmo_engine_array_column_type(engine, array, 6) == CKARRAYTYPE_INT &&
            nmo_engine_array_get_cell(engine, array, i, 6, &cell) == NMO_OK) {
            f->fade_out = (int16_t)(int32_t)cell.value;
        }
        if (nmo_guid_equals(types[7], CKPGUID_ANGLE) && nmo_engine_array_get_cell(engine, array, i, 7, &cell) == NMO_OK) {
            float angle = 0.0f;
            if (cell.data != NULL && cell.size >= sizeof(float)) {
                memcpy(&angle, cell.data, sizeof(float));
            }
            if (angle == 0.0f) {
                angle = 1.5700001f;
            }
            f->cos_limit = (float)cos((double)angle);
        }
    }
}

/* A shown flare starts fading out (from where its fade in got to) */
static void flare_fade_out(addons_flare_t *f)
{
    f->time = (f->state & 1) ? (float)((double)f->fade_out * (double)f->time / (double)f->fade_in) : (float)f->fade_out;
    f->state = 2;
}

/* The render context's post render callback: the flares fade in while the light is seen (not
   picked behind something) within their cone, out otherwise, by the frame's time; then draw as
   quads on a plane twice the near plane in front of the eye, alpha blended additively */
static void lens_flare_render(nmo_engine_t *engine, int32_t node)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t count = 0;
    addons_flare_t *flares = flares_of(engine, node, &count);
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t light = eng_entity_index(engine, target), camera = eng_entity_index(engine, engine->render.camera);
    float near_plane = 0.0f, far_plane = 0.0f;
    if (flares == NULL || light < 0 || camera < 0 ||
        !eng_camera_planes(engine, engine->render.camera, &near_plane, &far_plane)) {
        return;
    }
    float front = (float)(near_plane + 0.0099999998);
    eng_vector_t world = eng_entity_position(engine, light, -1), local;
    eng_entity_inverse_transform(engine, camera, &local, &world, -1);
    double length = sqrt((double)(local.x * local.x + local.z * local.z + local.y * local.y));
    float facing = (float)(1.0 / sqrt((double)(front * front)) * front * (local.z / length));
    eng_vector_t screen;
    eng_render_project(engine, &world, &screen);
    nmo_engine_pick_t pick;
    memset(&pick, 0, sizeof(pick));
    bool hit = engine->input.pick != NULL &&
               engine->input.pick(engine->input.pick_user_data, (int32_t)(int64_t)screen.x, (int32_t)(int64_t)screen.y, &pick);
    eng_vector_t eye = eng_entity_position(engine, camera, -1);
    double apart = sqrt((double)((eye.x - world.x) * (eye.x - world.x) + (eye.y - world.y) * (eye.y - world.y) +
                                 (eye.z - world.z) * (eye.z - world.z)));
    if (!hit || apart <= (double)pick.distance) {
        for (int32_t i = 0; i < count; i++) {
            addons_flare_t *f = &flares[i];
            if (f->cos_limit >= facing) {
                if (f->state != 0 && !(f->state & 2)) {
                    flare_fade_out(f);
                }
            } else if (f->state == 0 || (f->state & 2)) {
                f->time = (f->state & 2) ? (float)((double)f->fade_in * (double)f->time / (double)f->fade_out) : 0.0f;
                f->state = 1;
            }
        }
    } else {
        bool any = false;
        for (int32_t i = 0; i < count; i++) {
            if (flares[i].state != 0) {
                any = true;
                if (!(flares[i].state & 2)) {
                    flare_fade_out(&flares[i]);
                }
            }
        }
        if (!any) {
            return;
        }
    }
    float multiplier[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, multiplier, sizeof(multiplier));
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 1);
    eng_texture_t *bitmap = texture != 0 ? eng_texture(engine, texture) : NULL;
    int32_t tw = 0, th = 0;
    float su = 1.0f, sv = 1.0f;
    if (bitmap != NULL && eng_bitmap_size(engine, bitmap, &tw, &th) && tw > 0 && th > 0) {
        su = (float)(1.0 / (double)tw);
        sv = (float)(1.0 / (double)th);
    }
    eng_vector_t *corners = (eng_vector_t *)malloc((size_t)count * 4u * sizeof(*corners));
    float (*uvs)[2] = (float (*)[2])malloc((size_t)count * 4u * sizeof(*uvs));
    uint32_t *colors = (uint32_t *)malloc((size_t)count * sizeof(*colors));
    int32_t quads = 0;
    float delta = engine->delta;
    const eng_matrix_t *view = &engine->entities.data[camera].world;
    for (int32_t i = 0; corners != NULL && uvs != NULL && colors != NULL && i < count; i++) {
        addons_flare_t *f = &flares[i];
        float k = 1.0f;
        if (f->state == 1) {
            f->time += delta;
            if ((double)f->fade_in >= (double)f->time) {
                k = (float)((double)f->time / (double)f->fade_in);
            } else {
                f->state = 4;
            }
        } else if (f->state == 2) {
            f->time -= delta;
            if (f->time < 0.0f) {
                f->state = 0;
                continue;
            }
            k = (float)((double)f->time / (double)f->fade_out);
        } else if (f->state != 4) {
            continue;
        }
        float plane = front + front, scale = plane / local.z;
        float x = f->position * local.x * scale, y = f->position * local.y * scale;
        double angle = (double)((y + x) * f->rotation);
        float c = (float)cos(angle), s = (float)sin(angle);
        float sx = multiplier[0] * f->size[0], sy = multiplier[1] * f->size[1];
        float ax = sx * c, ay = sx * -s, bx = sy * s, by = c * sy;
        const eng_vector_t quad[4] = {{bx + (x - ax), by + (y - ay), plane},
                                      {bx + ax + x, by + ay + y, plane},
                                      {ax + x - bx, ay + y - by, plane},
                                      {x - ax - bx, y - ay - by, plane}};
        for (int q = 0; q < 4; q++) {
            eng_vector_transform(&corners[4 * quads + q], view, &quad[q]);
        }
        float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
        if (f->rect[0] != f->rect[2] && f->rect[3] != f->rect[1]) {
            u0 = su * f->rect[0], u1 = su * f->rect[2], v0 = sv * f->rect[1], v1 = sv * f->rect[3];
        }
        const float quad_uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        memcpy(uvs[4 * quads], quad_uv, sizeof(quad_uv));
        const float tone[4] = {f->color[0], f->color[1], f->color[2], k * f->color[3]};
        colors[quads++] = vxcolor_to_color(tone);
    }
    if (quads > 0) {
        eng_render_push_quads(engine, target, corners, (const float (*)[2])uvs, colors, quads, texture,
                              VXBLEND_SRCALPHA, VXBLEND_ONE);
    }
    free(corners);
    free(uvs);
    free(colors);
}

/*
 * Lens Flare: each frame the render context draws the target light's flares
 * (a post render callback); On reads them from Flare Array, and every frame
 * with Read Array Each Frame. Off stops.
 */
static int bb_lens_flare(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (eng_entity_index(engine, nmo_engine_block_target(b)) < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool read = get_local_int(b, 1, 0) != 0;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        read = true;
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    addons_state_t *addons = (addons_state_t *)eng_manager_state(engine, &addons_manager);
    int32_t *slot = NULL;
    if (addons != NULL && ENG_VEC_PUSH(addons->flares, slot)) {
        *slot = b->node;
    }
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    if (read && array != 0) {
        lens_flare_read(b, array);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}


/* ============================================================================
 * ShadowStencil
 * ============================================================================ */

/* A face edge for the neighbours: its vertices (merged by position, the lower first), its face */
typedef struct stencil_edge_key {
    int32_t a, b, face;
} stencil_edge_key_t;

static int stencil_edge_cmp(const void *x, const void *y)
{
    const stencil_edge_key_t *p = (const stencil_edge_key_t *)x, *q = (const stencil_edge_key_t *)y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    if (p->b != q->b) return p->b < q->b ? -1 : 1;
    return p->face < q->face ? -1 : p->face > q->face;
}

typedef struct stencil_vertex_key {
    nmo_vector_t p;
    int32_t index;
} stencil_vertex_key_t;

static int stencil_vertex_cmp(const void *x, const void *y)
{
    const stencil_vertex_key_t *p = (const stencil_vertex_key_t *)x, *q = (const stencil_vertex_key_t *)y;
    if (p->p.x != q->p.x) return p->p.x < q->p.x ? -1 : 1;
    if (p->p.y != q->p.y) return p->p.y < q->p.y ? -1 : 1;
    if (p->p.z != q->p.z) return p->p.z < q->p.z ? -1 : 1;
    return p->index < q->index ? -1 : p->index > q->index;
}

/*
 * NeigFaceStructure::BuildNeigFaceStruct: each face's neighbour across each
 * edge (vertices merged where they have the same position; an edge keeps the
 * first face that has it and the last; 0xFFFF for none).
 */
static uint16_t *stencil_neighbours(const nmo_vertex_t *vertices, int32_t vcount, const uint16_t *indices, int32_t fcount)
{
    stencil_vertex_key_t *sorted = vcount > 0 ? (stencil_vertex_key_t *)malloc((size_t)vcount * sizeof(*sorted)) : NULL;
    int32_t *merged = vcount > 0 ? (int32_t *)malloc((size_t)vcount * sizeof(int32_t)) : NULL;
    stencil_edge_key_t *edges = fcount > 0 ? (stencil_edge_key_t *)malloc((size_t)fcount * 3u * sizeof(*edges)) : NULL;
    uint16_t *neighbour = fcount > 0 ? (uint16_t *)malloc((size_t)fcount * 3u * sizeof(uint16_t)) : NULL;
    if (sorted == NULL || merged == NULL || edges == NULL || neighbour == NULL) {
        free(sorted), free(merged), free(edges), free(neighbour);
        return NULL;
    }
    for (int32_t v = 0; v < vcount; v++) {
        sorted[v] = (stencil_vertex_key_t){vertices[v].position, v};
    }
    qsort(sorted, (size_t)vcount, sizeof(*sorted), stencil_vertex_cmp);
    for (int32_t v = 0; v < vcount; v++) {
        bool same = v > 0 && sorted[v].p.x == sorted[v - 1].p.x && sorted[v].p.y == sorted[v - 1].p.y &&
                    sorted[v].p.z == sorted[v - 1].p.z;
        merged[sorted[v].index] = same ? merged[sorted[v - 1].index] : sorted[v].index;
    }
    for (int32_t f = 0; f < fcount; f++) {
        for (int k = 0; k < 3; k++) {
            int32_t i = merged[indices[3 * f + k] < vcount ? indices[3 * f + k] : 0];
            int32_t j = merged[indices[3 * f + (k + 1) % 3] < vcount ? indices[3 * f + (k + 1) % 3] : 0];
            edges[3 * f + k] = (stencil_edge_key_t){i < j ? i : j, i < j ? j : i, f};
        }
    }
    stencil_edge_key_t *order = (stencil_edge_key_t *)malloc((size_t)fcount * 3u * sizeof(*order));
    if (order == NULL) {
        free(sorted), free(merged), free(edges), free(neighbour);
        return NULL;
    }
    memcpy(order, edges, (size_t)fcount * 3u * sizeof(*order));
    qsort(order, (size_t)fcount * 3u, sizeof(*order), stencil_edge_cmp);
    for (int32_t f = 0; f < fcount; f++) {
        for (int k = 0; k < 3; k++) {
            stencil_edge_key_t key = edges[3 * f + k];
            /* the faces that have the edge: the first and the last (addEdge) */
            size_t lo = 0, hi = (size_t)fcount * 3u;
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                stencil_edge_key_t m = order[mid];
                if (m.a < key.a || (m.a == key.a && m.b < key.b)) lo = mid + 1;
                else hi = mid;
            }
            size_t end = lo;
            while (end < (size_t)fcount * 3u && order[end].a == key.a && order[end].b == key.b) {
                end++;
            }
            int32_t first = order[lo].face, last = end - lo > 1 ? order[end - 1].face : 0xFFFF;
            neighbour[3 * f + k] = (uint16_t)(first == f ? last : first);
        }
    }
    free(order), free(sorted), free(merged), free(edges);
    return neighbour;
}

static void stencil_vertex(nmo_engine_t *engine, const eng_matrix_t *world, const eng_vector_t *p, uint32_t color)
{
    eng_vector_t at;
    eng_vector_transform(&at, world, p);
    nmo_engine_vertex_t *vertex = NULL;
    if (ENG_VEC_PUSH(engine->render.vertices, vertex)) {
        *vertex = (nmo_engine_vertex_t){at.x, at.y, at.z, 0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f};
    }
}

/* A post primitive of the vertices pushed since `first` */
static nmo_engine_draw_t *stencil_primitive(nmo_engine_t *engine, nmo_object_id_t object, int32_t first)
{
    int32_t count = (int32_t)engine->render.vertices.count - first;
    nmo_engine_draw_t *draw = count > 0 ? eng_render_push(engine) : NULL;
    if (draw != NULL) {
        draw->kind = NMO_ENGINE_DRAW_PRIMITIVE;
        draw->object = object;
        draw->first_vertex = first;
        draw->vertex_count = count;
        draw->post = true;
        draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
    }
    return draw;
}

/*
 * ShadowStencil's post render callback: the objects with the "Shadow Stencil
 * Caster" attribute that are shown cast shadow volumes from the target light
 * (each silhouette edge of the faces toward the light pushed away by Shadow
 * Range Factor), drawn into the stencil (front faces adding, back faces
 * taking off) with the depth a little nearer (x 0.999); then the Shadow
 * Material's diffuse covers the view where the stencil is not 0. With Show
 * Shadow Volumes the volumes are drawn in the material instead. With Camera
 * Can Penetrate Shadow the stencil starts at the count of casters between the
 * light and the eye (Light Occluders Count).
 */
static void shadow_stencil_render(nmo_engine_t *engine, int32_t node)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t type = -1;
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *desc = &engine->attributes.types.data[t];
        if (desc->present && desc->name != NULL && strcmp(desc->name, "Shadow Stencil Caster") == 0) {
            type = (int32_t)t;
        }
    }
    int32_t light = eng_entity_index(engine, nmo_engine_block_target(b));
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 1);
    if (type < 0 || light < 0 || material == 0) {
        return;
    }
    ENG_VEC(nmo_object_id_t) casters = {0};
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        nmo_object_id_t *slot = NULL;
        if (attribute->type == type && eng_entity_index(engine, attribute->object) >= 0 &&
            (eng_object_visibility(engine, attribute->object) & CK_OBJECT_VISIBLE) && ENG_VEC_PUSH(casters, slot)) {
            *slot = attribute->object;
        }
    }
    if (casters.count == 0) {
        free(casters.data);
        return;
    }
    eng_vector_t light_world = eng_entity_position(engine, light, -1);
    int32_t camera = eng_entity_index(engine, engine->render.camera);
    eng_vector_t eye = camera >= 0 ? eng_entity_position(engine, camera, -1) : (eng_vector_t){0.0f, 0.0f, 0.0f};
    float near_plane = 0.0f, far_plane = 0.0f;
    bool has_planes = camera >= 0 && eng_camera_planes(engine, engine->render.camera, &near_plane, &far_plane);
    if (has_planes) {
        const eng_matrix_t *m = &engine->entities.data[camera].world;
        eng_vector_t axis = {m->m[2][0], m->m[2][1], m->m[2][2]};
        eng_vector_normalize(&axis);
        eye.x += axis.x * near_plane, eye.y += axis.y * near_plane, eye.z += axis.z * near_plane;
    }
    float range = get_in_float(b, 0, 1.0f);
    bool show = get_in_int(b, 2, 0) != 0, penetrate = get_in_int(b, 3, 0) != 0;
    int32_t occluders = 0;
    for (size_t c = 0; penetrate && c < casters.count; c++) {
        int32_t index = eng_entity_index(engine, casters.data[c]);
        eng_ray_hit_t hit;
        if (eng_entity_ray_intersection(engine, index, &light_world, &eye, &hit) > 0) {
            eng_vector_t at;
            eng_entity_transform(engine, index, &at, &hit.point, -1);
            float seg = (eye.x - light_world.x) * (eye.x - light_world.x) + (eye.y - light_world.y) * (eye.y - light_world.y) +
                        (eye.z - light_world.z) * (eye.z - light_world.z);
            float to = (at.x - light_world.x) * (eye.x - light_world.x) + (at.y - light_world.y) * (eye.y - light_world.y) +
                       (at.z - light_world.z) * (eye.z - light_world.z);
            occluders += to >= 0.0f && to <= seg; /* CKRAYINTERSECTION_SEGMENT */
        }
    }
    set_out_int(b, 0, occluders);
    nmo_engine_stencil_t volume = {true, VXCMP_ALWAYS, VXSTENCILOP_KEEP, VXSTENCILOP_KEEP, VXSTENCILOP_INCR, 0u, 0xFFu, 0xFFu};
    if (!show) {
        int32_t first = (int32_t)engine->render.vertices.count;
        nmo_engine_draw_t *clear = eng_render_push(engine);
        if (clear != NULL) {
            clear->kind = NMO_ENGINE_DRAW_PRIMITIVE;
            clear->first_vertex = first;
            clear->post = true;
            clear->clears_stencil = true;
            clear->stencil_clear = (uint32_t)occluders;
        }
    }
    eng_material_t *shadow = eng_material(engine, material);
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (shadow != NULL) {
        memcpy(diffuse, shadow->desc.diffuse, sizeof(diffuse));
    }
    uint32_t color = rgbaf_to_color(diffuse);
    for (size_t c = 0; c < casters.count; c++) {
        nmo_object_id_t id = casters.data[c];
        int32_t index = eng_entity_index(engine, id);
        nmo_object_t *object = eng_find_object(engine, id);
        const nmo_3dentity_state_t *state = object != NULL
            ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                              CKPGUID_3DENTITY)
            : NULL;
        nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
        nmo_engine_mesh_t shape;
        if (mesh == 0 || !nmo_engine_mesh(engine, mesh, &shape) || shape.face_count <= 0 || shape.indices == NULL) {
            continue;
        }
        uint16_t *neighbour = stencil_neighbours(shape.vertices, shape.vertex_count, shape.indices, shape.face_count);
        uint8_t *front = (uint8_t *)malloc((size_t)shape.face_count);
        eng_vector_t *pairs = (eng_vector_t *)malloc((size_t)shape.vertex_count * 2u * sizeof(eng_vector_t));
        int32_t *placed = (int32_t *)malloc((size_t)shape.vertex_count * sizeof(int32_t));
        if (neighbour == NULL || front == NULL || pairs == NULL || placed == NULL) {
            free(neighbour), free(front), free(pairs), free(placed);
            continue;
        }
        eng_vector_t light_local;
        eng_entity_inverse_transform(engine, index, &light_local, &light_world, -1);
        for (int32_t f = 0; f < shape.face_count; f++) {
            uint16_t v0 = shape.indices[3 * f];
            const nmo_vector_t *p = &shape.vertices[v0 < shape.vertex_count ? v0 : 0].position;
            const nmo_vector_t *n = shape.faces != NULL ? &shape.faces[f].normal : &(nmo_vector_t){0.0f, 0.0f, 0.0f};
            float d = (p->x - light_local.x) * n->x + (p->y - light_local.y) * n->y + (p->z - light_local.z) * n->z;
            front[f] = d < 0.0f ? 1 : 2;
        }
        for (int32_t v = 0; v < shape.vertex_count; v++) {
            placed[v] = -1;
        }
        int32_t pair_count = 0;
        const eng_matrix_t *world = &engine->entities.data[index].world;
        int32_t first[2];
        for (int pass = 0; pass < 2; pass++) {
            first[pass] = (int32_t)engine->render.vertices.count;
            for (int32_t f = 0; f < shape.face_count; f++) {
                if (front[f] != 1) {
                    continue;
                }
                for (int k = 0; k < 3; k++) {
                    uint16_t other = neighbour[3 * f + k];
                    if (other != 0xFFFF && (other >= shape.face_count || front[other] != 2)) {
                        continue;
                    }
                    int32_t ends[2];
                    for (int e = 0; e < 2; e++) {
                        uint16_t v = shape.indices[3 * f + (k + e) % 3];
                        v = v < shape.vertex_count ? v : 0;
                        if (placed[v] < 0) {
                            const nmo_vector_t *p = &shape.vertices[v].position;
                            eng_vector_t near = {p->x, p->y, p->z};
                            eng_vector_t far = {p->x + (p->x - light_local.x) * range, p->y + (p->y - light_local.y) * range,
                                                p->z + (p->z - light_local.z) * range};
                            placed[v] = pair_count;
                            pairs[2 * pair_count] = near;
                            pairs[2 * pair_count + 1] = far;
                            pair_count++;
                        }
                        ends[e] = placed[v];
                    }
                    /* (i0, i0 + 1, i1) and (i1, i0 + 1, i1 + 1) */
                    const eng_vector_t *tri[6] = {&pairs[2 * ends[0]], &pairs[2 * ends[0] + 1], &pairs[2 * ends[1]],
                                                  &pairs[2 * ends[1]], &pairs[2 * ends[0] + 1], &pairs[2 * ends[1] + 1]};
                    for (int t = 0; t < 6; t++) {
                        stencil_vertex(engine, world, tri[t], color);
                    }
                }
            }
            nmo_engine_draw_t *draw = stencil_primitive(engine, id, first[pass]);
            if (draw != NULL) {
                draw->projection_z_scale = 0.999f;
                draw->mirrored = pass == 1; /* VXCULL_CCW, then VXCULL_CW */
                if (show) {
                    draw->material = material;
                } else {
                    draw->blend = true;
                    draw->source_blend = VXBLEND_ZERO;
                    draw->dest_blend = VXBLEND_ONE;
                    draw->no_z_write = true;
                    draw->stencil = volume;
                    draw->stencil.pass = pass == 0 ? VXSTENCILOP_INCR : VXSTENCILOP_DECR;
                }
            }
        }
        free(neighbour), free(front), free(pairs), free(placed);
    }
    free(casters.data);
    if (show || camera < 0) {
        return;
    }
    /* DrawViewRect: the material's diffuse over the view where the stencil is not 0 */
    float fov = 0.0f;
    eng_camera_fov(engine, engine->render.camera, &fov);
    int32_t viewport[4];
    eng_render_viewport(engine, viewport);
    float aspect = viewport[3] != 0 ? (float)viewport[2] / (float)viewport[3] : 1.3333334f;
    float depth = (has_planes ? near_plane : 1.0f) * 1.01f;
    float half_h = depth * tanf(fov * 0.5f) * 1.1f, half_w = half_h * aspect;
    const eng_vector_t corner[4] = {{-half_w, half_h, depth}, {half_w, half_h, depth}, {half_w, -half_h, depth}, {-half_w, -half_h, depth}};
    int32_t start = (int32_t)engine->render.vertices.count;
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        stencil_vertex(engine, &engine->entities.data[camera].world, &corner[order[i]], color);
    }
    nmo_engine_draw_t *cover = stencil_primitive(engine, engine->render.camera, start);
    if (cover != NULL) {
        cover->material = material;
        cover->two_sided = true;
        cover->no_z_write = true;
        cover->stencil = (nmo_engine_stencil_t){true, VXCMP_NOTEQUAL, VXSTENCILOP_KEEP, VXSTENCILOP_KEEP,
                                                VXSTENCILOP_KEEP, 0u, 0xFFu, 0xFFu};
    }
}

/* ShadowStencil: while On, each frame the render context draws the shadows (a post render
   callback); the machine has a stencil buffer. Off stops. */
static int bb_shadow_stencil(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    addons_state_t *addons = (addons_state_t *)eng_manager_state(b->engine, &addons_manager);
    int32_t *slot = NULL;
    if (addons != NULL && ENG_VEC_PUSH(addons->shadows, slot)) {
        *slot = b->node;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}


/* ============================================================================
 * Create Decal
 * ============================================================================ */

typedef struct vx_plane {
    eng_vector_t n;
    float d;
} vx_plane_t;

static float plane_classify(const vx_plane_t *p, const eng_vector_t *v)
{
    return p->n.x * v->x + p->n.y * v->y + p->n.z * v->z + p->d;
}

static vx_plane_t plane_from_point(eng_vector_t n, eng_vector_t p)
{
    return (vx_plane_t){n, (float)(-((double)n.x * p.x + (double)n.y * p.y + (double)n.z * p.z))};
}

/* VxPlane::Create(a, b, c): its normal (the a-b-c winding), through a */
static vx_plane_t plane_from_triangle(eng_vector_t a, eng_vector_t b, eng_vector_t c)
{
    double bx = (double)b.x - a.x, by = (double)b.y - a.y, bz = (double)b.z - a.z;
    double cx = (double)c.x - a.x, cy = (double)c.y - a.y, cz = (double)c.z - a.z;
    double nx = by * cz - bz * cy, ny = bz * cx - bx * cz, nz = bx * cy - by * cx;
    double l = nx * nx + ny * ny + nz * nz;
    eng_vector_t n = {0.0f, 0.0f, 1.0f};
    if (l != 0.0) {
        double inv = 1.0 / sqrt(l);
        n = (eng_vector_t){(float)(nx * inv), (float)(ny * inv), (float)(nz * inv)};
    }
    return plane_from_point(n, a);
}

/* VxFrustum: its frame, bounds and planes (near, far, left, right, bottom, up) */
typedef struct vx_frustum {
    eng_vector_t origin, right, up, dir;
    float rbound, ubound, dmin, dmax, dratio;
    vx_plane_t near, far, left, right_plane, bottom, top;
} vx_frustum_t;

static eng_vector_t vec_add(eng_vector_t a, eng_vector_t b) { return (eng_vector_t){a.x + b.x, a.y + b.y, a.z + b.z}; }
static eng_vector_t vec_sub(eng_vector_t a, eng_vector_t b) { return (eng_vector_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static eng_vector_t vec_scale(eng_vector_t a, float k) { return (eng_vector_t){a.x * k, a.y * k, a.z * k}; }

/* VxFrustum::Update */
static void frustum_update(vx_frustum_t *f)
{
    f->dratio = f->dmax / f->dmin;
    eng_vector_t near_dir = vec_scale(f->dir, f->dmin), up = vec_scale(f->up, f->ubound);
    eng_vector_t right = vec_scale(f->right, f->rbound);
    eng_vector_t nbl = vec_sub(vec_sub(near_dir, right), up), ntl = vec_add(vec_sub(near_dir, right), up);
    eng_vector_t nbr = vec_sub(vec_add(near_dir, right), up), ntr = vec_add(vec_add(near_dir, right), up);
    const eng_vector_t zero = {0.0f, 0.0f, 0.0f};
    f->near = plane_from_point(vec_scale(f->dir, -1.0f), vec_add(f->origin, nbl));
    f->far = plane_from_point(f->dir, vec_add(f->origin, vec_scale(ntr, f->dratio)));
    f->left = plane_from_point(plane_from_triangle(zero, nbl, ntl).n, f->origin);
    f->right_plane = plane_from_point(plane_from_triangle(zero, ntr, nbr).n, f->origin);
    f->bottom = plane_from_point(plane_from_triangle(zero, nbr, nbl).n, f->origin);
    f->top = plane_from_point(plane_from_triangle(zero, ntl, ntr).n, f->origin);
}

/* VxFrustum::Transform by an inverse world matrix */
static void frustum_transform(vx_frustum_t *f, const eng_matrix_t *inverse)
{
    eng_vector_t axes[3] = {vec_scale(f->right, f->rbound), vec_scale(f->up, f->ubound), vec_scale(f->dir, f->dmin)};
    eng_vector_t origin;
    eng_vector_transform(&origin, inverse, &f->origin);
    f->origin = origin;
    double lengths[3];
    for (int i = 0; i < 3; i++) {
        eng_vector_t r;
        eng_vector_rotate(&r, inverse, &axes[i]);
        lengths[i] = sqrt((double)r.x * r.x + (double)r.y * r.y + (double)r.z * r.z);
        axes[i] = lengths[i] > 0.0 ? (eng_vector_t){(float)(r.x / lengths[i]), (float)(r.y / lengths[i]), (float)(r.z / lengths[i])}
                                   : (eng_vector_t){0.0f, 0.0f, 0.0f};
    }
    f->rbound = (float)lengths[0];
    f->ubound = (float)lengths[1];
    f->dmax = (float)lengths[2] * f->dratio;
    f->dmin = (float)lengths[2];
    f->right = axes[0], f->up = axes[1], f->dir = axes[2];
    frustum_update(f);
}

enum { CLIP_LEFT = 0x10, CLIP_RIGHT = 0x20, CLIP_TOP = 0x40, CLIP_BOTTOM = 0x80, CLIP_FRONT = 0x100, CLIP_BACK = 0x200 };

static uint32_t frustum_classify(const vx_frustum_t *f, const eng_vector_t *v)
{
    uint32_t flags = 0;
    if (plane_classify(&f->near, v) > 0.0f) flags |= CLIP_FRONT;
    else if (plane_classify(&f->far, v) > 0.0f) flags |= CLIP_BACK;
    if (plane_classify(&f->left, v) > 0.0f) flags |= CLIP_LEFT;
    else if (plane_classify(&f->right_plane, v) > 0.0f) flags |= CLIP_RIGHT;
    if (plane_classify(&f->bottom, v) > 0.0f) flags |= CLIP_BOTTOM;
    else if (plane_classify(&f->top, v) > 0.0f) flags |= CLIP_TOP;
    return flags;
}

/* A polygon being clipped (VxPolygon) */
typedef struct decal_polygon {
    eng_vector_t *v;
    int32_t count;
} decal_polygon_t;

#define DECAL_EPSILON 1.192092896e-07f

static eng_vector_t lerp_point(eng_vector_t a, eng_vector_t b, float d)
{
    return (eng_vector_t){a.x + (b.x - a.x) * d, a.y + (b.y - a.y) * d, a.z + (b.z - a.z) * d};
}

/* VxPolygon::Clip: 0 all out, 1 all in, 2 clipped (`scratch` holds 2 x count + 2 points) */
static int polygon_clip(decal_polygon_t *p, const vx_plane_t *plane, eng_vector_t *scratch)
{
    int32_t n = 0, count = p->count;
    float dist0 = plane_classify(plane, &p->v[0]), dist1 = dist0;
    bool inside = dist0 <= 0.0f, clipped = false;
    int32_t i = 1;
    for (; i <= count; i++) {
        const eng_vector_t *cur = i < count ? &p->v[i] : &p->v[0];
        float dist2 = i < count ? plane_classify(plane, cur) : dist0;
        if (inside && dist2 <= 0.0f) {
            scratch[n++] = *cur;
        } else if (!inside && dist2 <= -DECAL_EPSILON) {
            clipped = inside = true;
            scratch[n++] = lerp_point(p->v[i - 1], *cur, dist1 / (dist1 - dist2));
            scratch[n++] = *cur;
        } else if (inside && dist2 >= DECAL_EPSILON) {
            clipped = true;
            inside = false;
            scratch[n++] = lerp_point(p->v[i - 1], *cur, dist1 / (dist1 - dist2));
        } else {
            clipped = true;
        }
        dist1 = dist2;
    }
    if (!clipped) {
        return 1;
    }
    if (n < 3) {
        return 0;
    }
    memcpy(p->v, scratch, (size_t)n * sizeof(eng_vector_t));
    p->count = n;
    return 2;
}

/*
 * Create Decal: a new 3D object ("<target>_Decal", dynamic unless Dynamic is
 * off) at the world origin with a mesh ("<target>_DecalMesh") of the target's
 * faces inside the Frustum camera's frustum (in the world): the whole faces
 * that are inside (any face it meets with Whole Faces), the others clipped to
 * it, back faces left out with Backface Cull; welded, prelit with Prelit,
 * with UVs from the camera's projection with Generate UVs, pushed Offset
 * along their normals, in Decal Material.
 */
static int bb_create_decal(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, target);
    nmo_object_t *object = index >= 0 ? eng_find_object(engine, target) : NULL;
    const nmo_3dentity_state_t *state = object != NULL
        ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                          CKPGUID_3DENTITY)
        : NULL;
    nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, target, &state->current_mesh) : 0;
    nmo_engine_mesh_t shape;
    if (mesh == 0 || !nmo_engine_mesh(engine, mesh, &shape)) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t camera_id = nmo_engine_block_get_input_object(b, 0);
    int32_t camera = eng_entity_index(engine, camera_id);
    float fov = 0.0f, near_plane = 1.0f, far_plane = 4000.0f;
    if (camera < 0 || !eng_camera_fov(engine, camera_id, &fov)) {
        return NMO_ENGINE_BR_OK;
    }
    eng_camera_planes(engine, camera_id, &near_plane, &far_plane);
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 1);
    bool cull = get_in_int(b, 3, 0) != 0, prelit = get_in_int(b, 4, 1) != 0;
    bool whole = get_in_int(b, 5, 0) != 0, make_uvs = get_in_int(b, 6, 0) != 0;
    nmo_object_t *camera_object = eng_find_object(engine, camera_id);
    const nmo_camera_state_t *lens = camera_object != NULL
        ? (const nmo_camera_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, camera_object,
                                                                                        CKPGUID_CAMERA)
        : NULL;
    int32_t aw = lens != NULL && lens->aspect_width > 0 ? lens->aspect_width : 4;
    int32_t ah = lens != NULL && lens->aspect_height > 0 ? lens->aspect_height : 3;
    const eng_matrix_t *cm = &engine->entities.data[camera].world;
    eng_vector_t axes[3];
    for (int k = 0; k < 3; k++) {
        axes[k] = (eng_vector_t){cm->m[k][0], cm->m[k][1], cm->m[k][2]};
        eng_vector_normalize(&axes[k]);
    }
    vx_frustum_t frustum = {.origin = {cm->m[3][0], cm->m[3][1], cm->m[3][2]}, .right = axes[0], .up = axes[1],
                            .dir = axes[2], .rbound = tanf(fov * 0.5f) * near_plane,
                            .ubound = tanf(fov * 0.5f) * near_plane * ((float)ah / (float)aw),
                            .dmin = near_plane, .dmax = far_plane};
    frustum_update(&frustum);
    eng_matrix_t inverse;
    eng_matrix_inverse(&inverse, &engine->entities.data[index].world);
    frustum_transform(&frustum, &inverse);
    int32_t vcount = shape.vertex_count, fcount = shape.face_count;
    uint32_t *flags = vcount > 0 ? (uint32_t *)malloc((size_t)vcount * sizeof(uint32_t)) : NULL;
    int32_t *map = vcount > 0 ? (int32_t *)malloc((size_t)vcount * sizeof(int32_t)) : NULL;
    eng_vector_t *inside = vcount > 0 ? (eng_vector_t *)malloc((size_t)vcount * sizeof(eng_vector_t)) : NULL;
    uint16_t *inside_faces = fcount > 0 ? (uint16_t *)malloc((size_t)fcount * 3u * sizeof(uint16_t)) : NULL;
    eng_vector_t *clipped = fcount > 0 ? (eng_vector_t *)malloc((size_t)fcount * 9u * sizeof(eng_vector_t)) : NULL;
    int32_t *sizes = fcount > 0 ? (int32_t *)malloc((size_t)fcount * sizeof(int32_t)) : NULL;
    if (vcount <= 0 || fcount <= 0 || flags == NULL || map == NULL || inside == NULL || inside_faces == NULL ||
        clipped == NULL || sizes == NULL) {
        free(flags), free(map), free(inside), free(inside_faces), free(clipped), free(sizes);
        return NMO_ENGINE_BR_OK;
    }
    for (int32_t v = 0; v < vcount; v++) {
        eng_vector_t p = {shape.vertices[v].position.x, shape.vertices[v].position.y, shape.vertices[v].position.z};
        flags[v] = frustum_classify(&frustum, &p);
        map[v] = -1;
    }
    int32_t inside_count = 0, inside_index = 0, polygons = 0, clipped_vertices = 0, clipped_faces = 0;
    for (int32_t f = 0; f < fcount; f++) {
        const uint16_t *k = &shape.indices[3 * f];
        if (k[0] >= vcount || k[1] >= vcount || k[2] >= vcount) {
            continue;
        }
        uint32_t af = flags[k[0]], bf = flags[k[1]], cf = flags[k[2]];
        if (cull && shape.faces != NULL) {
            const nmo_vector_t *n = &shape.faces[f].normal;
            if (frustum.dir.x * n->x + frustum.dir.y * n->y + frustum.dir.z * n->z > 0.0f) {
                continue;
            }
        }
        if (af & bf & cf) {
            continue;
        }
        if (whole || (!af && !bf && !cf)) {
            for (int c = 0; c < 3; c++) {
                if (map[k[c]] < 0) {
                    map[k[c]] = inside_count;
                    const nmo_vector_t *p = &shape.vertices[k[c]].position;
                    inside[inside_count++] = (eng_vector_t){p->x, p->y, p->z};
                }
                inside_faces[inside_index++] = (uint16_t)map[k[c]];
            }
            continue;
        }
        eng_vector_t points[9], scratch[20];
        decal_polygon_t polygon = {points, 3};
        for (int c = 0; c < 3; c++) {
            const nmo_vector_t *p = &shape.vertices[k[c]].position;
            points[c] = (eng_vector_t){p->x, p->y, p->z};
        }
        uint32_t ff = (af ^ bf) | (af ^ cf);
        const struct { uint32_t bit; const vx_plane_t *plane; } order[6] = {
            {CLIP_LEFT, &frustum.left}, {CLIP_RIGHT, &frustum.right_plane}, {CLIP_FRONT, &frustum.near},
            {CLIP_BACK, &frustum.far}, {CLIP_BOTTOM, &frustum.bottom}, {CLIP_TOP, &frustum.top}};
        bool kept = true;
        for (int o = 0; o < 6 && kept; o++) {
            if ((ff & order[o].bit) && polygon_clip(&polygon, order[o].plane, scratch) == 0) {
                kept = false;
            }
        }
        if (kept && polygon.count <= 9) {
            memcpy(&clipped[clipped_vertices], polygon.v, (size_t)polygon.count * sizeof(eng_vector_t));
            sizes[polygons++] = polygon.count;
            clipped_vertices += polygon.count;
            clipped_faces += polygon.count - 2;
        }
    }
    /* the vertices in the world, then welded (the same position is one vertex) */
    int32_t total = inside_count + clipped_vertices, faces = inside_index / 3 + clipped_faces;
    eng_vector_t *world = total > 0 ? (eng_vector_t *)malloc((size_t)total * sizeof(eng_vector_t)) : NULL;
    uint16_t *indices = faces > 0 ? (uint16_t *)malloc((size_t)faces * 3u * sizeof(uint16_t)) : NULL;
    int32_t *weld = total > 0 ? (int32_t *)malloc((size_t)total * sizeof(int32_t)) : NULL;
    const eng_matrix_t *placed = &engine->entities.data[index].world;
    for (int32_t v = 0; world != NULL && v < total; v++) {
        const eng_vector_t *p = v < inside_count ? &inside[v] : &clipped[v - inside_count];
        eng_vector_transform(&world[v], placed, p);
    }
    int32_t f3 = 0;
    for (int32_t i = 0; indices != NULL && i < inside_index; i++) {
        indices[f3++] = inside_faces[i];
    }
    int32_t base = inside_count;
    for (int32_t p = 0; indices != NULL && p < polygons; p++) {
        for (int32_t i = 0; i < sizes[p] - 2; i++) {
            indices[f3++] = (uint16_t)base;
            indices[f3++] = (uint16_t)(base + i + 1);
            indices[f3++] = (uint16_t)(base + i + 2);
        }
        base += sizes[p];
    }
    int32_t welded = 0;
    for (int32_t v = 0; world != NULL && weld != NULL && v < total; v++) {
        weld[v] = -1;
        for (int32_t u = 0; u < welded; u++) {
            if (world[u].x == world[v].x && world[u].y == world[v].y && world[u].z == world[v].z) {
                weld[v] = u;
                break;
            }
        }
        if (weld[v] < 0) {
            weld[v] = welded;
            world[welded++] = world[v];
        }
    }
    for (int32_t i = 0; indices != NULL && weld != NULL && i < faces * 3; i++) {
        indices[i] = (uint16_t)weld[indices[i]];
    }
    free(flags), free(map), free(inside), free(inside_faces), free(clipped), free(sizes), free(weld);
    if ((total > 0 && world == NULL) || (faces > 0 && indices == NULL)) {
        free(world), free(indices);
        return NMO_ENGINE_BR_OK;
    }
    const char *name = nmo_engine_object_name(engine, target);
    size_t length = name != NULL ? strlen(name) : 0;
    char *decal_name = (char *)malloc(length + sizeof("_DecalMesh"));
    nmo_object_id_t decal = 0, decal_mesh = 0;
    nmo_mesh_state_t *made = NULL;
    if (decal_name != NULL) {
        memcpy(decal_name, name != NULL ? name : "", length);
        memcpy(decal_name + length, "_Decal", sizeof("_Decal"));
        decal = eng_create_object(engine, NMO_CID_3DOBJECT, decal_name);
        memcpy(decal_name + length, "_DecalMesh", sizeof("_DecalMesh"));
        decal_mesh = decal != 0 ? eng_create_mesh(engine, decal_name, welded, faces, &made) : 0;
    }
    free(decal_name);
    if (decal == 0 || decal_mesh == 0 || made == NULL) {
        free(world), free(indices);
        return NMO_ENGINE_BR_OK;
    }
    if (get_local_int(b, 0, 1) != 0) {
        eng_mark_dynamic(engine, decal);
        eng_mark_dynamic(engine, decal_mesh);
    }
    nmo_object_id_t out = decal;
    set_out(b, 0, &out, sizeof(out));
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nmo_engine_set_entity_world_matrix(engine, decal, identity, false);
    eng_entity_set_current_mesh(engine, decal, decal_mesh);
    eng_entity_add_mesh(engine, decal, decal_mesh);
    if (prelit) {
        made->flags |= VXMESH_PRELITMODE;
    }
    for (int32_t v = 0; v < welded; v++) {
        made->vertices[v].position = (nmo_vector_t){world[v].x, world[v].y, world[v].z};
    }
    if (faces > 0) {
        memcpy(made->face_vertex_indices, indices, (size_t)faces * 3u * sizeof(uint16_t));
    }
    free(world), free(indices);
    if (make_uvs && welded > 0) {
        eng_matrix_t total_matrix;
        if (eng_camera_view_projection(engine, camera_id, &total_matrix, NULL)) {
            for (int32_t v = 0; v < welded; v++) {
                const nmo_vector_t *p = &made->vertices[v].position;
                const float (*m)[4] = total_matrix.m;
                float x = p->x * m[0][0] + p->y * m[1][0] + p->z * m[2][0] + m[3][0];
                float y = p->x * m[0][1] + p->y * m[1][1] + p->z * m[2][1] + m[3][1];
                float w = p->x * m[0][3] + p->y * m[1][3] + p->z * m[2][3] + m[3][3];
                float inv = 1.0f / w;
                made->vertices[v].uv = (nmo_vector2_t){(1.0f + x * inv) * 0.5f, (1.0f - y * inv) * 0.5f};
            }
        }
    }
    /* BuildNormals, then each vertex pushed Offset along its normal */
    nmo_vector_t *normals = welded > 0 ? (nmo_vector_t *)malloc((size_t)welded * sizeof(nmo_vector_t)) : NULL;
    if (normals != NULL && nmo_mesh_build_vertex_normals(made, normals) == NMO_OK) {
        for (int32_t v = 0; v < welded; v++) {
            made->vertices[v].normal = normals[v];
        }
    }
    free(normals);
    for (int32_t f = 0; f < faces; f++) {
        const uint16_t *k = &made->face_vertex_indices[3 * f];
        eng_vector_t a = {made->vertices[k[0]].position.x, made->vertices[k[0]].position.y, made->vertices[k[0]].position.z};
        eng_vector_t e1 = {made->vertices[k[1]].position.x - a.x, made->vertices[k[1]].position.y - a.y,
                           made->vertices[k[1]].position.z - a.z};
        eng_vector_t e2 = {made->vertices[k[2]].position.x - a.x, made->vertices[k[2]].position.y - a.y,
                           made->vertices[k[2]].position.z - a.z};
        eng_vector_t n = eng_vector_cross(&e1, &e2);
        eng_vector_normalize(&n);
        made->faces[f].normal = (nmo_vector_t){n.x, n.y, n.z};
    }
    float offset = get_in_float(b, 2, 0.01f);
    if (offset != 0.0f) {
        for (int32_t v = 0; v < welded; v++) {
            nmo_vertex_t *vertex = &made->vertices[v];
            vertex->position.x += vertex->normal.x * offset;
            vertex->position.y += vertex->normal.y * offset;
            vertex->position.z += vertex->normal.z * offset;
        }
    }
    nmo_mesh_update_bounding_volumes(made);
    eng_mesh_apply_material(engine, decal_mesh, material);
    return NMO_ENGINE_BR_OK;
}


/* ============================================================================
 * Animation Recorder
 * ============================================================================ */

#define REC_LINEAR_POSITION 0x637c4301u
#define REC_LINEAR_ROTATION 0x49ed4002u
#define REC_LINEAR_SCALE 0x654a3a04u

/* Animation Recorder's inner struct (its local buffer): the recording, then an entry each
   object animation */
typedef struct rec_header {
    int32_t count;
    float time;        /* the next key's time: one a recorded frame */
    int32_t step;      /* frames since the last key */
    nmo_object_id_t keyed;
} rec_header_t;

typedef struct rec_entry {
    nmo_object_id_t animation;
    int32_t capacity;  /* the keys its controllers have room for */
} rec_entry_t;

static rec_header_t *rec_state(blk_t *b, int32_t count, bool resize)
{
    eng_param_t *param = block_local(b, 0);
    size_t size = sizeof(rec_header_t) + (size_t)(count > 0 ? count : 0) * sizeof(rec_entry_t);
    if (param == NULL || (resize && !eng_param_resize(param, size))) {
        return NULL;
    }
    if (param->data == NULL || param->size < sizeof(rec_header_t)) {
        return NULL;
    }
    return (rec_header_t *)param->data;
}

static rec_entry_t *rec_entries(rec_header_t *h)
{
    return (rec_entry_t *)(h + 1);
}

/* An object animation of that name made in the level, or the one there emptied of its keys */
static nmo_object_id_t rec_animation(nmo_engine_t *engine, const char *name)
{
    nmo_object_id_t id = eng_object_named(engine, name, NMO_CID_OBJECTANIMATION);
    if (id == 0) {
        id = eng_create_object(engine, NMO_CID_OBJECTANIMATION, name);
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    nmo_objectanimation_state_t *state = object != NULL ? (nmo_objectanimation_state_t *)nmo_object_get_state(object) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->controller_count = 0;
    state->controllers = NULL;
    state->has_length = 1;
    state->length = 0.0f;
    eng_animation_keys_changed(engine, id, state);
    return id;
}

/* AddPositionKey, AddRotationKey and AddScaleKey at `time` (linear controllers), the arrays
   doubled when full */
static void rec_add_keys(nmo_engine_t *engine, rec_entry_t *entry, float time, const nmo_vector_t *position,
                         const nmo_quaternion_t *rotation, const nmo_vector_t *scale)
{
    nmo_object_t *object = eng_find_object(engine, entry->animation);
    nmo_objectanimation_state_t *state = object != NULL ? (nmo_objectanimation_state_t *)nmo_object_get_state(object) : NULL;
    nmo_arena_t *arena = state != NULL ? eng_animation_edit_arena(engine, entry->animation) : NULL;
    if (arena == NULL) {
        return;
    }
    static const uint32_t types[3] = {REC_LINEAR_POSITION, REC_LINEAR_ROTATION, REC_LINEAR_SCALE};
    static const uint32_t strides[3] = {4, 5, 4};
    uint32_t count = state->controller_count == 3 ? state->controllers[0].key_count : 0;
    if (state->controller_count != 3 || (int32_t)count >= entry->capacity) {
        int32_t capacity = entry->capacity > 0 ? entry->capacity * 2 : 16;
        nmo_objanim_controller_t *controllers = (nmo_objanim_controller_t *)nmo_arena_alloc(
            arena, 3 * sizeof(nmo_objanim_controller_t), _Alignof(nmo_objanim_controller_t));
        if (controllers == NULL) {
            return;
        }
        for (int c = 0; c < 3; c++) {
            float *keys = (float *)nmo_arena_alloc(arena, (size_t)capacity * strides[c] * sizeof(float), _Alignof(float));
            if (keys == NULL) {
                return;
            }
            if (count > 0) {
                memcpy(keys, state->controllers[c].data, (size_t)count * strides[c] * sizeof(float));
            }
            controllers[c] = (nmo_objanim_controller_t){types[c], count, count * strides[c] * (uint32_t)sizeof(float), keys};
        }
        state->controllers = controllers;
        state->controller_count = 3;
        entry->capacity = capacity;
    }
    const float values[3][5] = {{time, position->x, position->y, position->z, 0.0f},
                                {time, rotation->x, rotation->y, rotation->z, rotation->w},
                                {time, scale->x, scale->y, scale->z, 0.0f}};
    for (int c = 0; c < 3; c++) {
        nmo_objanim_controller_t *controller = &state->controllers[c];
        memcpy((float *)controller->data + (size_t)count * strides[c], values[c], strides[c] * sizeof(float));
        controller->key_count = count + 1;
        controller->data_size = (count + 1) * strides[c] * (uint32_t)sizeof(float);
    }
    eng_animation_keys_changed(engine, entry->animation, state);
}

static void rec_set_length(nmo_engine_t *engine, nmo_object_id_t animation, float length)
{
    nmo_object_t *object = animation != 0 ? eng_find_object(engine, animation) : NULL;
    if (object == NULL) {
        return;
    }
    if (nmo_object_get_class_id(object) == NMO_CID_KEYEDANIMATION) {
        nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)nmo_object_get_state(object);
        keyed->base.has_length = 1;
        keyed->base.length = length;
        eng_keyed_animation_refresh(engine, animation);
    } else {
        nmo_objectanimation_state_t *state = (nmo_objectanimation_state_t *)nmo_object_get_state(object);
        state->has_length = 1;
        state->length = length;
        eng_animation_keys_changed(engine, animation, state);
    }
}

/*
 * Animation Recorder: On starts recording Object(s) into the object animation
 * Animation Name (made when missing, emptied otherwise); with Use Grouped
 * Animation, Object(s) is a group: a keyed animation of that name gets an
 * object animation "<object>_<name>" each. Every Frame Step frames each
 * object's world matrix gives a position, rotation and scale key at the next
 * whole frame. Off gives the animations their entities and length, and the
 * recording as Animation.
 */
static int bb_animation_recorder(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    bool grouped = get_local_int(b, 1, 0) != 0;
    nmo_object_id_t input = nmo_engine_block_get_input_object(b, 0);
    if (input == 0) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        int32_t count = grouped ? nmo_engine_group_count(engine, input) : 1;
        rec_header_t *h = rec_state(b, count, true);
        if (h == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        *h = (rec_header_t){count, 0.0f, 0, 0};
        const char *name = input_string(b, 1);
        name = name != NULL ? name : "";
        rec_entry_t *entries = rec_entries(h);
        memset(entries, 0, (size_t)count * sizeof(rec_entry_t));
        if (!grouped) {
            entries[0].animation = rec_animation(engine, name);
        } else {
            h->keyed = eng_object_named(engine, name, NMO_CID_KEYEDANIMATION);
            if (h->keyed == 0) {
                h->keyed = eng_create_object(engine, NMO_CID_KEYEDANIMATION, name);
            }
            nmo_object_t *keyed_object = h->keyed != 0 ? eng_find_object(engine, h->keyed) : NULL;
            nmo_keyedanimation_state_t *keyed = keyed_object != NULL ? (nmo_keyedanimation_state_t *)nmo_object_get_state(keyed_object) : NULL;
            nmo_arena_t *arena = keyed != NULL ? eng_animation_edit_arena(engine, h->keyed) : NULL;
            nmo_ref_t *refs = arena != NULL && count > 0
                ? (nmo_ref_t *)nmo_arena_alloc(arena, (size_t)count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t)) : NULL;
            uint32_t held = 0;
            for (int32_t j = 0; j < count; j++) {
                nmo_object_id_t object = nmo_engine_group_object(engine, input, j);
                const char *object_name = object != 0 ? nmo_engine_object_name(engine, object) : NULL;
                if (object == 0) {
                    continue;
                }
                object_name = object_name != NULL ? object_name : "(null)";
                size_t a = strlen(object_name), c = strlen(name);
                char *full = (char *)malloc(a + c + 2u);
                if (full == NULL) {
                    continue;
                }
                memcpy(full, object_name, a);
                full[a] = '_';
                memcpy(full + a + 1, name, c + 1u);
                entries[j].animation = rec_animation(engine, full);
                free(full);
                if (refs != NULL && entries[j].animation != 0 &&
                    eng_animation_local_ref(engine, h->keyed, entries[j].animation, &refs[held])) {
                    held++;
                }
            }
            if (keyed != NULL) {
                keyed->animation_ids = refs;
                keyed->animation_count = refs != NULL ? held : 0;
                eng_keyed_animation_refresh(engine, h->keyed);
            }
        }
    }
    rec_header_t *h = rec_state(b, 0, false);
    if (h == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    rec_entry_t *entries = rec_entries(h);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        nmo_object_id_t result = 0;
        if (grouped) {
            for (int32_t k = 0; k < h->count; k++) {
                nmo_object_id_t object = nmo_engine_group_object(engine, input, k);
                if (object != 0 && entries[k].animation != 0) {
                    eng_object_animation_set_entity(engine, entries[k].animation, object);
                }
            }
            rec_set_length(engine, h->keyed, h->time);
            result = h->keyed;
        } else if (entries[0].animation != 0) {
            rec_set_length(engine, entries[0].animation, h->time);
            eng_object_animation_set_entity(engine, entries[0].animation, input);
            result = entries[0].animation;
        }
        set_out(b, 0, &result, sizeof(result));
        h->count = 0;
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (++h->step >= get_in_int(b, 2, 1)) {
        h->step = 0;
        for (int32_t i = 0; i < h->count; i++) {
            nmo_object_id_t object = grouped ? nmo_engine_group_object(engine, input, i) : input;
            int32_t index = eng_entity_index(engine, object);
            if (index < 0 || entries[i].animation == 0) {
                continue;
            }
            const eng_matrix_t *world = &engine->entities.data[index].world;
            eng_matrix_t turn;
            eng_vector_t size = {1.0f, 1.0f, 1.0f};
            eng_matrix_identity(&turn);
            eng_matrix_decompose(world, false, &turn, &size);
            nmo_quaternion_t q = {0.0f, 0.0f, 0.0f, 1.0f};
            eng_quaternion_from_matrix(&turn, &q);
            nmo_vector_t position = {world->m[3][0], world->m[3][1], world->m[3][2]};
            nmo_vector_t scale = {size.x, size.y, size.z};
            rec_add_keys(engine, &entries[i], h->time, &position, &q, &scale);
        }
        h->time += 1.0f;
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
    {NMO_GUID_INIT(0xb0f1ca98, 0xe9dabe5f), "Volumetric Fog", bb_volumetric_fog, NULL, NULL},
    {NMO_GUID_INIT(0x76926718, 0x7ee807d5), "Lens Flare", bb_lens_flare, NULL, NULL},
    {NMO_GUID_INIT(0x60a873c5, 0x51245a67), "ShadowStencil", bb_shadow_stencil, NULL, NULL},
    {NMO_GUID_INIT(0x27b71ada, 0x22191538), "Create Decal", bb_create_decal, NULL, NULL},
    {NMO_GUID_INIT(0x00532d07, 0x1db631cc), "Animation Recorder", bb_animation_recorder, NULL, NULL},
};

nmo_status_t eng_register_addons(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &addons_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_addons_blocks) : status;
}
