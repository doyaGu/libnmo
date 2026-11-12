/**
 * @file soft_raster.c
 * @brief Direct3D 7's fixed function pipeline in C: transform, texture
 *        coordinate generation, fog, clipping, culling, rasterization and
 *        the fragment tests, as render_state_t describes them.
 *
 * Triangles are clipped against the near and far planes and rasterized at
 * pixel centers with the top-left rule, their attributes interpolated with
 * perspective; lines are stepped a pixel at a time. Flat shading takes a
 * primitive's first vertex, as Direct3D does. Colors are written as bytes,
 * depth as a float from 0 (near) to 1 (far). There is no multisampling.
 */

#include "soft_internal.h"
#include "../render_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SOFT_ATTRIBUTES 12 /* texture coordinates 4, color 4, specular 3, fog 1 */
#define SOFT_ATTR_TEX 0
#define SOFT_ATTR_COLOR 4
#define SOFT_ATTR_SPEC 8
#define SOFT_ATTR_FOG 11
#define SOFT_MAX_POLYGON 9

/* A vertex in clip space (Direct3D's: z from 0 to w) with what it carries */
typedef struct soft_vertex {
    float clip[4];
    float attr[SOFT_ATTRIBUTES];
} soft_vertex_t;

/* A vertex on the target: pixels, depth, 1/w and its attributes over w */
typedef struct soft_point {
    float x, y, z, inv_w;
    float attr[SOFT_ATTRIBUTES];
} soft_point_t;

typedef struct soft_context {
    soft_surface_t *surface;
    const render_state_t *state;
    const soft_texture_t *texture; /* NULL: untextured */
    int32_t bounds[4];             /* left, top, right, bottom (exclusive) */
    float viewport[4];
    float model_view[16], projection[16], normal_matrix[16];
    float fog_color[3];
    float border[4];
    bool flat;
    float flat_color[4], flat_spec[3];
} soft_context_t;

/* ============================================================================
 * Small helpers
 * ============================================================================ */

static float soft_clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

static uint8_t soft_byte(float v)
{
    return (uint8_t)(soft_clamp01(v) * 255.0f + 0.5f);
}

/* VXCMP: whether `a` passes against `b` */
static bool soft_compare(int32_t func, float a, float b)
{
    switch (func) {
    case 1: return false;
    case 2: return a < b;
    case 3: return a == b;
    case 4: return a <= b;
    case 5: return a > b;
    case 6: return a != b;
    case 7: return a >= b;
    default: return true;
    }
}

/* VXSTENCILOP applied to a stencil value */
static uint8_t soft_stencil_op(int32_t op, uint8_t value, uint8_t ref)
{
    switch (op) {
    case 2: return 0;
    case 3: return ref;
    case 4: return (uint8_t)(value == 255 ? 255 : value + 1);
    case 5: return (uint8_t)(value == 0 ? 0 : value - 1);
    case 6: return (uint8_t)~value;
    case 7: return (uint8_t)(value + 1);
    case 8: return (uint8_t)(value - 1);
    default: return value;
    }
}

static void soft_stencil_write(const render_state_t *s, uint8_t *stored, int32_t op)
{
    uint8_t mask = (uint8_t)s->stencil_write_mask;
    uint8_t value = soft_stencil_op(op, *stored, (uint8_t)s->stencil_ref);
    *stored = (uint8_t)((*stored & (uint8_t)~mask) | (value & mask));
}

