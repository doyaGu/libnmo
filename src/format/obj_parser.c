/**
 * @file obj_parser.c
 * @brief Wavefront OBJ file parser implementation
 */

#include "format/nmo_obj_parser.h"
#include "core/nmo_arena.h"

#include <ctype.h>
#include <math.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Dynamic arrays
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

static int dyn_reserve(dyn_array_t *a, size_t need) {
    if (need <= a->capacity) return 0;
    size_t new_cap = a->capacity ? a->capacity : 16;
    while (new_cap < need) {
        if (new_cap > ((size_t)-1) / 2u) return -1;
        new_cap *= 2u;
    }
    void *tmp = realloc(a->data, new_cap * a->elem_size);
    if (!tmp) return -1;
    a->data = tmp;
    a->capacity = new_cap;
    return 0;
}

static void *dyn_push(dyn_array_t *a) {
    if (dyn_reserve(a, a->count + 1u) < 0) return NULL;
    void *ptr = (char *)a->data + a->count * a->elem_size;
    memset(ptr, 0, a->elem_size);
    a->count++;
    return ptr;
}

static int dyn_push_copy(dyn_array_t *a, const void *src) {
    void *dst = dyn_push(a);
    if (!dst) return -1;
    memcpy(dst, src, a->elem_size);
    return 0;
}

static void dyn_free(dyn_array_t *a) {
    free(a->data);
    memset(a, 0, sizeof(*a));
}

/* ============================================================================
 * Name interning
 * ============================================================================ */

typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} name_list_t;

static void name_list_free(name_list_t *list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->names[i]);
    }
    free(list->names);
    memset(list, 0, sizeof(*list));
}

static bool name_list_find_or_add(name_list_t *list, const char *name,
                                  size_t name_len, uint32_t *out_idx) {
    for (size_t i = 0; i < list->count; ++i) {
        if (strlen(list->names[i]) == name_len &&
            memcmp(list->names[i], name, name_len) == 0) {
            *out_idx = (uint32_t)i;
            return true;
        }
    }

    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2u : 16u;
        char **tmp = (char **)realloc(list->names, new_cap * sizeof(char *));
        if (!tmp) return false;
        list->names = tmp;
        list->capacity = new_cap;
    }

    char *dup = (char *)malloc(name_len + 1u);
    if (!dup) return false;
    memcpy(dup, name, name_len);
    dup[name_len] = '\0';
    list->names[list->count] = dup;
    *out_idx = (uint32_t)list->count++;
    return true;
}

/* ============================================================================
 * Parser state and helpers
 * ============================================================================ */

typedef struct {
    dyn_array_t positions;
    dyn_array_t colors;
    dyn_array_t position_has_color;
    dyn_array_t uvs;
    dyn_array_t normals;
    dyn_array_t faces;
    dyn_array_t lines;
    dyn_array_t points;
    dyn_array_t temp_vertices;
    name_list_t materials;
    name_list_t objects;
    name_list_t groups;
    name_list_t mtllibs;
    uint32_t current_material;
    uint32_t current_object;
    uint32_t current_group;
    uint32_t current_smoothing;
    uint32_t line_number;
    nmo_status_t status;
} obj_parse_ctx_t;

static void ctx_init(obj_parse_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    dyn_init(&ctx->positions, sizeof(float) * 3u);
    dyn_init(&ctx->colors, sizeof(float) * 3u);
    dyn_init(&ctx->position_has_color, sizeof(bool));
    dyn_init(&ctx->uvs, sizeof(float) * 2u);
    dyn_init(&ctx->normals, sizeof(float) * 3u);
    dyn_init(&ctx->faces, sizeof(nmo_obj_face_t));
    dyn_init(&ctx->lines, sizeof(nmo_obj_line_t));
    dyn_init(&ctx->points, sizeof(nmo_obj_point_t));
    dyn_init(&ctx->temp_vertices, sizeof(nmo_obj_face_vertex_t));
    ctx->current_material = NMO_OBJ_NO_MATERIAL;
    ctx->current_object = NMO_OBJ_NO_NAME;
    ctx->current_group = NMO_OBJ_NO_NAME;
    ctx->status = NMO_OK;
}

