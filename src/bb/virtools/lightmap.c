/**
 * @file lightmap.c
 * @brief Lightmap and Static Lightmap (BuildingBlocksAddons1).
 *
 * The LightmapGenerator of BuildingBlocksAddons1.dll: the faces of each
 * receiving object grouped into polygons of about the same normal (its
 * PolyhedronConstructor, the edge method), a texture a polygon lit by the
 * lights with the occluders' shadows, the textures of an object packed into
 * one. Lightmap draws them over the objects (a post render callback);
 * Static Lightmap makes objects of them. The reference is the Ballanced
 * source of the generator, or the DLL where they differ.
 */

#include "virtools.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LM_MAX_LIGHT 256
#define LIGHTMAPSV1_IDENTIFIER 4510204
#define LM_EPSILON 1.1920929e-7f /* VxMath's EPSILON */

/* ============================================================================
 * The lightmaps
 * ============================================================================ */

/* LightVertex, as the chunk saves it */
typedef struct lm_vertex {
    float x, y, z;
    uint32_t diffuse, specular;
    float u, v;
    uint32_t index; /* the mesh's vertex */
} lm_vertex_t;

_Static_assert(sizeof(lm_vertex_t) == 32, "LightVertex is 32 bytes");

/* LightMapTexture: a texture and the faces it lights */
typedef struct lm_map {
    nmo_object_id_t texture;
    ENG_VEC(lm_vertex_t) vertices;
    ENG_VEC(uint16_t) indices;
    ENG_VEC(uint16_t) original; /* each face's face of the mesh */
} lm_map_t;

/* LightMapFaces: an object and its lightmaps */
typedef struct lm_object {
    nmo_object_id_t object;
    ENG_VEC(lm_map_t *) maps;
} lm_object_t;

typedef struct lm_poly lm_poly_t;

typedef struct lm_edge {
    int32_t a, b;
    lm_poly_t *p[2];
} lm_edge_t;

/* PolyhedronConstructor::Polyhedron */
struct lm_poly {
    int32_t magic; /* 1 once merged into another */
    bool smooth;
    eng_vector_t max, min;
    eng_vector_t normal;
    ENG_VEC(int32_t) faces;
    ENG_VEC(int32_t) vertices;
    ENG_VEC(lm_edge_t *) edges;
};

/* PolyhedronConstructor (the edge method) */
typedef struct lm_builder {
    const nmo_engine_mesh_t *mesh;
    const eng_vector_t *face_normals;
    lm_edge_t *edges;        /* three a face at most */
    int32_t edge_count;
    int32_t *slots;          /* (a, b) -> edge, open addressing */
    uint32_t slot_mask;
    ENG_VEC(lm_poly_t *) polys;
    ENG_VEC(lm_poly_t *) all; /* every one made, freed with the builder */
    float threshold;
} lm_builder_t;

typedef struct vx_plane {
    eng_vector_t n;
    float d;
} vx_plane_t;

/* A LightmapGenerator: the block's, kept by the manager */
typedef struct lightmap {
    int32_t node;
    ENG_VEC(lm_object_t *) objects;
    bool show_polygons;
    uint32_t opacity;

    /* Generate's world */
    ENG_VEC(nmo_object_id_t) receivers, occluders, lights, filtered_lights;
    ENG_VEC(nmo_object_id_t) filtered_occluders[LM_MAX_LIGHT];
    int32_t current_light;
    /* its temporary data for the localized functions */
    lm_builder_t *builder;
    int32_t poly;
    int32_t major_x, major_y;
    eng_vector_t box_world[4], polygon_world[4];
    nmo_object_id_t entity;
    int32_t entity_index;
    nmo_object_id_t mesh;
    nmo_engine_mesh_t shape;
    float ambient[4];
    int32_t super_sampling;
    float inverse_sampling;
    vx_plane_t plane;
} lightmap_t;

typedef struct lightmap_state {
    ENG_VEC(lightmap_t *) maps;
} lightmap_state_t;

static void lm_free_map(lm_map_t *map)
{
    free(map->vertices.data);
    free(map->indices.data);
    free(map->original.data);
    free(map);
}

static void lm_destroy_object(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (id != 0 && eng_find_object(engine, id) != NULL) {
        eng_destroy_objects(engine, &id, 1, NULL);
    }
}

/* LightmapGenerator::Clean: the lightmaps gone (their textures destroyed when `textures`) */
static void lm_clean(nmo_engine_t *engine, lightmap_t *lm, bool textures)
{
    for (size_t o = 0; o < lm->objects.count; o++) {
        lm_object_t *object = lm->objects.data[o];
        for (size_t t = 0; t < object->maps.count; t++) {
            if (textures) {
                lm_destroy_object(engine, object->maps.data[t]->texture);
            }
            lm_free_map(object->maps.data[t]);
        }
        free(object->maps.data);
        free(object);
    }
    lm->objects.count = 0;
}

static void lightmap_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    lightmap_state_t *s = (lightmap_state_t *)state;
    for (size_t i = 0; i < s->maps.count; i++) {
        lightmap_t *lm = s->maps.data[i];
        for (size_t o = 0; o < lm->objects.count; o++) {
            lm_object_t *object = lm->objects.data[o];
            for (size_t t = 0; t < object->maps.count; t++) {
                lm_free_map(object->maps.data[t]);
            }
            free(object->maps.data);
            free(object);
        }
        free(lm->objects.data);
        free(lm->receivers.data), free(lm->occluders.data), free(lm->lights.data), free(lm->filtered_lights.data);
        for (int l = 0; l < LM_MAX_LIGHT; l++) {
            free(lm->filtered_occluders[l].data);
        }
        free(lm);
    }
    free(s->maps.data);
}

static const eng_manager_desc_t lightmap_manager = {
    .name = "Lightmap Generators",
    .state_size = sizeof(lightmap_state_t),
    .destroy = lightmap_destroy,
};

/* The block's generator (`create`: made when it has none) */
static lightmap_t *lm_of(nmo_engine_t *engine, int32_t node, bool create)
{
    lightmap_state_t *s = (lightmap_state_t *)eng_manager_state(engine, &lightmap_manager);
    if (s == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < s->maps.count; i++) {
        if (s->maps.data[i]->node == node) {
            return s->maps.data[i];
        }
    }
    lightmap_t **slot = NULL;
    lightmap_t *lm = create ? (lightmap_t *)calloc(1, sizeof(lightmap_t)) : NULL;
    if (lm == NULL || !ENG_VEC_PUSH(s->maps, slot)) {
        free(lm);
        return NULL;
    }
    lm->node = node;
    *slot = lm;
    return lm;
}

/* ============================================================================
 * Small math
 * ============================================================================ */