/* A row vector (x, y, z, w) times a VxMatrix */
static void soft_transform4(const float v[4], const float m[16], float out[4])
{
    for (int c = 0; c < 4; c++) {
        out[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + v[3] * m[12 + c];
    }
}

/* ============================================================================
 * Textures
 * ============================================================================ */

/* A texel index of a level `size` texels wide as the address mode has it; -1 for the border */
static int32_t soft_address(int32_t i, int32_t size, int32_t mode)
{
    switch (mode) {
    case 2: { /* MIRROR */
        int32_t period = size * 2;
        int32_t m = ((i % period) + period) % period;
        return m < size ? m : period - 1 - m;
    }
    case 3: /* CLAMP */
        return i < 0 ? 0 : i >= size ? size - 1 : i;
    case 4: /* BORDER */
        return i < 0 || i >= size ? -1 : i;
    case 5: { /* MIRRORONCE */
        int32_t m = i < 0 ? -1 - i : i;
        return m >= size ? size - 1 : m;
    }
    default: /* WRAP */
        return ((i % size) + size) % size;
    }
}

static void soft_texel(const soft_context_t *ctx, const soft_level_t *level, int32_t x, int32_t y,
                       int32_t address, float out[4])
{
    int32_t tx = soft_address(x, level->width, address);
    int32_t ty = soft_address(y, level->height, address);
    if (tx < 0 || ty < 0) {
        memcpy(out, ctx->border, 4 * sizeof(float));
        return;
    }
    const uint8_t *p = &level->rgba[((size_t)ty * (size_t)level->width + (size_t)tx) * 4u];
    for (int c = 0; c < 4; c++) {
        out[c] = (float)p[c] / 255.0f;
    }
}

/* A level sampled at u, v (0..1 across it), nearest or bilinear */
static void soft_sample_level(const soft_context_t *ctx, const soft_level_t *level, float u, float v,
                              bool linear, int32_t address, float out[4])
{
    float x = u * (float)level->width, y = v * (float)level->height;
    if (!linear) {
        soft_texel(ctx, level, (int32_t)floorf(x), (int32_t)floorf(y), address, out);
        return;
    }
    x -= 0.5f;
    y -= 0.5f;
    float fx = floorf(x), fy = floorf(y);
    float ax = x - fx, ay = y - fy;
    int32_t ix = (int32_t)fx, iy = (int32_t)fy;
    float t00[4], t10[4], t01[4], t11[4];
    soft_texel(ctx, level, ix, iy, address, t00);
    soft_texel(ctx, level, ix + 1, iy, address, t10);
    soft_texel(ctx, level, ix, iy + 1, address, t01);
    soft_texel(ctx, level, ix + 1, iy + 1, address, t11);
    for (int c = 0; c < 4; c++) {
        float top = t00[c] + (t10[c] - t00[c]) * ax;
        float bottom = t01[c] + (t11[c] - t01[c]) * ax;
        out[c] = top + (bottom - top) * ay;
    }
}

/* The cube face a direction points at, and where on it (GL's and Direct3D's table) */
static int32_t soft_cube_face(float x, float y, float z, float *out_u, float *out_v)
{
    float ax = fabsf(x), ay = fabsf(y), az = fabsf(z);
    int32_t face;
    float sc, tc, ma;
    if (ax >= ay && ax >= az) {
        face = x >= 0.0f ? 0 : 1;
        sc = x >= 0.0f ? -z : z;
        tc = -y;
        ma = ax;
    } else if (ay >= az) {
        face = y >= 0.0f ? 2 : 3;
        sc = x;
        tc = y >= 0.0f ? z : -z;
        ma = ay;
    } else {
        face = z >= 0.0f ? 4 : 5;
        sc = z >= 0.0f ? x : -x;
        tc = -y;
        ma = az;
    }
    if (ma <= 0.0f) {
        ma = 1.0f;
    }
    *out_u = (sc / ma + 1.0f) * 0.5f;
    *out_v = (tc / ma + 1.0f) * 0.5f;
    return face;
}

/* The texture at coordinates (s, t, r, q), `lod` levels of detail down; false when incomplete */
static bool soft_sample(const soft_context_t *ctx, const float tex[4], float lod, float out[4])
{
    const soft_texture_t *texture = ctx->texture;
    const render_sampler_t *sampler = &ctx->state->sampler;
    if (texture->kind == RENDER_TEXTURE_CUBE) {
        float u = 0.0f, v = 0.0f;
        int32_t face = soft_cube_face(tex[0], tex[1], tex[2], &u, &v);
        const soft_level_t *level = &texture->faces[face][0];
        if (level->rgba == NULL) {
            return false;
        }
        bool linear = (lod <= 0.0f ? sampler->mag_filter : sampler->min_filter) == RENDER_FILTER_LINEAR;
        soft_sample_level(ctx, level, u, v, linear, 3, out);
        return true;
    }
    const soft_level_t *base = &texture->faces[0][0];
    if (base->rgba == NULL) {
        return false;
    }
    float q = tex[3] != 0.0f ? tex[3] : 1.0f;
    float u = tex[0] / q, v = tex[1] / q;
    if (lod <= 0.0f) {
        soft_sample_level(ctx, base, u, v, sampler->mag_filter == RENDER_FILTER_LINEAR,
                          sampler->address, out);
        return true;
    }
    bool linear = sampler->min_filter == RENDER_FILTER_LINEAR;
    int32_t last = texture->last;
    while (last > 0 && texture->faces[0][last].rgba == NULL) {
        last--;
    }
    if (sampler->mip == RENDER_MIP_NONE || last == 0) {
        soft_sample_level(ctx, base, u, v, linear, sampler->address, out);
        return true;
    }
    if (lod > (float)last) {
        lod = (float)last;
    }
    if (sampler->mip == RENDER_MIP_NEAREST) {
        int32_t level = (int32_t)floorf(lod + 0.5f);
        soft_sample_level(ctx, &texture->faces[0][level], u, v, linear, sampler->address, out);
        return true;
    }
    int32_t level = (int32_t)floorf(lod);
    int32_t next = level < last ? level + 1 : level;
    float a = lod - (float)level, first[4], second[4];
    soft_sample_level(ctx, &texture->faces[0][level], u, v, linear, sampler->address, first);
    soft_sample_level(ctx, &texture->faces[0][next], u, v, linear, sampler->address, second);
    for (int c = 0; c < 4; c++) {
        out[c] = first[c] + (second[c] - first[c]) * a;
    }
    return true;
}

/* ============================================================================
 * Fragments
 * ============================================================================ */

static void soft_blend_factor(int32_t factor, const float src[4], const float dst[4], float out[4])
{
    for (int c = 0; c < 4; c++) {
        float f;
        switch (factor) {
        case 1: f = 0.0f; break;
        case 3: f = src[c]; break;
        case 4: f = 1.0f - src[c]; break;
        case 5: f = src[3]; break;
        case 6: f = 1.0f - src[3]; break;
        case 7: f = dst[3]; break;
        case 8: f = 1.0f - dst[3]; break;
        case 9: f = dst[c]; break;
        case 10: f = 1.0f - dst[c]; break;
        case 11: f = c == 3 ? 1.0f : fminf(src[3], 1.0f - dst[3]); break;
        default: f = 1.0f; break;
        }
        out[c] = f;
    }
}

/* One fragment at pixel x, y with depth z and its attributes (lod: the texture's level of detail) */
static void soft_fragment(const soft_context_t *ctx, int32_t x, int32_t y, float z,
                          const float attr[SOFT_ATTRIBUTES], float lod)
{
    const render_state_t *s = ctx->state;
    soft_surface_t *surface = ctx->surface;
    float c[4], spec[3];
    if (ctx->flat) {
        memcpy(c, ctx->flat_color, sizeof(c));
        memcpy(spec, ctx->flat_spec, sizeof(spec));
    } else {
        for (int i = 0; i < 4; i++) {
            c[i] = soft_clamp01(attr[SOFT_ATTR_COLOR + i]);
        }
        for (int i = 0; i < 3; i++) {
            spec[i] = soft_clamp01(attr[SOFT_ATTR_SPEC + i]);
        }
    }
    float t[4];
    if (ctx->texture != NULL && soft_sample(ctx, &attr[SOFT_ATTR_TEX], lod, t)) {
        switch (s->texenv) {
        case RENDER_TEXENV_DECAL:
            for (int i = 0; i < 3; i++) {
                c[i] = c[i] * (1.0f - t[3]) + t[i] * t[3];
            }
            break;
        case RENDER_TEXENV_REPLACE:
            memcpy(c, t, sizeof(c));
            break;
        case RENDER_TEXENV_ADD:
            for (int i = 0; i < 3; i++) {
                c[i] = fminf(c[i] + t[i], 1.0f);
            }
            c[3] *= t[3];
            break;
        default:
            for (int i = 0; i < 4; i++) {
                c[i] *= t[i];
            }
            break;
        }
    }
    if (s->color_sum) {
        for (int i = 0; i < 3; i++) {
            c[i] = fminf(c[i] + spec[i], 1.0f);
        }
    }
    if (s->fog) {
        float f = soft_clamp01(attr[SOFT_ATTR_FOG]);
        for (int i = 0; i < 3; i++) {
            c[i] = c[i] * f + ctx->fog_color[i] * (1.0f - f);
        }
    }
    if (s->alpha_test &&
        !soft_compare(s->alpha_func, (float)soft_byte(c[3]), (float)soft_byte(s->alpha_ref))) {
        return;
    }
    size_t at = (size_t)y * (size_t)surface->width + (size_t)x;
    z = soft_clamp01(z);
    if (s->stencil) {
        uint8_t *stored = &surface->stencil[at];
        uint32_t mask = s->stencil_mask & 0xFFu;
        if (!soft_compare(s->stencil_func, (float)(s->stencil_ref & mask), (float)(*stored & mask))) {
            soft_stencil_write(s, stored, s->stencil_fail);
            return;
        }
    }
    if (s->depth_test && !soft_compare(s->depth_func, z, surface->depth[at])) {
        if (s->stencil) {
            soft_stencil_write(s, &surface->stencil[at], s->stencil_zfail);
        }
        return;
    }
    if (s->stencil) {
        soft_stencil_write(s, &surface->stencil[at], s->stencil_pass);
    }
    if (s->depth_test && s->depth_write) {
        surface->depth[at] = z;
    }
    if (!s->color_write) {
        return;
    }
    uint8_t *p = &surface->color[at * 4u];
    if (s->blend) {
        float dst[4] = {(float)p[0] / 255.0f, (float)p[1] / 255.0f, (float)p[2] / 255.0f,
                        (float)p[3] / 255.0f};
        float sf[4], df[4];
        soft_blend_factor(s->source_blend, c, dst, sf);
        soft_blend_factor(s->dest_blend, c, dst, df);
        for (int i = 0; i < 4; i++) {
            c[i] = c[i] * sf[i] + dst[i] * df[i];
        }
    }
    for (int i = 0; i < 4; i++) {
        p[i] = soft_byte(c[i]);
    }
}

/* ============================================================================
 * Vertices
 * ============================================================================ */

static const float *soft_stream(const void *base, size_t stride, int32_t index)
{
    return (const float *)((const uint8_t *)base + (size_t)index * stride);
}

static void soft_vertex(const soft_context_t *ctx, const render_geometry_t *g, int32_t index,
                        soft_vertex_t *out)
{
    const render_state_t *s = ctx->state;
    if (index < 0 || index >= g->vertex_count) {
        index = 0;
    }
    const float *p = soft_stream(g->positions, g->position_stride, index);
    float position[4] = {p[0], p[1], p[2], 1.0f}, eye[4];
    soft_transform4(position, ctx->model_view, eye);
    soft_transform4(eye, ctx->projection, out->clip);
    float *attr = out->attr;
    if (ctx->texture != NULL) {
        float tex[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (s->texgen == RENDER_TEXGEN_EYE) {
            tex[0] = eye[0], tex[1] = eye[1], tex[2] = eye[2];
        } else if (s->texgen == RENDER_TEXGEN_REFLECT || s->texgen == RENDER_TEXGEN_NORMAL) {
            float n[3] = {0.0f, 0.0f, 1.0f};
            if (g->normals != NULL) {
                const float *k = soft_stream(g->normals, g->normal_stride, index);
                for (int c = 0; c < 3; c++) {
                    n[c] = k[0] * ctx->normal_matrix[c * 4 + 0] + k[1] * ctx->normal_matrix[c * 4 + 1] +
                           k[2] * ctx->normal_matrix[c * 4 + 2];
                }
            }
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0.0f) {
                n[0] /= len, n[1] /= len, n[2] /= len;
            }
            if (s->texgen == RENDER_TEXGEN_NORMAL) {
                tex[0] = n[0], tex[1] = n[1], tex[2] = n[2];
            } else {
                float u[3] = {eye[0], eye[1], eye[2]};
                float ulen = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                if (ulen > 0.0f) {
                    u[0] /= ulen, u[1] /= ulen, u[2] /= ulen;
                }
                float d = n[0] * u[0] + n[1] * u[1] + n[2] * u[2];
                for (int c = 0; c < 3; c++) {
                    tex[c] = u[c] - 2.0f * n[c] * d;
                }
            }
        } else if (g->uvs != NULL) {
            const float *uv = soft_stream(g->uvs, g->uv_stride, index);
            tex[0] = uv[0], tex[1] = uv[1];
        }
        soft_transform4(tex, s->texture_matrix, &attr[SOFT_ATTR_TEX]);
    } else {
        attr[0] = attr[1] = attr[2] = 0.0f;
        attr[3] = 1.0f;
    }
    if (g->float_colors != NULL) {
        memcpy(&attr[SOFT_ATTR_COLOR], &g->float_colors[(size_t)index * 4u], 4 * sizeof(float));
    } else if (g->colors != NULL) {
        for (int c = 0; c < 4; c++) {
            attr[SOFT_ATTR_COLOR + c] = (float)g->colors[(size_t)index * 4u + (size_t)c] / 255.0f;
        }
    } else {
        memcpy(&attr[SOFT_ATTR_COLOR], s->color, 4 * sizeof(float));
    }
    const uint8_t *spec = g->speculars != NULL ? &g->speculars[(size_t)index * g->specular_stride]
                                               : s->secondary;
    for (int c = 0; c < 3; c++) {
        attr[SOFT_ATTR_SPEC + c] = (float)spec[c] / 255.0f;
    }
    float fog = 1.0f;
    if (s->fog) {
        float d = fabsf(eye[2]);
        if (s->fog_mode == 1) {
            fog = expf(-s->fog_density * d);
        } else if (s->fog_mode == 2) {
            float e = s->fog_density * d;
            fog = expf(-e * e);
        } else if (s->fog_end != s->fog_start) {
            fog = (s->fog_end - d) / (s->fog_end - s->fog_start);
        }
    }
    attr[SOFT_ATTR_FOG] = soft_clamp01(fog);
}

/* ============================================================================
 * Clipping and projection
 * ============================================================================ */

static void soft_lerp(const soft_vertex_t *a, const soft_vertex_t *b, float t, soft_vertex_t *out)
{
    for (int c = 0; c < 4; c++) {
        out->clip[c] = a->clip[c] + (b->clip[c] - a->clip[c]) * t;
    }
    for (int c = 0; c < SOFT_ATTRIBUTES; c++) {
        out->attr[c] = a->attr[c] + (b->attr[c] - a->attr[c]) * t;
    }
}

/* Distance of a vertex inside clip plane `plane`: 0 near (z >= 0), 1 far (z <= w), 2 w > 0 */
static float soft_plane(const soft_vertex_t *v, int plane)
{
    switch (plane) {
    case 0: return v->clip[2];
    case 1: return v->clip[3] - v->clip[2];
    default: return v->clip[3] - 1e-6f;
    }
}

/* A polygon clipped against the near, far and w planes; how many vertices it keeps */
static int soft_clip_polygon(soft_vertex_t *polygon, int count)
{
    soft_vertex_t scratch[SOFT_MAX_POLYGON];
    for (int plane = 0; plane < 3 && count > 0; plane++) {
        int out = 0;
        for (int i = 0; i < count; i++) {
            const soft_vertex_t *a = &polygon[i], *b = &polygon[(i + 1) % count];
            float da = soft_plane(a, plane), db = soft_plane(b, plane);
            if (da >= 0.0f && out < SOFT_MAX_POLYGON) {
                scratch[out++] = *a;
            }
            if ((da >= 0.0f) != (db >= 0.0f) && out < SOFT_MAX_POLYGON) {
                soft_lerp(a, b, da / (da - db), &scratch[out++]);
            }
        }
        memcpy(polygon, scratch, (size_t)out * sizeof(soft_vertex_t));
        count = out;
    }
    return count;
}

static void soft_project(const soft_context_t *ctx, const soft_vertex_t *v, soft_point_t *out)
{
    float inv_w = 1.0f / v->clip[3];
    out->inv_w = inv_w;
    out->x = ctx->viewport[0] + (v->clip[0] * inv_w + 1.0f) * 0.5f * ctx->viewport[2];
    out->y = ctx->viewport[1] + (1.0f - v->clip[1] * inv_w) * 0.5f * ctx->viewport[3];
    out->z = v->clip[2] * inv_w;
    for (int c = 0; c < SOFT_ATTRIBUTES; c++) {
        out->attr[c] = v->attr[c] * inv_w;
    }
}

/* ============================================================================
 * Rasterization
 * ============================================================================ */

typedef struct soft_triangle {
    soft_point_t p[3];
    float area;
} soft_triangle_t;

static float soft_edge(const soft_point_t *a, const soft_point_t *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

/* An edge whose pixels on it are drawn: a top edge or a left one (the triangle is clockwise) */
static bool soft_top_left(const soft_point_t *a, const soft_point_t *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    return (dy == 0.0f && dx > 0.0f) || dy < 0.0f;
}

/* The barycentric weights at x, y */
static void soft_weights(const soft_triangle_t *t, float x, float y, float out[3])
{
    out[0] = soft_edge(&t->p[1], &t->p[2], x, y) / t->area;
    out[1] = soft_edge(&t->p[2], &t->p[0], x, y) / t->area;
    out[2] = soft_edge(&t->p[0], &t->p[1], x, y) / t->area;
}

/* The texture coordinates (s, t, r, q) at x, y with perspective */
static void soft_tex_at(const soft_triangle_t *t, float x, float y, float out[4])
{
    float l[3];
    soft_weights(t, x, y, l);
    float inv_w = l[0] * t->p[0].inv_w + l[1] * t->p[1].inv_w + l[2] * t->p[2].inv_w;
    for (int c = 0; c < 4; c++) {
        float v = l[0] * t->p[0].attr[c] + l[1] * t->p[1].attr[c] + l[2] * t->p[2].attr[c];
        out[c] = inv_w != 0.0f ? v / inv_w : v;
    }
}

/* The level of detail at x, y: log2 of the texels a pixel step covers in level 0 */
static float soft_lod(const soft_context_t *ctx, const soft_triangle_t *t, float x, float y,
                      const float tex[4])
{
    const soft_texture_t *texture = ctx->texture;
    if (texture == NULL || texture->kind == RENDER_TEXTURE_CUBE || texture->faces[0][0].rgba == NULL) {
        return 0.0f;
    }
    float w = (float)texture->faces[0][0].width, h = (float)texture->faces[0][0].height;
    float right[4], down[4];
    soft_tex_at(t, x + 1.0f, y, right);
    soft_tex_at(t, x, y + 1.0f, down);
    float q = tex[3] != 0.0f ? tex[3] : 1.0f;
    float qr = right[3] != 0.0f ? right[3] : 1.0f, qd = down[3] != 0.0f ? down[3] : 1.0f;
    float u = tex[0] / q, v = tex[1] / q;
    float dux = (right[0] / qr - u) * w, dvx = (right[1] / qr - v) * h;
    float duy = (down[0] / qd - u) * w, dvy = (down[1] / qd - v) * h;
    float rho = fmaxf(sqrtf(dux * dux + dvx * dvx), sqrtf(duy * duy + dvy * dvy));
    return rho > 0.0f ? log2f(rho) : 0.0f;
}

static void soft_fill_triangle(const soft_context_t *ctx, const soft_point_t *a, const soft_point_t *b,
                               const soft_point_t *c)
{
    soft_triangle_t t;
    t.p[0] = *a;
    t.p[1] = *b;
    t.p[2] = *c;
    t.area = soft_edge(a, b, c->x, c->y);
    if (t.area < 0.0f) {
        t.p[1] = *c;
        t.p[2] = *b;
        t.area = -t.area;
    }
    if (!(t.area > 0.0f)) {
        return;
    }
    float min_x = fminf(t.p[0].x, fminf(t.p[1].x, t.p[2].x));
    float max_x = fmaxf(t.p[0].x, fmaxf(t.p[1].x, t.p[2].x));
    float min_y = fminf(t.p[0].y, fminf(t.p[1].y, t.p[2].y));
    float max_y = fmaxf(t.p[0].y, fmaxf(t.p[1].y, t.p[2].y));
    int32_t x0 = (int32_t)fmaxf(floorf(min_x), (float)ctx->bounds[0]);
    int32_t x1 = (int32_t)fminf(ceilf(max_x), (float)ctx->bounds[2]);
    int32_t y0 = (int32_t)fmaxf(floorf(min_y), (float)ctx->bounds[1]);
    int32_t y1 = (int32_t)fminf(ceilf(max_y), (float)ctx->bounds[3]);
    bool top_left[3] = {soft_top_left(&t.p[1], &t.p[2]), soft_top_left(&t.p[2], &t.p[0]),
                        soft_top_left(&t.p[0], &t.p[1])};
    for (int32_t y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (int32_t x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float e[3] = {soft_edge(&t.p[1], &t.p[2], px, py), soft_edge(&t.p[2], &t.p[0], px, py),
                          soft_edge(&t.p[0], &t.p[1], px, py)};
            bool inside = true;
            for (int k = 0; k < 3 && inside; k++) {
                inside = e[k] > 0.0f || (e[k] == 0.0f && top_left[k]);
            }
            if (!inside) {
                continue;
            }
            float l[3] = {e[0] / t.area, e[1] / t.area, e[2] / t.area};
            float inv_w = l[0] * t.p[0].inv_w + l[1] * t.p[1].inv_w + l[2] * t.p[2].inv_w;
            float z = l[0] * t.p[0].z + l[1] * t.p[1].z + l[2] * t.p[2].z;
            float attr[SOFT_ATTRIBUTES];
            for (int k = 0; k < SOFT_ATTRIBUTES; k++) {
                float v = l[0] * t.p[0].attr[k] + l[1] * t.p[1].attr[k] + l[2] * t.p[2].attr[k];
                attr[k] = inv_w != 0.0f ? v / inv_w : v;
            }
            float lod = ctx->texture != NULL ? soft_lod(ctx, &t, px, py, &attr[SOFT_ATTR_TEX]) : 0.0f;
            soft_fragment(ctx, x, y, z, attr, lod);
        }
    }
}

static bool soft_in_bounds(const soft_context_t *ctx, int32_t x, int32_t y)
{
    return x >= ctx->bounds[0] && x < ctx->bounds[2] && y >= ctx->bounds[1] && y < ctx->bounds[3];
}

/* A line a pixel wide, a fragment for each pixel step along its longer axis */
static void soft_fill_line(const soft_context_t *ctx, const soft_point_t *a, const soft_point_t *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    int32_t steps = (int32_t)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (steps < 1) {
        steps = 1;
    }
    for (int32_t i = 0; i < steps; i++) {
        float t = ((float)i + 0.5f) / (float)steps;
        int32_t x = (int32_t)floorf(a->x + dx * t), y = (int32_t)floorf(a->y + dy * t);
        if (!soft_in_bounds(ctx, x, y)) {
            continue;
        }
        float inv_w = a->inv_w + (b->inv_w - a->inv_w) * t;
        float attr[SOFT_ATTRIBUTES];
        for (int k = 0; k < SOFT_ATTRIBUTES; k++) {
            float v = a->attr[k] + (b->attr[k] - a->attr[k]) * t;
            attr[k] = inv_w != 0.0f ? v / inv_w : v;
        }
        soft_fragment(ctx, x, y, a->z + (b->z - a->z) * t, attr, 0.0f);
    }
}

static void soft_fill_point(const soft_context_t *ctx, const soft_point_t *p)
{
    int32_t x = (int32_t)floorf(p->x), y = (int32_t)floorf(p->y);
    if (!soft_in_bounds(ctx, x, y)) {
        return;
    }
    float attr[SOFT_ATTRIBUTES];
    for (int k = 0; k < SOFT_ATTRIBUTES; k++) {
        attr[k] = p->inv_w != 0.0f ? p->attr[k] / p->inv_w : p->attr[k];
    }
    soft_fragment(ctx, x, y, p->z, attr, 0.0f);
}

/* ============================================================================
 * Primitives
 * ============================================================================ */

static void soft_take_flat(soft_context_t *ctx, const soft_vertex_t *first)
{
    if (!ctx->flat) {
        return;
    }
    for (int c = 0; c < 4; c++) {
        ctx->flat_color[c] = soft_clamp01(first->attr[SOFT_ATTR_COLOR + c]);
    }
    for (int c = 0; c < 3; c++) {
        ctx->flat_spec[c] = soft_clamp01(first->attr[SOFT_ATTR_SPEC + c]);
    }
}

static void soft_triangle(soft_context_t *ctx, const soft_vertex_t *a, const soft_vertex_t *b,
                          const soft_vertex_t *c)
{
    soft_vertex_t polygon[SOFT_MAX_POLYGON];
    polygon[0] = *a;
    polygon[1] = *b;
    polygon[2] = *c;
    int count = soft_clip_polygon(polygon, 3);
    if (count < 3) {
        return;
    }
    soft_point_t points[SOFT_MAX_POLYGON];
    float area = 0.0f;
    for (int i = 0; i < count; i++) {
        soft_project(ctx, &polygon[i], &points[i]);
    }
    for (int i = 0; i < count; i++) {
        const soft_point_t *p = &points[i], *q = &points[(i + 1) % count];
        area += p->x * q->y - q->x * p->y;
    }
    if (area == 0.0f) {
        return;
    }
    /* Positive with y down: clockwise on the target */
    bool front = ctx->state->front_ccw ? area < 0.0f : area > 0.0f;
    if (ctx->state->cull && !front) {
        return;
    }
    soft_take_flat(ctx, a);
    if (ctx->state->fill == RENDER_FILL_POINT) {
        for (int i = 0; i < count; i++) {
            soft_fill_point(ctx, &points[i]);
        }
    } else if (ctx->state->fill == RENDER_FILL_LINE) {
        for (int i = 0; i < count; i++) {
            soft_fill_line(ctx, &points[i], &points[(i + 1) % count]);
        }
    } else {
        for (int i = 1; i + 1 < count; i++) {
            soft_fill_triangle(ctx, &points[0], &points[i], &points[i + 1]);
        }
    }
}

static void soft_line(soft_context_t *ctx, const soft_vertex_t *a, const soft_vertex_t *b)
{
    soft_vertex_t ends[2] = {*a, *b};
    for (int plane = 0; plane < 3; plane++) {
        float da = soft_plane(&ends[0], plane), db = soft_plane(&ends[1], plane);
        if (da < 0.0f && db < 0.0f) {
            return;
        }
        if (da < 0.0f) {
            soft_lerp(&ends[0], &ends[1], da / (da - db), &ends[0]);
        } else if (db < 0.0f) {
            soft_lerp(&ends[1], &ends[0], db / (db - da), &ends[1]);
        }
    }
    soft_point_t p[2];
    soft_project(ctx, &ends[0], &p[0]);
    soft_project(ctx, &ends[1], &p[1]);
    soft_take_flat(ctx, a);
    soft_fill_line(ctx, &p[0], &p[1]);
}

/* ============================================================================
 * Draws
 * ============================================================================ */

static void soft_projection_matrix(const render_state_t *s, float out[16])
{
    memset(out, 0, 16 * sizeof(float));
    if (s->projection_kind == RENDER_PROJECTION_2D) {
        /* x right, y down in render context pixels; z -1..1 to 1..0 */
        out[0] = 2.0f / s->projection_rect[0];
        out[5] = -2.0f / s->projection_rect[1];
        out[10] = -0.5f;
        out[12] = -1.0f;
        out[13] = 1.0f;
        out[14] = 0.5f;
        out[15] = 1.0f;
    } else if (s->projection_kind == RENDER_PROJECTION_SCREEN) {
        float left = s->projection_rect[0], top = s->projection_rect[1];
        float width = s->projection_rect[2], height = s->projection_rect[3];
        out[0] = 2.0f / width;
        out[5] = -2.0f / height;
        out[10] = 1.0f;
        out[12] = -2.0f * left / width - 1.0f;
        out[13] = 2.0f * top / height + 1.0f;
        out[15] = 1.0f;
    } else {
        memcpy(out, s->projection, 16 * sizeof(float));
        if (s->depth_scale != 0.0f) {
            out[14] *= s->depth_scale;
        }
    }
}

/* What a draw may touch: the viewport, cut by the scissor, on the surface */
static void soft_bounds(const soft_surface_t *surface, const render_state_t *s, bool viewport,
                        int32_t out[4])
{
    out[0] = 0;
    out[1] = 0;
    out[2] = surface->width;
    out[3] = surface->height;
    const int32_t *rects[2] = {viewport ? s->viewport : NULL, s->scissor ? s->scissor_rect : NULL};
    for (int r = 0; r < 2; r++) {
        const int32_t *rect = rects[r];
        if (rect == NULL) {
            continue;
        }
        out[0] = rect[0] > out[0] ? rect[0] : out[0];
        out[1] = rect[1] > out[1] ? rect[1] : out[1];
        out[2] = rect[0] + rect[2] < out[2] ? rect[0] + rect[2] : out[2];
        out[3] = rect[1] + rect[3] < out[3] ? rect[1] + rect[3] : out[3];
    }
}

void soft_clear(soft_surface_t *surface, const render_state_t *s, bool color, bool depth,
                bool stencil, const float value[4])
{
    if (surface == NULL || surface->color == NULL) {
        return;
    }
    int32_t bounds[4];
    soft_bounds(surface, s, false, bounds);
    uint8_t bytes[4] = {soft_byte(value[0]), soft_byte(value[1]), soft_byte(value[2]), soft_byte(value[3])};
    uint8_t mask = (uint8_t)s->stencil_write_mask;
    uint8_t clear_stencil = (uint8_t)s->clear_stencil;
    for (int32_t y = bounds[1]; y < bounds[3]; y++) {
        for (int32_t x = bounds[0]; x < bounds[2]; x++) {
            size_t at = (size_t)y * (size_t)surface->width + (size_t)x;
            if (color && s->color_write) {
                memcpy(&surface->color[at * 4u], bytes, 4);
            }
            if (depth && s->depth_write) {
                surface->depth[at] = 1.0f;
            }
            if (stencil) {
                surface->stencil[at] = (uint8_t)((surface->stencil[at] & (uint8_t)~mask) | (clear_stencil & mask));
            }
        }
    }
}

void soft_draw(soft_surface_t *surface, const render_state_t *s, const render_geometry_t *g)
{
    if (surface == NULL || surface->color == NULL || g->positions == NULL || g->vertex_count <= 0) {
        return;
    }
    soft_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.surface = surface;
    ctx.state = s;
    if (s->texture_kind != RENDER_TEXTURE_NONE && s->texture != 0) {
        ctx.texture = (const soft_texture_t *)s->texture;
    }
    soft_bounds(surface, s, true, ctx.bounds);
    if (ctx.bounds[0] >= ctx.bounds[2] || ctx.bounds[1] >= ctx.bounds[3]) {
        return;
    }
    for (int c = 0; c < 4; c++) {
        ctx.viewport[c] = (float)s->viewport[c];
    }
    memcpy(ctx.model_view, s->model_view, sizeof(ctx.model_view));
    soft_projection_matrix(s, ctx.projection);
    float inverse[16];
    if (!render_affine_inverse(inverse, s->model_view)) {
        memset(inverse, 0, sizeof(inverse));
        inverse[0] = inverse[5] = inverse[10] = inverse[15] = 1.0f;
    }
    memcpy(ctx.normal_matrix, inverse, sizeof(inverse));
    float fog[4];
    render_color(s->fog_color, fog);
    memcpy(ctx.fog_color, fog, sizeof(ctx.fog_color));
    render_color(s->sampler.border_color, ctx.border);
    ctx.flat = s->flat;

    int32_t count = g->indices != NULL ? g->index_count : g->vertex_count;
    soft_vertex_t corner[3];
    if (g->primitive == RENDER_PRIMITIVE_LINES) {
        for (int32_t i = 0; i + 1 < count; i += 2) {
            for (int k = 0; k < 2; k++) {
                int32_t index = g->indices != NULL ? g->indices[i + k] : i + k;
                soft_vertex(&ctx, g, index, &corner[k]);
            }
            soft_line(&ctx, &corner[0], &corner[1]);
        }
        return;
    }
    if (g->primitive == RENDER_PRIMITIVE_TRIANGLE_FAN) {
        if (count < 3) {
            return;
        }
        soft_vertex(&ctx, g, g->indices != NULL ? g->indices[0] : 0, &corner[0]);
        soft_vertex(&ctx, g, g->indices != NULL ? g->indices[1] : 1, &corner[1]);
        for (int32_t i = 2; i < count; i++) {
            soft_vertex(&ctx, g, g->indices != NULL ? g->indices[i] : i, &corner[2]);
            soft_triangle(&ctx, &corner[0], &corner[1], &corner[2]);
            corner[1] = corner[2];
        }
        return;
    }
    for (int32_t i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; k++) {
            int32_t index = g->indices != NULL ? g->indices[i + k] : i + k;
            soft_vertex(&ctx, g, index, &corner[k]);
        }
        soft_triangle(&ctx, &corner[0], &corner[1], &corner[2]);
    }
}