static void ctx_free(obj_parse_ctx_t *ctx) {
    dyn_free(&ctx->positions);
    dyn_free(&ctx->colors);
    dyn_free(&ctx->position_has_color);
    dyn_free(&ctx->uvs);
    dyn_free(&ctx->normals);
    dyn_free(&ctx->faces);
    dyn_free(&ctx->lines);
    dyn_free(&ctx->points);
    dyn_free(&ctx->temp_vertices);
    name_list_free(&ctx->materials);
    name_list_free(&ctx->objects);
    name_list_free(&ctx->groups);
    name_list_free(&ctx->mtllibs);
}

static void ctx_set_error(obj_parse_ctx_t *ctx, nmo_status_t status,
                          const char *message) {
    if (ctx->status == NMO_OK) {
        ctx->status = status;
        nmo_last_error_setf((nmo_error_code_t)status, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "OBJ line %u: %s",
                            ctx->line_number, message);
    }
}

static const char *skip_space(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

static const char *trim_end(const char *start, const char *end) {
    while (end > start &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    return end;
}

static bool at_token_end(const char *p, const char *end) {
    return p >= end || *p == ' ' || *p == '\t' || *p == '\r' ||
           *p == '\n' || *p == '#';
}

static const char *next_token_end(const char *p, const char *end) {
    while (p < end && !at_token_end(p, end)) ++p;
    return p;
}

static bool match_directive(const char *p, const char *end, const char *kw) {
    size_t len = strlen(kw);
    return (size_t)(end - p) >= len &&
           memcmp(p, kw, len) == 0 &&
           (p + len >= end || p[len] == ' ' || p[len] == '\t' ||
            p[len] == '\r' || p[len] == '\n' || p[len] == '#');
}

static bool parse_float_token(obj_parse_ctx_t *ctx, const char **pp,
                              const char *end, float *out) {
    const char *p = skip_space(*pp, end);
    if (p >= end || *p == '#') {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "missing float token");
        return false;
    }
    const char *tok_end = next_token_end(p, end);
    size_t len = (size_t)(tok_end - p);
    char *buf = (char *)malloc(len + 1u);
    if (!buf) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to allocate float token");
        return false;
    }
    memcpy(buf, p, len);
    buf[len] = '\0';

    char *ep = NULL;
    float v = strtof(buf, &ep);
    if (ep == buf || *ep != '\0') {
        free(buf);
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "malformed float token");
        return false;
    }
    free(buf);
    *out = v;
    *pp = tok_end;
    return true;
}

static bool parse_optional_float_token(const char **pp, const char *end,
                                       float *out) {
    const char *p = skip_space(*pp, end);
    if (p >= end || *p == '#') return false;
    const char *tok_end = next_token_end(p, end);
    size_t len = (size_t)(tok_end - p);
    char stack_buf[64];
    char *buf = stack_buf;
    if (len + 1u > sizeof(stack_buf)) {
        buf = (char *)malloc(len + 1u);
        if (!buf) return false;
    }
    memcpy(buf, p, len);
    buf[len] = '\0';
    char *ep = NULL;
    float v = strtof(buf, &ep);
    bool ok = (ep != buf && *ep == '\0');
    if (ok) {
        *out = v;
        *pp = tok_end;
    }
    if (buf != stack_buf) free(buf);
    return ok;
}

static bool parse_int_in_token(const char **pp, const char *end, int32_t *out) {
    const char *p = *pp;
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) {
        neg = (*p == '-');
        ++p;
    }
    if (p >= end || !isdigit((unsigned char)*p)) return false;

    int64_t value = 0;
    while (p < end && isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        if (value > INT32_MAX) return false;
        ++p;
    }
    *out = neg ? (int32_t)-value : (int32_t)value;
    *pp = p;
    return true;
}

static bool resolve_index(obj_parse_ctx_t *ctx, int32_t raw, size_t count,
                          bool allow_zero_missing, int32_t *out) {
    if (raw > 0) {
        int64_t idx = (int64_t)raw - 1;
        if (idx >= (int64_t)count) {
            ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "OBJ index is out of range");
            return false;
        }
        *out = (int32_t)idx;
        return true;
    }
    if (raw == 0) {
        if (allow_zero_missing) {
            *out = -1;
            return true;
        }
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "position index 0 is invalid");
        return false;
    }

    int64_t idx = (int64_t)count + raw;
    if (idx < 0 || idx >= (int64_t)count) {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "relative OBJ index is out of range");
        return false;
    }
    *out = (int32_t)idx;
    return true;
}

