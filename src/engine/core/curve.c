/**
 * @file curve.c
 * @brief 2D curve parameter values (CK2dCurve), as CK2.dll reads and evaluates them.
 *
 * A 2D curve parameter saves its curve in a sub-chunk. The engine reads it
 * once, when the parameter loads, into flat bytes: a header and the control
 * points sorted by x with their tangents worked out (CK2dCurve::Read and
 * Update). Flat bytes copy like any other parameter value, so a curve passes
 * along links and shared inputs. nmo_engine_curve_y() is CK2dCurve::GetY.
 */

#include "../engine_internal.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_enum_defs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CURVE_MAGIC 0x56524332u /* "2CRV" */

/* CK2dCurvePoint flags */
#define CURVE_POINT_USE_TANGENTS 0x01u /* the saved tangents, not t,c,b ones */
#define CURVE_POINT_LINEAR 0x02u       /* a straight segment to the next point */
#define CURVE_POINT_SAVED 0x10000000u  /* the point's data follows its flags */

/* Where a 2D curve parameter's saved chunk holds the curve */
#define CURVE_ID_PARAMETER 0x0eu

/* CK_STATESAVE_2DCURVE* identifiers */
#define CURVE_ID_FIT_COEFF 0x40u
#define CURVE_ID_NEW_DATA 0x100u
#define CURVE_ID_SAVE_POINTS 0x3f800u
#define CURVE_POINT_ID_TCB 0x800u
#define CURVE_POINT_ID_TANGENTS 0x1000u
#define CURVE_POINT_ID_DEFAULT_DATA 0x4000u

#define CURVE_EPSILON 0.000099999997f

typedef struct curve_point {
    float x, y;
    float in_x, in_y;
    float out_x, out_y;
    float tension, continuity, bias;
    uint32_t flags;
} curve_point_t;

typedef struct curve_header {
    uint32_t magic;
    uint32_t count;
} curve_header_t;

/* ============================================================================
 * Update
 * ============================================================================ */

/* Scale tangent (x, y) so its x is `to_x` (left alone when its x is 0). */
static void curve_scale_tangent(float *x, float *y, float to_x)
{
    if (*x != 0.0f) {
        float scale = to_x / *x;
        *x *= scale;
        *y *= scale;
    }
}

/* CK2dCurve::UpdatePointsAndTangents, without the fitted positions nothing reads here */
static void curve_update_tangents(curve_point_t *points, uint32_t count)
{
    if (count < 2) {
        return;
    }
    curve_point_t *first = &points[0];
    float dx = points[1].x - first->x, dy = points[1].y - first->y;
    if ((first->flags & CURVE_POINT_USE_TANGENTS) == 0) {
        float t = 1.0f - first->tension, c = first->continuity, b = 1.0f - first->bias;
        float out = (1.0f - c) * t * b, in = (c + 1.0f) * t * b;
        first->out_x = dx * out * 0.5f;
        first->out_y = dy * out * 0.5f;
        first->in_x = dx * in * 0.5f;
        first->in_y = dy * in * 0.5f;
    }
    curve_scale_tangent(&first->in_x, &first->in_y, CURVE_EPSILON);
    curve_scale_tangent(&first->out_x, &first->out_y, dx == 0.0f ? CURVE_EPSILON : dx);

    for (uint32_t i = 1; i + 1 < count; i++) {
        curve_point_t *p = &points[i];
        float pdx = p->x - points[i - 1].x, pdy = p->y - points[i - 1].y;
        float ndx = points[i + 1].x - p->x, ndy = points[i + 1].y - p->y;
        if ((p->flags & CURVE_POINT_USE_TANGENTS) == 0) {
            float t = 1.0f - p->tension, c = p->continuity, b = p->bias;
            float out_prev = (c + 1.0f) * (b + 1.0f) * t, out_next = t * (1.0f - c) * (1.0f - b);
            float in_prev = (b + 1.0f) * t * (1.0f - c), in_next = (c + 1.0f) * t * (1.0f - b);
            p->out_x = (pdx * out_prev + ndx * out_next) * 0.5f;
            p->out_y = (pdy * out_prev + ndy * out_next) * 0.5f;
            p->in_x = (pdx * in_prev + ndx * in_next) * 0.5f;
            p->in_y = (pdy * in_prev + ndy * in_next) * 0.5f;
        }
        curve_scale_tangent(&p->in_x, &p->in_y, pdx == 0.0f ? CURVE_EPSILON : pdx);
        curve_scale_tangent(&p->out_x, &p->out_y, ndx == 0.0f ? CURVE_EPSILON : ndx);
    }

    curve_point_t *last = &points[count - 1];
    dx = last->x - points[count - 2].x;
    dy = last->y - points[count - 2].y;
    if ((last->flags & CURVE_POINT_USE_TANGENTS) == 0) {
        float t = 1.0f - last->tension, c = last->continuity, b = last->bias;
        float out = (c + 1.0f) * (b + 1.0f) * t, in = t * (1.0f - c) * (b + 1.0f);
        last->out_x = dx * out * 0.5f;
        last->out_y = dy * out * 0.5f;
        last->in_x = dx * in * 0.5f;
        last->in_y = dy * in * 0.5f;
    }
    curve_scale_tangent(&last->in_x, &last->in_y, dx == 0.0f ? CURVE_EPSILON : dx);
    curve_scale_tangent(&last->out_x, &last->out_y, 0.001f);
}

