/**
 * @file obj_parser.c
 * @brief Wavefront OBJ file parser implementation
 */

#include "format/nmo_obj_parser.h"
#include "core/nmo_arena.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
 * Dynamic array helpers (arena-based with realloc fallback)
 * ============================================================================ */

typedef struct {
    void *data;
    size_t count;
    size_t capacity;
    size_t elem_size;
} dyn_array_t;

static void dyn_init(dyn_array_t *a, size_t elem_size) {
    memset(a, 0, sizeof(*a));
    a->elem_size = elem_size;
}

static int dyn_grow(dyn_array_t *a) {
    size_t new_cap = a->capacity ? a->capacity * 2 : 256;
    void *tmp = realloc(a->data, new_cap * a->elem_size);
    if (!tmp) return -1;
    a->data = tmp;
    a->capacity = new_cap;
    return 0;
}

static void *dyn_push(dyn_array_t *a) {
    if (a->count >= a->capacity) {
        if (dyn_grow(a) < 0) return NULL;
    }
    void *ptr = (char *)a->data + a->count * a->elem_size;
    a->count++;
    return ptr;
}

static void dyn_free(dyn_array_t *a) {
    free(a->data);
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

/* ============================================================================
 * Line parsing helpers
 * ============================================================================ */

static const char *skip_whitespace(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

static const char *skip_to_eol(const char *p, const char *end) {
    while (p < end && *p != '\n' && *p != '\r') ++p;
    return p;
}

static const char *skip_eol(const char *p, const char *end) {
    if (p < end && *p == '\r') ++p;
    if (p < end && *p == '\n') ++p;
    return p;
}

static float parse_float(const char **pp, const char *end) {
    const char *p = skip_whitespace(*pp, end);
    /* Build a temporary null-terminated buffer for strtof */
    char buf[64];
    size_t i = 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' &&
           *p != '\r' && i < sizeof(buf) - 1) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    char *ep;
    float val = strtof(buf, &ep);
    *pp = p;
    return val;
}

/**
 * Parse a face vertex index component: an integer that may be negative.
 * Returns the parsed integer via *out and advances *pp.
 * Returns false if no integer could be parsed.
 */
static bool parse_face_int(const char **pp, const char *end, int32_t *out) {
    const char *p = *pp;
    if (p >= end) return false;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (p >= end || *p < '0' || *p > '9') return false;
    int32_t val = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -val : val;
    *pp = p;
    return true;
}

/**
 * Parse a face vertex token: v, v/vt, v/vt/vn, v//vn
 * OBJ indices are 1-based; we convert to 0-based.
 * Negative indices are relative to end of array.
 */
static bool parse_face_vertex(const char **pp, const char *end,
                              nmo_obj_face_vertex_t *fv,
                              size_t pos_count, size_t uv_count,
                              size_t normal_count) {
    const char *p = skip_whitespace(*pp, end);
    if (p >= end || *p == '\n' || *p == '\r') return false;

    fv->pos_idx = -1;
    fv->uv_idx = -1;
    fv->normal_idx = -1;

    /* Position index (required) */
    int32_t idx;
    if (!parse_face_int(&p, end, &idx)) return false;
    if (idx > 0) fv->pos_idx = idx - 1;
    else if (idx < 0) fv->pos_idx = (int32_t)pos_count + idx;

    /* Check for / separator */
    if (p < end && *p == '/') {
        p++; /* skip first / */
        /* Check for // (skip UV) */
        if (p < end && *p == '/') {
            p++; /* skip second / */
            /* Normal index */
            if (parse_face_int(&p, end, &idx)) {
                if (idx > 0) fv->normal_idx = idx - 1;
                else if (idx < 0) fv->normal_idx = (int32_t)normal_count + idx;
            }
        } else {
            /* UV index */
            if (parse_face_int(&p, end, &idx)) {
                if (idx > 0) fv->uv_idx = idx - 1;
                else if (idx < 0) fv->uv_idx = (int32_t)uv_count + idx;
            }
            /* Check for /normal */
            if (p < end && *p == '/') {
                p++; /* skip / */
                if (parse_face_int(&p, end, &idx)) {
                    if (idx > 0) fv->normal_idx = idx - 1;
                    else if (idx < 0) fv->normal_idx = (int32_t)normal_count + idx;
                }
            }
        }
    }

    *pp = p;
    return true;
}

/* ============================================================================
 * Material name tracking
 * ============================================================================ */

typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} mat_name_list_t;