static bool parse_index_tuple(obj_parse_ctx_t *ctx, const char **pp,
                              const char *line_end,
                              nmo_obj_face_vertex_t *fv) {
    const char *p = skip_space(*pp, line_end);
    if (p >= line_end || *p == '#') return false;

    const char *tok_end = next_token_end(p, line_end);
    const char *q = p;
    int32_t raw = 0;
    fv->pos_idx = -1;
    fv->uv_idx = -1;
    fv->normal_idx = -1;

    if (!parse_int_in_token(&q, tok_end, &raw) ||
        !resolve_index(ctx, raw, ctx->positions.count, false, &fv->pos_idx)) {
        return false;
    }

    if (q < tok_end && *q == '/') {
        ++q;
        if (q < tok_end && *q == '/') {
            ++q;
            if (q < tok_end) {
                if (!parse_int_in_token(&q, tok_end, &raw) ||
                    !resolve_index(ctx, raw, ctx->normals.count, true,
                                   &fv->normal_idx)) {
                    return false;
                }
            }
        } else {
            if (q < tok_end && *q != '/') {
                if (!parse_int_in_token(&q, tok_end, &raw) ||
                    !resolve_index(ctx, raw, ctx->uvs.count, true,
                                   &fv->uv_idx)) {
                    return false;
                }
            }
            if (q < tok_end && *q == '/') {
                ++q;
                if (q < tok_end) {
                    if (!parse_int_in_token(&q, tok_end, &raw) ||
                        !resolve_index(ctx, raw, ctx->normals.count, true,
                                       &fv->normal_idx)) {
                        return false;
                    }
                }
            }
        }
    }

    if (q != tok_end) {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "malformed index tuple");
        return false;
    }

    *pp = tok_end;
    return true;
}

static float sqr_dist3(const float *positions, int32_t a, int32_t b) {
    const float *pa = positions + (size_t)a * 3u;
    const float *pb = positions + (size_t)b * 3u;
    float dx = pa[0] - pb[0];
    float dy = pa[1] - pb[1];
    float dz = pa[2] - pb[2];
    return dx * dx + dy * dy + dz * dz;
}

static bool emit_face(obj_parse_ctx_t *ctx,
                      const nmo_obj_face_vertex_t *a,
                      const nmo_obj_face_vertex_t *b,
                      const nmo_obj_face_vertex_t *c) {
    nmo_obj_face_t face;
    memset(&face, 0, sizeof(face));
    face.verts[0] = *a;
    face.verts[1] = *b;
    face.verts[2] = *c;
    face.material_group = ctx->current_material;
    face.smoothing_group = ctx->current_smoothing;
    face.object_idx = ctx->current_object;
    face.group_idx = ctx->current_group;
    face.source_line = ctx->line_number;
    if (dyn_push_copy(&ctx->faces, &face) < 0) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append face");
        return false;
    }
    return true;
}