/* CK2dCurve::Update: points in x order (a point moves only before a smaller x), tangents */
static void curve_update(curve_point_t *points, uint32_t count)
{
    for (uint32_t i = 1; i < count; i++) {
        curve_point_t point = points[i];
        uint32_t j = i;
        for (; j > 0 && point.x < points[j - 1].x; j--) {
            points[j] = points[j - 1];
        }
        points[j] = point;
    }
    curve_update_tangents(points, count);
}

/* ============================================================================
 * Read
 * ============================================================================ */

static float read_float(nmo_chunk_t *chunk)
{
    float value = 0.0f;
    nmo_chunk_read_float(chunk, &value);
    return value;
}

static int32_t read_int(nmo_chunk_t *chunk)
{
    int32_t value = 0;
    nmo_chunk_read_int(chunk, &value);
    return value;
}

/* The points a CK2dCurve has before it reads: (0,0) and (1,1). */
static curve_point_t *curve_default_points(uint32_t count)
{
    curve_point_t *points = (curve_point_t *)calloc(count > 2 ? count : 2, sizeof(*points));
    if (points != NULL) {
        points[1].x = 1.0f;
        points[1].y = 1.0f;
    }
    return points;
}

/* Resize, as XArray::Resize does: the points kept stay, new ones start zeroed. */
static curve_point_t *curve_resize(curve_point_t *points, uint32_t *count, uint32_t new_count)
{
    curve_point_t *grown = (curve_point_t *)realloc(points, (new_count > 0 ? new_count : 1) *
                                                                sizeof(*points));
    if (grown == NULL) {
        free(points);
        return NULL;
    }
    if (new_count > *count) {
        memset(&grown[*count], 0, (new_count - *count) * sizeof(*grown));
    }
    *count = new_count;
    return grown;
}

/* CK2dCurvePoint::Read: a point of an old curve, from its own sub-chunk. */
static void curve_read_old_point(nmo_chunk_t *chunk, curve_point_t *point)
{
    if (nmo_chunk_seek_identifier(chunk, CURVE_POINT_ID_DEFAULT_DATA) == NMO_OK) {
        nmo_object_id_t ignored = 0;
        nmo_chunk_read_object_id(chunk, &ignored);
        point->flags = read_int(chunk) != 0 ? point->flags | CURVE_POINT_USE_TANGENTS
                                            : point->flags & ~CURVE_POINT_USE_TANGENTS;
        point->flags = read_int(chunk) != 0 ? point->flags | CURVE_POINT_LINEAR
                                            : point->flags & ~CURVE_POINT_LINEAR;
        point->x = read_float(chunk);
        point->y = read_float(chunk);
    }
    if (nmo_chunk_seek_identifier(chunk, CURVE_POINT_ID_TCB) == NMO_OK) {
        point->tension = read_float(chunk);
        point->continuity = read_float(chunk);
        point->bias = read_float(chunk);
    }
    if (nmo_chunk_seek_identifier(chunk, CURVE_POINT_ID_TANGENTS) == NMO_OK) {
        point->in_x = read_float(chunk);
        point->in_y = read_float(chunk);
        point->out_x = read_float(chunk);
        point->out_y = read_float(chunk);
    }
}

/* CK2dCurve::Read. Returns the points (count in *count), NULL when out of memory. */
static curve_point_t *curve_read(nmo_chunk_t *chunk, uint32_t *count)
{
    *count = 2;
    curve_point_t *points = curve_default_points(2);
    if (points == NULL || nmo_chunk_start_read(chunk) != NMO_OK) {
        return points;
    }
    if ((int16_t)nmo_chunk_get_data_version(chunk) < 1 &&
        nmo_chunk_seek_identifier(chunk, CURVE_ID_SAVE_POINTS) == NMO_OK) {
        int32_t saved = read_int(chunk);
        points = curve_resize(points, count, saved > 0 ? (uint32_t)saved : 0u);
        for (uint32_t i = 0; points != NULL && i < *count; i++) {
            nmo_object_id_t ignored = 0;
            nmo_chunk_t *sub = NULL;
            nmo_chunk_read_object_id(chunk, &ignored);
            if (nmo_chunk_read_sub_chunk(chunk, &sub) == NMO_OK && sub != NULL) {
                if (nmo_chunk_start_read(sub) == NMO_OK) {
                    curve_read_old_point(sub, &points[i]);
                }
                nmo_chunk_destroy(sub);
            }
        }
    }
    if (points != NULL && nmo_chunk_seek_identifier(chunk, CURVE_ID_NEW_DATA) == NMO_OK) {
        /* An object array (a count and one ID per point), then the fitting coefficient */
        int32_t saved = read_int(chunk);
        for (int32_t i = 0; i < saved; i++) {
            read_int(chunk);
        }
        read_float(chunk);
        points = curve_resize(points, count, saved > 0 ? (uint32_t)saved : 0u);
        uint32_t kept = 0;
        for (uint32_t i = 0; points != NULL && i < *count; i++) {
            curve_point_t *point = &points[i];
            point->flags = (uint32_t)read_int(chunk);
            if ((point->flags & CURVE_POINT_SAVED) == 0) {
                continue;
            }
            point->flags &= ~CURVE_POINT_SAVED;
            point->x = read_float(chunk);
            point->y = read_float(chunk);
            point->tension = read_float(chunk);
            point->continuity = read_float(chunk);
            point->bias = read_float(chunk);
            read_float(chunk); /* length and time, worked out again */
            read_float(chunk);
            point->in_x = read_float(chunk);
            point->in_y = read_float(chunk);
            point->out_x = read_float(chunk);
            point->out_y = read_float(chunk);
            kept++;
        }
        if (points != NULL) {
            points = curve_resize(points, count, kept);
        }
    }
    return points;
}