static uint32_t mat_name_find_or_add(mat_name_list_t *list, const char *name,
                                     size_t name_len) {
    /* Search existing */
    for (size_t i = 0; i < list->count; i++) {
        if (strlen(list->names[i]) == name_len &&
            memcmp(list->names[i], name, name_len) == 0) {
            return (uint32_t)i;
        }
    }
    /* Add new */
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 16;
        char **tmp = (char **)realloc(list->names, new_cap * sizeof(char *));
        if (!tmp) return 0;
        list->names = tmp;
        list->capacity = new_cap;
    }
    char *dup = (char *)malloc(name_len + 1);
    if (!dup) return 0;
    memcpy(dup, name, name_len);
    dup[name_len] = '\0';
    list->names[list->count] = dup;
    return (uint32_t)list->count++;
}

static void mat_name_free(mat_name_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->names[i]);
    }
    free(list->names);
    list->names = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ============================================================================
 * Main parser
 * ============================================================================ */

NMO_API nmo_status_t nmo_obj_parse(
    nmo_arena_t *arena,
    const char *text,
    size_t text_size,
    nmo_obj_data_t *out)
{
    if (!arena || !text || !out) return NMO_ERR_INVALID_ARGUMENT;

    memset(out, 0, sizeof(*out));

    const char *p = text;
    const char *end = text + text_size;

    /* Temporary growable arrays (heap-allocated, copied to arena at end) */
    dyn_array_t positions; dyn_init(&positions, sizeof(float) * 3);
    dyn_array_t uvs;       dyn_init(&uvs, sizeof(float) * 2);
    dyn_array_t normals;   dyn_init(&normals, sizeof(float) * 3);
    dyn_array_t faces;     dyn_init(&faces, sizeof(nmo_obj_face_t));

    mat_name_list_t mat_names;
    memset(&mat_names, 0, sizeof(mat_names));

    uint32_t current_material = 0;

    while (p < end) {
        p = skip_whitespace(p, end);
        if (p >= end) break;

        /* Skip comments and empty lines */
        if (*p == '#' || *p == '\n' || *p == '\r') {
            p = skip_to_eol(p, end);
            p = skip_eol(p, end);
            continue;
        }

        /* Identify directive */
        if (p + 2 <= end && p[0] == 'v' && p[1] == 't' &&
            (p + 2 >= end || p[2] == ' ' || p[2] == '\t')) {
            /* vt u v */
            p += 2;
            float *uv = (float *)dyn_push(&uvs);
            if (!uv) goto oom;
            uv[0] = parse_float(&p, end);
            uv[1] = parse_float(&p, end);
        } else if (p + 2 <= end && p[0] == 'v' && p[1] == 'n' &&
                   (p + 2 >= end || p[2] == ' ' || p[2] == '\t')) {
            /* vn x y z */
            p += 2;
            float *n = (float *)dyn_push(&normals);
            if (!n) goto oom;
            n[0] = parse_float(&p, end);
            n[1] = parse_float(&p, end);
            n[2] = parse_float(&p, end);
        } else if (p[0] == 'v' &&
                   (p + 1 >= end || p[1] == ' ' || p[1] == '\t')) {
            /* v x y z */
            p += 1;
            float *pos = (float *)dyn_push(&positions);
            if (!pos) goto oom;
            pos[0] = parse_float(&p, end);
            pos[1] = parse_float(&p, end);
            pos[2] = parse_float(&p, end);
        } else if (p[0] == 'f' &&
                   (p + 1 >= end || p[1] == ' ' || p[1] == '\t')) {
            /* f v1 v2 v3 [v4] */
            p += 1;
            nmo_obj_face_vertex_t fverts[4];
            int nv = 0;
            while (nv < 4) {
                const char *before = p;
                nmo_obj_face_vertex_t fv;
                if (!parse_face_vertex(&p, end, &fv,
                                       positions.count, uvs.count,
                                       normals.count)) {
                    break;
                }
                if (p == before) break; /* no progress */
                fverts[nv++] = fv;
            }

            if (nv >= 3) {
                /* First triangle: 0,1,2 */
                nmo_obj_face_t *face = (nmo_obj_face_t *)dyn_push(&faces);
                if (!face) goto oom;
                face->verts[0] = fverts[0];
                face->verts[1] = fverts[1];
                face->verts[2] = fverts[2];
                face->material_group = current_material;

                /* Quad: second triangle 0,2,3 */
                if (nv >= 4) {
                    nmo_obj_face_t *face2 = (nmo_obj_face_t *)dyn_push(&faces);
                    if (!face2) goto oom;
                    face2->verts[0] = fverts[0];
                    face2->verts[1] = fverts[2];
                    face2->verts[2] = fverts[3];
                    face2->material_group = current_material;
                }
            }
        } else if (p + 6 <= end && memcmp(p, "usemtl", 6) == 0 &&
                   (p + 6 >= end || p[6] == ' ' || p[6] == '\t')) {
            /* usemtl name */
            p += 6;
            p = skip_whitespace(p, end);
            const char *name_start = p;
            p = skip_to_eol(p, end);
            /* Trim trailing whitespace */
            const char *name_end = p;
            while (name_end > name_start &&
                   (name_end[-1] == ' ' || name_end[-1] == '\t' ||
                    name_end[-1] == '\r')) {
                name_end--;
            }
            if (name_end > name_start) {
                current_material = mat_name_find_or_add(
                    &mat_names, name_start, (size_t)(name_end - name_start));
            }
        }
        /* else: skip unknown directive (mtllib, o, g, s, etc.) */

        p = skip_to_eol(p, end);
        p = skip_eol(p, end);
    }

    /* Copy results to arena */
    if (positions.count > 0) {
        size_t sz = positions.count * sizeof(float) * 3;
        out->positions = (float *)nmo_arena_alloc(arena, sz, alignof(float));
        if (!out->positions) goto oom;
        memcpy(out->positions, positions.data, sz);
        out->pos_count = positions.count;
    }

    if (uvs.count > 0) {
        size_t sz = uvs.count * sizeof(float) * 2;
        out->uvs = (float *)nmo_arena_alloc(arena, sz, alignof(float));
        if (!out->uvs) goto oom;
        memcpy(out->uvs, uvs.data, sz);
        out->uv_count = uvs.count;
    }

    if (normals.count > 0) {
        size_t sz = normals.count * sizeof(float) * 3;
        out->normals = (float *)nmo_arena_alloc(arena, sz, alignof(float));
        if (!out->normals) goto oom;
        memcpy(out->normals, normals.data, sz);
        out->normal_count = normals.count;
    }

    if (faces.count > 0) {
        size_t sz = faces.count * sizeof(nmo_obj_face_t);
        out->faces = (nmo_obj_face_t *)nmo_arena_alloc(arena, sz,
                                                        alignof(nmo_obj_face_t));
        if (!out->faces) goto oom;
        memcpy(out->faces, faces.data, sz);
        out->face_count = faces.count;
    }

    if (mat_names.count > 0) {
        out->material_names = (const char **)nmo_arena_alloc(
            arena, mat_names.count * sizeof(const char *), alignof(const char *));
        if (!out->material_names) goto oom;
        for (size_t i = 0; i < mat_names.count; i++) {
            size_t len = strlen(mat_names.names[i]);
            char *s = (char *)nmo_arena_alloc(arena, len + 1, 1);
            if (!s) goto oom;
            memcpy(s, mat_names.names[i], len + 1);
            out->material_names[i] = s;
        }
        out->material_name_count = mat_names.count;
    }

    dyn_free(&positions);
    dyn_free(&uvs);
    dyn_free(&normals);
    dyn_free(&faces);
    mat_name_free(&mat_names);
    return NMO_OK;

oom:
    dyn_free(&positions);
    dyn_free(&uvs);
    dyn_free(&normals);
    dyn_free(&faces);
    mat_name_free(&mat_names);
    return NMO_ERR_NOMEM;
}