static double orient2(double ax, double ay, double bx, double by,
                      double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static bool point_in_tri(double px, double py, double ax, double ay,
                         double bx, double by, double cx, double cy,
                         bool ccw) {
    double o0 = orient2(ax, ay, bx, by, px, py);
    double o1 = orient2(bx, by, cx, cy, px, py);
    double o2 = orient2(cx, cy, ax, ay, px, py);
    const double eps = 1e-12;
    if (ccw) return o0 >= -eps && o1 >= -eps && o2 >= -eps;
    return o0 <= eps && o1 <= eps && o2 <= eps;
}

static bool triangulate_ngon(obj_parse_ctx_t *ctx,
                             const nmo_obj_face_vertex_t *verts,
                             size_t count) {
    const float *pos = (const float *)ctx->positions.data;
    double nx = 0.0, ny = 0.0, nz = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float *p0 = pos + (size_t)verts[i].pos_idx * 3u;
        const float *p1 = pos + (size_t)verts[(i + 1u) % count].pos_idx * 3u;
        nx += ((double)p0[1] - (double)p1[1]) * ((double)p0[2] + (double)p1[2]);
        ny += ((double)p0[2] - (double)p1[2]) * ((double)p0[0] + (double)p1[0]);
        nz += ((double)p0[0] - (double)p1[0]) * ((double)p0[1] + (double)p1[1]);
    }

    double ax = fabs(nx), ay = fabs(ny), az = fabs(nz);
    int drop_axis = 2;
    if (ax >= ay && ax >= az) drop_axis = 0;
    else if (ay >= ax && ay >= az) drop_axis = 1;

    double *xs = (double *)malloc(count * sizeof(double));
    double *ys = (double *)malloc(count * sizeof(double));
    size_t *idx = (size_t *)malloc(count * sizeof(size_t));
    if (!xs || !ys || !idx) {
        free(xs); free(ys); free(idx);
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to allocate triangulation buffers");
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const float *p = pos + (size_t)verts[i].pos_idx * 3u;
        if (drop_axis == 0) {
            xs[i] = p[1]; ys[i] = p[2];
        } else if (drop_axis == 1) {
            xs[i] = p[0]; ys[i] = p[2];
        } else {
            xs[i] = p[0]; ys[i] = p[1];
        }
        idx[i] = i;
    }

    double area = 0.0;
    for (size_t i = 0; i < count; ++i) {
        size_t j = (i + 1u) % count;
        area += xs[i] * ys[j] - xs[j] * ys[i];
    }
    if (fabs(area) <= 1e-12) {
        free(xs); free(ys); free(idx);
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "degenerate polygon");
        return false;
    }
    bool ccw = area > 0.0;
    size_t remaining = count;

    while (remaining > 3u) {
        bool clipped = false;
        for (size_t i = 0; i < remaining; ++i) {
            size_t ip = (i == 0) ? remaining - 1u : i - 1u;
            size_t in = (i + 1u) % remaining;
            size_t a = idx[ip], b = idx[i], c = idx[in];
            double cross = orient2(xs[a], ys[a], xs[b], ys[b], xs[c], ys[c]);
            if ((ccw && cross <= 1e-12) || (!ccw && cross >= -1e-12)) {
                continue;
            }

            bool contains = false;
            for (size_t j = 0; j < remaining; ++j) {
                if (j == ip || j == i || j == in) continue;
                size_t p = idx[j];
                if (point_in_tri(xs[p], ys[p], xs[a], ys[a], xs[b], ys[b],
                                 xs[c], ys[c], ccw)) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;

            if (!emit_face(ctx, &verts[a], &verts[b], &verts[c])) {
                free(xs); free(ys); free(idx);
                return false;
            }
            memmove(&idx[i], &idx[i + 1u], (remaining - i - 1u) * sizeof(size_t));
            remaining--;
            clipped = true;
            break;
        }
        if (!clipped) {
            free(xs); free(ys); free(idx);
            ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "failed to triangulate polygon");
            return false;
        }
    }

    bool ok = emit_face(ctx, &verts[idx[0]], &verts[idx[1]], &verts[idx[2]]);
    free(xs); free(ys); free(idx);
    return ok;
}

static bool triangulate_face(obj_parse_ctx_t *ctx,
                             const nmo_obj_face_vertex_t *verts,
                             size_t count) {
    if (count < 3u) {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "face has fewer than 3 vertices");
        return false;
    }
    if (count == 3u) {
        return emit_face(ctx, &verts[0], &verts[1], &verts[2]);
    }
    if (count == 4u) {
        const float *positions = (const float *)ctx->positions.data;
        float sqr02 = sqr_dist3(positions, verts[0].pos_idx, verts[2].pos_idx);
        float sqr13 = sqr_dist3(positions, verts[1].pos_idx, verts[3].pos_idx);
        if (sqr02 < sqr13) {
            return emit_face(ctx, &verts[0], &verts[1], &verts[2]) &&
                   emit_face(ctx, &verts[0], &verts[2], &verts[3]);
        }
        return emit_face(ctx, &verts[0], &verts[1], &verts[3]) &&
               emit_face(ctx, &verts[1], &verts[2], &verts[3]);
    }
    return triangulate_ngon(ctx, verts, count);
}