bool eng_curve_value(const nmo_parameter_state_t *state, uint8_t **out_data, uint32_t *out_size)
{
    uint32_t count = 2;
    curve_point_t *points = NULL;
    if (state != NULL && state->mode == CKPARAM_MODE_SUBCHUNK && state->subchunk != NULL) {
        /* The 2D curve type's save function wraps the curve: identifier 0xE, then its chunk */
        nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
        nmo_chunk_t *saved = arena != NULL ? nmo_chunk_clone(state->subchunk, arena) : NULL;
        nmo_chunk_t *chunk = NULL;
        if (saved == NULL) {
            nmo_arena_destroy(arena);
            return false;
        }
        if (nmo_chunk_start_read(saved) == NMO_OK &&
            nmo_chunk_seek_identifier(saved, CURVE_ID_PARAMETER) == NMO_OK) {
            nmo_chunk_read_sub_chunk(saved, &chunk);
        }
        points = chunk != NULL ? curve_read(chunk, &count) : curve_default_points(count);
        nmo_chunk_destroy(chunk);
        nmo_arena_destroy(arena);
    } else {
        points = curve_default_points(count);
    }
    if (points == NULL) {
        return false;
    }
    curve_update(points, count);

    size_t size = sizeof(curve_header_t) + count * sizeof(curve_point_t);
    uint8_t *data = (uint8_t *)malloc(size);
    if (data == NULL) {
        free(points);
        return false;
    }
    const curve_header_t header = {CURVE_MAGIC, count};
    memcpy(data, &header, sizeof(header));
    if (count > 0) {
        memcpy(data + sizeof(header), points, count * sizeof(curve_point_t));
    }
    free(points);
    *out_data = data;
    *out_size = (uint32_t)size;
    return true;
}

/* ============================================================================
 * GetY
 * ============================================================================ */

float nmo_engine_curve_y(const void *curve, size_t size, float x)
{
    curve_header_t header;
    if (curve == NULL || size < sizeof(header)) {
        return 0.0f;
    }
    memcpy(&header, curve, sizeof(header));
    if (header.magic != CURVE_MAGIC ||
        (size - sizeof(header)) / sizeof(curve_point_t) < header.count) {
        return 0.0f;
    }
    const curve_point_t *points =
        (const curve_point_t *)(const void *)((const uint8_t *)curve + sizeof(header));
    uint32_t count = header.count;

    if (x > 1.0f) {
        x = 1.0f;
    }
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (count < 2) {
        return 0.0f;
    }
    /* The segment ends at the first point at or past x */
    uint32_t end = 0;
    while (end < count && !(points[end].x >= x)) {
        end++;
    }
    if (end == count) {
        return 0.0f;
    }
    const curve_point_t *s = &points[end > 0 ? end - 1 : 0];
    const curve_point_t *e = &points[end];

    float y = 0.0f;
    if (s->x == e->x) {
        y = s->y;
    } else if ((s->flags & CURVE_POINT_LINEAR) != 0) {
        y = (e->y - s->y) * (x - s->x) / (e->x - s->x) + s->y;
    } else {
        /* Find the Hermite parameter whose x is x by halving, from the linear guess */
        float t = (x - s->x) / (e->x - s->x), low = 0.0f, high = 1.0f;
        for (int i = 0; i < 1000; i++) {
            float t2 = t * t;
            float h1 = (t + t - 3.0f) * t2 + 1.0f, h2 = (3.0f - (t + t)) * t2;
            float h3 = ((t - 2.0f) * t + 1.0f) * t, h4 = (t - 1.0f) * t2;
            float cx = h1 * s->x + h2 * e->x + h3 * s->out_x + h4 * e->in_x;
            y = h1 * s->y + h2 * e->y + h3 * s->out_y + h4 * e->in_y;
            float middle = (high + low) * 0.5f;
            t = middle;
            if (cx < x) {
                low = middle;
            }
            if (cx > x) {
                high = middle;
            }
            if (fabsf(cx - x) <= 0.000009999999747378752f) {
                break;
            }
        }
    }
    if (y > 1.0f) {
        return 1.0f;
    }
    return y >= 0.0f ? y : 0.0f;
}