static eng_vector_t v_add(eng_vector_t a, eng_vector_t b) { return (eng_vector_t){a.x + b.x, a.y + b.y, a.z + b.z}; }
static eng_vector_t v_sub(eng_vector_t a, eng_vector_t b) { return (eng_vector_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static eng_vector_t v_scale(eng_vector_t a, float k) { return (eng_vector_t){a.x * k, a.y * k, a.z * k}; }
static float v_dot(eng_vector_t a, eng_vector_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float v_mag(eng_vector_t a) { return sqrtf(v_dot(a, a)); }
static float *v_at(eng_vector_t *v, int k) { return k == 0 ? &v->x : k == 1 ? &v->y : &v->z; }
static float v_get(eng_vector_t v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }
static eng_vector_t v_of(const nmo_vector_t *p) { return (eng_vector_t){p->x, p->y, p->z}; }

static eng_vector_t v_normalized(eng_vector_t a)
{
    float m = v_mag(a);
    return m > 0.0f ? v_scale(a, 1.0f / m) : a;
}

static eng_vector_t v_cross(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float plane_classify(const vx_plane_t *p, eng_vector_t v) { return v_dot(p->n, v) + p->d; }

/* VxPlane::Create(a, b, c) */
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
    return (vx_plane_t){n, (float)(-((double)n.x * a.x + (double)n.y * a.y + (double)n.z * a.z))};
}

/* VxPlane::Classify(box, matrix): 0 when the box placed by the matrix crosses the plane */
static float plane_classify_box(const vx_plane_t *p, eng_vector_t min, eng_vector_t max, const eng_matrix_t *m)
{
    eng_vector_t axis[3];
    const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    for (int k = 0; k < 3; k++) {
        axis[k] = (eng_vector_t){m->m[k][0] * half[k], m->m[k][1] * half[k], m->m[k][2] * half[k]};
    }
    eng_vector_t center = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f}, at;
    eng_vector_transform(&at, m, &center);
    float radius = fabsf(v_dot(p->n, axis[0])) + fabsf(v_dot(p->n, axis[1])) + fabsf(v_dot(p->n, axis[2]));
    float distance = plane_classify(p, at);
    if (distance > radius) {
        return distance - radius;
    }
    if (distance < -radius) {
        return distance + radius;
    }
    return 0.0f;
}

/* VxIntersect::LinePlane; false (the point left alone) when parallel */
static bool line_plane(eng_vector_t origin, eng_vector_t dir, const vx_plane_t *plane, eng_vector_t *point)
{
    double denom = (double)plane->n.x * dir.x + (double)plane->n.y * dir.y + (double)plane->n.z * dir.z;
    if (fabs(denom) < LM_EPSILON) {
        return false;
    }
    double num = (double)plane->n.x * origin.x + (double)plane->n.y * origin.y + (double)plane->n.z * origin.z + plane->d;
    double t = -num / denom;
    *point = (eng_vector_t){(float)(origin.x + dir.x * t), (float)(origin.y + dir.y * t), (float)(origin.z + dir.z * t)};
    return true;
}

/* VxRay(a, b) with its direction normalized, then SquareDistance(p) */
static float edge_square_distance(eng_vector_t a, eng_vector_t b, eng_vector_t p)
{
    eng_vector_t d = v_normalized(v_sub(b, a));
    double x = (double)p.x - a.x, y = (double)p.y - a.y, z = (double)p.z - a.z;
    double dd = (double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z;
    if (dd == 0.0) {
        return (float)(x * x + y * y + z * z);
    }
    double cx = y * d.z - z * d.y, cy = z * d.x - x * d.z, cz = x * d.y - y * d.x;
    return (float)((cx * cx + cy * cy + cz * cz) / dd);
}

/* VxIntersect::SegmentBox: the segment from `o` along `d` (to o + d) meets the box */
static bool segment_box(eng_vector_t o, eng_vector_t d, eng_vector_t min, eng_vector_t max)
{
    double near_t = 0.0, far_t = 1.0;
    for (int k = 0; k < 3; k++) {
        double ok = v_get(o, k), dk = v_get(d, k), lo = v_get(min, k), hi = v_get(max, k);
        if (dk == 0.0) {
            if (ok < lo || ok > hi) {
                return false;
            }
            continue;
        }
        double a = (lo - ok) / dk, b = (hi - ok) / dk;
        double first = a < b ? a : b, last = a < b ? b : a;
        near_t = first > near_t ? first : near_t;
        far_t = last < far_t ? last : far_t;
    }
    return near_t <= far_t;
}

/* VxIntersect::GetPointCoefficients on the axes i1, i2 */
static void point_coefficients(eng_vector_t p, eng_vector_t a, eng_vector_t b, eng_vector_t c, int i1, int i2,
                               float *c0, float *c1, float *c2)
{
    const double ax = v_get(a, i1), ay = v_get(a, i2), bx = v_get(b, i1), by = v_get(b, i2);
    const double cx = v_get(c, i1), cy = v_get(c, i2), px = v_get(p, i1), py = v_get(p, i2);
    const double det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    *c0 = (float)(((bx - px) * (cy - py) - (by - py) * (cx - px)) / det);
    *c1 = (float)(((px - ax) * (cy - ay) - (py - ay) * (cx - ax)) / det);
    *c2 = (float)(((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / det);
}

/* The smallest power of two not under n (the DLL's Near2Power: n's lowest bit doubled) */
static int32_t lm_power2(int32_t n)
{
    int32_t p = n & -n;
    while (p < n) {
        p *= 2;
    }
    return p;
}

static void color_of(uint32_t argb, float c[4])
{
    c[0] = (float)((argb >> 16) & 0xFFu) * 0.003921568627f;
    c[1] = (float)((argb >> 8) & 0xFFu) * 0.003921568627f;
    c[2] = (float)(argb & 0xFFu) * 0.003921568627f;
    c[3] = (float)(argb >> 24) * 0.003921568627f;
}

static uint32_t rgbai(int32_t r, int32_t g, int32_t b, int32_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* MSVC's rand() */
static int32_t lm_rand(uint32_t *seed)
{
    *seed = *seed * 214013u + 2531011u;
    return (int32_t)((*seed >> 16) & 0x7FFFu);
}

/* ============================================================================
 * Textures as CKDWORD images
 * ============================================================================ */

/* CKTexture::Create(width, height): a new 32 bit image */
static bool lm_texture_create(nmo_engine_t *engine, nmo_object_id_t id, int32_t w, int32_t h)
{
    eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    return texture != NULL && eng_texture_create(engine, texture, w, h, 0);
}

/* LockSurfacePtr: the image as ARGB dwords (a copy, written back by lm_unlock) */
static uint32_t *lm_lock(nmo_engine_t *engine, nmo_object_id_t id, int32_t *w, int32_t *h)
{
    eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    eng_surface_t *surface = texture != NULL ? eng_texture_surface(engine, texture, -1) : NULL;
    if (surface == NULL || surface->rgba == NULL || surface->width <= 0 || surface->height <= 0) {
        return NULL;
    }
    size_t n = (size_t)surface->width * (size_t)surface->height;
    uint32_t *pixels = (uint32_t *)malloc(n * sizeof(uint32_t));
    for (size_t i = 0; pixels != NULL && i < n; i++) {
        const uint8_t *p = &surface->rgba[4 * i];
        pixels[i] = rgbai(p[0], p[1], p[2], p[3]);
    }
    *w = surface->width;
    *h = surface->height;
    return pixels;
}

/* ReleaseSurfacePtr */
static void lm_unlock(nmo_engine_t *engine, nmo_object_id_t id, uint32_t *pixels)
{
    eng_texture_t *texture = id != 0 && pixels != NULL ? eng_texture(engine, id) : NULL;
    eng_surface_t *surface = texture != NULL ? eng_texture_surface(engine, texture, -1) : NULL;
    if (surface != NULL && surface->rgba != NULL) {
        size_t n = (size_t)surface->width * (size_t)surface->height;
        for (size_t i = 0; i < n; i++) {
            uint8_t *p = &surface->rgba[4 * i];
            p[0] = (uint8_t)(pixels[i] >> 16);
            p[1] = (uint8_t)(pixels[i] >> 8);
            p[2] = (uint8_t)pixels[i];
            p[3] = (uint8_t)(pixels[i] >> 24);
        }
        eng_texture_changed(engine, id);
    }
    free(pixels);
}

static int32_t clamp255(int32_t v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* 2 a - b of each color channel, opaque */
static uint32_t extend_pixel(uint32_t a, uint32_t b)
{
    int32_t r = clamp255(2 * (int32_t)((a >> 16) & 0xFFu) - (int32_t)((b >> 16) & 0xFFu));
    int32_t g = clamp255(2 * (int32_t)((a >> 8) & 0xFFu) - (int32_t)((b >> 8) & 0xFFu));
    int32_t bl = clamp255(2 * (int32_t)(a & 0xFFu) - (int32_t)(b & 0xFFu));
    return rgbai(r, g, bl, 255);
}

/* LightmapGenerator::ExtendBorderTexture: the border texels extrapolated from the two inside */
static void extend_border(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t w = 0, h = 0;
    uint32_t *img = lm_lock(engine, id, &w, &h);
    if (img == NULL) {
        return;
    }
    if (w > 2) {
        for (int32_t i = 1; i < h - 1; i++) {
            img[i * w] = extend_pixel(img[i * w + 1], img[i * w + 2]);
            img[i * w + w - 1] = extend_pixel(img[i * w + w - 2], img[i * w + w - 3]);
        }
    }
    if (h > 2) {
        for (int32_t i = 0; i < w; i++) {
            img[i] = extend_pixel(img[w + i], img[2 * w + i]);
            img[(h - 1) * w + i] = extend_pixel(img[(h - 2) * w + i], img[(h - 3) * w + i]);
        }
    }
    lm_unlock(engine, id, img);
}

/* ApplyMatrixToTextureInsides with CMGaussianBlur2: the texels off the border averaged */
static void gaussian_blur(nmo_engine_t *engine, nmo_object_id_t id)
{
    static const int32_t matrix[3][3] = {{2, 4, 2}, {4, 0, 4}, {2, 4, 2}};
    const int32_t sum = 24;
    int32_t w = 0, h = 0;
    uint32_t *img = lm_lock(engine, id, &w, &h);
    uint32_t *out = img != NULL ? (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t)) : NULL;
    if (out == NULL) {
        free(img);
        return;
    }
    for (int32_t i = 0; i < h; i++) {
        for (int32_t j = 0; j < w; j++) {
            if (i == 0 || i == h - 1 || j == 0 || j == w - 1) {
                out[i * w + j] = img[i * w + j];
                continue;
            }
            int32_t acc[4] = {0, 0, 0, 0};
            for (int32_t a = 0; a < 3; a++) {
                for (int32_t b = 0; b < 3; b++) {
                    /* matrix[a][b] takes column j - 1 + a, row i - 1 + b */
                    uint32_t c = img[(j - 1 + a) + (i - 1 + b) * w];
                    acc[0] += (int32_t)((c >> 16) & 0xFFu) * matrix[a][b];
                    acc[1] += (int32_t)((c >> 8) & 0xFFu) * matrix[a][b];
                    acc[2] += (int32_t)(c & 0xFFu) * matrix[a][b];
                    acc[3] += (int32_t)(c >> 24) * matrix[a][b];
                }
            }
            out[i * w + j] = rgbai(acc[0] / sum, acc[1] / sum, acc[2] / sum, acc[3] / sum);
        }
    }
    memcpy(img, out, (size_t)w * (size_t)h * sizeof(uint32_t));
    free(out);
    lm_unlock(engine, id, img);
}

/* ============================================================================
 * PolyhedronConstructor (the edge method)
 * ============================================================================ */

static void builder_free(lm_builder_t *b)
{
    for (size_t i = 0; i < b->all.count; i++) {
        lm_poly_t *p = b->all.data[i];
        free(p->faces.data), free(p->vertices.data), free(p->edges.data), free(p);
    }
    free(b->all.data), free(b->polys.data), free(b->edges), free(b->slots);
    free(b);
}

static bool push_int(void *vec, int32_t value)
{
    ENG_VEC(int32_t) *v = vec;
    int32_t *slot = NULL;
    if (!ENG_VEC_PUSH(*v, slot)) {
        return false;
    }
    *slot = value;
    return true;
}

static bool contains_int(const int32_t *data, size_t count, int32_t value)
{
    for (size_t i = 0; i < count; i++) {
        if (data[i] == value) {
            return true;
        }
    }
    return false;
}

static bool push_edge(lm_poly_t *p, lm_edge_t *e)
{
    lm_edge_t **slot = NULL;
    if (!ENG_VEC_PUSH(p->edges, slot)) {
        return false;
    }
    *slot = e;
    return true;
}

/* m_Edges[a * vcount + b], made the first time */
static lm_edge_t *builder_edge(lm_builder_t *b, int32_t a, int32_t c)
{
    uint64_t key = ((uint64_t)(uint32_t)a << 32) | (uint32_t)c;
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 32) & b->slot_mask;
    while (b->slots[h] >= 0) {
        lm_edge_t *e = &b->edges[b->slots[h]];
        if (e->a == a && e->b == c) {
            return e;
        }
        h = (h + 1u) & b->slot_mask;
    }
    b->slots[h] = b->edge_count;
    lm_edge_t *e = &b->edges[b->edge_count++];
    e->a = a;
    e->b = c;
    return e;
}

static void edge_take(lm_edge_t *e, lm_poly_t *p)
{
    if (e->p[0] != NULL) {
        e->p[1] = p;
    } else {
        e->p[0] = p;
    }
}

/* The constructor: a polyhedron a face, and the edges between them */
static lm_builder_t *builder_create(const nmo_engine_mesh_t *mesh, const eng_vector_t *face_normals)
{
    lm_builder_t *b = (lm_builder_t *)calloc(1, sizeof(lm_builder_t));
    int32_t fcount = mesh->face_count;
    uint32_t slots = 16u;
    while (slots < (uint32_t)fcount * 6u) {
        slots *= 2u;
    }
    if (b != NULL) {
        b->mesh = mesh;
        b->face_normals = face_normals;
        b->edges = (lm_edge_t *)calloc((size_t)fcount * 3u + 1u, sizeof(lm_edge_t));
        b->slots = (int32_t *)malloc(slots * sizeof(int32_t));
        b->slot_mask = slots - 1u;
    }
    if (b == NULL || b->edges == NULL || b->slots == NULL) {
        if (b != NULL) {
            builder_free(b);
        }
        return NULL;
    }
    memset(b->slots, 0xFF, slots * sizeof(int32_t));
    for (int32_t i = 0; i < fcount; i++) {
        lm_poly_t *p = (lm_poly_t *)calloc(1, sizeof(lm_poly_t));
        lm_poly_t **slot = NULL, **all = NULL;
        if (p == NULL || !ENG_VEC_PUSH(b->all, all)) {
            free(p);
            builder_free(b);
            return NULL;
        }
        *all = p;
        if (!ENG_VEC_PUSH(b->polys, slot)) {
            builder_free(b);
            return NULL;
        }
        *slot = p;
        p->max = (eng_vector_t){-1.0e6f, -1.0e6f, -1.0e6f};
        p->min = (eng_vector_t){1.0e6f, 1.0e6f, 1.0e6f};
        p->normal = face_normals[i];
        const uint16_t *k = &mesh->indices[3 * i];
        for (int c = 0; c < 3; c++) {
            eng_vector_t v = v_of(&mesh->vertices[k[c]].position);
            for (int a = 0; a < 3; a++) {
                if (v_get(v, a) > v_get(p->max, a)) {
                    *v_at(&p->max, a) = v_get(v, a);
                }
                if (v_get(v, a) < v_get(p->min, a)) {
                    *v_at(&p->min, a) = v_get(v, a);
                }
            }
        }
        push_int(&p->faces, i);
        int32_t x = k[0], y = k[1], z = k[2], lo, mid, hi;
        lo = x < y ? (x < z ? x : z) : (y < z ? y : z);
        hi = x > y ? (x > z ? x : z) : (y > z ? y : z);
        mid = x + y + z - lo - hi;
        for (int c = 0; c < 3; c++) {
            eng_vector_t n = v_of(&mesh->vertices[k[c]].normal);
            if (v_dot(n, p->normal) < 0.99f) { /* the vertex normal is not the face's */
                p->smooth = true;
                break;
            }
        }
        push_int(&p->vertices, lo);
        push_int(&p->vertices, mid);
        push_int(&p->vertices, hi);
        const int32_t pairs[3][2] = {{lo, mid}, {lo, hi}, {mid, hi}};
        for (int e = 0; e < 3; e++) {
            lm_edge_t *edge = builder_edge(b, pairs[e][0], pairs[e][1]);
            edge_take(edge, p);
            push_edge(p, edge);
        }
    }
    return b;
}

static float poly_dot(const lm_poly_t *a, const lm_poly_t *b) { return fabsf(v_dot(a->normal, b->normal)); }

static void poly_remove(lm_builder_t *b, lm_poly_t *p)
{
    for (size_t i = 0; i < b->polys.count; i++) {
        if (b->polys.data[i] == p) {
            memmove(&b->polys.data[i], &b->polys.data[i + 1], (b->polys.count - i - 1) * sizeof(lm_poly_t *));
            b->polys.count--;
            return;
        }
    }
}

/* PolyhedronConstructor::RSimplify: `p` (reached through `from`) merged into `ref`, and on through
   its edges to the polyhedrons of about the same normal */
static void rsimplify(lm_builder_t *b, lm_poly_t *ref, lm_edge_t *from, lm_poly_t *p)
{
    for (size_t i = 0; i < p->faces.count; i++) {
        push_int(&ref->faces, p->faces.data[i]);
    }
    for (int a = 0; a < 3; a++) {
        if (v_get(p->max, a) > v_get(ref->max, a)) {
            *v_at(&ref->max, a) = v_get(p->max, a);
        }
        if (v_get(p->min, a) < v_get(ref->min, a)) {
            *v_at(&ref->min, a) = v_get(p->min, a);
        }
    }
    for (size_t i = 0; i < p->edges.count; i++) {
        lm_edge_t *e = p->edges.data[i];
        int side = p != e->p[0] ? 1 : 0;
        if (e->p[side] != NULL) {
            e->p[side] = ref;
        }
    }
    for (size_t i = 0; i < p->edges.count; i++) {
        lm_edge_t *e = p->edges.data[i];
        if (e == from) {
            continue; /* where we came from */
        }
        if (!contains_int(ref->vertices.data, ref->vertices.count, e->a)) {
            push_int(&ref->vertices, e->a);
        }
        if (!contains_int(ref->vertices.data, ref->vertices.count, e->b)) {
            push_int(&ref->vertices, e->b);
        }
        int side = ref == e->p[0] ? 1 : 0;
        lm_poly_t *next = e->p[side];
        if (next == NULL) {
            push_edge(ref, e); /* a frontier */
            continue;
        }
        if (next != ref && poly_dot(next, ref) >= b->threshold) {
            ref->smooth |= next->smooth;
            e->p[1 - side] = NULL;
            e->p[side] = NULL;
            rsimplify(b, ref, e, next);
        }
    }
    poly_remove(b, p);
    p->magic = 1;
}

/* PolyhedronConstructor::Simplify: each polyhedron left grows over its neighbours of about the same
   normal (|dot| at least `threshold`) */
static bool builder_simplify(lm_builder_t *b, float threshold)
{
    b->threshold = threshold;
    ENG_VEC(lm_poly_t *) kept = {0};
    ENG_VEC(lm_edge_t *) temp = {0};
    while (b->polys.count > 0) {
        lm_poly_t *ref = b->polys.data[--b->polys.count];
        lm_poly_t **slot = NULL;
        if (!ENG_VEC_PUSH(kept, slot)) {
            free(kept.data), free(temp.data);
            return false;
        }
        *slot = ref;
        /* tempedges.Swap(reference->m_Edges) */
        lm_edge_t **data = temp.data;
        size_t capacity = temp.capacity;
        temp.data = ref->edges.data, temp.count = ref->edges.count, temp.capacity = ref->edges.capacity;
        ref->edges.data = data, ref->edges.count = 0, ref->edges.capacity = capacity;
        for (size_t i = 0; i < temp.count; i++) {
            lm_edge_t *e = temp.data[i];
            int side = ref == e->p[0] ? 1 : 0;
            lm_poly_t *next = e->p[side];
            if (next == NULL) {
                push_edge(ref, e); /* a frontier */
                continue;
            }
            if (next != ref && poly_dot(next, ref) >= threshold) {
                ref->smooth |= next->smooth;
                rsimplify(b, ref, e, next);
            }
        }
        temp.count = 0;
    }
    free(b->polys.data);
    b->polys.data = kept.data, b->polys.count = kept.count, b->polys.capacity = kept.capacity;
    free(temp.data);
    return true;
}

/* ============================================================================
 * Generate
 * ============================================================================ */

static nmo_object_id_t lm_entity_mesh(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_entity_index(engine, id) >= 0 ? eng_find_object(engine, id) : NULL;
    const nmo_3dentity_state_t *state = object != NULL
        ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                          CKPGUID_3DENTITY)
        : NULL;
    return state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
}

static void push_id(void *vec, nmo_object_id_t id)
{
    ENG_VEC(nmo_object_id_t) *v = vec;
    nmo_object_id_t *slot = NULL;
    if (ENG_VEC_PUSH(*v, slot)) {
        *slot = id;
    }
}

/* SetReceivers and SetOccluders: the group's objects, every 3D object without one */
static void lm_set_objects(nmo_engine_t *engine, void *vec, nmo_object_id_t group)
{
    ENG_VEC(nmo_object_id_t) *v = vec;
    v->count = 0;
    if (group != 0) {
        int32_t count = nmo_engine_group_count(engine, group);
        for (int32_t i = 0; i < count; i++) {
            push_id(v, nmo_engine_group_object(engine, group, i));
        }
        return;
    }
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(engine, NMO_CID_3DOBJECT, &ids);
    for (size_t i = 0; i < count; i++) {
        push_id(v, ids[i]);
    }
    free(ids);
}

/* SetLights: the group's lights, without one every light then every target light */
static void lm_set_lights(nmo_engine_t *engine, lightmap_t *lm, nmo_object_id_t group)
{
    lm->lights.count = 0;
    if (group != 0) {
        int32_t count = nmo_engine_group_count(engine, group);
        for (int32_t i = 0; i < count; i++) {
            nmo_object_id_t id = nmo_engine_group_object(engine, group, i);
            if (id != 0 && eng_light(engine, id) != NULL) {
                push_id(&lm->lights, id);
            }
        }
        return;
    }
    const uint32_t classes[2] = {NMO_CID_LIGHT, NMO_CID_TARGETLIGHT};
    for (int c = 0; c < 2; c++) {
        nmo_object_id_t *ids = NULL;
        size_t count = eng_objects_of_class(engine, classes[c], &ids);
        for (size_t i = 0; i < count; i++) {
            push_id(&lm->lights, ids[i]);
        }
        free(ids);
    }
}

static const eng_matrix_t *lm_world(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine, id);
    return index >= 0 ? &engine->entities.data[index].world : NULL;
}

/* The face's material, as the occluder's current mesh has it */
static nmo_object_id_t lm_face_material(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t face)
{
    nmo_engine_mesh_t shape;
    if (!nmo_engine_mesh(engine, mesh, &shape) || shape.faces == NULL || face < 0 || face >= shape.face_count) {
        return 0;
    }
    return nmo_engine_mesh_material(engine, mesh, (int32_t)shape.faces[face].material_group_idx);
}

/* LightmapGenerator::ComputeTransparency: the occluder's face lets the light through when its
   material is alpha transparent, mixing its color (times its texture's there) in */
static bool compute_transparency(nmo_engine_t *engine, nmo_object_id_t occluder, const eng_ray_hit_t *hit,
                                 float trans[4])
{
    nmo_object_id_t id = lm_face_material(engine, lm_entity_mesh(engine, occluder), hit->face);
    nmo_engine_material_t mat;
    if (id == 0 || !nmo_engine_material(engine, id, &mat)) {
        return false;
    }
    if (!mat.alpha_blend || mat.dest_blend == VXBLEND_ZERO) { /* IsAlphaTransparent */
        return false;
    }
    float diffuse[4] = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.diffuse[3]};
    nmo_engine_image_t image;
    if (mat.texture != 0 && nmo_engine_texture_image(engine, mat.texture, &image) && image.rgba != NULL &&
        image.width > 0 && image.height > 0) {
        float u = hit->u, v = hit->v;
        switch (mat.address_mode) {
        case VXTEXTURE_ADDRESSWRAP:
            u -= (float)(int32_t)u;
            u = u < 0.0f ? u + 1.0f : u;
            v -= (float)(int32_t)v;
            v = v < 0.0f ? v + 1.0f : v;
            break;
        case VXTEXTURE_ADDRESSCLAMP:
        case VXTEXTURE_ADDRESSBORDER:
            u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
            v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            break;
        case VXTEXTURE_ADDRESSMIRROR: {
            int32_t tu = (int32_t)u, tv = (int32_t)v;
            u -= (float)tu;
            u = u < 0.0f ? u + 1.0f : u;
            u = (tu & 1) ? 1.0f - u : u;
            v -= (float)tv;
            v = v < 0.0f ? v + 1.0f : v;
            v = (tv & 1) ? 1.0f - v : v;
            break;
        }
        case VXTEXTURE_ADDRESSMIRRORONCE:
            u = fminf(1.0f, fabsf(u));
            v = fminf(1.0f, fabsf(v));
            break;
        default:
            break;
        }
        int32_t x = (int32_t)(u * (float)(image.width - 1)), y = (int32_t)(v * (float)(image.height - 1));
        if (x >= 0 && y >= 0 && x < image.width && y < image.height) {
            const uint8_t *p = &image.rgba[4 * ((size_t)y * (size_t)image.width + (size_t)x)];
            const float col[4] = {p[0] * 0.003921568627f, p[1] * 0.003921568627f, p[2] * 0.003921568627f,
                                  p[3] * 0.003921568627f};
            for (int k = 0; k < 4; k++) {
                diffuse[k] *= col[k];
            }
        }
    }
    /* VxColor(diffuse.a) * diffuse + trans * (1 - diffuse.a): VxColor(a) is (a, a, a, 1) */
    const float a = diffuse[3];
    trans[0] = a * diffuse[0] + trans[0] * (1.0f - a);
    trans[1] = a * diffuse[1] + trans[1] * (1.0f - a);
    trans[2] = a * diffuse[2] + trans[2] * (1.0f - a);
    trans[3] = diffuse[3] + trans[3] * (1.0f - a);
    return true;
}

/* LightmapGenerator::ComputeVisibility: nothing of the current light's occluders between `start`
   and `dest` (the first one met: transparent lets the light through tinted) */
static bool compute_visibility(nmo_engine_t *engine, lightmap_t *lm, eng_vector_t start, eng_vector_t dest,
                               float trans[4])
{
    trans[0] = trans[1] = trans[2] = 1.0f;
    trans[3] = 0.0f;
    const ENG_VEC(nmo_object_id_t) *occluders = (void *)&lm->filtered_occluders[lm->current_light];
    if (occluders->count == 0) {
        return true;
    }
    eng_vector_t dir = v_sub(dest, start);
    float distance = v_mag(dir);
    for (size_t i = 0; i < occluders->count; i++) {
        nmo_object_id_t id = occluders->data[i];
        int32_t index = eng_entity_index(engine, id);
        eng_vector_t min, max;
        if (index < 0 || !eng_entity_world_box(engine, index, &min, &max) || !segment_box(start, dir, min, max)) {
            continue;
        }
        eng_ray_hit_t hit;
        memset(&hit, 0, sizeof(hit));
        if (eng_entity_ray_intersection(engine, index, &start, &dest, &hit) == 0) {
            continue;
        }
        if (hit.distance > distance) {
            continue;
        }
        if (id == lm->entity && lm->builder != NULL) {
            const lm_poly_t *p = lm->builder->polys.data[lm->poly];
            if (contains_int(p->faces.data, p->faces.count, hit.face)) {
                continue;
            }
        }
        return compute_transparency(engine, id, &hit, trans);
    }
    return true;
}

/* LightmapGenerator::ComputePointColor: the filtered lights' light at the point */
static void compute_point_color(nmo_engine_t *engine, lightmap_t *lm, float lum[4], eng_vector_t position,
                                eng_vector_t normal)
{
    float trans[4];
    lm->current_light = 0;
    for (size_t i = 0; i < lm->filtered_lights.count; i++, lm->current_light++) {
        nmo_object_id_t id = lm->filtered_lights.data[i];
        const eng_light_t *light = eng_light(engine, id);
        const eng_matrix_t *world = lm_world(engine, id);
        if (light == NULL || world == NULL) {
            continue;
        }
        eng_vector_t light_position = {world->m[3][0], world->m[3][1], world->m[3][2]};
        eng_vector_t axis = v_normalized((eng_vector_t){world->m[2][0], world->m[2][1], world->m[2][2]});
        float factor = 0.0f;
        bool lit = false;
        switch (light->type) {
        case VX_LIGHTDIREC:
            factor = -v_dot(normal, axis);
            lit = factor > 0.0f && compute_visibility(engine, lm, v_add(position, v_scale(axis, -100000.0f)), position, trans);
            break;
        case VX_LIGHTPOINT: {
            eng_vector_t direction = v_sub(light_position, position);
            float att = v_mag(direction), range = light->range;
            if (att < range) {
                direction = v_scale(direction, 1.0f / att);
                att = (range - att) / range;
                att = light->attenuation[0] + light->attenuation[1] * att + light->attenuation[2] * att * att;
                factor = att * v_dot(normal, direction);
                lit = factor > 0.0f && compute_visibility(engine, lm, light_position, position, trans);
            }
            break;
        }
        case VX_LIGHTSPOT: {
            eng_vector_t direction = v_sub(light_position, position);
            float att = v_mag(direction), range = light->range;
            float costheta = cosf(light->hot_spot * 0.5f), cosphi = cosf(light->fall_off * 0.5f);
            float inv = 1.0f / (costheta - cosphi);
            if (att < range) {
                direction = v_scale(direction, 1.0f / att);
                float rho = v_dot(v_scale(axis, -1.0f), direction);
                if (rho > cosphi) {
                    float spot = 1.0f;
                    if (rho <= costheta) {
                        spot = (rho - cosphi) * inv;
                        if (light->fall_off_shape != 1.0f) {
                            spot = powf(spot, light->fall_off_shape);
                        }
                    }
                    att = (range - att) / range;
                    float attspot = light->attenuation[0] + light->attenuation[1] * att + light->attenuation[2] * att * att;
                    attspot *= spot;
                    factor = attspot * v_dot(normal, direction);
                    lit = factor > 0.0f && compute_visibility(engine, lm, light_position, position, trans);
                }
            }
            break;
        }
        default: /* VX_LIGHTPARA: no contribution */
            break;
        }
        if (lit) {
            for (int k = 0; k < 4; k++) {
                lum[k] += light->color[k] * trans[k] * factor;
            }
        }
    }
}

static void lumel_finish(const lightmap_t *lm, float lum[4], uint32_t *pixel)
{
    float s = lm->inverse_sampling * lm->inverse_sampling;
    for (int k = 0; k < 4; k++) {
        lum[k] = lum[k] * s + lm->ambient[k];
    }
    lum[3] = 1.0f;
    *pixel = vxcolor_to_color(lum);
}

/* LightmapGenerator::ComputeLightmapFlat: the polygon's world quad sampled, off the border */
static void compute_flat(nmo_engine_t *engine, lightmap_t *lm, uint32_t *map, int32_t w, int32_t h)
{
    eng_vector_t vx = v_scale(v_sub(lm->polygon_world[3], lm->polygon_world[0]), 1.0f / (float)(w - 2));
    eng_vector_t vy = v_scale(v_sub(lm->polygon_world[1], lm->polygon_world[0]), 1.0f / (float)(h - 2));
    eng_vector_t svx = v_scale(vx, lm->inverse_sampling), svy = v_scale(vy, lm->inverse_sampling);
    eng_vector_t start = v_add(v_add(v_add(lm->polygon_world[0], v_scale(lm->plane.n, 0.01f)), v_scale(svx, 0.5f)),
                               v_scale(svy, 0.5f));
    uint32_t *row = map + w; /* the first line skipped */
    for (int32_t i = 1; i < h - 1; i++, row += w, start = v_add(start, vy)) {
        uint32_t *pixel = row + 1;
        eng_vector_t v = start;
        for (int32_t j = 1; j < w - 1; j++, pixel++, v = v_add(v, vx)) {
            float lum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            eng_vector_t s = v;
            for (int32_t si = 0; si < lm->super_sampling; si++, s = v_add(s, svy)) {
                eng_vector_t sv = s;
                for (int32_t sj = 0; sj < lm->super_sampling; sj++, sv = v_add(sv, svx)) {
                    compute_point_color(engine, lm, lum, sv, lm->plane.n);
                }
            }
            lumel_finish(lm, lum, pixel);
        }
    }
}

/* LightmapGenerator::ComputeLightmapSmooth: the polygon's box sampled on its two major axes, each
   sample on the face under it with the vertex normals interpolated */
static void compute_smooth(nmo_engine_t *engine, lightmap_t *lm, uint32_t *map, int32_t w, int32_t h)
{
    const lm_poly_t *p = lm->builder->polys.data[lm->poly];
    const nmo_engine_mesh_t *shape = &lm->shape;
    const eng_matrix_t *world = &engine->entities.data[lm->entity_index].world;
    const int mx = lm->major_x, my = lm->major_y;
    eng_vector_t size = v_sub(p->max, p->min);
    float pvx = v_get(size, mx) / (float)(w - 2), pvy = v_get(size, my) / (float)(h - 2);
    float svx = pvx * lm->inverse_sampling, svy = pvy * lm->inverse_sampling;
    eng_vector_t init = p->min, start = init;
    uint32_t *row = map + 1 + w;
    for (int32_t i = 1; i < h - 1; i++, row += w) {
        *v_at(&start, mx) = v_get(init, mx);
        uint32_t *pixel = row;
        for (int32_t j = 1; j < w - 1; j++, pixel++) {
            eng_vector_t v = start;
            float lum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            eng_vector_t world_normal = lm->plane.n; /* no face found: the global normal */
            int32_t a = 0, b = 0, c = 0;
            float c0, c1, c2;
            for (size_t f = 0; f < p->faces.count; f++) {
                const uint16_t *k = &shape->indices[3 * p->faces.data[f]];
                a = k[0], b = k[1], c = k[2];
                point_coefficients(v, v_of(&shape->vertices[a].position), v_of(&shape->vertices[b].position),
                                   v_of(&shape->vertices[c].position), mx, my, &c0, &c1, &c2);
                if (c0 >= -LM_EPSILON && c1 >= -LM_EPSILON && c2 >= -LM_EPSILON) {
                    break; /* on a real face */
                }
            }
            const eng_vector_t p0 = v_of(&shape->vertices[a].position), p1 = v_of(&shape->vertices[b].position),
                               p2 = v_of(&shape->vertices[c].position);
            for (int32_t sy = 0; sy < lm->super_sampling; sy++) {
                for (int32_t sx = 0; sx < lm->super_sampling; sx++) {
                    float last[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    point_coefficients(v, p0, p1, p2, mx, my, &c0, &c1, &c2);
                    if (c0 >= -LM_EPSILON && c1 >= -LM_EPSILON && c2 >= -LM_EPSILON) {
                        eng_vector_t lv = v_add(v_add(v_scale(p0, c0), v_scale(p1, c1)), v_scale(p2, c2));
                        eng_vector_t position;
                        eng_vector_transform(&position, world, &lv);
                        eng_vector_t n = v_add(v_add(v_scale(v_of(&shape->vertices[a].normal), c0),
                                                     v_scale(v_of(&shape->vertices[b].normal), c1)),
                                               v_scale(v_of(&shape->vertices[c].normal), c2));
                        eng_vector_rotate(&world_normal, world, &n);
                        world_normal = v_normalized(world_normal);
                        position = v_add(position, v_scale(world_normal, 0.01f));
                        compute_point_color(engine, lm, last, position, world_normal);
                        *v_at(&v, mx) += svx;
                    } else {
                        compute_point_color(engine, lm, last, v, world_normal);
                    }
                    for (int k = 0; k < 4; k++) {
                        lum[k] += last[k];
                    }
                }
                *v_at(&v, mx) = v_get(start, mx);
                *v_at(&v, my) += svy;
            }
            lumel_finish(lm, lum, pixel);
            *v_at(&start, mx) += pvx;
        }
        *v_at(&start, my) += pvy;
    }
}

/* MSVC's qsort (msvcrt), for XArray::Sort's order of equal textures */
typedef int (*lm_cmp_fn)(const lm_map_t *, const lm_map_t *, nmo_engine_t *);

static void lm_swap(lm_map_t **a, lm_map_t **b)
{
    lm_map_t *t = *a;
    *a = *b;
    *b = t;
}

static void lm_shortsort(lm_map_t **lo, lm_map_t **hi, lm_cmp_fn cmp, nmo_engine_t *e)
{
    while (hi > lo) {
        lm_map_t **max = lo;
        for (lm_map_t **p = lo + 1; p <= hi; p++) {
            if (cmp(*p, *max, e) > 0) {
                max = p;
            }
        }
        lm_swap(max, hi);
        hi--;
    }
}

static void lm_qsort(lm_map_t **base, size_t num, lm_cmp_fn cmp, nmo_engine_t *e)
{
    lm_map_t **lostk[64], **histk[64];
    int stkptr = 0;
    if (num < 2) {
        return;
    }
    lm_map_t **lo = base, **hi = base + (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) + 1;
    if (size <= 8) {
        lm_shortsort(lo, hi, cmp, e);
    } else {
        lm_map_t **mid = lo + size / 2;
        if (cmp(*lo, *mid, e) > 0) lm_swap(lo, mid);
        if (cmp(*lo, *hi, e) > 0) lm_swap(lo, hi);
        if (cmp(*mid, *hi, e) > 0) lm_swap(mid, hi);
        lm_map_t **loguy = lo, **higuy = hi;
        for (;;) {
            if (mid > loguy) {
                do {
                    loguy++;
                } while (loguy < mid && cmp(*loguy, *mid, e) <= 0);
            }
            if (mid <= loguy) {
                do {
                    loguy++;
                } while (loguy <= hi && cmp(*loguy, *mid, e) <= 0);
            }
            do {
                higuy--;
            } while (higuy > mid && cmp(*higuy, *mid, e) > 0);
            if (higuy < loguy) {
                break;
            }
            lm_swap(loguy, higuy);
            if (mid == higuy) {
                mid = loguy;
            }
        }
        higuy++;
        if (mid < higuy) {
            do {
                higuy--;
            } while (higuy > mid && cmp(*higuy, *mid, e) == 0);
        }
        if (mid >= higuy) {
            do {
                higuy--;
            } while (higuy > lo && cmp(*higuy, *mid, e) == 0);
        }
        if (higuy - lo >= hi - loguy) {
            if (lo < higuy) {
                lostk[stkptr] = lo, histk[stkptr] = higuy, stkptr++;
            }
            if (loguy < hi) {
                lo = loguy;
                goto recurse;
            }
        } else {
            if (loguy < hi) {
                lostk[stkptr] = loguy, histk[stkptr] = hi, stkptr++;
            }
            if (lo < higuy) {
                hi = higuy;
                goto recurse;
            }
        }
    }
    if (--stkptr >= 0) {
        lo = lostk[stkptr], hi = histk[stkptr];
        goto recurse;
    }
}

static void texture_size(nmo_engine_t *engine, nmo_object_id_t id, int32_t *w, int32_t *h)
{
    eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    *w = *h = 0;
    if (texture != NULL) {
        eng_bitmap_size(engine, texture, w, h);
    }
}

/* LightmapGenerator::TextureSort: the larger ones first */
static int texture_sort(const lm_map_t *a, const lm_map_t *b, nmo_engine_t *e)
{
    int32_t w1, h1, w2, h2;
    texture_size(e, a->texture, &w1, &h1);
    texture_size(e, b->texture, &w2, &h2);
    return ((w2 << 8) + h2) - ((w1 << 8) + h1);
}

static char *lm_name(const char *format, const char *name, int32_t number)
{
    int length = snprintf(NULL, 0, format, name != NULL ? name : "(null)", number);
    char *text = length >= 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (text != NULL) {
        snprintf(text, (size_t)length + 1u, format, name != NULL ? name : "(null)", number);
    }
    return text;
}

/* LightmapGenerator::PackTextures: the object's textures copied into one, from the larger ones,
   each where the last left room (to its right, under it); the faces' UVs moved there */
static void pack_textures(nmo_engine_t *engine, lm_object_t *object)
{
    if (object->maps.count <= 1) {
        return; /* the one texture is in the level already */
    }
    int32_t total = 0, max_width = 0;
    for (size_t i = 0; i < object->maps.count; i++) {
        int32_t w, h;
        texture_size(engine, object->maps.data[i]->texture, &w, &h);
        total += w * h;
        max_width = w > max_width ? w : max_width;
    }
    int32_t approximate = lm_power2((int32_t)(int64_t)sqrt((double)total));
    if (approximate * approximate < total) {
        approximate *= 2;
    }
    int32_t fw = approximate > max_width ? approximate : max_width;
    char *name = lm_name("LightMap %s", nmo_engine_object_name(engine, object->object), 0);
    nmo_object_id_t final_texture = name != NULL ? eng_create_object(engine, NMO_CID_TEXTURE, name) : 0;
    free(name);
    lm_map_t *final_map = (lm_map_t *)calloc(1, sizeof(lm_map_t));
    uint32_t *dest = (uint32_t *)calloc((size_t)fw * (size_t)fw, sizeof(uint32_t));
    if (final_texture == 0 || final_map == NULL || dest == NULL || !lm_texture_create(engine, final_texture, fw, fw)) {
        free(final_map), free(dest);
        return;
    }
    final_map->texture = final_texture;
    lm_qsort(object->maps.data, object->maps.count, texture_sort, engine);
    ENG_VEC(float) free_positions = {0}; /* x, y pairs */
    float *fp_slot = NULL;
    if (ENG_VEC_PUSH(free_positions, fp_slot)) {
        *fp_slot = 0.0f;
    }
    if (ENG_VEC_PUSH(free_positions, fp_slot)) {
        *fp_slot = 0.0f;
    }
    int32_t last_x = 0, last_y = 0;
    size_t it = 0;
    while (it < object->maps.count) {
        lm_map_t *map = object->maps.data[it];
        int32_t w = 0, h = 0;
        uint32_t *src = lm_lock(engine, map->texture, &w, &h);
        if (free_positions.count < 2) {
            free(src);
            break;
        }
        float fx = free_positions.data[free_positions.count - 2], fy = free_positions.data[free_positions.count - 1];
        free_positions.count -= 2;
        if (fx + (float)w > (float)fw || fy + (float)h > (float)fw) {
            free(src);
            continue; /* the next free position for the same texture */
        }
        last_x = (int32_t)fx + w > last_x ? (int32_t)fx + w : last_x;
        last_y = (int32_t)fy + h > last_y ? (int32_t)fy + h : last_y;
        for (int32_t i = 0; src != NULL && i < h; i++) {
            memcpy(dest + (size_t)fw * (size_t)((int32_t)fy + i) + (int32_t)fx, src + (size_t)w * (size_t)i,
                   (size_t)w * sizeof(uint32_t));
        }
        free(src);
        size_t offset = final_map->vertices.count;
        for (size_t i = 0; i < map->indices.count; i++) {
            map->indices.data[i] = (uint16_t)(map->indices.data[i] + offset);
            uint16_t *slot = NULL;
            if (ENG_VEC_PUSH(final_map->indices, slot)) {
                *slot = map->indices.data[i];
            }
        }
        for (size_t i = 0; i < map->original.count; i++) {
            uint16_t *slot = NULL;
            if (ENG_VEC_PUSH(final_map->original, slot)) {
                *slot = map->original.data[i];
            }
        }
        float su = fx / (float)fw, sv = fy / (float)fw, zu = (float)w / (float)fw, zv = (float)h / (float)fw;
        for (size_t i = 0; i < map->vertices.count; i++) {
            lm_vertex_t *v = &map->vertices.data[i];
            v->u = su + v->u * zu;
            v->v = sv + v->v * zv;
            lm_vertex_t *slot = NULL;
            if (ENG_VEC_PUSH(final_map->vertices, slot)) {
                *slot = *v;
            }
        }
        float nx = (float)((int32_t)fx + w), ny = fy;
        int32_t max_x = fw;
        for (size_t i = 0; i + 1 < free_positions.count; i += 2) {
            max_x = (int32_t)free_positions.data[i] < max_x ? (int32_t)free_positions.data[i] : max_x;
        }
        if (nx < (float)fw && nx < (float)max_x) {
            if (ENG_VEC_PUSH(free_positions, fp_slot)) *fp_slot = nx;
            if (ENG_VEC_PUSH(free_positions, fp_slot)) *fp_slot = ny;
        }
        nx = (float)(int32_t)fx;
        ny = (float)((int32_t)fy + h);
        if (ny < (float)fw) {
            if (ENG_VEC_PUSH(free_positions, fp_slot)) *fp_slot = nx;
            if (ENG_VEC_PUSH(free_positions, fp_slot)) *fp_slot = ny;
        }
        it++;
    }
    free(free_positions.data);
    int32_t new_width = lm_power2(last_x), width = fw;
    if (new_width < fw) {
        uint32_t *data = (uint32_t *)malloc((size_t)new_width * (size_t)fw * sizeof(uint32_t));
        for (int32_t i = 0; data != NULL && i < fw; i++) {
            memcpy(data + (size_t)new_width * i, dest + (size_t)fw * i, (size_t)new_width * sizeof(uint32_t));
        }
        if (data != NULL) {
            free(dest);
            dest = data;
            width = new_width;
            float factor = (float)fw / (float)new_width;
            for (size_t i = 0; i < final_map->vertices.count; i++) {
                final_map->vertices.data[i].u *= factor;
            }
        }
    }
    int32_t new_height = lm_power2(last_y), height = fw;
    if (new_height < fw) {
        height = new_height;
        float factor = (float)fw / (float)new_height;
        for (size_t i = 0; i < final_map->vertices.count; i++) {
            final_map->vertices.data[i].v *= factor;
        }
    }
    if (lm_texture_create(engine, final_texture, width, height)) {
        int32_t w = 0, h = 0;
        uint32_t *img = lm_lock(engine, final_texture, &w, &h);
        if (img != NULL && w == width && h == height) {
            memcpy(img, dest, (size_t)width * (size_t)height * sizeof(uint32_t));
        }
        lm_unlock(engine, final_texture, img);
    }
    free(dest);
    /* the packed ones destroyed; the ones left over kept apart */
    ENG_VEC(lm_map_t *) maps = {0};
    lm_map_t **slot = NULL;
    if (ENG_VEC_PUSH(maps, slot)) {
        *slot = final_map;
    } else {
        lm_free_map(final_map);
    }
    for (size_t i = 0; i < object->maps.count; i++) {
        lm_map_t *map = object->maps.data[i];
        if (i < it) {
            lm_destroy_object(engine, map->texture);
            lm_free_map(map);
        } else if (ENG_VEC_PUSH(maps, slot)) {
            *slot = map;
        } else {
            lm_free_map(map);
        }
    }
    free(object->maps.data);
    object->maps.data = maps.data, object->maps.count = maps.count, object->maps.capacity = maps.capacity;
}

/* The face normals CKMesh builds (BuildFaceNormals) */
static eng_vector_t *face_normals_of(const nmo_engine_mesh_t *shape)
{
    eng_vector_t *normals = (eng_vector_t *)calloc((size_t)shape->face_count + 1u, sizeof(eng_vector_t));
    for (int32_t f = 0; normals != NULL && f < shape->face_count; f++) {
        const uint16_t *k = &shape->indices[3 * f];
        eng_vector_t a = v_of(&shape->vertices[k[0]].position);
        normals[f] = v_normalized(v_cross(v_sub(v_of(&shape->vertices[k[1]].position), a),
                                          v_sub(v_of(&shape->vertices[k[2]].position), a)));
    }
    return normals;
}

static bool mesh_valid(const nmo_engine_mesh_t *shape)
{
    for (int32_t i = 0; i < shape->face_count * 3; i++) {
        if (shape->indices[i] >= shape->vertex_count) {
            return false;
        }
    }
    return shape->vertices != NULL && shape->indices != NULL;
}

/* The lights of the polygon (all of them for a smooth one), and each light's occluders: in front
   of the polygon, inside the pyramid from the light through the polygon's quad */
static void filter_world(nmo_engine_t *engine, lightmap_t *lm, bool smooth, eng_vector_t boxdir)
{
    lm->filtered_lights.count = 0;
    for (size_t l = 0; l < lm->lights.count; l++) {
        nmo_object_id_t id = lm->lights.data[l];
        const eng_light_t *light = eng_light(engine, id);
        const eng_matrix_t *world = lm_world(engine, id);
        if (light == NULL || world == NULL) {
            continue;
        }
        if (!smooth) { /* only when somewhat planar */
            eng_vector_t pos = {world->m[3][0], world->m[3][1], world->m[3][2]};
            float dp = plane_classify(&lm->plane, pos);
            if (dp < 0.0f) {
                continue;
            }
            if (light->type != VX_LIGHTDIREC) {
                float range = light->range;
                if (range < dp) {
                    continue;
                }
                range *= range;
                bool in_range = false;
                for (int e = 0; e < 4 && !in_range; e++) {
                    in_range = edge_square_distance(lm->polygon_world[e], lm->polygon_world[(e + 1) & 3], pos) < range;
                }
                if (!in_range) {
                    continue;
                }
            }
        }
        if (lm->filtered_lights.count < LM_MAX_LIGHT) {
            push_id(&lm->filtered_lights, id);
        }
    }
    bool swing = v_dot(lm->plane.n, boxdir) > 0.0f; /* the planes the wrong way */
    for (size_t l = 0; l < lm->filtered_lights.count; l++) {
        const eng_matrix_t *world = lm_world(engine, lm->filtered_lights.data[l]);
        eng_vector_t pos = {world->m[3][0], world->m[3][1], world->m[3][2]};
        vx_plane_t sides[4];
        for (int e = 0; e < 4; e++) {
            sides[e] = plane_from_triangle(lm->polygon_world[e], lm->polygon_world[(e + 1) & 3], pos);
            if (swing) {
                sides[e].n = v_scale(sides[e].n, -1.0f);
                sides[e].d = -sides[e].d;
            }
        }
        lm->filtered_occluders[l].count = 0;
        for (size_t o = 0; o < lm->occluders.count; o++) {
            nmo_object_id_t id = lm->occluders.data[o];
            int32_t index = eng_entity_index(engine, id);
            eng_vector_t min, max;
            if (id == 0 || index < 0 || lm_entity_mesh(engine, id) == 0 || !eng_entity_local_box(engine, index, &min, &max)) {
                continue;
            }
            const eng_matrix_t *m = &engine->entities.data[index].world;
            if (plane_classify_box(&lm->plane, min, max, m) < 0.0f) {
                continue;
            }
            bool outside = false;
            for (int e = 0; e < 4 && !outside; e++) {
                outside = plane_classify_box(&sides[e], min, max, m) > 0.0f;
            }
            if (!outside) {
                push_id(&lm->filtered_occluders[l], id);
            }
        }
    }
}

static lm_map_t *object_add_map(lm_object_t *object)
{
    lm_map_t *map = (lm_map_t *)calloc(1, sizeof(lm_map_t));
    lm_map_t **slot = NULL;
    if (map == NULL || !ENG_VEC_PUSH(object->maps, slot)) {
        free(map);
        return NULL;
    }
    *slot = map;
    return map;
}

/* The lightmap of polygon `pi` of the current entity */
static void generate_polygon(nmo_engine_t *engine, lightmap_t *lm, lm_object_t *object, int32_t pi, float density,
                             int32_t blur, eng_vector_t scale)
{
    lm_poly_t *p = lm->builder->polys.data[pi];
    lm->poly = pi;
    const nmo_engine_mesh_t *shape = &lm->shape;
    eng_vector_t box_size = v_sub(p->max, p->min);
    eng_vector_t size = {2.0f * scale.x * box_size.x, 2.0f * scale.y * box_size.y, 2.0f * scale.z * box_size.z};
    float min = size.x;
    int x = 1, y = 2;
    if (min > size.y) {
        min = size.y;
        x = 0, y = 2;
    }
    if (min > size.z) {
        x = 0, y = 1;
    }
    if (v_get(size, y) > v_get(size, x)) {
        int t = x;
        x = y, y = t;
    }
    lm->major_x = x, lm->major_y = y;
    int32_t tw = lm_power2((int32_t)(int64_t)(v_get(size, x) * density));
    tw = tw < 4 ? 4 : tw;
    int32_t th = lm_power2((int32_t)(int64_t)(v_get(size, y) * density));
    th = th < 4 ? 4 : th;
    float inv_w = 1.0f / (float)tw, inv_h = 1.0f / (float)th;
    char *name = lm_name("%s - face %d", nmo_engine_object_name(engine, lm->entity), pi);
    nmo_object_id_t texture = name != NULL ? eng_create_object(engine, NMO_CID_TEXTURE, name) : 0;
    free(name);
    lm_map_t *map = object_add_map(object);
    if (map == NULL) {
        return;
    }
    map->texture = texture;
    for (size_t j = 0; j < p->vertices.count; j++) {
        lm_vertex_t *v = NULL;
        if (!ENG_VEC_PUSH(map->vertices, v)) {
            return;
        }
        int32_t index = p->vertices.data[j];
        const nmo_vector_t *pos = &shape->vertices[index].position;
        *v = (lm_vertex_t){pos->x, pos->y, pos->z, 0xFFFFFFFFu, 0u, 0.0f, 0.0f, (uint32_t)index};
        eng_vector_t at = v_of(pos);
        v->u = (v_get(at, x) - v_get(p->min, x)) / v_get(box_size, x);
        v->v = (v_get(at, y) - v_get(p->min, y)) / v_get(box_size, y);
        v->u = inv_w + (1.0f - 2.0f * inv_w) * v->u;
        v->v = inv_h + (1.0f - 2.0f * inv_h) * v->v;
    }
    for (size_t j = 0; j < p->faces.count; j++) {
        const uint16_t *k = &shape->indices[3 * p->faces.data[j]];
        uint16_t *slot = NULL;
        if (ENG_VEC_PUSH(map->original, slot)) {
            *slot = (uint16_t)p->faces.data[j];
        }
        for (int c = 0; c < 3; c++) {
            uint16_t position = 0xFFFFu; /* XArray::GetPosition: -1 when missing */
            for (size_t v = 0; v < p->vertices.count; v++) {
                if (p->vertices.data[v] == k[c]) {
                    position = (uint16_t)v;
                    break;
                }
            }
            if (ENG_VEC_PUSH(map->indices, slot)) {
                *slot = position;
            }
        }
    }
    const eng_matrix_t *world = &engine->entities.data[lm->entity_index].world;
    eng_vector_t pts[3] = {{0}};
    for (int c = 0; c < 3 && (size_t)c < map->indices.count; c++) {
        uint16_t k = map->indices.data[c];
        if (k < map->vertices.count) {
            eng_vector_t local = {map->vertices.data[k].x, map->vertices.data[k].y, map->vertices.data[k].z};
            eng_vector_transform(&pts[c], world, &local);
        }
    }
    lm->plane = plane_from_triangle(pts[0], pts[1], pts[2]);
    for (int c = 0; c < 4; c++) {
        lm->box_world[c] = p->max;
    }
    *v_at(&lm->box_world[0], x) = v_get(p->min, x);
    *v_at(&lm->box_world[0], y) = v_get(p->min, y);
    *v_at(&lm->box_world[1], x) = v_get(p->min, x);
    *v_at(&lm->box_world[3], y) = v_get(p->min, y);
    for (int c = 0; c < 4; c++) {
        eng_vector_t local = lm->box_world[c];
        eng_vector_transform(&lm->box_world[c], world, &local);
    }
    eng_vector_t boxdir = v_normalized(v_cross(v_sub(lm->box_world[3], lm->box_world[0]),
                                               v_sub(lm->box_world[1], lm->box_world[0])));
    for (int c = 0; c < 4; c++) {
        line_plane(lm->box_world[c], boxdir, &lm->plane, &lm->polygon_world[c]);
    }
    filter_world(engine, lm, p->smooth, boxdir);
    if (lm->filtered_lights.count > 0) { /* the lightmap is computed */
        if (!lm_texture_create(engine, texture, tw, th)) {
            return;
        }
        int32_t w = 0, h = 0;
        uint32_t *img = lm_lock(engine, texture, &w, &h);
        if (img == NULL) {
            return;
        }
        const float purple[4] = {1.0f, 0.0f, 1.0f, 0.5f};
        uint32_t fill = rgbaf_to_color(purple);
        for (int32_t i = 0; i < w * h; i++) {
            img[i] = fill;
        }
        if (p->smooth) {
            compute_smooth(engine, lm, img, w, h);
        } else {
            compute_flat(engine, lm, img, w, h);
        }
        lm_unlock(engine, texture, img);
        if (blur == 0) {
            extend_border(engine, texture);
        }
        for (int32_t i = 0; i < blur; i++) {
            extend_border(engine, texture);
            gaussian_blur(engine, texture);
        }
    } else { /* fully the ambient color */
        if (!lm_texture_create(engine, texture, 2, 2)) {
            return;
        }
        for (size_t j = 0; j < map->vertices.count; j++) {
            map->vertices.data[j].u = map->vertices.data[j].v = 0.5f;
        }
        int32_t w = 0, h = 0;
        uint32_t *img = lm_lock(engine, texture, &w, &h);
        uint32_t color = vxcolor_to_color(lm->ambient);
        for (int32_t i = 0; img != NULL && i < w * h && i < 4; i++) {
            img[i] = color;
        }
        lm_unlock(engine, texture, img);
    }
}

/* LightmapGenerator::Generate: the receivers' lightmaps, `density` texels a unit, sampled
   `super_sampling` squared times a texel, blurred `blur` times; faces within `threshold`
   (a cosine) one polygon */
static void lm_generate(nmo_engine_t *engine, lightmap_t *lm, float density, float threshold, int32_t super_sampling,
                        int32_t blur)
{
    lm_clean(engine, lm, true);
    lm->super_sampling = super_sampling;
    lm->inverse_sampling = 1.0f / (float)super_sampling;
    color_of(engine->render.ambient_light, lm->ambient);
    lm->ambient[3] = 1.0f;
    for (size_t r = 0; r < lm->receivers.count; r++) {
        nmo_object_id_t id = lm->receivers.data[r];
        int32_t index = id != 0 ? eng_entity_index(engine, id) : -1;
        nmo_object_id_t mesh = index >= 0 ? lm_entity_mesh(engine, id) : 0;
        if (mesh == 0 || !nmo_engine_mesh(engine, mesh, &lm->shape) || lm->shape.face_count == 0 ||
            !mesh_valid(&lm->shape)) {
            continue;
        }
        lm->entity = id, lm->entity_index = index, lm->mesh = mesh;
        eng_vector_t scale = eng_entity_scale(engine, index, false);
        lm_object_t *object = (lm_object_t *)calloc(1, sizeof(lm_object_t));
        lm_object_t **slot = NULL;
        if (object == NULL || !ENG_VEC_PUSH(lm->objects, slot)) {
            free(object);
            continue;
        }
        *slot = object;
        object->object = id;
        eng_vector_t *normals = face_normals_of(&lm->shape);
        lm->builder = normals != NULL ? builder_create(&lm->shape, normals) : NULL;
        if (lm->builder == NULL || !builder_simplify(lm->builder, threshold)) {
            char *line = lm_name("Lightmap does not support object : %s", nmo_engine_object_name(engine, id), 0);
            if (line != NULL) {
                eng_console(engine, line, false);
            }
            free(line);
            if (lm->builder != NULL) {
                builder_free(lm->builder);
            }
            lm->builder = NULL;
            free(normals);
            continue;
        }
        for (int32_t p = 0; p < (int32_t)lm->builder->polys.count; p++) {
            if (lm->builder->polys.data[p]->magic == 0) { /* not merged into another since */
                generate_polygon(engine, lm, object, p, density, blur, scale);
            }
        }
        pack_textures(engine, object);
        builder_free(lm->builder);
        lm->builder = NULL;
        free(normals);
    }
}

/* ============================================================================
 * Save chunk
 * ============================================================================ */

/* LightmapGenerator::Load: the lightmaps the block saved in its state chunk local */
static void lm_load(nmo_engine_t *engine, lightmap_t *lm, nmo_object_id_t chunk_param)
{
    lm_clean(engine, lm, false); /* a new generator: the old one's textures left alone */
    nmo_object_t *param = chunk_param != 0 ? eng_find_object(engine, chunk_param) : NULL;
    const nmo_parameter_state_t *value =
        param != NULL ? (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                            engine->types, param, CKPGUID_PARAMETER)
                      : NULL;
    if (value == NULL || value->mode != CKPARAM_MODE_SUBCHUNK || value->subchunk == NULL) {
        return;
    }
    const eng_module_t *module = eng_module_of(engine, chunk_param, NULL);
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
    nmo_chunk_t *chunk = arena != NULL ? nmo_chunk_clone(value->subchunk, arena) : NULL;
    int32_t count = 0;
    if (chunk == NULL || module == NULL || nmo_chunk_start_read(chunk) != NMO_OK ||
        nmo_chunk_seek_identifier(chunk, LIGHTMAPSV1_IDENTIFIER) != NMO_OK || nmo_chunk_read_int(chunk, &count) != NMO_OK) {
        nmo_arena_destroy(arena);
        return;
    }
    for (int32_t o = 0; o < count; o++) {
        lm_object_t *object = (lm_object_t *)calloc(1, sizeof(lm_object_t));
        lm_object_t **slot = NULL;
        if (object == NULL || !ENG_VEC_PUSH(lm->objects, slot)) {
            free(object);
            break;
        }
        *slot = object;
        nmo_object_id_t local = 0;
        int32_t maps = 0;
        nmo_chunk_read_object_id(chunk, &local);
        object->object = local != 0 ? eng_module_id(module, local) : 0;
        nmo_chunk_read_int(chunk, &maps);
        for (int32_t t = 0; t < maps; t++) {
            lm_map_t *map = object_add_map(object);
            int32_t n = 0;
            if (map == NULL) {
                break;
            }
            nmo_chunk_read_object_id(chunk, &local);
            map->texture = local != 0 ? eng_module_id(module, local) : 0;
            nmo_chunk_read_int(chunk, &n);
            if (n > 0 && eng_vec_reserve((void **)&map->indices.data, &map->indices.capacity, (size_t)n, sizeof(uint16_t))) {
                map->indices.count = (size_t)n;
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, map->indices.data, (size_t)n * sizeof(uint16_t));
            }
            n = 0;
            nmo_chunk_read_int(chunk, &n);
            if (n > 0 && eng_vec_reserve((void **)&map->vertices.data, &map->vertices.capacity, (size_t)n, sizeof(lm_vertex_t))) {
                map->vertices.count = (size_t)n;
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, map->vertices.data, (size_t)n * sizeof(lm_vertex_t));
            }
        }
    }
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * The render callback
 * ============================================================================ */

static void lm_push_vertex(nmo_engine_t *engine, const eng_matrix_t *world, const lm_vertex_t *v, uint32_t color)
{
    eng_vector_t local = {v->x, v->y, v->z}, at;
    eng_vector_transform(&at, world, &local);
    nmo_engine_vertex_t *out = NULL;
    if (ENG_VEC_PUSH(engine->render.vertices, out)) {
        *out = (nmo_engine_vertex_t){at.x, at.y, at.z, v->u, v->v, color, 0.0f, 0.0f, 0.0f};
    }
}

/* The map's triangles as a primitive of the entity's frame */
static nmo_engine_draw_t *lm_primitive(nmo_engine_t *engine, nmo_object_id_t entity, const eng_matrix_t *world,
                                       const lm_map_t *map, bool transparent)
{
    int32_t first = (int32_t)engine->render.vertices.count;
    for (size_t i = 0; i < map->indices.count; i++) {
        uint16_t k = map->indices.data[i];
        if (k < map->vertices.count) {
            lm_push_vertex(engine, world, &map->vertices.data[k], map->vertices.data[k].diffuse);
        }
    }
    int32_t count = (int32_t)engine->render.vertices.count - first;
    count -= count % 3;
    nmo_engine_draw_t *draw = count > 0 ? eng_render_push(engine) : NULL;
    if (draw != NULL) {
        draw->kind = NMO_ENGINE_DRAW_PRIMITIVE;
        draw->object = entity;
        draw->first_vertex = first;
        draw->vertex_count = count;
        draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
        draw->no_z_write = true;
        if (transparent) {
            draw->transparent = true;
            draw->has_sort_position = true;
            draw->sort_position[0] = world->m[3][0];
            draw->sort_position[1] = world->m[3][1];
            draw->sort_position[2] = world->m[3][2];
        }
    }
    return draw;
}

/*
 * LightmapGenerator::LightMapsRender, the entity's post render callback: its
 * lightmaps multiplied over it (ZERO, SRCCOLOR; the texture modulated by the
 * Opacity color, clamped and linear; no z write; the depth translation x
 * 1.0001 against z fighting). With Show Polygons each lightmap's faces in a
 * color of their own instead (srand(1) each time), opaque. The first object
 * entry of the entity only; an entry whose object is gone is dropped.
 */
static void lm_render(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    lightmap_t *lm = lm_of(engine, node, false);
    const eng_matrix_t *world = lm_world(engine, entity);
    if (lm == NULL || world == NULL) {
        return;
    }
    uint32_t seed = 1u;
    for (size_t o = 0; o < lm->objects.count;) {
        lm_object_t *object = lm->objects.data[o];
        if (object->object == 0 || eng_find_object(engine, object->object) == NULL) {
            for (size_t t = 0; t < object->maps.count; t++) {
                lm_destroy_object(engine, object->maps.data[t]->texture);
                lm_free_map(object->maps.data[t]);
            }
            free(object->maps.data);
            free(object);
            memmove(&lm->objects.data[o], &lm->objects.data[o + 1], (lm->objects.count - o - 1) * sizeof(lm_object_t *));
            lm->objects.count--;
            continue;
        }
        if (object->object != entity) {
            o++;
            continue;
        }
        for (size_t t = 0; t < object->maps.count; t++) {
            lm_map_t *map = object->maps.data[t];
            if (map->vertices.count == 0) {
                continue;
            }
            if (lm->show_polygons) {
                /* the arguments are evaluated right to left: blue first */
                float b = (float)lm_rand(&seed) * 0.000030518509f;
                float g = (float)lm_rand(&seed) * 0.000030518509f;
                float r = (float)lm_rand(&seed) * 0.000030518509f;
                const float c[4] = {r, g, b, 1.0f};
                uint32_t unique = rgbaf_to_color(c);
                for (size_t i = 0; i < map->vertices.count; i++) {
                    map->vertices.data[i].diffuse = unique;
                }
                lm_primitive(engine, entity, world, map, transparent); /* ONE, ZERO */
                for (size_t i = 0; i < map->vertices.count; i++) {
                    map->vertices.data[i].diffuse = 0xFFFFFFFFu;
                }
                continue;
            }
            if (map->vertices.data[0].diffuse != lm->opacity) {
                for (size_t i = 0; i < map->vertices.count; i++) {
                    map->vertices.data[i].diffuse = lm->opacity;
                }
            }
            nmo_engine_draw_t *draw = lm_primitive(engine, entity, world, map, transparent);
            if (draw != NULL) {
                draw->texture = map->texture;
                draw->blend = true;
                draw->source_blend = VXBLEND_ZERO;
                draw->dest_blend = VXBLEND_SRCCOLOR;
                draw->projection_z_scale = 1.0001f;
            }
        }
        break; /* one entity */
    }
}

/* AttachRenderCallbacks: each lightmapped object's temporary post render callback */
static void lm_attach(nmo_engine_t *engine, lightmap_t *lm)
{
    for (size_t o = 0; o < lm->objects.count; o++) {
        nmo_object_id_t id = lm->objects.data[o]->object;
        if (id != 0 && eng_entity_index(engine, id) >= 0) {
            eng_entity_callback_t callback = {id, ENG_ENTITY_POST_RENDER, lm_render, lm->node, true};
            eng_add_entity_callback(engine, &callback);
        }
    }
}

/* ============================================================================
 * The blocks
 * ============================================================================ */

/* LightMapsCreation's parameters, then Generate */
static void lm_creation(blk_t *b, lightmap_t *lm, int32_t time_setting)
{
    nmo_engine_t *engine = b->engine;
    float units = get_in_float(b, 3, 4.0f);
    int32_t supersample = get_in_int(b, 4, 1);
    supersample = supersample < 1 ? 1 : supersample;
    int32_t blur = get_in_int(b, 5, 0);
    float threshold = cosf(get_in_float(b, 6, 0.5f));
    lm_set_objects(engine, &lm->receivers, nmo_engine_block_get_input_object(b, 0));
    lm_set_objects(engine, &lm->occluders, nmo_engine_block_get_input_object(b, 1));
    lm_set_lights(engine, lm, nmo_engine_block_get_input_object(b, 2));
    lm_generate(engine, lm, units, threshold, supersample, blur);
    if (get_local_int(b, time_setting, 1) != 0) {
        /* TimeManager::GetTime is the frame's: the same before and after */
        eng_console(engine, "Time : 0.000000", false);
    }
}

/*
 * Lightmap: Generate computes the lightmaps of the receiving objects (all the
 * 3D objects without a group) lit by the lights with the occluders' shadows;
 * while On (and after Generate) each frame they are drawn over the objects.
 * Off stops.
 */
static int bb_lightmap(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    lightmap_t *lm = lm_of(engine, b->node, true);
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        if (lm != NULL) {
            lm_creation(b, lm, 4);
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    if (lm == NULL) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    lm->show_polygons = get_local_int(b, 3, 0) != 0;
    float opacity[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 7, opacity, sizeof(opacity));
    lm->opacity = vxcolor_to_color(opacity);
    lm_attach(engine, lm);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* CKM_BEHAVIORLOAD: a new generator with the lightmaps of the state chunk */
static int cb_lightmap(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        lightmap_t *lm = lm_of(b->engine, b->node, true);
        eng_param_t *chunk = block_local(b, 1);
        if (lm != NULL && chunk != NULL) {
            lm_load(b->engine, lm, chunk->id);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* LightmapGenerator::GenerateObjects: a prelit-or-not copy of each lightmapped object ("LM name"),
   its faces in their materials with the lightmap a channel (ZERO, SRCCOLOR, unlit) */
static void lm_generate_objects(nmo_engine_t *engine, lightmap_t *lm)
{
    for (size_t o = 0; o < lm->objects.count; o++) {
        lm_object_t *object = lm->objects.data[o];
        nmo_object_id_t id = object->object;
        int32_t index = eng_entity_index(engine, id);
        nmo_object_id_t mesh = index >= 0 ? lm_entity_mesh(engine, id) : 0;
        nmo_engine_mesh_t shape;
        if (mesh == 0 || object->maps.count == 0 || !nmo_engine_mesh(engine, mesh, &shape)) {
            continue;
        }
        const char *name = nmo_engine_object_name(engine, id);
        char *entity_name = lm_name("LM %s", name, 0), *mesh_name = lm_name("LM %s Mesh", name, 0);
        char *material_name = lm_name("LM %s Material", name, 0);
        nmo_object_id_t nent = entity_name != NULL ? eng_create_object(engine, NMO_CID_3DOBJECT, entity_name) : 0;
        int32_t vcount = 0, icount = 0;
        for (size_t t = 0; t < object->maps.count; t++) {
            vcount += (int32_t)object->maps.data[t]->vertices.count;
            icount += (int32_t)object->maps.data[t]->indices.count;
        }
        nmo_mesh_state_t *made = NULL;
        nmo_object_id_t nmesh = mesh_name != NULL ? eng_create_mesh(engine, mesh_name, vcount, icount / 3, &made) : 0;
        nmo_object_id_t nmat = material_name != NULL ? eng_create_object(engine, NMO_CID_MATERIAL, material_name) : 0;
        free(entity_name), free(mesh_name), free(material_name);
        int32_t nindex = nent != 0 ? eng_entity_index(engine, nent) : -1;
        if (nindex < 0 || nmesh == 0 || made == NULL || nmat == 0) {
            continue;
        }
        eng_entity_set_current_mesh(engine, nent, nmesh);
        eng_entity_add_mesh(engine, nent, nmesh);
        nmo_engine_set_entity_world_matrix(engine, nent, engine->entities.data[index].world.m[0], false);
        int32_t parent = engine->entities.data[index].parent;
        if (parent >= 0) {
            eng_entity_set_parent(engine, nindex, parent, true);
        }
        eng_material_t *material = eng_material(engine, nmat);
        if (material != NULL) {
            material->desc.texture = object->maps.data[0]->texture;
        }
        bool prelit = shape.prelit;
        nmo_object_t *source = eng_find_object(engine, mesh);
        const nmo_mesh_state_t *state = source != NULL
            ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, source, CKPGUID_MESH)
            : NULL;
        int32_t v = 0, f = 0, offset = 0;
        for (size_t t = 0; t < object->maps.count; t++) {
            const lm_map_t *map = object->maps.data[t];
            for (size_t i = 0; i < map->vertices.count; i++, v++) {
                uint32_t k = map->vertices.data[i].index;
                if (k >= (uint32_t)shape.vertex_count) {
                    continue;
                }
                made->vertices[v].position = shape.vertices[k].position;
                if (prelit) {
                    if (made->vertex_colors != NULL) {
                        made->vertex_colors[v] = shape.colors != NULL ? shape.colors[k] : 0xFFFFFFFFu;
                    }
                    if (made->vertex_specular != NULL) {
                        made->vertex_specular[v] = state != NULL && state->vertex_specular != NULL ? state->vertex_specular[k] : 0u;
                    }
                } else {
                    made->vertices[v].normal = shape.vertices[k].normal;
                }
                made->vertices[v].uv = shape.vertices[k].uv;
            }
            for (size_t i = 0; i + 2 < map->indices.count; i += 3, f++) {
                for (int c = 0; c < 3; c++) {
                    made->face_vertex_indices[3 * f + c] = (uint16_t)(offset + map->indices.data[i + c]);
                }
            }
            offset += (int32_t)map->vertices.count;
        }
        nmo_mesh_update_bounding_volumes(made);
        /* the geometry written, then what makes the mesh's run time copy */
        if (prelit) {
            eng_mesh_set_lit_mode(engine, nmesh, 0);
        }
        f = 0;
        for (size_t t = 0; t < object->maps.count; t++) {
            const lm_map_t *map = object->maps.data[t];
            for (size_t face = 0; face < map->indices.count / 3; face++, f++) {
                nmo_object_id_t face_material =
                    face < map->original.count && shape.faces != NULL && map->original.data[face] < shape.face_count
                        ? nmo_engine_mesh_material(engine, mesh, (int32_t)shape.faces[map->original.data[face]].material_group_idx)
                        : 0;
                eng_mesh_set_face_material(engine, nmesh, f, face_material);
            }
        }
        eng_mesh_copy_t *copy = eng_mesh_copy(engine, nmesh);
        int32_t channel = copy != NULL ? eng_mesh_add_channel(copy, nmat) : -1;
        if (channel >= 0) {
            float *uvs = vcount > 0 ? (float *)malloc((size_t)vcount * 2u * sizeof(float)) : NULL;
            int32_t u = 0;
            for (size_t t = 0; uvs != NULL && t < object->maps.count; t++) {
                const lm_map_t *map = object->maps.data[t];
                for (size_t i = 0; i < map->vertices.count; i++, u++) {
                    uvs[2 * u] = map->vertices.data[i].u;
                    uvs[2 * u + 1] = map->vertices.data[i].v;
                }
            }
            copy->channels.data[channel].uvs = uvs;
            copy->channels.data[channel].active = true;
            copy->channels.data[channel].source_blend = VXBLEND_ZERO;
            copy->channels.data[channel].dest_blend = VXBLEND_SRCCOLOR;
            copy->channels.data[channel].lit = false;
        }
        if (copy != NULL) {
            eng_mesh_build_face_normals(engine, copy);
        }
    }
}

/* Static Lightmap: the lightmaps of the receiving objects made objects of their own (the
   generator then forgets them, keeping the textures) */
static int bb_static_lightmap(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    lightmap_t *lm = lm_of(b->engine, b->node, true);
    if (lm != NULL) {
        lm_creation(b, lm, 1);
        lm_generate_objects(b->engine, lm);
        lm_clean(b->engine, lm, false);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_lightmap_blocks[] = {
    {NMO_GUID_INIT(0x4a797d21, 0x214d37f9), "Lightmap", bb_lightmap, cb_lightmap, NULL},
    {NMO_GUID_INIT(0x4a797d21, 0x214d37fa), "Static Lightmap", bb_static_lightmap, NULL, NULL},
};

nmo_status_t eng_register_lightmap(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &lightmap_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_lightmap_blocks) : status;
}