static bool parse_vertex(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    float xyz[3];
    if (!parse_float_token(ctx, &p, end, &xyz[0]) ||
        !parse_float_token(ctx, &p, end, &xyz[1]) ||
        !parse_float_token(ctx, &p, end, &xyz[2])) {
        return false;
    }

    float r = 0.0f, g = 0.0f, b = 0.0f;
    bool has_r = parse_optional_float_token(&p, end, &r);
    bool has_g = false;
    bool has_b = false;
    if (has_r) {
        has_g = parse_optional_float_token(&p, end, &g);
        if (has_g) has_b = parse_optional_float_token(&p, end, &b);
    }

    if (dyn_push_copy(&ctx->positions, xyz) < 0) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append position");
        return false;
    }

    float rgb[3] = {0.0f, 0.0f, 0.0f};
    bool explicit_color = has_r && has_g && has_b;
    if (explicit_color) {
        rgb[0] = r; rgb[1] = g; rgb[2] = b;
    }
    if (dyn_push_copy(&ctx->colors, rgb) < 0 ||
        dyn_push_copy(&ctx->position_has_color, &explicit_color) < 0) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append vertex color");
        return false;
    }
    return true;
}

static bool parse_uv(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    float uv[2];
    if (!parse_float_token(ctx, &p, end, &uv[0]) ||
        !parse_float_token(ctx, &p, end, &uv[1])) {
        return false;
    }
    if (dyn_push_copy(&ctx->uvs, uv) < 0) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append texture coordinate");
        return false;
    }
    return true;
}

static bool parse_normal(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    float n[3];
    if (!parse_float_token(ctx, &p, end, &n[0]) ||
        !parse_float_token(ctx, &p, end, &n[1]) ||
        !parse_float_token(ctx, &p, end, &n[2])) {
        return false;
    }
    if (dyn_push_copy(&ctx->normals, n) < 0) {
        ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append normal");
        return false;
    }
    return true;
}

static bool parse_vertex_list(obj_parse_ctx_t *ctx, const char *p,
                              const char *end, size_t *out_count) {
    ctx->temp_vertices.count = 0;
    while (true) {
        p = skip_space(p, end);
        if (p >= end || *p == '#') break;
        nmo_obj_face_vertex_t fv;
        if (!parse_index_tuple(ctx, &p, end, &fv)) return false;
        if (dyn_push_copy(&ctx->temp_vertices, &fv) < 0) {
            ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append primitive vertex");
            return false;
        }
    }
    *out_count = ctx->temp_vertices.count;
    return true;
}

static bool parse_face(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    size_t count = 0;
    if (!parse_vertex_list(ctx, p, end, &count)) return false;
    return triangulate_face(ctx,
        (const nmo_obj_face_vertex_t *)ctx->temp_vertices.data, count);
}

static bool parse_line(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    size_t count = 0;
    if (!parse_vertex_list(ctx, p, end, &count)) return false;
    if (count < 2u) {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "line has fewer than 2 vertices");
        return false;
    }
    nmo_obj_face_vertex_t *verts = (nmo_obj_face_vertex_t *)ctx->temp_vertices.data;
    for (size_t i = 0; i + 1u < count; ++i) {
        nmo_obj_line_t line;
        memset(&line, 0, sizeof(line));
        line.verts[0] = verts[i];
        line.verts[1] = verts[i + 1u];
        line.material_group = ctx->current_material;
        line.smoothing_group = ctx->current_smoothing;
        line.object_idx = ctx->current_object;
        line.group_idx = ctx->current_group;
        line.source_line = ctx->line_number;
        if (dyn_push_copy(&ctx->lines, &line) < 0) {
            ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append line");
            return false;
        }
    }
    return true;
}

static bool parse_point(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    size_t count = 0;
    if (!parse_vertex_list(ctx, p, end, &count)) return false;
    if (count == 0u) {
        ctx_set_error(ctx, NMO_ERR_INVALID_FORMAT, "point directive has no vertices");
        return false;
    }
    nmo_obj_face_vertex_t *verts = (nmo_obj_face_vertex_t *)ctx->temp_vertices.data;
    for (size_t i = 0; i < count; ++i) {
        nmo_obj_point_t point;
        memset(&point, 0, sizeof(point));
        point.verts = verts[i];
        point.material_group = ctx->current_material;
        point.smoothing_group = ctx->current_smoothing;
        point.object_idx = ctx->current_object;
        point.group_idx = ctx->current_group;
        point.source_line = ctx->line_number;
        if (dyn_push_copy(&ctx->points, &point) < 0) {
            ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append point");
            return false;
        }
    }
    return true;
}

static bool parse_name_directive(name_list_t *list, const char *p, const char *end,
                                 uint32_t *current) {
    p = skip_space(p, end);
    const char *name_end = trim_end(p, end);
    if (name_end <= p) {
        *current = NMO_OBJ_NO_NAME;
        return true;
    }
    return name_list_find_or_add(list, p, (size_t)(name_end - p), current);
}

static bool parse_usemtl(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    p = skip_space(p, end);
    const char *name_end = trim_end(p, end);
    if (name_end <= p) {
        ctx->current_material = NMO_OBJ_NO_MATERIAL;
        return true;
    }
    return name_list_find_or_add(&ctx->materials, p, (size_t)(name_end - p),
                                 &ctx->current_material);
}

static bool parse_smoothing(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    p = skip_space(p, end);
    if (p >= end || *p == '#') {
        ctx->current_smoothing = 0;
        return true;
    }
    const char *tok_end = next_token_end(p, end);
    if ((size_t)(tok_end - p) == 3u && memcmp(p, "off", 3) == 0) {
        ctx->current_smoothing = 0;
        return true;
    }
    int32_t value = 0;
    const char *q = p;
    if (!parse_int_in_token(&q, tok_end, &value) || q != tok_end || value < 0) {
        ctx->current_smoothing = 0;
        return true;
    }
    ctx->current_smoothing = (uint32_t)value;
    return true;
}

static bool parse_mtllib(obj_parse_ctx_t *ctx, const char *p, const char *end) {
    p = skip_space(p, end);
    end = trim_end(p, end);
    while (p < end) {
        p = skip_space(p, end);
        if (p >= end || *p == '#') break;
        const char *start = p;
        size_t max_len = (size_t)(end - p);
        char *buf = (char *)malloc(max_len + 1u);
        if (!buf) {
            ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to allocate mtllib name");
            return false;
        }
        size_t len = 0;
        while (p < end && *p != '#') {
            if (*p == '\\' && p + 1 < end &&
                (p[1] == ' ' || p[1] == '\t' || p[1] == '\\')) {
                buf[len++] = p[1];
                p += 2;
                continue;
            }
            if (*p == ' ' || *p == '\t') break;
            buf[len++] = *p++;
        }
        if (len > 0u) {
            uint32_t ignored = 0;
            if (!name_list_find_or_add(&ctx->mtllibs, buf, len, &ignored)) {
                free(buf);
                ctx_set_error(ctx, NMO_ERR_NOMEM, "failed to append mtllib name");
                return false;
            }
        }
        free(buf);
        if (p == start) break;
    }
    return true;
}

static bool copy_dyn_to_arena(nmo_arena_t *arena, dyn_array_t *src, size_t align,
                              void **out_ptr) {
    *out_ptr = NULL;
    if (src->count == 0u) return true;
    size_t size = src->count * src->elem_size;
    void *dst = nmo_arena_alloc(arena, size, align);
    if (!dst) return false;
    memcpy(dst, src->data, size);
    *out_ptr = dst;
    return true;
}

static bool copy_names_to_arena(nmo_arena_t *arena, const name_list_t *list,
                                const char ***out_names) {
    *out_names = NULL;
    if (list->count == 0u) return true;
    const char **names = (const char **)nmo_arena_alloc(
        arena, list->count * sizeof(const char *), alignof(const char *));
    if (!names) return false;
    for (size_t i = 0; i < list->count; ++i) {
        size_t len = strlen(list->names[i]);
        char *copy = (char *)nmo_arena_alloc(arena, len + 1u, 1u);
        if (!copy) return false;
        memcpy(copy, list->names[i], len + 1u);
        names[i] = copy;
    }
    *out_names = names;
    return true;
}

static bool copy_results_to_arena(nmo_arena_t *arena, obj_parse_ctx_t *ctx,
                                  nmo_obj_data_t *out) {
    if (!copy_dyn_to_arena(arena, &ctx->positions, alignof(float),
                           (void **)&out->positions) ||
        !copy_dyn_to_arena(arena, &ctx->colors, alignof(float),
                           (void **)&out->colors) ||
        !copy_dyn_to_arena(arena, &ctx->position_has_color,
                           alignof(bool),
                           (void **)&out->position_has_color) ||
        !copy_dyn_to_arena(arena, &ctx->uvs, alignof(float),
                           (void **)&out->uvs) ||
        !copy_dyn_to_arena(arena, &ctx->normals, alignof(float),
                           (void **)&out->normals) ||
        !copy_dyn_to_arena(arena, &ctx->faces, alignof(nmo_obj_face_t),
                           (void **)&out->faces) ||
        !copy_dyn_to_arena(arena, &ctx->lines, alignof(nmo_obj_line_t),
                           (void **)&out->lines) ||
        !copy_dyn_to_arena(arena, &ctx->points, alignof(nmo_obj_point_t),
                           (void **)&out->points) ||
        !copy_names_to_arena(arena, &ctx->materials, &out->material_names) ||
        !copy_names_to_arena(arena, &ctx->objects, &out->object_names) ||
        !copy_names_to_arena(arena, &ctx->groups, &out->group_names) ||
        !copy_names_to_arena(arena, &ctx->mtllibs, &out->mtllib_names)) {
        return false;
    }

    out->pos_count = ctx->positions.count;
    out->uv_count = ctx->uvs.count;
    out->normal_count = ctx->normals.count;
    out->face_count = ctx->faces.count;
    out->line_count = ctx->lines.count;
    out->point_count = ctx->points.count;
    out->material_name_count = ctx->materials.count;
    out->object_name_count = ctx->objects.count;
    out->group_name_count = ctx->groups.count;
    out->mtllib_name_count = ctx->mtllibs.count;
    return true;
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
    if (!arena || !text || !out) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_obj_parse");
    }

    memset(out, 0, sizeof(*out));

    obj_parse_ctx_t ctx;
    ctx_init(&ctx);

    const char *p = text;
    const char *end = text + text_size;
    while (p < end && ctx.status == NMO_OK) {
        const char *line_start = p;
        const char *line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            ++line_end;
        }
        ctx.line_number++;

        const char *q = skip_space(line_start, line_end);
        if (q < line_end && *q != '#') {
            if (match_directive(q, line_end, "vt")) {
                (void)parse_uv(&ctx, q + 2, line_end);
            } else if (match_directive(q, line_end, "vn")) {
                (void)parse_normal(&ctx, q + 2, line_end);
            } else if (match_directive(q, line_end, "v")) {
                (void)parse_vertex(&ctx, q + 1, line_end);
            } else if (match_directive(q, line_end, "f")) {
                (void)parse_face(&ctx, q + 1, line_end);
            } else if (match_directive(q, line_end, "l")) {
                (void)parse_line(&ctx, q + 1, line_end);
            } else if (match_directive(q, line_end, "p")) {
                (void)parse_point(&ctx, q + 1, line_end);
            } else if (match_directive(q, line_end, "o")) {
                if (!parse_name_directive(&ctx.objects, q + 1, line_end,
                                          &ctx.current_object)) {
                    ctx_set_error(&ctx, NMO_ERR_NOMEM, "failed to append object name");
                }
            } else if (match_directive(q, line_end, "g")) {
                if (!parse_name_directive(&ctx.groups, q + 1, line_end,
                                          &ctx.current_group)) {
                    ctx_set_error(&ctx, NMO_ERR_NOMEM, "failed to append group name");
                }
            } else if (match_directive(q, line_end, "s")) {
                (void)parse_smoothing(&ctx, q + 1, line_end);
            } else if (match_directive(q, line_end, "usemtl")) {
                if (!parse_usemtl(&ctx, q + 6, line_end)) {
                    ctx_set_error(&ctx, NMO_ERR_NOMEM, "failed to append material name");
                }
            } else if (match_directive(q, line_end, "mtllib")) {
                (void)parse_mtllib(&ctx, q + 6, line_end);
            }
        }

        p = line_end;
        if (p < end && *p == '\r') ++p;
        if (p < end && *p == '\n') ++p;
    }

    if (ctx.status == NMO_OK &&
        !copy_results_to_arena(arena, &ctx, out)) {
        ctx_set_error(&ctx, NMO_ERR_NOMEM, "failed to copy OBJ data to arena");
    }

    nmo_status_t status = ctx.status;
    ctx_free(&ctx);
    if (status != NMO_OK) return status;
    NMO_RETURN_OK();
}
