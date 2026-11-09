/**
 * @file collisions.c
 * @brief Ports of the Collisions building blocks, on the engine's 3D
 *        entities.
 *
 * Each function does what the block's function in Collisions.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "virtools.h"

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "../../engine/engine_curve.h"
#include "../../engine/engine_message.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_query.h"
#include "../../engine/engine_animation.h"
#include "../../engine/engine_character.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* VxOBB: a local box through a world matrix (unit axes, extents scaled by the axes' lengths) */
typedef struct obb {
    float center[3];
    float axis[3][3];
    float extent[3];
} obb_t;

static void obb_from(nmo_engine_t *engine, int32_t entity, obb_t *out)
{
    eng_vector_t min, max;
    eng_entity_local_box(engine, entity, &min, &max);
    const eng_matrix_t *m = &engine->entities.data[entity].world;
    eng_vector_t center = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f}, c;
    eng_vector_transform(&c, m, &center);
    out->center[0] = c.x;
    out->center[1] = c.y;
    out->center[2] = c.z;
    const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    for (int i = 0; i < 3; i++) {
        double length = sqrt((double)m->m[i][0] * m->m[i][0] + (double)m->m[i][1] * m->m[i][1] +
                             (double)m->m[i][2] * m->m[i][2]);
        for (int k = 0; k < 3; k++) {
            out->axis[i][k] = length > 0.0 ? (float)(m->m[i][k] / length) : 0.0f;
        }
        out->extent[i] = (float)(length * half[i]);
    }
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* VxIntersect::OBBOBB: no separating axis among the fifteen */
static bool obb_overlap(const obb_t *a, const obb_t *b)
{
    const float t[3] = {b->center[0] - a->center[0], b->center[1] - a->center[1],
                        b->center[2] - a->center[2]};
    float r[3][3], abs_r[3][3], ta[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = dot3(a->axis[i], b->axis[j]);
            abs_r[i][j] = fabsf(r[i][j]);
        }
        ta[i] = dot3(t, a->axis[i]);
        if (fabsf(ta[i]) > a->extent[i] + abs_r[i][0] * b->extent[0] + abs_r[i][1] * b->extent[1] +
                               abs_r[i][2] * b->extent[2]) {
            return false;
        }
    }
    for (int j = 0; j < 3; j++) {
        if (fabsf(dot3(t, b->axis[j])) > b->extent[j] + abs_r[0][j] * a->extent[0] +
                                             abs_r[1][j] * a->extent[1] + abs_r[2][j] * a->extent[2]) {
            return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        for (int j = 0; j < 3; j++) {
            int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float distance = fabsf(ta[i2] * r[i1][j] - ta[i1] * r[i2][j]);
            float reach = a->extent[i1] * abs_r[i2][j] + a->extent[i2] * abs_r[i1][j] +
                          b->extent[j1] * abs_r[i][j2] + b->extent[j2] * abs_r[i][j1];
            if (distance > reach) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Box Box Intersection (Collisions.dll): True when the two entities' boxes,
 * local ones through their world matrices (CKCollisionManager::
 * BoxBoxIntersection with local boxes), overlap; False otherwise. The
 * Hierarchy inputs, and characters' body parts, are not taken into account.
 */
static int bb_box_box_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    if (first >= 0 && second >= 0) {
        obb_t a, c;
        obb_from(b->engine, first, &a);
        obb_from(b->engine, second, &c);
        if (obb_overlap(&a, &c)) {
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_OK;
        }
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * VxMath
 * ============================================================================ */

#define VX_EPSILON 1.1920929e-7f

static eng_vector_t v3(float x, float y, float z)
{
    return (eng_vector_t){x, y, z};
}

static eng_vector_t vsub(eng_vector_t a, eng_vector_t b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static float vcomp(const eng_vector_t *v, int i)
{
    return i == 0 ? v->x : i == 1 ? v->y : v->z;
}

/* VxBbox::TransformFrom: the world-aligned box of a box through a matrix */
static void box_transform_from(eng_vector_t *min, eng_vector_t *max, eng_vector_t smin, eng_vector_t smax,
                               const eng_matrix_t *m)
{
    eng_vector_t center = v3((smin.x + smax.x) * 0.5f, (smin.y + smax.y) * 0.5f, (smin.z + smax.z) * 0.5f), c;
    float size[3] = {smax.x - smin.x, smax.y - smin.y, smax.z - smin.z};
    eng_vector_transform(&c, m, &center);
    float half[3];
    for (int k = 0; k < 3; k++) {
        half[k] = (fabsf(size[0] * m->m[0][k]) + fabsf(size[1] * m->m[1][k]) + fabsf(size[2] * m->m[2][k])) * 0.5f;
    }
    *min = v3(c.x - half[0], c.y - half[1], c.z - half[2]);
    *max = v3(c.x + half[0], c.y + half[1], c.z + half[2]);
}

/* VxIntersect::SegmentBox: the segment from `o` along `d` (t in [0, 1]) meets the box */
static bool segment_box(eng_vector_t o, eng_vector_t d, eng_vector_t min, eng_vector_t max)
{
    double near_t = 0.0, far_t = 1.0;
    for (int i = 0; i < 3; i++) {
        double origin = vcomp(&o, i), direction = vcomp(&d, i), lo = vcomp(&min, i), hi = vcomp(&max, i);
        if (direction == 0.0) {
            if (origin < lo || origin > hi) {
                return false;
            }
            continue;
        }
        double end = origin + direction;
        if ((origin < lo && end < lo) || (origin > hi && end > hi)) {
            return false;
        }
        double t0 = (lo - origin) / direction, t1 = (hi - origin) / direction;
        if (t0 > t1) {
            double t = t0;
            t0 = t1;
            t1 = t;
        }
        if (t0 > near_t) near_t = t0;
        if (t1 < far_t) far_t = t1;
    }
    return near_t <= far_t;
}

/* VxIntersect::PointInFace: inside the triangle as projected on its two minor axes */
static bool point_in_face(eng_vector_t p, eng_vector_t a0, eng_vector_t a1, eng_vector_t a2, eng_vector_t n)
{
    int first = 1, second = 2;
    float most = fabsf(n.x);
    if (most < fabsf(n.y)) {
        first = 0;
        second = 2;
        most = fabsf(n.y);
    }
    if (most < fabsf(n.z)) {
        first = 0;
        second = 1;
    }
    const eng_vector_t *v[3] = {&a0, &a1, &a2};
    bool positive = false, negative = false;
    for (int e = 0; e < 3; e++) {
        const eng_vector_t *a = v[e], *b = v[(e + 1) % 3];
        double px = (double)vcomp(&p, first) - vcomp(a, first), py = (double)vcomp(&p, second) - vcomp(a, second);
        double dx = (double)vcomp(b, first) - vcomp(a, first), dy = (double)vcomp(b, second) - vcomp(a, second);
        double side = px * dy - py * dx;
        positive = positive || side > 0.0;
        negative = negative || side < 0.0;
        if (positive && negative) {
            return false;
        }
    }
    if (positive || negative) {
        return true;
    }
    for (int k = 0; k < 2; k++) {
        int axis = k == 0 ? first : second;
        float x = vcomp(&p, axis), lo = fminf(vcomp(&a0, axis), fminf(vcomp(&a1, axis), vcomp(&a2, axis)));
        float hi = fmaxf(vcomp(&a0, axis), fmaxf(vcomp(&a1, axis), vcomp(&a2, axis)));
        if (x < lo || x > hi) {
            return false;
        }
    }
    return true;
}

/* VxIntersect::SegmentFace: the segment from `o` along `d` crosses the face's plane (t in
   [-EPSILON, 1 + EPSILON]) inside the face */
static bool segment_face(eng_vector_t o, eng_vector_t d, eng_vector_t a0, eng_vector_t a1, eng_vector_t a2,
                         eng_vector_t n)
{
    double den = (double)n.x * d.x + (double)n.y * d.y + (double)n.z * d.z;
    if (fabs(den) < VX_EPSILON) {
        return false;
    }
    double num = (double)n.x * ((double)a0.x - o.x) + (double)n.y * ((double)a0.y - o.y) + (double)n.z * ((double)a0.z - o.z);
    double t = num / den;
    if (t < -VX_EPSILON || t > 1.0 + VX_EPSILON) {
        return false;
    }
    eng_vector_t p = v3((float)(o.x + d.x * t), (float)(o.y + d.y * t), (float)(o.z + d.z * t));
    return point_in_face(p, a0, a1, a2, n);
}

/* Moller's coplanar triangle test (coplanar_tri_tri), relative to V0 in doubles */
static bool coplanar_tri_tri(eng_vector_t n, const eng_vector_t vi[3], const eng_vector_t ui[3])
{
    float a[3] = {fabsf(n.x), fabsf(n.y), fabsf(n.z)};
    int i0, i1;
    if (a[0] > a[1]) {
        if (a[0] > a[2]) {
            i0 = 1, i1 = 2;
        } else {
            i0 = 0, i1 = 1;
        }
    } else if (a[2] > a[1]) {
        i0 = 0, i1 = 1;
    } else {
        i0 = 0, i1 = 2;
    }
    double v[3][2], u[3][2];
    for (int i = 0; i < 3; i++) {
        v[i][0] = (double)vcomp(&vi[i], i0) - vcomp(&vi[0], i0);
        v[i][1] = (double)vcomp(&vi[i], i1) - vcomp(&vi[0], i1);
        u[i][0] = (double)vcomp(&ui[i], i0) - vcomp(&vi[0], i0);
        u[i][1] = (double)vcomp(&ui[i], i1) - vcomp(&vi[0], i1);
    }
    for (int e = 0; e < 3; e++) { /* EDGE_AGAINST_TRI_EDGES */
        const double *p0 = v[e], *p1 = v[(e + 1) % 3];
        double ax = p1[0] - p0[0], ay = p1[1] - p0[1];
        for (int k = 0; k < 3; k++) { /* EDGE_EDGE_TEST */
            const double *q0 = u[k], *q1 = u[(k + 1) % 3];
            double bx = q0[0] - q1[0], by = q0[1] - q1[1], cx = p0[0] - q0[0], cy = p0[1] - q0[1];
            double f = ay * bx - ax * by, d = by * cx - bx * cy;
            if ((f > 0 && d >= 0 && d <= f) || (f < 0 && d <= 0 && d >= f)) {
                double g = ax * cy - ay * cx;
                if (f > 0 ? (g >= 0 && g <= f) : (g <= 0 && g >= f)) {
                    return true;
                }
            }
        }
    }
    for (int pass = 0; pass < 2; pass++) { /* POINT_IN_TRI both ways */
        const double *p = pass == 0 ? v[0] : u[0];
        double (*t)[2] = pass == 0 ? u : v;
        double d[3];
        for (int k = 0; k < 3; k++) {
            const double *q0 = t[k], *q1 = t[(k + 1) % 3];
            double a1 = q1[1] - q0[1], b1 = -(q1[0] - q0[0]), c1 = -a1 * q0[0] - b1 * q0[1];
            d[k] = a1 * p[0] + b1 * p[1] + c1;
        }
        if (d[0] * d[1] > 0.0 && d[0] * d[2] > 0.0) {
            return true;
        }
    }
    return false;
}

/* The interval a triangle's edges cut on the line of the two planes (FaceFaceFromDistances) */
static bool face_interval(const double v[3], const double dist[3], double *base, double *num0, double *num1,
                          double *den0, double *den1)
{
    int k;
    double d01 = dist[0] * dist[1], d02 = dist[0] * dist[2];
    if (d01 > 0.0) {
        k = 2;
    } else if (d02 > 0.0) {
        k = 1;
    } else if (dist[1] * dist[2] > 0.0 || dist[0] != 0.0) {
        k = 0;
    } else if (dist[1] != 0.0) {
        k = 1;
    } else if (dist[2] != 0.0) {
        k = 2;
    } else {
        return false; /* coplanar */
    }
    int o0 = k == 0 ? 1 : 0, o1 = k == 2 ? 1 : 2;
    *base = v[k];
    *num0 = (v[o0] - v[k]) * dist[k];
    *num1 = (v[o1] - v[k]) * dist[k];
    *den0 = dist[k] - dist[o0];
    *den1 = dist[k] - dist[o1];
    return true;
}

/* VxIntersect::FaceFace: Moller's triangle-triangle test, without divisions */
static bool face_face(eng_vector_t a0, eng_vector_t a1, eng_vector_t a2, eng_vector_t n0, eng_vector_t b0,
                      eng_vector_t b1, eng_vector_t b2, eng_vector_t n1)
{
    const eng_vector_t a[3] = {a0, a1, a2}, b[3] = {b0, b1, b2};
    double dist_a[3], dist_b[3];
    for (int i = 0; i < 3; i++) {
        dist_b[i] = ((double)b[i].x - a0.x) * n0.x + ((double)b[i].y - a0.y) * n0.y + ((double)b[i].z - a0.z) * n0.z;
        dist_a[i] = ((double)a[i].x - b0.x) * n1.x + ((double)a[i].y - b0.y) * n1.y + ((double)a[i].z - b0.z) * n1.z;
        if (fabs(dist_b[i]) < VX_EPSILON) dist_b[i] = 0.0;
        if (fabs(dist_a[i]) < VX_EPSILON) dist_a[i] = 0.0;
    }
    if (dist_b[0] * dist_b[1] > 0.0 && dist_b[0] * dist_b[2] > 0.0) return false;
    if (dist_a[0] * dist_a[1] > 0.0 && dist_a[0] * dist_a[2] > 0.0) return false;
    double d[3] = {(double)n0.y * n1.z - (double)n0.z * n1.y, (double)n0.z * n1.x - (double)n0.x * n1.z,
                   (double)n0.x * n1.y - (double)n0.y * n1.x};
    /* binary rescaling, keeping the products in range */
    double scale_a = 0.0, scale_b = 0.0;
    for (int i = 0; i < 3; i++) {
        scale_a = fmax(scale_a, fabs(dist_a[i]));
        scale_b = fmax(scale_b, fabs(dist_b[i]));
    }
    if (scale_a == 0.0 || scale_b == 0.0) {
        return coplanar_tri_tri(n0, a, b);
    }
    int ea, eb;
    frexp(scale_a, &ea);
    frexp(scale_b, &eb);
    for (int i = 0; i < 3; i++) {
        dist_a[i] = ldexp(dist_a[i], -ea);
        dist_b[i] = ldexp(dist_b[i], -eb);
    }
    if (dist_a[0] * dist_a[1] > 0.0 && dist_a[0] * dist_a[2] > 0.0) return false;
    int maxc = 0;
    double most = fabs(d[0]);
    if (fabs(d[1]) > most) {
        maxc = 1;
        most = fabs(d[1]);
    }
    if (fabs(d[2]) > most) {
        maxc = 2;
    }
    const double va[3] = {0.0, (double)vcomp(&a1, maxc) - vcomp(&a0, maxc), (double)vcomp(&a2, maxc) - vcomp(&a0, maxc)};
    const double vb[3] = {(double)vcomp(&b0, maxc) - vcomp(&a0, maxc), (double)vcomp(&b1, maxc) - vcomp(&a0, maxc),
                          (double)vcomp(&b2, maxc) - vcomp(&a0, maxc)};
    double abase, anum0, anum1, aden0, aden1, bbase, bnum0, bnum1, bden0, bden1;
    if (!face_interval(va, dist_a, &abase, &anum0, &anum1, &aden0, &aden1) ||
        !face_interval(vb, dist_b, &bbase, &bnum0, &bnum1, &bden0, &bden1)) {
        return coplanar_tri_tri(n0, a, b);
    }
    double aprod = aden0 * aden1, bprod = bden0 * bden1, prod = aprod * bprod;
    double ia0 = bprod * (aden1 * anum0) + abase * prod, ia1 = bprod * (aden0 * anum1) + abase * prod;
    double ib0 = aprod * (bden1 * bnum0) + bbase * prod, ib1 = aprod * (bden0 * bnum1) + bbase * prod;
    if (ia0 > ia1) {
        double t = ia0;
        ia0 = ia1;
        ia1 = t;
    }
    if (ib0 > ib1) {
        double t = ib0;
        ib0 = ib1;
        ib1 = t;
    }
    return ia1 >= ib0 && ib1 >= ia0;
}

static double dot3d(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3d(const double *a, const double *b, double *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* VxDistance::SegmentSegmentSquareDistance of the segments from o0 along d0 and o1 along d1 */
static float segment_segment_square_distance(eng_vector_t o0, eng_vector_t d0, eng_vector_t o1, eng_vector_t d1)
{
    const double u[3] = {d0.x, d0.y, d0.z}, v[3] = {d1.x, d1.y, d1.z};
    const double w[3] = {(double)o0.x - o1.x, (double)o0.y - o1.y, (double)o0.z - o1.z};
    double normal[3];
    cross3d(u, v, normal);
    double den = dot3d(normal, normal), a = dot3d(u, u), c = dot3d(v, v), d = dot3d(w, u), square = 0.0;
    if (a == 0.0) {
        return (float)dot3d(w, w);
    }
    int begin = 0, end = 4;
    if (den == 0.0) {
        begin = d > 0.0 ? 2 : -d >= a ? 3 : 0;
        end = begin + 1;
    }
    square = HUGE_VAL;
    for (int boundary = begin; boundary < end; boundary++) {
        double fixed_s = boundary == 3 ? 1.0 : 0.0, fixed_t = boundary == 1 ? 1.0 : 0.0;
        const double *direction = boundary < 2 ? u : v, *other = boundary < 2 ? v : u;
        double length = boundary < 2 ? a : c, relative[3];
        for (int j = 0; j < 3; j++) {
            relative[j] = w[j] + fixed_s * u[j] - fixed_t * v[j];
        }
        double sign = boundary < 2 ? -1.0 : 1.0, projection = sign * dot3d(relative, direction), candidate, gradient;
        if (projection <= 0.0) {
            gradient = dot3d(relative, other);
            candidate = dot3d(relative, relative);
        } else if (projection >= length) {
            for (int j = 0; j < 3; j++) {
                relative[j] -= sign * direction[j];
            }
            gradient = dot3d(relative, other);
            candidate = dot3d(relative, relative);
        } else {
            double cr[3], ocr[3];
            cross3d(relative, direction, cr);
            candidate = dot3d(cr, cr) / length;
            cross3d(other, direction, ocr);
            gradient = dot3d(cr, ocr) / length;
        }
        bool at_minimum = boundary == 0 || boundary == 3 ? gradient <= 0.0 : gradient >= 0.0;
        if (den == 0.0 || at_minimum) {
            square = candidate;
            break;
        }
    }
    if (den != 0.0) {
        double vw[3], uw[3], end_s[3], end_t[3], cs[3], ct[3];
        cross3d(v, w, vw);
        cross3d(u, w, uw);
        double s = dot3d(vw, normal) / den, t = dot3d(uw, normal) / den;
        for (int j = 0; j < 3; j++) {
            end_s[j] = w[j] + u[j];
            end_t[j] = w[j] - v[j];
        }
        cross3d(v, end_s, cs);
        cross3d(u, end_t, ct);
        if (s >= 0.0 && t >= 0.0 && dot3d(cs, normal) <= 0.0 && dot3d(ct, normal) <= 0.0) {
            double height = dot3d(w, normal);
            square = height * height / den;
        }
    }
    return (float)square;
}

/* ============================================================================
 * Geometry of the entities
 * ============================================================================ */

/* The entity's current mesh; 0 for none */
static nmo_object_id_t current_mesh(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    return state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
}

/* The descendants' boxes of `parent`, in the space `inverse` takes the world to, added to the box */
static void add_child_boxes(nmo_engine_t *engine, int32_t parent, const eng_matrix_t *inverse, bool *any,
                            eng_vector_t *min, eng_vector_t *max)
{
    for (int32_t child = engine->entities.data[parent].first_child; child >= 0;
         child = engine->entities.data[child].next_sibling) {
        eng_vector_t lmin, lmax, bmin, bmax;
        if (eng_entity_local_box(engine, child, &lmin, &lmax)) {
            eng_matrix_t to_root;
            eng_matrix_multiply(&to_root, inverse, &engine->entities.data[child].world);
            box_transform_from(&bmin, &bmax, lmin, lmax, &to_root);
            if (!*any) {
                *min = bmin;
                *max = bmax;
                *any = true;
            } else {
                *min = v3(fminf(min->x, bmin.x), fminf(min->y, bmin.y), fminf(min->z, bmin.z));
                *max = v3(fmaxf(max->x, bmax.x), fmaxf(max->y, bmax.y), fmaxf(max->z, bmax.z));
            }
        }
        add_child_boxes(engine, child, inverse, any, min, max);
    }
}

/* GetHierarchicalBox(TRUE): the entity's and its descendants' boxes in its space */
static bool local_hierarchical_box(nmo_engine_t *engine, int32_t root, eng_vector_t *min, eng_vector_t *max)
{
    bool any = eng_entity_local_box(engine, root, min, max);
    eng_matrix_t inverse;
    eng_matrix_inverse(&inverse, &engine->entities.data[root].world);
    add_child_boxes(engine, root, &inverse, &any, min, max);
    return any;
}

/* A 3D entity's faces (and lines) in its space: its mesh's, or a 3D sprite's two triangles */
typedef struct entity_faces {
    int32_t vertex_count, face_count, line_count;
    const nmo_vertex_t *vertices;
    const uint16_t *indices, *lines;
    const nmo_face_t *faces;
    nmo_vertex_t sprite[4];
    uint16_t sprite_indices[6];
} entity_faces_t;

static bool entity_faces(nmo_engine_t *engine, nmo_object_id_t id, entity_faces_t *out)
{
    memset(out, 0, sizeof(*out));
    eng_sprite3d_t *sprite = eng_sprite3d(engine, id);
    if (sprite != NULL) {
        const float corners[4][2] = {{sprite->min.x, sprite->min.y}, {sprite->min.x, sprite->max.y},
                                     {sprite->max.x, sprite->max.y}, {sprite->max.x, sprite->min.y}};
        for (int k = 0; k < 4; k++) {
            out->sprite[k].position = (nmo_vector_t){corners[k][0], corners[k][1], 0.0f};
        }
        const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        memcpy(out->sprite_indices, indices, sizeof(indices));
        out->vertex_count = 4;
        out->face_count = 2;
        out->vertices = out->sprite;
        out->indices = out->sprite_indices;
        return true;
    }
    nmo_object_id_t mesh_id = current_mesh(engine, id);
    nmo_engine_mesh_t mesh;
    if (mesh_id == 0 || !nmo_engine_mesh(engine, mesh_id, &mesh) || mesh.vertex_count == 0) {
        return false;
    }
    out->vertex_count = mesh.vertex_count;
    out->face_count = mesh.face_count;
    out->vertices = mesh.vertices;
    out->indices = mesh.indices;
    out->faces = mesh.faces;
    nmo_object_t *object = eng_find_object(engine, mesh_id);
    const nmo_mesh_state_t *state = object != NULL
        ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_MESH)
        : NULL;
    if (state != NULL && state->line_indices != NULL) {
        out->line_count = (int32_t)state->line_count;
        out->lines = state->line_indices;
    }
    return true;
}

static eng_vector_t face_vertex(const entity_faces_t *f, int32_t i)
{
    const nmo_vector_t *p = &f->vertices[i].position;
    return v3(p->x, p->y, p->z);
}

/* CKMesh::GetFaceNormal: the one the mesh keeps, built from the face when it keeps none */
static eng_vector_t face_normal(const entity_faces_t *f, int32_t face)
{
    if (f->faces != NULL) {
        const nmo_vector_t *n = &f->faces[face].normal;
        if (n->x != 0.0f || n->y != 0.0f || n->z != 0.0f) {
            return v3(n->x, n->y, n->z);
        }
    }
    if (f->vertices == f->sprite) {
        return v3(0.0f, 0.0f, 1.0f);
    }
    eng_vector_t p0 = face_vertex(f, f->indices[face * 3]), p1 = face_vertex(f, f->indices[face * 3 + 1]);
    eng_vector_t p2 = face_vertex(f, f->indices[face * 3 + 2]);
    eng_vector_t e1 = vsub(p1, p0), e2 = vsub(p2, p0), n = eng_vector_cross(&e1, &e2);
    eng_vector_normalize(&n);
    return n;
}

#define V_RIGHT 0x01
#define V_LEFT 0x02
#define V_TOP 0x04
#define V_BOTTOM 0x08
#define V_FRONT 0x10
#define V_BACK 0x20
#define V_ALL 0x3F

static uint32_t box_flags(eng_vector_t p, eng_vector_t min, eng_vector_t max)
{
    uint32_t flags = 0;
    flags |= p.x < min.x ? V_LEFT : p.x > max.x ? V_RIGHT : 0u;
    flags |= p.y < min.y ? V_BOTTOM : p.y > max.y ? V_TOP : 0u;
    flags |= p.z < min.z ? V_FRONT : p.z > max.z ? V_BACK : 0u;
    return flags;
}

/* CKCollisionManager::BoxFaceIntersection: the first entity's box (a character's root body part's
   hierarchy, or with `hierarchy` its own; local or world) against the second's faces */
static bool box_face_intersection(nmo_engine_t *engine, int32_t first, bool hierarchy, bool local, int32_t second)
{
    eng_matrix_t inverse;
    eng_matrix_identity(&inverse);
    eng_vector_t min, max;
    nmo_object_id_t id = engine->entities.data[first].id;
    if (nmo_engine_object_class(engine, id) == NMO_CID_CHARACTER) {
        int32_t root = eng_entity_index(engine, eng_character_root(engine, id));
        if (root < 0) {
            return false;
        }
        first = root;
        hierarchy = true;
    }
    if (local) {
        eng_matrix_inverse(&inverse, &engine->entities.data[first].world);
        if (hierarchy) {
            local_hierarchical_box(engine, first, &min, &max);
        } else {
            eng_entity_local_box(engine, first, &min, &max);
        }
    } else if (hierarchy) {
        eng_entity_hierarchical_box(engine, first, &min, &max);
    } else {
        eng_entity_world_box(engine, first, &min, &max);
    }
    eng_vector_t pts[8];
    for (int k = 0; k < 8; k++) {
        pts[k] = v3((k & 4) ? max.x : min.x, (k & 2) ? max.y : min.y, (k & 1) ? max.z : min.z);
    }
    entity_faces_t faces;
    if (!entity_faces(engine, engine->entities.data[second].id, &faces) || faces.vertices == faces.sprite ||
        faces.face_count == 0) {
        return false;
    }
    eng_matrix_t m;
    eng_matrix_multiply(&m, &inverse, &engine->entities.data[second].world);
    eng_vector_t *av = (eng_vector_t *)malloc((size_t)faces.vertex_count * sizeof(eng_vector_t));
    uint32_t *flags = (uint32_t *)malloc((size_t)faces.vertex_count * sizeof(uint32_t));
    bool hit = false;
    uint32_t all = V_ALL;
    for (int32_t v = 0; av != NULL && flags != NULL && v < faces.vertex_count; v++) {
        eng_vector_t p = face_vertex(&faces, v);
        eng_vector_transform(&av[v], &m, &p);
        flags[v] = box_flags(av[v], min, max);
        all &= flags[v];
    }
    for (int32_t i = 0; av != NULL && flags != NULL && !all && !hit && i < faces.face_count; i++) {
        const uint16_t *k = &faces.indices[i * 3];
        if (!flags[k[0]] || !flags[k[1]] || !flags[k[2]]) {
            hit = true;
            break;
        }
        if (flags[k[0]] & flags[k[1]] & flags[k[2]]) {
            continue;
        }
        eng_vector_t p0 = av[k[0]], p1 = av[k[1]], p2 = av[k[2]];
        if (segment_box(p0, vsub(p1, p0), min, max) || segment_box(p1, vsub(p2, p1), min, max) ||
            segment_box(p0, vsub(p2, p0), min, max)) {
            hit = true;
            break;
        }
        eng_vector_t n0 = face_normal(&faces, i), normal;
        eng_vector_rotate(&normal, &m, &n0);
        /* the box's diagonal most along the normal */
        int i0 = 0, i1 = 7;
        eng_vector_t diag = vsub(pts[0], pts[7]);
        float most = fabsf(normal.x * diag.x + normal.y * diag.y + normal.z * diag.z);
        for (int pair = 1; pair < 4; pair++) {
            diag = vsub(pts[pair], pts[7 - pair]);
            float dp = fabsf(normal.x * diag.x + normal.y * diag.y + normal.z * diag.z);
            if (dp > most) {
                most = dp;
                i0 = pair;
                i1 = 7 - pair;
            }
        }
        if (segment_face(pts[i0], vsub(pts[i1], pts[i0]), p0, p1, p2, normal)) {
            hit = true;
        }
    }
    free(av);
    free(flags);
    return hit;
}

/* CKCollisionManager::FaceFaceIntersection: a face (or line) of one entity across one of the other; on a
   face against a face, the first's face in `touching` and the second's in `touched` when given */
#define FACE_EPSILON 0.001f
static bool face_face_intersection(nmo_engine_t *engine, int32_t first, int32_t second, int32_t *touching,
                                   int32_t *touched)
{
    eng_vector_t wmin1, wmax1, wmin2, wmax2;
    eng_entity_world_box(engine, first, &wmin1, &wmax1);
    eng_entity_world_box(engine, second, &wmin2, &wmax2);
    if (wmin1.x > wmax2.x + FACE_EPSILON || wmin1.y > wmax2.y + FACE_EPSILON || wmin1.z > wmax2.z + FACE_EPSILON ||
        wmax1.x < wmin2.x - FACE_EPSILON || wmax1.y < wmin2.y - FACE_EPSILON || wmax1.z < wmin2.z - FACE_EPSILON) {
        return false;
    }
    entity_faces_t f1, f2;
    if (!entity_faces(engine, engine->entities.data[first].id, &f1) ||
        !entity_faces(engine, engine->entities.data[second].id, &f2)) {
        return false;
    }
    eng_matrix_t inv1, inv2, m21, m12;
    eng_matrix_inverse(&inv1, &engine->entities.data[first].world);
    eng_matrix_inverse(&inv2, &engine->entities.data[second].world);
    eng_matrix_multiply(&m21, &inv1, &engine->entities.data[second].world);
    eng_matrix_multiply(&m12, &inv2, &engine->entities.data[first].world);
    eng_vector_t lmin1, lmax1, lmin2, lmax2, imin1, imax1, imin2, imax2;
    eng_entity_local_box(engine, first, &lmin1, &lmax1);
    eng_entity_local_box(engine, second, &lmin2, &lmax2);
    if (f1.vertices == f1.sprite) {
        lmin1 = v3(f1.sprite[0].position.x, f1.sprite[0].position.y, 0.0f);
        lmax1 = v3(f1.sprite[2].position.x, f1.sprite[2].position.y, 0.0f);
    }
    if (f2.vertices == f2.sprite) {
        lmin2 = v3(f2.sprite[0].position.x, f2.sprite[0].position.y, 0.0f);
        lmax2 = v3(f2.sprite[2].position.x, f2.sprite[2].position.y, 0.0f);
    }
    const eng_vector_t eps = {FACE_EPSILON, FACE_EPSILON, FACE_EPSILON};
    lmin1 = vsub(lmin1, eps), lmax1 = v3(lmax1.x + eps.x, lmax1.y + eps.y, lmax1.z + eps.z);
    lmin2 = vsub(lmin2, eps), lmax2 = v3(lmax2.x + eps.x, lmax2.y + eps.y, lmax2.z + eps.z);
    box_transform_from(&imin1, &imax1, lmin1, lmax1, &m12); /* the first's box in the second's space */
    box_transform_from(&imin2, &imax2, lmin2, lmax2, &m21);
    int32_t most = f1.vertex_count > f2.vertex_count ? f1.vertex_count : f2.vertex_count;
    /* One flag array for both: the original indexes the first's vertices and the second's alike */
    uint32_t *flags = (uint32_t *)calloc((size_t)most, sizeof(uint32_t));
    eng_vector_t *bv = (eng_vector_t *)malloc((size_t)f2.vertex_count * sizeof(eng_vector_t));
    if (flags == NULL || bv == NULL) {
        free(flags);
        free(bv);
        return false;
    }
    for (int32_t x = 0; x < f2.vertex_count; x++) {
        eng_vector_t p = face_vertex(&f2, x);
        eng_vector_transform(&bv[x], &m21, &p);
    }
    for (int32_t x = 0; x < f1.vertex_count; x++) {
        flags[x] |= box_flags(face_vertex(&f1, x), imin2, imax2);
    }
    for (int32_t x = 0; x < f2.vertex_count; x++) {
        flags[x] |= box_flags(face_vertex(&f2, x), imin1, imax1) << 8; /* the second's own, against the first's box */
    }
    bool hit = false;
    for (int32_t i = 0; !hit && i < f2.face_count; i++) {
        const uint16_t *bk = &f2.indices[i * 3];
        if ((flags[bk[0]] & flags[bk[1]] & flags[bk[2]]) & (V_ALL << 8)) {
            continue;
        }
        eng_vector_t n2 = face_normal(&f2, i), bn;
        eng_vector_rotate(&bn, &m21, &n2);
        for (int32_t j = 0; j < f1.face_count; j++) {
            const uint16_t *ak = &f1.indices[j * 3];
            if ((flags[ak[0]] & flags[ak[1]] & flags[ak[2]]) & V_ALL) {
                continue;
            }
            if (face_face(face_vertex(&f1, ak[0]), face_vertex(&f1, ak[1]), face_vertex(&f1, ak[2]), face_normal(&f1, j),
                          bv[bk[0]], bv[bk[1]], bv[bk[2]], bn)) {
                if (touching != NULL) {
                    *touching = j;
                    *touched = i;
                }
                hit = true;
                break;
            }
        }
    }
    for (int32_t i = 0; !hit && f2.line_count > 0 && i < f1.face_count; i++) {
        const uint16_t *ak = &f1.indices[i * 3];
        if ((flags[ak[0]] & flags[ak[1]] & flags[ak[2]]) & V_ALL) {
            continue;
        }
        eng_vector_t a0 = face_vertex(&f1, ak[0]);
        for (int32_t j = 0; j < f2.line_count; j++) {
            const uint16_t *bk = &f2.lines[j * 2];
            if ((flags[bk[0]] & flags[bk[1]]) & (V_ALL << 8)) {
                continue;
            }
            if (segment_face(bv[bk[0]], vsub(bv[bk[1]], bv[bk[0]]), a0, face_vertex(&f1, ak[1]), face_vertex(&f1, ak[2]),
                             face_normal(&f1, i))) {
                hit = true;
                break;
            }
        }
    }
    for (int32_t i = 0; !hit && f1.line_count > 0 && i < f2.face_count; i++) {
        const uint16_t *bk = &f2.indices[i * 3];
        if ((flags[bk[0]] & flags[bk[1]] & flags[bk[2]]) & (V_ALL << 8)) {
            continue;
        }
        eng_vector_t n2 = face_normal(&f2, i), bn;
        eng_vector_rotate(&bn, &m21, &n2);
        for (int32_t j = 0; j < f1.line_count; j++) {
            const uint16_t *ak = &f1.lines[j * 2];
            if ((flags[ak[0]] & flags[ak[1]]) & V_ALL) {
                continue;
            }
            eng_vector_t a0 = face_vertex(&f1, ak[0]);
            if (segment_face(a0, vsub(face_vertex(&f1, ak[1]), a0), bv[bk[0]], bv[bk[1]], bv[bk[2]], bn)) {
                hit = true;
                break;
            }
        }
    }
    if (!hit && f1.line_count > 0 && f2.line_count > 0) {
        eng_vector_t size = vsub(wmax1, wmin1);
        float threshold = (size.x * size.x + size.y * size.y + size.z * size.z) * 0.0001f;
        for (int32_t i = 0; !hit && i < f2.line_count; i++) {
            const uint16_t *bk = &f2.lines[i * 2];
            if ((flags[bk[0]] & flags[bk[1]]) & (V_ALL << 8)) {
                continue;
            }
            for (int32_t j = 0; j < f1.line_count; j++) {
                const uint16_t *ak = &f1.lines[j * 2];
                if ((flags[ak[0]] & flags[ak[1]]) & V_ALL) {
                    continue;
                }
                eng_vector_t a0 = face_vertex(&f1, ak[0]);
                if (segment_segment_square_distance(a0, vsub(face_vertex(&f1, ak[1]), a0), bv[bk[0]],
                                                    vsub(bv[bk[1]], bv[bk[0]])) < threshold) {
                    hit = true;
                    break;
                }
            }
        }
    }
    free(flags);
    free(bv);
    return hit;
}

/* The VxFrustum of a camera (VxFrustum::Update's planes): the five plane normals FrustumOBB tests */
typedef struct frustum {
    eng_vector_t origin, right, up, dir;
    float rbound, ubound, dmin, dmax, dratio;
    eng_vector_t normals[5]; /* far, bottom, up, left, right */
} frustum_t;

static eng_vector_t plane_normal(eng_vector_t b, eng_vector_t c) /* VxPlane(0, b, c) */
{
    double nx = (double)b.y * c.z - (double)b.z * c.y, ny = (double)b.z * c.x - (double)b.x * c.z;
    double nz = (double)b.x * c.y - (double)b.y * c.x, length = nx * nx + ny * ny + nz * nz;
    if (length == 0.0) {
        return v3(0.0f, 0.0f, 0.0f);
    }
    double inverse = 1.0 / sqrt(length);
    return v3((float)(nx * inverse), (float)(ny * inverse), (float)(nz * inverse));
}

static void frustum_make(frustum_t *f, eng_vector_t origin, eng_vector_t right, eng_vector_t up, eng_vector_t dir,
                         float near_plane, float far_plane, float fov, float aspect)
{
    f->origin = origin, f->right = right, f->up = up, f->dir = dir;
    f->rbound = tanf(fov * 0.5f) * near_plane;
    f->ubound = tanf(fov * 0.5f) * near_plane * aspect;
    f->dmin = near_plane;
    f->dmax = far_plane;
    f->dratio = far_plane / near_plane;
    eng_vector_t n = v3(dir.x * f->dmin, dir.y * f->dmin, dir.z * f->dmin);
    eng_vector_t u = v3(up.x * f->ubound, up.y * f->ubound, up.z * f->ubound);
    eng_vector_t r = v3(right.x * f->rbound, right.y * f->rbound, right.z * f->rbound);
    eng_vector_t nbl = vsub(vsub(n, r), u), ntl = v3(n.x - r.x + u.x, n.y - r.y + u.y, n.z - r.z + u.z);
    eng_vector_t nbr = v3(n.x + r.x - u.x, n.y + r.y - u.y, n.z + r.z - u.z);
    eng_vector_t ntr = v3(n.x + r.x + u.x, n.y + r.y + u.y, n.z + r.z + u.z);
    f->normals[0] = dir;
    f->normals[1] = plane_normal(nbr, nbl);
    f->normals[2] = plane_normal(ntl, ntr);
    f->normals[3] = plane_normal(nbl, ntl);
    f->normals[4] = plane_normal(ntr, nbr);
}

static double ddot(eng_vector_t a, eng_vector_t b)
{
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}

/* VxIntersect::FrustumOBB: no separating axis among the box's three and the frustum's five */
static bool frustum_obb(const frustum_t *f, eng_vector_t min, eng_vector_t max, const eng_matrix_t *m)
{
    eng_vector_t half = v3((max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f);
    eng_vector_t center = v3((max.x + min.x) * 0.5f, (max.y + min.y) * 0.5f, (max.z + min.z) * 0.5f), world;
    eng_vector_transform(&world, m, &center);
    eng_vector_t rel = vsub(world, f->origin);
    eng_vector_t axes[3];
    float halves[3] = {half.x, half.y, half.z};
    for (int i = 0; i < 3; i++) {
        double x = m->m[i][0], y = m->m[i][1], z = m->m[i][2], length = sqrt(x * x + y * y + z * z);
        axes[i] = length != 0.0 ? v3((float)(x / length), (float)(y / length), (float)(z / length)) : v3((float)x, (float)y, (float)z);
        halves[i] = (float)((double)halves[i] * length);
    }
    const eng_vector_t tests[8] = {axes[0], axes[1], axes[2], f->normals[0], f->normals[1], f->normals[2],
                                   f->normals[3], f->normals[4]};
    for (int t = 0; t < 8; t++) {
        eng_vector_t axis = tests[t];
        double c = ddot(axis, rel);
        double r = fabs(ddot(axis, axes[0])) * halves[0] + fabs(ddot(axis, axes[1])) * halves[1] +
                   fabs(ddot(axis, axes[2])) * halves[2];
        double bound = fabs(ddot(axis, f->up)) * f->ubound + fabs(ddot(axis, f->right)) * f->rbound;
        double d = ddot(axis, f->dir) * f->dmin, lo = d - bound, hi = d + bound;
        if (lo < 0.0) lo *= f->dratio;
        if (hi > 0.0) hi *= f->dratio;
        if (c + r < lo || c - r > hi) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * The blocks
 * ============================================================================ */

/*
 * Sphere Sphere Intersection: True when the two entities' spheres meet: each
 * its mesh's sphere in the world (CK3dEntity::GetBaryCenter and GetRadius),
 * or with its Hierarchy input the sphere around its hierarchy's world box.
 */
static void entity_sphere(nmo_engine_t *engine, int32_t index, bool hierarchy, eng_vector_t *center, float *radius)
{
    if (hierarchy) {
        eng_vector_t min, max;
        eng_entity_hierarchical_box(engine, index, &min, &max);
        eng_vector_t size = vsub(max, min);
        *radius = 0.5f * eng_vector_magnitude(&size);
        *center = v3((max.x + min.x) * 0.5f, (max.y + min.y) * 0.5f, (max.z + min.z) * 0.5f);
    } else {
        *radius = eng_entity_radius(engine, index);
        eng_entity_bary_center(engine, index, center);
    }
}

static int bb_sphere_sphere_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    if (first < 0 || second < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    eng_vector_t c1, c2;
    float r1, r2;
    entity_sphere(b->engine, first, get_in_int(b, 2, 0) != 0, &c1, &r1);
    entity_sphere(b->engine, second, get_in_int(b, 3, 0) != 0, &c2, &r2);
    eng_vector_t d = vsub(c2, c1);
    float reach = (r1 + r2) * (r1 + r2);
    OUT_ON(b, reach < d.x * d.x + d.y * d.y + d.z * d.z ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Test Bounding Box Y Overlapping (obsolete): how the two entities' world
 * boxes stand along Y: A above B, B above A, A above B overlapping, B above
 * A overlapping, or overlapped (one within the other's height).
 */
static int bb_test_bounding_box_y_overlapping(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t a = input_entity(b, 0);
    if (a < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t c = input_entity(b, 1);
    if (c < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t amin, amax, bmin, bmax;
    eng_entity_world_box(b->engine, a, &amin, &amax);
    eng_entity_world_box(b->engine, c, &bmin, &bmax);
    int out = amin.y > bmax.y ? 0
            : bmin.y > amax.y ? 1
            : (amin.y > bmin.y && amax.y > bmax.y) ? 2
            : (bmin.y > amin.y && bmax.y > amax.y) ? 3
                                                    : 4;
    OUT_ON(b, out);
    return NMO_ENGINE_BR_OK;
}

/*
 * Frustum Object Intersection: True when the Object's box (with Local its
 * local box through its world matrix, else its world box; with Hierarchy its
 * hierarchy's) meets the target camera's view frustum.
 */
static int bb_frustum_object_intersection(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t cam = camera != 0 ? eng_entity_index(engine, camera) : -1;
    int32_t ent = input_entity(b, 0);
    if (cam < 0 || ent < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    bool local = get_in_int(b, 1, 0) != 0, hierarchy = get_in_int(b, 2, 0) != 0;
    eng_vector_t dir, up, right, pos = eng_entity_position(engine, cam, -1);
    eng_entity_orientation(engine, cam, &dir, &up, &right, -1);
    float fov = 0.5f, near_plane = 1.0f, far_plane = 4000.0f;
    eng_camera_fov(engine, camera, &fov);
    eng_camera_planes(engine, camera, &near_plane, &far_plane);
    nmo_object_t *object = eng_find_object(engine, camera);
    const nmo_camera_state_t *state = object != NULL
        ? (const nmo_camera_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_CAMERA)
        : NULL;
    int32_t width = state != NULL ? state->aspect_width : 4, height = state != NULL ? state->aspect_height : 3;
    frustum_t frustum;
    frustum_make(&frustum, pos, right, up, dir, near_plane, far_plane, fov, (float)height / (float)width);
    eng_vector_t min, max;
    eng_matrix_t identity;
    eng_matrix_identity(&identity);
    if (local) {
        if (hierarchy) {
            local_hierarchical_box(engine, ent, &min, &max);
        } else {
            eng_entity_local_box(engine, ent, &min, &max);
        }
    } else if (hierarchy) {
        eng_entity_hierarchical_box(engine, ent, &min, &max);
    } else {
        eng_entity_world_box(engine, ent, &min, &max);
    }
    bool meets = frustum_obb(&frustum, min, max, local ? &engine->entities.data[ent].world : &identity);
    OUT_ON(b, meets ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Box Face Intersection: True when Entity 1's local box (with Hierarchy 1
 * its hierarchy's; a character's body parts') meets one of Entity 2's
 * faces.
 */
static int bb_box_face_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    bool hit = first >= 0 && second >= 0 &&
               box_face_intersection(b->engine, first, get_in_int(b, 2, 0) != 0, true, second);
    OUT_ON(b, hit ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Face Face Intersection: True when a face (or line) of Entity 1 crosses one
 * of Entity 2 (a 3D sprite is its quad's two triangles).
 */
static int bb_face_face_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    if (first < 0 || second < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    bool hit = face_face_intersection(b->engine, first, second, NULL, NULL);
    nmo_engine_block_activate_output(b, 0, hit);
    nmo_engine_block_activate_output(b, 1, !hit);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Floors
 * ============================================================================ */

/* CK3dEntity::GetBoundingBox: a character's is its hierarchy's; at the entity's position when nothing
   there has geometry */
static void bounding_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    bool valid = nmo_engine_object_class(engine, engine->entities.data[index].id) == NMO_CID_CHARACTER
                     ? eng_entity_hierarchical_box(engine, index, min, max)
                     : eng_entity_world_box(engine, index, min, max);
    if (!valid) {
        *min = *max = eng_entity_position(engine, index, -1);
    }
}

/* The floor CK_FLOORNEAREST names and its distance */
static nmo_object_id_t nearest_floor(int nearest, const eng_floor_point_t *point, float *distance)
{
    *distance = nearest == CKFLOOR_UP ? point->up_distance : point->down_distance;
    return nearest == CKFLOOR_UP ? point->up_floor : nearest == CKFLOOR_DOWN ? point->down_floor : 0;
}

/*
 * Declare Floors: every 3D entity of the level whose name holds Key Substring
 * gets the Floor attribute, with the Attributes structure's fields.
 */
static int bb_declare_floors(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const char *key = input_string(b, 0);
    eng_floor_settings_t settings = {CKFLOOR_FACES, 1, 0, 0, 1};
    eng_param_t *attributes = input_source(b, 1, true);
    if (attributes != NULL && attributes->data != NULL) {
        int32_t *fields[5] = {&settings.geometry, &settings.moving, &settings.type, &settings.hierarchy,
                              &settings.first};
        for (uint32_t i = 0; i < 5; i++) {
            eng_struct_field(engine, attributes->id, attributes->data, attributes->size, i, fields[i]);
        }
    }
    for (size_t i = 0; key != NULL && i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        const char *name = entity->destroyed ? NULL : nmo_engine_object_name(engine, entity->id);
        if (name != NULL && strstr(name, key) != NULL) {
            eng_floor_add(engine, entity->id, &settings);
        }
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Floor Manager Setup: the limit angle (the cache settings do nothing) */
static int bb_floor_manager_setup(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float angle = get_in_float(b, 0, 0.0f);
    b->engine->floors.cache_size = get_in_int(b, 1, 8);
    b->engine->floors.cache_threshold = get_in_float(b, 2, 0.1f);
    eng_floor_set_limit_angle(b->engine, angle);
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Nearest Floors: the floors above and below Position (in Referential's
 * space); both floors' outputs are written whichever is nearer, the Floor
 * Type of a missing one being the other's.
 */
static int bb_get_nearest_floors(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (target_entity(b) < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    eng_vector_t pos = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), local = pos;
    int32_t ref = input_entity(b, 1);
    if (ref >= 0) {
        eng_entity_transform(engine, ref, &pos, &local, -1);
    }
    eng_floor_point_t point;
    int nearest = eng_floor_nearest(engine, &pos, &point, 0);
    if (nearest == CKFLOOR_NOFLOOR) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    eng_floor_settings_t settings = {0, 0, 0, 0, 0};
    set_out(b, 0, &point.down_floor, sizeof(point.down_floor));
    eng_floor_read(engine, point.down_floor, &settings);
    set_out_int(b, 1, settings.type);
    set_out(b, 2, &point.down_normal, sizeof(point.down_normal));
    set_out_float(b, 3, point.down_distance);
    set_out_int(b, 4, point.down_face);
    set_out(b, 5, &point.up_floor, sizeof(point.up_floor));
    eng_floor_read(engine, point.up_floor, &settings);
    set_out_int(b, 6, settings.type);
    set_out(b, 7, &point.up_normal, sizeof(point.up_normal));
    set_out_float(b, 8, point.up_distance);
    set_out_int(b, 9, point.up_face);
    OUT_ON(b, nearest == CKFLOOR_DOWN ? 1 : 2);
    return NMO_ENGINE_BR_OK;
}

/* Object Keep On Floor: the object moved up or down onto the floor nearest its box's bottom center */
static int bb_object_keep_on_floor(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_vector_t min, max;
    bounding_box(engine, entity, &min, &max);
    eng_vector_t pos = v3((max.x + min.x) * 0.5f, min.y, (max.z + min.z) * 0.5f);
    eng_floor_point_t point;
    float distance;
    if (nearest_floor(eng_floor_nearest(engine, &pos, &point, 0), &point, &distance) != 0) {
        eng_vector_t move = v3(0.0f, distance, 0.0f);
        eng_entity_translate(engine, entity, &move, -1, false);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Character Keep On Floor: the character moved onto the floors under its
 * floor reference object (else under its box's bottom center): onto the upper
 * floor whenever there is one, nearer or not.
 */
static int bb_character_keep_on_floor(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t character = eng_entity_index(engine, owner);
    IN_OFF(b, 0);
    if (character >= 0) {
        int32_t reference = eng_entity_index(engine, eng_character_floor_reference(engine, owner));
        eng_vector_t pos, min, max;
        if (reference >= 0) {
            pos = eng_entity_position(engine, reference, -1);
        } else {
            bounding_box(engine, character, &min, &max);
            pos = v3((max.x + min.x) * 0.5f, min.y, (max.z + min.z) * 0.5f);
        }
        eng_floor_point_t point;
        if (eng_floor_nearest(engine, &pos, &point, 0) != CKFLOOR_NOFLOOR) {
            pos = eng_entity_position(engine, character, -1);
            if (point.up_floor != 0) {
                pos.y += point.up_distance;
            } else if (point.down_floor != 0) {
                pos.y += point.down_distance;
            }
            eng_entity_set_position(engine, character, &pos, -1, false);
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Floor Slider (later SDK, not in Ballance's DLL): the object kept a Radius
 * inside the borders of the floors it moves over, from where it was last
 * frame (the local, at the origin at first).
 */
static int bb_floor_slider(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ball = target_entity(b);
    if (ball < 0) {
        if (IN_ACTIVE(b, 0)) {
            IN_OFF(b, 0);
        }
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t pos = eng_entity_position(engine, ball, -1), old = pos, out;
    nmo_engine_block_get_local_value(b, 0, &old, sizeof(old));
    float radius = get_in_float(b, 0, 1.0f);
    bool touched = eng_floor_constrain(engine, &old, &pos, radius, &out, -1);
    if (touched) {
        eng_entity_set_position(engine, ball, &out, -1, false);
        nmo_engine_block_set_local_value(b, 0, &out, sizeof(out));
    } else {
        nmo_engine_block_set_local_value(b, 0, &pos, sizeof(pos));
    }
    OUT_ON(b, touched ? 0 : 1);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Keeping objects within the floors (FloorTools)
 * ============================================================================ */

#define FLOOR_FACES_MAX 64 /* FACESMAXNUMBER */

/* VxIntersect::SegmentPlane, the plane VxPlane::Create(n, point) makes */
static bool segment_plane(eng_vector_t o, eng_vector_t d, eng_vector_t n, eng_vector_t point, eng_vector_t *out)
{
    float plane_d = (float)-((double)n.x * point.x + (double)n.y * point.y + (double)n.z * point.z);
    double den = (double)n.x * d.x + (double)n.y * d.y + (double)n.z * d.z;
    if (fabs(den) < VX_EPSILON) {
        return false;
    }
    double t = -((double)n.x * o.x + (double)n.y * o.y + (double)n.z * o.z + plane_d) / den;
    if (t < -VX_EPSILON || t > 1.0 + VX_EPSILON) {
        return false;
    }
    *out = v3((float)(o.x + (double)d.x * t), (float)(o.y + (double)d.y * t), (float)(o.z + (double)d.z * t));
    return true;
}

/* Normalize(v): v by the inverse of its magnitude */
static eng_vector_t normalized(eng_vector_t v)
{
    double inv = 1.0 / sqrt((double)(v.x * v.x + v.y * v.y + v.z * v.z));
    return v3((float)(v.x * inv), (float)(v.y * inv), (float)(v.z * inv));
}

/* CrossedFaces: the faces of the floor whose extents, across its axis nearest its up, meet the segment's */
static int crossed_faces(nmo_engine_t *engine, nmo_object_id_t floor, eng_vector_t a, eng_vector_t b, int32_t *faces,
                         int max)
{
    int32_t index = eng_entity_index(engine, floor);
    entity_faces_t f;
    if (index < 0 || !entity_faces(engine, floor, &f) || f.vertices == f.sprite) {
        return 0;
    }
    eng_vector_t la, lb, up;
    eng_entity_inverse_transform(engine, index, &la, &a, -1);
    eng_entity_inverse_transform(engine, index, &lb, &b, -1);
    eng_entity_orientation(engine, index, NULL, &up, NULL, -1);
    int x = 1, z = 2;
    float most = fabsf(up.x);
    if (fabsf(up.y) > most) {
        most = fabsf(up.y);
        x = 0;
    }
    if (fabsf(up.z) > most) {
        x = 0;
        z = 1;
    }
    float rx0 = fminf(vcomp(&la, x), vcomp(&lb, x)), rx1 = fmaxf(vcomp(&la, x), vcomp(&lb, x));
    float rz0 = fminf(vcomp(&la, z), vcomp(&lb, z)), rz1 = fmaxf(vcomp(&la, z), vcomp(&lb, z));
    int count = 0;
    for (int32_t i = 0; i < f.face_count; i++) {
        eng_vector_t p0 = face_vertex(&f, f.indices[i * 3]), p1 = face_vertex(&f, f.indices[i * 3 + 1]);
        eng_vector_t p2 = face_vertex(&f, f.indices[i * 3 + 2]);
        float lo = fminf(vcomp(&p0, x), fminf(vcomp(&p1, x), vcomp(&p2, x)));
        float hi = fmaxf(vcomp(&p0, x), fmaxf(vcomp(&p1, x), vcomp(&p2, x)));
        if (rx1 < lo || rx0 > hi) {
            continue;
        }
        lo = fminf(vcomp(&p0, z), fminf(vcomp(&p1, z), vcomp(&p2, z)));
        hi = fmaxf(vcomp(&p0, z), fmaxf(vcomp(&p1, z), vcomp(&p2, z)));
        if (rz1 < lo || rz0 > hi) {
            continue;
        }
        if (count >= max) {
            return count;
        }
        faces[count++] = i;
    }
    return count;
}

/*
 * RayExtrudedFacesIntersection: the segment through the vertical planes on
 * the face's edges, the crossing nearest its end (the square distance
 * returned) with that edge. As in the DLL, each plane is made with the
 * edge's first vertex as its normal and the edge's normal as its point.
 */
static float ray_extruded_face(nmo_engine_t *engine, nmo_object_id_t floor, eng_vector_t p1, eng_vector_t p2,
                               int32_t face, eng_vector_t *intersection, eng_vector_t *edge1, eng_vector_t *edge2)
{
    int32_t index = eng_entity_index(engine, floor);
    entity_faces_t f;
    float best = 10000000.0f;
    if (index < 0 || !entity_faces(engine, floor, &f)) {
        return best;
    }
    eng_vector_t v[3];
    for (int k = 0; k < 3; k++) {
        eng_vector_t local = face_vertex(&f, f.indices[face * 3 + k]);
        eng_entity_transform(engine, index, &v[k], &local, -1);
    }
    /* (a, b) with a as the plane's normal, (a, c) with c, (b, c) with b */
    const int edges[3][3] = {{0, 1, 0}, {0, 2, 2}, {1, 2, 1}};
    eng_vector_t d = vsub(p2, p1);
    for (int e = 0; e < 3; e++) {
        eng_vector_t a = v[edges[e][0]], c = v[edges[e][1]], inter;
        eng_vector_t norm = v3(c.z - a.z, 0.0f, a.x - c.x);
        eng_vector_normalize(&norm);
        if (!segment_plane(p1, d, v[edges[e][2]], norm, &inter)) {
            continue;
        }
        double lo = fmin(a.x, c.x), hi = fmax(a.x, c.x);
        if (inter.x < lo - 0.001 || inter.x > hi + 0.001) {
            continue;
        }
        eng_vector_t to_end = vsub(p2, inter);
        float sm = to_end.x * to_end.x + to_end.y * to_end.y + to_end.z * to_end.z;
        if (sm < best) {
            best = sm;
            *intersection = inter;
            *edge1 = a;
            *edge2 = c;
        }
    }
    return best;
}

/* CK3dEntity::GetBoundingBox(TRUE): a character's is its hierarchy's; empty at the entity's origin when
   nothing there has geometry */
static void local_bounding_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    bool valid = nmo_engine_object_class(engine, engine->entities.data[index].id) == NMO_CID_CHARACTER
                     ? local_hierarchical_box(engine, index, min, max)
                     : eng_entity_local_box(engine, index, min, max);
    if (!valid) {
        *min = *max = v3(0.0f, 0.0f, 0.0f);
    }
}

/* VxBbox::TransformTo: the box's corners through a matrix; 0, 1, 4 and 5 are its bottom ones */
static void box_corners(eng_vector_t min, eng_vector_t max, const eng_matrix_t *m, eng_vector_t pts[8])
{
    eng_vector_transform(&pts[0], m, &min);
    float size[3] = {max.x - min.x, max.y - min.y, max.z - min.z};
    eng_vector_t axis[3];
    for (int k = 0; k < 3; k++) {
        axis[k] = v3(size[k] * m->m[k][0], size[k] * m->m[k][1], size[k] * m->m[k][2]);
    }
    pts[1] = v3(pts[0].x + axis[2].x, pts[0].y + axis[2].y, pts[0].z + axis[2].z);
    pts[2] = v3(pts[0].x + axis[1].x, pts[0].y + axis[1].y, pts[0].z + axis[1].z);
    pts[3] = v3(pts[2].x + axis[2].x, pts[2].y + axis[2].y, pts[2].z + axis[2].z);
    pts[4] = v3(pts[0].x + axis[0].x, pts[0].y + axis[0].y, pts[0].z + axis[0].z);
    pts[5] = v3(pts[4].x + axis[2].x, pts[4].y + axis[2].y, pts[4].z + axis[2].z);
    pts[6] = v3(pts[4].x + axis[1].x, pts[4].y + axis[1].y, pts[4].z + axis[1].z);
    pts[7] = v3(pts[6].x + axis[2].x, pts[6].y + axis[2].y, pts[6].z + axis[2].z);
}

static const int bottom_corners[4] = {0, 1, 4, 5};

/* The bottom corner's bit of the corners off the floors */
static int corner_bit(int i)
{
    return i == 0 ? 1 : i == 1 ? 2 : i == 4 ? 4 : 8;
}

/* Test4Points: the bottom corners' floors, looked for (detection offset) higher on every axis; their
   bits when off the floors */
static int test_4_points(nmo_engine_t *engine, eng_vector_t pts[8], eng_floor_point_t fps[6], int floors[6],
                         float det)
{
    int off = 0;
    for (int k = 0; k < 4; k++) {
        int i = bottom_corners[k];
        pts[i] = v3(det + pts[i].x, det + pts[i].y, det + pts[i].z);
        floors[i] = eng_floor_nearest(engine, &pts[i], &fps[i], 0);
        if (floors[i] == CKFLOOR_UP) {
            fps[i].up_distance -= det;
        } else if (floors[i] == CKFLOOR_DOWN) {
            fps[i].down_distance -= det;
        } else {
            off |= corner_bit(i);
        }
        pts[i] = v3(pts[i].x - det, pts[i].y - det, pts[i].z - det);
    }
    return off;
}

/* The box's bottom orientation from its bottom corners: dir along Z, right along X */
static void corners_orientation(const eng_vector_t pts[8], eng_vector_t *dir, eng_vector_t *up, eng_vector_t *right)
{
    *dir = normalized(v3(pts[5].x + pts[1].x - pts[0].x - pts[4].x, pts[5].y + pts[1].y - pts[0].y - pts[4].y,
                         pts[5].z + (pts[1].z - pts[0].z) - pts[4].z));
    *right = normalized(v3(pts[5].x + pts[4].x - pts[0].x - pts[1].x, pts[5].y + pts[4].y - pts[0].y - pts[1].y,
                           pts[5].z + (pts[4].z - pts[0].z) - pts[1].z));
    *up = normalized(eng_vector_cross(dir, right));
}

/* The distance to a corner's floor, the nearest up one winning over any down one */
static void corner_floor(int floorn, const eng_floor_point_t *fp, eng_vector_t *pt, float det, float *distmin,
                         bool *up)
{
    if (floorn == CKFLOOR_DOWN) {
        pt->y = fp->down_distance + pt->y + det;
        if (!*up && fp->down_distance > *distmin) {
            *distmin = fp->down_distance;
        }
    } else if (floorn == CKFLOOR_UP) {
        pt->y = fp->up_distance + pt->y + det;
        if (fp->up_distance > *distmin) {
            *distmin = fp->up_distance;
            *up = true;
        }
    }
}

typedef struct keep_on_floor_inputs {
    bool follow, keep_children, keep_in;
    float offset, det, attenuation;
} keep_on_floor_inputs_t;

/* The new up of a followed inclination: right and dir rebuilt around the floor's normal */
static void follow_floor(nmo_engine_t *engine, int32_t entity, eng_vector_t up, eng_vector_t vup, bool keep_children)
{
    eng_vector_t dir, right;
    eng_entity_orientation(engine, entity, &dir, NULL, NULL, -1);
    right = normalized(eng_vector_cross(&up, &dir));
    dir = normalized(eng_vector_cross(&right, &up));
    eng_entity_set_orientation(engine, entity, &dir, &vup, &right, -1, keep_children);
}

/* Object Keep On Floor V2 with Point extents: the floors under the bottom center of the object's box */
static int keep_on_floor_point(blk_t *b, int32_t entity, const keep_on_floor_inputs_t *in, eng_floor_point_t *fp,
                               float *distance)
{
    nmo_engine_t *engine = b->engine;
    eng_vector_t min, max;
    bounding_box(engine, entity, &min, &max);
    eng_vector_t pos = v3((min.x + max.x) * 0.5f, in->det + min.y, (min.z + max.z) * 0.5f);
    int floorn = eng_floor_nearest(engine, &pos, fp, 0);
    if (floorn != CKFLOOR_NOFLOOR) {
        eng_vector_t up = floorn == CKFLOOR_DOWN ? fp->down_normal : fp->up_normal;
        float dist = floorn == CKFLOOR_DOWN ? fp->down_distance : fp->up_distance;
        pos = eng_entity_position(engine, entity, -1);
        pos.y = dist + pos.y + in->det + in->offset;
        eng_entity_set_position(engine, entity, &pos, -1, in->keep_children);
        if (in->follow) {
            eng_vector_t dir, vup, right;
            eng_entity_orientation(engine, entity, &dir, &vup, &right, -1);
            float rest = 1.0f - in->attenuation;
            vup = v3(up.x * in->attenuation + vup.x * rest, up.y * in->attenuation + vup.y * rest,
                     up.z * in->attenuation + vup.z * rest);
            right = normalized(eng_vector_cross(&up, &dir));
            dir = normalized(eng_vector_cross(&right, &up));
            eng_entity_set_orientation(engine, entity, &dir, &vup, &right, -1, in->keep_children);
        }
    } else {
        OUT_ON(b, 2);
        /* "Keep In Floor Boundary": back over the edge the bottom center crossed since last frame
           (the DLL's flag test is always true) */
        if (in->keep_in) {
            eng_vector_t newpos = v3((min.x + max.x) * 0.5f, min.y, (min.z + max.z) * 0.5f);
            eng_matrix_t world = engine->entities.data[entity].world;
            eng_matrix_t last = *eng_entity_last_frame_matrix(engine, entity);
            eng_entity_set_world_matrix(engine, entity, &last, false);
            eng_vector_t lmin, lmax;
            bounding_box(engine, entity, &lmin, &lmax);
            eng_vector_t oldpos = v3((lmin.x + lmax.x) * 0.5f, in->det + lmin.y, (lmin.z + lmax.z) * 0.5f);
            eng_entity_set_world_matrix(engine, entity, &world, false);
            floorn = eng_floor_nearest(engine, &oldpos, fp, 0);
            if (floorn == CKFLOOR_NOFLOOR) {
                eng_entity_set_world_matrix(engine, entity, &last, false);
            } else {
                nmo_object_id_t floor = floorn == CKFLOOR_DOWN ? fp->down_floor : fp->up_floor;
                int32_t faces[FLOOR_FACES_MAX];
                for (int failure = 0; failure < 8;) {
                    int count = crossed_faces(engine, floor, oldpos, newpos, faces, FLOOR_FACES_MAX);
                    if (count == 0) {
                        break;
                    }
                    float best = 100000.0f;
                    bool crossed = false;
                    eng_vector_t inter, e1, e2, intersection = v3(0, 0, 0), edge1 = v3(0, 0, 0), edge2 = v3(0, 0, 0);
                    for (int i = 0; i < count; i++) {
                        float sm = ray_extruded_face(engine, floor, oldpos, newpos, faces[i], &inter, &e1, &e2);
                        if (sm < best) {
                            best = sm;
                            intersection = inter;
                            edge1 = e1;
                            edge2 = e2;
                            crossed = true;
                        }
                    }
                    if (crossed) {
                        eng_vector_t edge = normalized(vsub(edge2, edge1));
                        eng_vector_t from = vsub(newpos, intersection);
                        float ps = edge.x * from.x + edge.y * from.y + edge.z * from.z;
                        newpos = v3(edge.x * ps + intersection.x + (oldpos.x - pos.x) * 0.01f,
                                    edge.y * ps + intersection.y + (oldpos.y - pos.y) * 0.01f,
                                    edge.z * ps + intersection.z + (oldpos.z - pos.z) * 0.01f);
                    } else {
                        newpos = oldpos;
                    }
                    newpos.y = min.y;
                    floorn = eng_floor_nearest(engine, &newpos, fp, 0);
                    failure++;
                    if (floorn != CKFLOOR_NOFLOOR) {
                        break;
                    }
                }
                eng_vector_t up = v3(0.0f, 0.0f, 0.0f);
                if (floorn != CKFLOOR_NOFLOOR) {
                    up = floorn == CKFLOOR_DOWN ? fp->down_normal : fp->up_normal;
                    float dist = floorn == CKFLOOR_DOWN ? fp->down_distance : fp->up_distance;
                    newpos.y = newpos.y + dist + in->det + in->offset;
                }
                newpos.y = (max.y - min.y) * 0.5f + newpos.y;
                eng_entity_set_position(engine, entity, &newpos, -1, in->keep_children);
                if (in->follow) {
                    follow_floor(engine, entity, up, up, in->keep_children);
                }
            }
        }
    }
    *distance = floorn == CKFLOOR_DOWN ? fp->down_distance : floorn == CKFLOOR_UP ? fp->up_distance : 0.0f;
    return floorn;
}

/*
 * Object Keep On Floor V2 with Bounding Box extents: the floors under the
 * four bottom corners of the object's local box, the object raised to the
 * highest; with Keep In Floor Boundary, corners off the floors are pushed
 * back over the edges they crossed since last frame.
 */
static int keep_on_floor_box(blk_t *b, int32_t entity, const keep_on_floor_inputs_t *in, eng_floor_point_t *fp,
                             float *distance)
{
    nmo_engine_t *engine = b->engine;
    eng_vector_t min, max, pts[8];
    local_bounding_box(engine, entity, &min, &max);
    box_corners(min, max, &engine->entities.data[entity].world, pts);
    int off = 0, floorn = CKFLOOR_NOFLOOR;
    bool up = false;
    float distmin = -1000000000.0f;
    for (int k = 0; k < 4; k++) {
        int i = bottom_corners[k];
        pts[i].y = in->det + pts[i].y;
        floorn = eng_floor_nearest(engine, &pts[i], fp, 0);
        if (floorn != CKFLOOR_NOFLOOR) {
            corner_floor(floorn, fp, &pts[i], in->det, &distmin, &up);
        } else {
            off |= corner_bit(i);
        }
        pts[i].y = pts[i].y - in->det;
    }
    eng_vector_t dir, vup, right;
    if (off == 0) {
        corners_orientation(pts, &dir, &vup, &right);
        if (in->follow) {
            eng_entity_set_orientation(engine, entity, &dir, &vup, &right, -1, in->keep_children);
        }
        eng_vector_t move = v3(0.0f, distmin + in->det + in->offset, 0.0f);
        eng_entity_translate(engine, entity, &move, -1, in->keep_children);
        if (floorn == CKFLOOR_UP) {
            fp->up_normal = vup;
        } else if (floorn == CKFLOOR_DOWN) {
            fp->down_normal = vup;
        }
        *distance = distmin;
        return floorn;
    }
    OUT_ON(b, 2);
    if (in->keep_in) { /* the DLL's flag test is always true */
        eng_matrix_t old = *eng_entity_last_frame_matrix(engine, entity);
        eng_vector_t oldpts[8];
        eng_floor_point_t oldfps[6], fps[6];
        int oldfloors[6] = {0}, floors[6] = {0};
        memset(oldfps, 0, sizeof(oldfps));
        box_corners(min, max, &old, oldpts);
        for (int k = 0; k < 4; k++) {
            int i = bottom_corners[k];
            floorn = eng_floor_nearest(engine, &oldpts[i], &oldfps[i], 0);
            oldfloors[i] = floorn;
            if (floorn == CKFLOOR_NOFLOOR) {
                break;
            }
        }
        if (floorn != CKFLOOR_NOFLOOR) {
            memset(fps, 0, sizeof(fps));
            bool outside = true;
            int tries = 0;
            int32_t faces[FLOOR_FACES_MAX];
            do {
                float max_distance = 0.0f;
                int max_i = 6;
                eng_vector_t max_edge1 = v3(0, 0, 0), max_edge2 = v3(0, 0, 0), max_intersection = v3(0, 0, 0);
                for (int k = 0; k < 4; k++) {
                    int i = bottom_corners[k];
                    if (!(off & corner_bit(i))) {
                        continue;
                    }
                    nmo_object_id_t floor = oldfloors[i] == CKFLOOR_UP     ? oldfps[i].up_floor
                                            : oldfloors[i] == CKFLOOR_DOWN ? oldfps[i].down_floor
                                                                           : 0;
                    if (eng_entity_index(engine, floor) < 0) {
                        continue;
                    }
                    int count = crossed_faces(engine, floor, oldpts[i], pts[i], faces, FLOOR_FACES_MAX);
                    if (count == 0) {
                        break; /* "No Faces crossed" */
                    }
                    float best = 100000.0f;
                    bool crossed = false;
                    eng_vector_t inter = v3(0, 0, 0), e1 = v3(0, 0, 0), e2 = v3(0, 0, 0), intersection = v3(0, 0, 0);
                    for (int j = 0; j < count; j++) {
                        float sm = ray_extruded_face(engine, floor, oldpts[i], pts[i], faces[j], &inter, &e1, &e2);
                        if (sm < best) {
                            best = sm;
                            intersection = inter;
                            crossed = true;
                        }
                    }
                    /* The edge kept is the last face's, as in the DLL */
                    if (crossed && best > max_distance) {
                        max_distance = best;
                        max_edge1 = e1;
                        max_edge2 = e2;
                        max_intersection = intersection;
                        max_i = i;
                    }
                }
                if (max_i < 6) {
                    eng_vector_t edge = normalized(vsub(max_edge2, max_edge1));
                    eng_vector_t from = vsub(pts[max_i], max_intersection);
                    float ps = edge.x * from.x + edge.y * from.y + edge.z * from.z;
                    eng_vector_t move = v3(edge.x * ps + max_intersection.x + (oldpts[max_i].x - pts[max_i].x) * 0.1f - pts[max_i].x,
                                           edge.y * ps + max_intersection.y + (oldpts[max_i].y - pts[max_i].y) * 0.1f - pts[max_i].y,
                                           edge.z * ps + max_intersection.z + (oldpts[max_i].z - pts[max_i].z) * 0.1f - pts[max_i].z);
                    for (int k = 0; k < 4; k++) {
                        int j = bottom_corners[k];
                        pts[j] = v3(move.x + pts[j].x, move.y + pts[j].y, move.z + pts[j].z);
                    }
                    eng_entity_translate(engine, entity, &move, -1, in->keep_children);
                    off = test_4_points(engine, pts, fps, floors, in->det);
                    outside = off != 0;
                } else { /* "No Replacement found": back where it was */
                    eng_entity_set_world_matrix(engine, entity, &old, false);
                    memcpy(pts, oldpts, sizeof(oldpts));
                    memcpy(fps, oldfps, sizeof(oldfps));
                    memcpy(floors, oldfloors, sizeof(oldfloors));
                    outside = false;
                }
                tries++;
            } while (outside && tries < 8);
            if (tries < 8) {
                for (int k = 0; k < 4; k++) {
                    int i = bottom_corners[k];
                    corner_floor(floors[i], &fps[i], &pts[i], 0.0f, &distmin, &up);
                }
                corners_orientation(pts, &dir, &vup, &right);
                if (in->follow) {
                    eng_entity_set_orientation(engine, entity, &dir, &vup, &right, -1, in->keep_children);
                }
                eng_vector_t move = v3(0.0f, distmin + in->offset, 0.0f);
                eng_entity_translate(engine, entity, &move, -1, in->keep_children);
                floorn = floors[0];
                /* The DLL takes the floor from past the corners' array: none here */
                if (floorn == CKFLOOR_UP) {
                    fp->up_floor = 0;
                    fp->up_normal = vup;
                } else if (floorn == CKFLOOR_DOWN) {
                    fp->down_floor = 0;
                    fp->down_normal = vup;
                }
            }
        }
    }
    *distance = distmin;
    return floorn;
}

/*
 * Object Keep On Floor V2: the object kept on the floors under it (its box's
 * bottom center, or its bottom corners), following their slope if asked;
 * the floor, its type, its slope and the distance to it.
 */
static int bb_object_keep_on_floor_v2(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
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
    keep_on_floor_inputs_t in;
    in.follow = get_in_int(b, 0, 0) != 0;
    in.offset = get_in_float(b, 1, 0.0f);
    in.keep_children = get_in_int(b, 2, 1) == 0;
    in.keep_in = get_in_int(b, 3, 0) != 0;
    in.det = get_in_float(b, 4, 0.0f);
    in.attenuation = get_in_float(b, 5, 0.9f);
    eng_floor_point_t fp;
    eng_floor_point_clear(&fp);
    float distance = 0.0f;
    int floorn = get_local_int(b, 0, 1) == 2 ? keep_on_floor_box(b, entity, &in, &fp, &distance)
                                             : keep_on_floor_point(b, entity, &in, &fp, &distance);
    eng_floor_settings_t settings = {0, 0, 0, 0, 0};
    float slope = 0.0f;
    if (floorn != CKFLOOR_NOFLOOR) {
        nmo_object_id_t floor = floorn == CKFLOOR_UP ? fp.up_floor : fp.down_floor;
        set_out(b, 0, &floor, sizeof(floor));
        eng_floor_read(engine, floor, &settings);
        eng_vector_t n = normalized(floorn == CKFLOOR_UP ? fp.up_normal : fp.down_normal);
        slope = (float)acos((n.z + n.x) * 0.0f + n.y);
    }
    set_out_int(b, 1, settings.type);
    set_out_float(b, 2, slope);
    set_out_float(b, 3, distance);
    if (in.keep_in) {
        engine->entities.data[entity].entity_flags |= CK_3DENTITY_UPDATELASTFRAME;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Characters on floors
 * ============================================================================ */

/* Enhanced Character Keep On Floor's last frame (its CKOF2 local) */
typedef struct enhanced_floor_state {
    eng_vector_t pts[4];
    eng_floor_point_t fps[4];
    int floors[4];
} enhanced_floor_state_t;

/* Test4Points3: the four points' floors, an up floor taken when it is within the detection offset;
   their bits when off the floors */
static int test_4_points_3(nmo_engine_t *engine, const eng_vector_t pts[4], eng_floor_point_t fps[4], int floors[4],
                           float det)
{
    int off = 0;
    for (int i = 0; i < 4; i++) {
        floors[i] = eng_floor_nearest(engine, &pts[i], &fps[i], 0);
        if (floors[i] == CKFLOOR_UP) {
            if (fps[i].down_floor != 0 && fps[i].up_distance > det) {
                floors[i] = CKFLOOR_DOWN;
            }
        } else if (floors[i] == CKFLOOR_DOWN) {
            if (fps[i].up_floor != 0 && fps[i].up_distance < det) {
                floors[i] = CKFLOOR_UP;
            }
        } else {
            off |= 1 << i;
        }
    }
    return off;
}

/* ReturnCharacterDirection, the direction only: the entity's axis the Character Direction picks the way
   the DLL calls GetOrientation (Y for X, X for Y, Z for Z), negated for the negative ones */
static eng_vector_t character_direction(nmo_engine_t *engine, int32_t entity, int direction)
{
    eng_vector_t dir = v3(0.0f, 0.0f, 0.0f);
    if (direction == 1 || direction == 2) {
        eng_entity_orientation(engine, entity, NULL, &dir, NULL, -1);
    } else if (direction == 3 || direction == 4) {
        eng_entity_orientation(engine, entity, NULL, NULL, &dir, -1);
    } else if (direction == 5 || direction == 6) {
        eng_entity_orientation(engine, entity, &dir, NULL, NULL, -1);
    }
    if (direction == 2 || direction == 4 || direction == 6) {
        dir = v3(-dir.x, -dir.y, -dir.z);
    }
    return dir;
}

/*
 * Enhanced Character Keep On Floor: the character falls with its Weight (a
 * Down Velocity) onto the floors under four points around its root's
 * center (Character Extents apart, at its floor reference or its bottom),
 * an upper floor within the Detection Offset stepped onto; with a point off
 * the floors it goes back to last frame's place with Keep In Floor Boundary.
 * The Show Extents entity is not made.
 */
static int bb_enhanced_character_keep_on_floor(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t character = eng_entity_index(engine, owner);
    if (character < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float offset = get_in_float(b, 1, 0.0f);
    eng_vector_t velocity;
    if (nmo_engine_block_get_output_value(b, 4, &velocity, sizeof(velocity)) != NMO_OK) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "Obsolete Version" */
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    eng_vector_t move;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        velocity = v3(0.0f, 0.0f, 0.0f);
    } else if (offset > 0.0f) {
        move = v3(0.0f, -offset, 0.0f);
        eng_entity_translate(engine, character, &move, -1, false);
    }
    float weight = get_in_float(b, 6, 0.0f);
    eng_entity_translate(engine, character, &velocity, -1, false);
    velocity.y -= weight * nmo_engine_block_delta_time(b) * 0.001f; /* gravity */
    bool follow = get_in_int(b, 0, 0) != 0, keep_in = get_in_int(b, 2, 0) != 0;
    float det = get_in_float(b, 3, 0.0f), extents[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 4, extents, sizeof(extents));
    float attenuation = get_in_float(b, 5, 0.9f);
    enhanced_floor_state_t *old = nmo_engine_block_state(b, sizeof(*old));
    if (old == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    nmo_object_id_t out_floor = 0;
    eng_vector_t out_normal = v3(0.0f, 0.0f, 0.0f);
    float out_distance = 0.0f, basement;
    int32_t reference = eng_entity_index(engine, eng_character_floor_reference(engine, owner));
    if (reference >= 0) {
        basement = eng_entity_position(engine, reference, -1).y;
    } else {
        eng_vector_t min, max;
        bounding_box(engine, character, &min, &max); /* GetHierarchicalBox */
        basement = min.y;
    }
    int32_t root = eng_entity_index(engine, eng_character_root(engine, owner));
    if (root < 0) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    eng_vector_t center;
    eng_entity_bary_center(engine, root, &center);
    eng_vector_t dir = character_direction(engine, root, get_in_int(b, 7, 1));
    dir.y = 0.0f;
    eng_vector_normalize(&dir);
    const eng_vector_t world_up = v3(0.0f, 1.0f, 0.0f);
    eng_vector_t right = normalized(eng_vector_cross(&world_up, &dir));
    right = v3(right.x * extents[0], right.y * extents[0], right.z * extents[0]);
    dir = v3(dir.x * extents[1], dir.y * extents[1], dir.z * extents[1]);
    eng_vector_t pts[4] = {
        v3(center.x + dir.x - right.x, basement, center.z + dir.z - right.z),
        v3(center.x + dir.x + right.x, basement, center.z + dir.z + right.z),
        v3(center.x - dir.x + right.x, basement, center.z - dir.z + right.z),
        v3(center.x - dir.x - right.x, basement, center.z - dir.z - right.z),
    };
    eng_floor_point_t fps[4];
    int floors[4];
    int off = test_4_points_3(engine, pts, fps, floors, det);
    bool up = false;
    float higher = -1000000000.0f;
    for (int i = 0; i < 4; i++) {
        if (floors[i] == CKFLOOR_DOWN || floors[i] == CKFLOOR_UP) {
            float distance = floors[i] == CKFLOOR_DOWN ? fps[i].down_distance : fps[i].up_distance;
            if (distance > higher) {
                higher = distance;
            }
            pts[i].y += distance;
            up = up || floors[i] == CKFLOOR_UP;
        }
    }
    if (up) {
        velocity = v3(0.0f, 0.0f, 0.0f);
    }
    if (off == 0) {
        eng_vector_t fdir = normalized(v3(pts[0].x - pts[3].x + pts[1].x - pts[2].x,
                                          pts[0].y - pts[3].y + pts[1].y - pts[2].y,
                                          pts[0].z - pts[3].z + pts[1].z - pts[2].z));
        eng_vector_t fright = normalized(v3(pts[1].x - pts[0].x + pts[2].x - pts[3].x,
                                            pts[1].y - pts[0].y + pts[2].y - pts[3].y,
                                            pts[1].z - pts[0].z + pts[2].z - pts[3].z));
        eng_vector_t vup = normalized(eng_vector_cross(&fdir, &fright));
        if (follow && up) {
            eng_vector_t cup;
            eng_entity_orientation(engine, character, &fdir, &cup, NULL, -1);
            float rest = 1.0f - attenuation;
            vup = v3(cup.x * attenuation + vup.x * rest, cup.y * attenuation + vup.y * rest,
                     cup.z * attenuation + vup.z * rest);
            if ((vup.z + vup.x) * 0.0f + vup.y > 0.8f) {
                fright = normalized(eng_vector_cross(&vup, &fdir));
                fdir = normalized(eng_vector_cross(&fright, &vup));
                eng_entity_set_orientation(engine, character, &fdir, &vup, &fright, -1, false);
            }
        }
        if (weight <= 0.0f || up) {
            move = v3(0.0f, higher, 0.0f);
            eng_entity_translate(engine, character, &move, -1, false);
        }
        out_normal = vup;
        out_floor = floors[0] == CKFLOOR_DOWN ? fps[0].down_floor : floors[0] == CKFLOOR_UP ? fps[0].up_floor : 0;
        out_distance = higher + offset;
        OUT_ON(b, 0);
    } else if (keep_in) { /* the DLL's flag test is always true */
        OUT_ON(b, 2);
        eng_matrix_t last = *eng_entity_last_frame_matrix(engine, character);
        eng_entity_set_world_matrix(engine, character, &last, false);
        memcpy(pts, old->pts, sizeof(pts));
        memcpy(fps, old->fps, sizeof(fps));
        memcpy(floors, old->floors, sizeof(floors));
    } else {
        OUT_ON(b, 1);
    }
    memcpy(old->pts, pts, sizeof(pts));
    memcpy(old->fps, fps, sizeof(fps));
    memcpy(old->floors, floors, sizeof(floors));
    eng_floor_settings_t settings = {0, 0, 0, 0, 0};
    eng_floor_read(engine, out_floor, &settings);
    set_out(b, 0, &out_floor, sizeof(out_floor));
    set_out_int(b, 1, settings.type);
    set_out(b, 2, &out_normal, sizeof(out_normal));
    set_out_float(b, 3, out_distance);
    if (keep_in) {
        engine->entities.data[character].entity_flags |= CK_3DENTITY_UPDATELASTFRAME;
    }
    if (offset > 0.0f) {
        move = v3(0.0f, offset, 0.0f);
        eng_entity_translate(engine, character, &move, -1, false);
    }
    set_out(b, 4, &velocity, sizeof(velocity));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* The bottom corners of a box: front left, front right, back left, back right */
static void bottom_box_corners(eng_vector_t min, eng_vector_t max, eng_vector_t corners[4])
{
    corners[0] = v3(min.x, min.y, max.z);
    corners[1] = v3(max.x, min.y, max.z);
    corners[2] = v3(min.x, min.y, min.z);
    corners[3] = v3(max.x, min.y, min.z);
}

/* The position, in the floor's space, moved back by each corner past the floor's local box on a side
   with no floor beyond it */
static eng_vector_t limits_inside(nmo_engine_t *engine, nmo_object_id_t floor, const eng_vector_t corners[4],
                                  eng_vector_t pos)
{
    int32_t index = eng_entity_index(engine, floor);
    eng_vector_t min, max, local[4], lpos, out;
    eng_entity_local_box(engine, index, &min, &max);
    for (int k = 0; k < 4; k++) {
        eng_entity_inverse_transform(engine, index, &local[k], &corners[k], -1);
    }
    eng_entity_inverse_transform(engine, index, &lpos, &pos, -1);
    for (int k = 0; k < 4; k++) {
        /* x past the max, x past the min, z past the max, z past the min */
        const float past[4] = {local[k].x - max.x, local[k].x - min.x, local[k].z - max.z, local[k].z - min.z};
        const bool beyond[4] = {local[k].x > max.x, local[k].x < min.x, local[k].z > max.z, local[k].z < min.z};
        for (int side = 0; side < 4; side++) {
            eng_floor_point_t fp;
            if (beyond[side] && eng_floor_nearest(engine, &corners[k], &fp, 0) == CKFLOOR_NOFLOOR) {
                if (side < 2) {
                    lpos.x -= past[side];
                } else {
                    lpos.z -= past[side];
                }
            }
        }
    }
    eng_entity_transform(engine, index, &out, &lpos, -1);
    return out;
}

/*
 * Character Keep On Floor Limits (obsolete): the character kept on the
 * floors (the upper whenever there is one). With a floor reference object,
 * a corner of its box off the floors sends it back past where it was last
 * time (1.5 times the way); without one, it is pushed back inside the
 * floor's local box on the sides with no floor beyond.
 */
static int bb_character_keep_on_floor_limits(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t character = eng_entity_index(engine, owner);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    if (character < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t old = v3(0.0f, 0.0f, 0.0f), min, max, corners[4];
    nmo_engine_block_get_local_value(b, 0, &old, sizeof(old));
    int32_t reference = eng_entity_index(engine, eng_character_floor_reference(engine, owner));
    eng_floor_point_t fp;
    if (reference >= 0) {
        eng_vector_t pos = eng_entity_position(engine, character, -1);
        eng_vector_t ref = eng_entity_position(engine, reference, -1);
        if (eng_floor_nearest(engine, &ref, &fp, 0) != CKFLOOR_NOFLOOR) {
            float up = fp.up_distance, down = fp.down_distance;
            bool has_up = fp.up_floor != 0, has_down = fp.down_floor != 0;
            bounding_box(engine, character, &min, &max);
            bottom_box_corners(min, max, corners);
            if (has_up || has_down) {
                eng_floor_point_t corner;
                for (int k = 0; k < 4; k++) {
                    if (eng_floor_nearest(engine, &corners[k], &corner, 0) == CKFLOOR_NOFLOOR) {
                        eng_vector_t back = v3((old.x - pos.x) * 1.5f + pos.x, (old.y - pos.y) * 1.5f + pos.y,
                                               (old.z - pos.z) * 1.5f + pos.z);
                        eng_entity_set_position(engine, character, &back, -1, false);
                        OUT_ON(b, 0);
                        return NMO_ENGINE_BR_OK;
                    }
                }
                pos.y += has_up ? up : down;
            }
            eng_entity_set_position(engine, character, &pos, -1, false);
            nmo_engine_block_set_local_value(b, 0, &pos, sizeof(pos));
        }
    } else {
        bounding_box(engine, character, &min, &max);
        eng_vector_t pos = v3((max.x + min.x) * 0.5f, min.y, (max.z + min.z) * 0.5f);
        if (eng_floor_nearest(engine, &pos, &fp, 0) != CKFLOOR_NOFLOOR) {
            nmo_object_id_t floor = fp.up_floor != 0 ? fp.up_floor : fp.down_floor;
            float distance = fp.up_floor != 0 ? fp.up_distance : fp.down_distance;
            bottom_box_corners(min, max, corners);
            pos = eng_entity_position(engine, character, -1);
            if (floor != 0) {
                pos = limits_inside(engine, floor, corners, pos);
                pos.y += distance;
            }
            eng_entity_set_position(engine, character, &pos, -1, false);
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Character Keep On Floor Limits' load: the character's place as the last one */
static int cb_character_keep_on_floor_limits(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        int32_t character = eng_entity_index(b->engine, nmo_engine_block_owner(b));
        if (character < 0) {
            return NMO_ENGINE_BR_OWNERERROR;
        }
        eng_vector_t pos = eng_entity_position(b->engine, character, -1);
        nmo_engine_block_set_local_value(b, 0, &pos, sizeof(pos));
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The collision manager (CollisionManager of Collisions.dll)
 * ============================================================================ */

/*
 * Obstacles are the 3D entities with the Fixed Obstacle or Moving Obstacle
 * attribute. The manager keeps them in the order they became obstacles
 * (BoundariesManager), each with the box it last took for it: a fixed
 * obstacle's from when it was added, a moving one's from the end of the last
 * frame, grown by its box when a detection runs. A detection looks only at
 * the obstacles whose boxes meet the tested one's (ties of touching bounds
 * taken as meeting), in that order, then drops the fixed ones away from the
 * tested entity's sweep (a filter of the DLL's). The engine does not call
 * the attributes' callbacks: the list is brought up to date with them when
 * the manager runs.
 */
typedef struct obstacle {
    nmo_object_id_t id;
    bool moving;
    eng_vector_t min, max;
} obstacle_t;

/* A moving obstacle's world matrix and hierarchy box at the end of the last frame */
typedef struct saved_moving {
    nmo_object_id_t id;
    eng_matrix_t world;
    eng_vector_t min, max;
} saved_moving_t;

typedef struct collision_manager {
    bool launched;
    ENG_VEC(obstacle_t) obstacles;
    ENG_VEC(saved_moving_t) movings;
    int32_t touched_face, touching_face; /* the faces a face test met */
    struct impact *impacts;              /* the impact array (GetImpactArray) and its size */
    size_t impact_count;
    ENG_VEC(struct nodal_graph) graphs;  /* the nodal paths, by group */
} collision_manager_t;

/* ImpactDesc */
typedef struct impact {
    nmo_object_id_t owner, obstacle, sub_obstacle, entity;
    int32_t touched_vertex, touching_vertex, touched_face, touching_face;
    eng_matrix_t matrix;
    eng_vector_t point, normal;
} impact_t;

/* N3DGraph: a nodal path's nodes (states) and their links (edges) */
#define EDGE_BASIC 1   /* STATEBASICACTIVITY */
#define EDGE_DYNAMIC 2 /* STATEDYNAMICACTIVITY */
typedef struct nodal_edge {
    int32_t successor;
    int32_t active;
    float distance, difficulty;
} nodal_edge_t;

typedef struct nodal_state {
    nmo_object_id_t node, occupier;
    nodal_edge_t *edges; /* in their list's order */
    int32_t edge_count;
    float g, h, f;       /* the A* search's */
    int32_t parent;
    uint32_t flag;       /* LISTOPEN 1, LISTCLOSE 2 */
    eng_vector_t position;
} nodal_state_t;

typedef struct nodal_graph {
    nmo_object_id_t group;
    nodal_state_t *states;
    int32_t count;
} nodal_graph_t;

static void nodal_graph_free(nodal_graph_t *graph)
{
    for (int32_t i = 0; i < graph->count; i++) {
        free(graph->states[i].edges);
    }
    free(graph->states);
    graph->states = NULL;
    graph->count = 0;
}

static void cm_destroy(nmo_engine_t *engine, void *state);
static void cm_pre_process(nmo_engine_t *engine, void *state);
static void cm_post_process(nmo_engine_t *engine, void *state);
static void cm_reset(nmo_engine_t *engine, void *state);

static const eng_manager_desc_t collision_manager_desc = {
    .name = "Collision Manager",
    .state_size = sizeof(collision_manager_t),
    .destroy = cm_destroy,
    .pre_process = cm_pre_process,
    .post_process = cm_post_process,
    .reset = cm_reset,
};

static collision_manager_t *cm_state(nmo_engine_t *engine)
{
    return (collision_manager_t *)eng_manager_state(engine, &collision_manager_desc);
}

static int32_t attribute_named(const nmo_engine_t *engine, const char *name)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

static int32_t fixed_attribute(const nmo_engine_t *engine)
{
    return attribute_named(engine, "Fixed Obstacle");
}

static int32_t moving_attribute(const nmo_engine_t *engine)
{
    return attribute_named(engine, "Moving Obstacle");
}

static bool has_attribute(const nmo_engine_t *engine, nmo_object_id_t id, int32_t type)
{
    return type >= 0 && eng_attribute_find(engine, id, type) != NULL;
}

/* ReadAttributeValues: the obstacle's precision and hierarchy (the moving attribute's first); false when
   it is no obstacle. A field without its parameter is left as it is. */
static bool read_obstacle(nmo_engine_t *engine, nmo_object_id_t id, int32_t *precision, int32_t *hierarchy)
{
    const eng_attribute_t *attribute = eng_attribute_find(engine, id, moving_attribute(engine));
    if (attribute == NULL) {
        attribute = eng_attribute_find(engine, id, fixed_attribute(engine));
    }
    if (attribute == NULL) {
        return false;
    }
    int32_t *fields[2] = {precision, hierarchy};
    if (attribute->fields != NULL) {
        for (uint32_t i = 0; i < 2 && i < attribute->field_count; i++) {
            *fields[i] = attribute->fields[i];
        }
        return true;
    }
    if (!attribute->has_value || !nmo_guid_equals(attribute->value.type, CKPGUID_OBSTACLE)) {
        *precision = CKCOLLISION_BOX;
        *hierarchy = 0;
        return true;
    }
    uint32_t ids[2] = {0, 0};
    memcpy(ids, attribute->value.data, attribute->value.size < sizeof(ids) ? attribute->value.size : sizeof(ids));
    if (ids[0] == 0 && ids[1] == 0) { /* set at run time: the attribute's default, "2;TRUE" */
        *precision = CKCOLLISION_FACE;
        *hierarchy = 1;
        return true;
    }
    for (uint32_t i = 0; i < 2; i++) {
        eng_struct_field(engine, id, attribute->value.data, attribute->value.size, i, fields[i]);
    }
    return true;
}

/* The box the manager takes for an obstacle: its hierarchy's when it is a hierarchical one */
static void obstacle_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    if (engine->entities.data[index].entity_flags & CK_3DENTITY_HIERARCHICALOBSTACLE) {
        if (!eng_entity_hierarchical_box(engine, index, min, max)) {
            *min = *max = eng_entity_position(engine, index, -1);
        }
    } else {
        bounding_box(engine, index, min, max);
    }
}

/* BoundariesManager::Add */
static void cm_add(nmo_engine_t *engine, collision_manager_t *cm, nmo_object_id_t id, bool moving)
{
    int32_t index = eng_entity_index(engine, id);
    for (size_t i = 0; i < cm->obstacles.count; i++) {
        if (cm->obstacles.data[i].id == id) {
            return;
        }
    }
    obstacle_t *obstacle = NULL;
    if (index < 0 || !ENG_VEC_PUSH(cm->obstacles, obstacle)) {
        return;
    }
    obstacle->id = id;
    obstacle->moving = moving;
    obstacle_box(engine, index, &obstacle->min, &obstacle->max);
}

static void cm_remove_at(collision_manager_t *cm, size_t i)
{
    memmove(&cm->obstacles.data[i], &cm->obstacles.data[i + 1], (cm->obstacles.count - i - 1) * sizeof(obstacle_t));
    cm->obstacles.count--;
}

/*
 * PostLaunchScene (once, at the first frame), then the attributes'
 * callbacks: the fixed obstacles then the moving ones, their hierarchical
 * flag from their attribute; afterwards, obstacles that lost their
 * attribute leave and new ones are added after the others.
 */
static void cm_sync(nmo_engine_t *engine, collision_manager_t *cm)
{
    int32_t fixed = fixed_attribute(engine), moving = moving_attribute(engine);
    if (!cm->launched) {
        cm->launched = true;
        cm->obstacles.count = 0;
        for (int pass = 0; pass < 2; pass++) {
            int32_t type = pass == 0 ? fixed : moving;
            for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
                const eng_attribute_t *attribute = &engine->attributes.values.data[i];
                int32_t index = attribute->type == type ? eng_entity_index(engine, attribute->object) : -1;
                if (index < 0) {
                    continue;
                }
                nmo_object_id_t id = attribute->object;
                cm_add(engine, cm, id, pass == 1);
                int32_t precision = CKCOLLISION_BOX, hierarchy = 0;
                read_obstacle(engine, id, &precision, &hierarchy);
                if (hierarchy) {
                    engine->entities.data[index].entity_flags |= CK_3DENTITY_HIERARCHICALOBSTACLE;
                } else {
                    engine->entities.data[index].entity_flags &= ~(uint32_t)CK_3DENTITY_HIERARCHICALOBSTACLE;
                }
            }
        }
        return;
    }
    for (size_t i = 0; i < cm->obstacles.count;) {
        const obstacle_t *obstacle = &cm->obstacles.data[i];
        bool kept = eng_entity_index(engine, obstacle->id) >= 0 &&
                    has_attribute(engine, obstacle->id, obstacle->moving ? moving : fixed);
        if (kept) {
            i++;
        } else {
            cm_remove_at(cm, i);
        }
    }
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        if ((attribute->type == fixed || attribute->type == moving) && attribute->type >= 0) {
            cm_add(engine, cm, attribute->object, attribute->type == moving);
        }
    }
}

/* AddObstacle: the attribute (the other one removed, as its callback does), then its fields */
static void cm_add_obstacle(nmo_engine_t *engine, nmo_object_id_t id, bool moving, int32_t precision, int32_t hierarchy)
{
    int32_t type = moving ? moving_attribute(engine) : fixed_attribute(engine);
    int32_t other = moving ? fixed_attribute(engine) : moving_attribute(engine);
    if (type < 0 || eng_entity_index(engine, id) < 0) {
        return;
    }
    if (eng_attribute_find(engine, id, type) == NULL) {
        if (other >= 0) {
            nmo_engine_object_remove_attribute(engine, id, other);
        }
        nmo_engine_object_set_attribute(engine, id, type);
        collision_manager_t *cm = cm_state(engine);
        if (cm != NULL && cm->launched) {
            cm_sync(engine, cm);
        }
    }
    eng_attribute_t *attribute = eng_attribute_find(engine, id, type);
    if (attribute == NULL || !attribute->has_value) {
        return;
    }
    if (attribute->fields == NULL) {
        attribute->fields = (int32_t *)malloc(2 * sizeof(int32_t));
        if (attribute->fields == NULL) {
            return;
        }
        attribute->field_count = 2;
    }
    attribute->fields[0] = precision;
    attribute->fields[1] = hierarchy;
}

static void cm_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    collision_manager_t *cm = (collision_manager_t *)state;
    free(cm->obstacles.data);
    free(cm->movings.data);
    free(cm->impacts);
    for (size_t i = 0; i < cm->graphs.count; i++) {
        nodal_graph_free(&cm->graphs.data[i]);
    }
    free(cm->graphs.data);
}

static void cm_pre_process(nmo_engine_t *engine, void *state)
{
    cm_sync(engine, (collision_manager_t *)state);
}

/* PostProcess: the moving obstacles' matrices and boxes saved, their bounds taken anew (UpdateStartMovings) */
static void cm_post_process(nmo_engine_t *engine, void *state)
{
    collision_manager_t *cm = (collision_manager_t *)state;
    cm_sync(engine, cm);
    cm->movings.count = 0;
    int32_t moving = moving_attribute(engine);
    for (size_t i = 0; moving >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        int32_t index = attribute->type == moving ? eng_entity_index(engine, attribute->object) : -1;
        saved_moving_t *saved = NULL;
        if (index < 0 || !ENG_VEC_PUSH(cm->movings, saved)) {
            continue;
        }
        saved->id = attribute->object;
        saved->world = engine->entities.data[index].world;
        if (!eng_entity_hierarchical_box(engine, index, &saved->min, &saved->max)) {
            saved->min = saved->max = eng_entity_position(engine, index, -1);
        }
    }
    for (size_t i = 0; i < cm->obstacles.count; i++) {
        obstacle_t *obstacle = &cm->obstacles.data[i];
        int32_t index = eng_entity_index(engine, obstacle->id);
        if (obstacle->moving && index >= 0) {
            obstacle_box(engine, index, &obstacle->min, &obstacle->max);
        }
    }
}

/* OnCKReset: the moving obstacles keep their last frame's matrix */
static void cm_reset(nmo_engine_t *engine, void *state)
{
    (void)state;
    int32_t moving = moving_attribute(engine);
    for (size_t i = 0; moving >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        int32_t index = attribute->type == moving ? eng_entity_index(engine, attribute->object) : -1;
        if (index >= 0) {
            engine->entities.data[index].entity_flags |= CK_3DENTITY_UPDATELASTFRAME;
        }
    }
}

/* SeekMovingMatrix: the saved moving obstacle; NULL when it is none */
static const saved_moving_t *cm_saved(const collision_manager_t *cm, nmo_object_id_t id)
{
    for (size_t i = 0; i < cm->movings.count; i++) {
        if (cm->movings.data[i].id == id) {
            return &cm->movings.data[i];
        }
    }
    return NULL;
}

/* UpdateEndMovings: the moving obstacles' bounds grown by their boxes now */
static void cm_update_end_movings(nmo_engine_t *engine, collision_manager_t *cm)
{
    for (size_t i = 0; i < cm->obstacles.count; i++) {
        obstacle_t *o = &cm->obstacles.data[i];
        int32_t index = eng_entity_index(engine, o->id);
        if (!o->moving || index < 0) {
            continue;
        }
        eng_vector_t min, max;
        obstacle_box(engine, index, &min, &max);
        o->min = v3(fminf(o->min.x, min.x), fminf(o->min.y, min.y), fminf(o->min.z, min.z));
        o->max = v3(fmaxf(o->max.x, max.x), fmaxf(o->max.y, max.y), fmaxf(o->max.z, max.z));
    }
}

static bool boxes_meet(eng_vector_t amin, eng_vector_t amax, eng_vector_t bmin, eng_vector_t bmax)
{
    return amin.x <= bmax.x && amax.x >= bmin.x && amin.y <= bmax.y && amax.y >= bmin.y && amin.z <= bmax.z &&
           amax.z >= bmin.z;
}

/* FindOverlapping: the obstacles (but `self`) whose bounds meet the box, in the list's order */
static size_t cm_overlapping(nmo_engine_t *engine, collision_manager_t *cm, nmo_object_id_t self, eng_vector_t min,
                             eng_vector_t max, nmo_object_id_t **out)
{
    *out = (nmo_object_id_t *)malloc((cm->obstacles.count + 1) * sizeof(nmo_object_id_t));
    size_t count = 0;
    for (size_t i = 0; *out != NULL && i < cm->obstacles.count; i++) {
        const obstacle_t *o = &cm->obstacles.data[i];
        if (o->id != self && eng_entity_index(engine, o->id) >= 0 && boxes_meet(min, max, o->min, o->max)) {
            (*out)[count++] = o->id;
        }
    }
    return count;
}

/* The center and half largest size of an entity's hierarchy box (world, or with `local` its own space) */
static float hierarchy_sphere(nmo_engine_t *engine, int32_t index, bool local, eng_vector_t *center)
{
    eng_vector_t min, max;
    bool valid = local ? local_hierarchical_box(engine, index, &min, &max)
                       : eng_entity_hierarchical_box(engine, index, &min, &max);
    if (!valid) {
        min = max = local ? v3(0.0f, 0.0f, 0.0f) : eng_entity_position(engine, index, -1);
    }
    *center = v3((max.x + min.x) * 0.5f, (max.y + min.y) * 0.5f, (max.z + min.z) * 0.5f);
    return fmaxf(fmaxf(max.x - min.x, max.y - min.y), max.z - min.z) * 0.5f;
}

/* The DLL's filter: the fixed obstacles whose hierarchy box's center is farther from the line the
   entity's moved along (start to now, the direction not normalized) than both half sizes */
static size_t cm_filter(nmo_engine_t *engine, const collision_manager_t *cm, int32_t entity, nmo_object_id_t *ids,
                        size_t count)
{
    eng_vector_t now, start, local;
    float radius = hierarchy_sphere(engine, entity, false, &now);
    const saved_moving_t *saved = cm_saved(cm, engine->entities.data[entity].id);
    if (saved == NULL) {
        start = now;
    } else {
        hierarchy_sphere(engine, entity, true, &local);
        eng_vector_transform(&start, &saved->world, &local);
    }
    eng_vector_t dir = vsub(now, start);
    int32_t moving = moving_attribute(engine);
    for (size_t i = 0; i < count;) {
        int32_t index = eng_entity_index(engine, ids[i]);
        eng_vector_t center = {0.0f, 0.0f, 0.0f};
        float r = index >= 0 ? hierarchy_sphere(engine, index, false, &center) : 0.0f;
        float d2 = 0.0f;
        if (!has_attribute(engine, ids[i], moving)) {
            eng_vector_t d = vsub(center, start);
            float t = dir.z * d.z + d.y * dir.y + d.x * dir.x;
            d2 = d.z * d.z + d.y * d.y + d.x * d.x - t * t;
        }
        if (d2 <= (r + radius) * (r + radius)) {
            i++;
        } else {
            memmove(&ids[i], &ids[i + 1], (count - i - 1) * sizeof(nmo_object_id_t));
            count--;
        }
    }
    return count;
}

/* The box BoxBoxIntersection takes for an entity: a character's root body part's hierarchy, else its
   own or its hierarchy's; local (with the matrix) or world */
static bool manager_box(nmo_engine_t *engine, int32_t *index, bool hierarchy, bool local, eng_vector_t *min,
                        eng_vector_t *max)
{
    nmo_object_id_t id = engine->entities.data[*index].id;
    if (nmo_engine_object_class(engine, id) == NMO_CID_CHARACTER) {
        *index = eng_entity_index(engine, eng_character_root(engine, id));
        if (*index < 0) {
            return false;
        }
        hierarchy = true;
    }
    bool valid = hierarchy ? (local ? local_hierarchical_box(engine, *index, min, max)
                                    : eng_entity_hierarchical_box(engine, *index, min, max))
                           : (local ? eng_entity_local_box(engine, *index, min, max)
                                    : eng_entity_world_box(engine, *index, min, max));
    if (!valid) {
        *min = *max = local ? v3(0.0f, 0.0f, 0.0f) : eng_entity_position(engine, *index, -1);
    }
    return true;
}

/* VxOBB::Create: a box through a matrix */
static void obb_create(eng_vector_t min, eng_vector_t max, const eng_matrix_t *m, obb_t *out)
{
    eng_vector_t center = v3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f), c;
    eng_vector_transform(&c, m, &center);
    out->center[0] = c.x;
    out->center[1] = c.y;
    out->center[2] = c.z;
    const float size[3] = {max.x - min.x, max.y - min.y, max.z - min.z};
    for (int i = 0; i < 3; i++) {
        float length = sqrtf(m->m[i][0] * m->m[i][0] + m->m[i][1] * m->m[i][1] + m->m[i][2] * m->m[i][2]);
        float inv = 1.0f / length;
        for (int k = 0; k < 3; k++) {
            out->axis[i][k] = inv * m->m[i][k];
        }
        out->extent[i] = size[i] * length * 0.5f;
    }
}

/* An axis-aligned box as an OBB, for VxIntersect::AABBOBB */
static void obb_aligned(eng_vector_t min, eng_vector_t max, obb_t *out)
{
    memset(out, 0, sizeof(*out));
    out->center[0] = (min.x + max.x) * 0.5f;
    out->center[1] = (min.y + max.y) * 0.5f;
    out->center[2] = (min.z + max.z) * 0.5f;
    out->axis[0][0] = out->axis[1][1] = out->axis[2][2] = 1.0f;
    out->extent[0] = (max.x - min.x) * 0.5f;
    out->extent[1] = (max.y - min.y) * 0.5f;
    out->extent[2] = (max.z - min.z) * 0.5f;
}

/* CollisionManager::BoxBoxIntersection: world boxes compared aligned, local ones through their matrices */
static bool cm_box_box(nmo_engine_t *engine, int32_t e1, bool h1, bool l1, int32_t e2, bool h2, bool l2)
{
    eng_vector_t min1, max1, min2, max2;
    if (!manager_box(engine, &e1, h1, l1, &min1, &max1) || !manager_box(engine, &e2, h2, l2, &min2, &max2)) {
        return false;
    }
    if (!l1 && !l2) {
        return boxes_meet(min1, max1, min2, max2);
    }
    obb_t a, b;
    if (l1) {
        obb_create(min1, max1, &engine->entities.data[e1].world, &a);
    } else {
        obb_aligned(min1, max1, &a);
    }
    if (l2) {
        obb_create(min2, max2, &engine->entities.data[e2].world, &b);
    } else {
        obb_aligned(min2, max2, &b);
    }
    return obb_overlap(&a, &b);
}

/* IsInCollision: box against box, faces against faces, or the box one's local box against the other's faces */
static bool cm_in_collision(nmo_engine_t *engine, collision_manager_t *cm, int32_t e1, int32_t p1, int32_t e2,
                            int32_t p2)
{
    switch (p1 | p2) {
    case CKCOLLISION_BOX:
        return cm_box_box(engine, e1, false, false, e2, false, false);
    case CKCOLLISION_FACE:
        return face_face_intersection(engine, e1, e2, &cm->touching_face, &cm->touched_face);
    case CKCOLLISION_BOX | CKCOLLISION_FACE:
        return (p1 & CKCOLLISION_BOX) ? box_face_intersection(engine, e1, false, true, e2)
                                      : box_face_intersection(engine, e2, false, true, e1);
    default:
        return false;
    }
}

/* The root to test of an entity: a character's root body part */
static int32_t collision_root(nmo_engine_t *engine, int32_t index)
{
    nmo_object_id_t id = engine->entities.data[index].id;
    return nmo_engine_object_class(engine, id) == NMO_CID_CHARACTER
               ? eng_entity_index(engine, eng_character_root(engine, id))
               : index;
}

/* IsInCollisionWithHierarchy: the first entity of `e2`'s hierarchy (depth first) `e1` collides with; -1 for none */
static int32_t cm_with_hierarchy(nmo_engine_t *engine, collision_manager_t *cm, int32_t e1, int32_t p1, int32_t e2,
                                 int32_t p2)
{
    e2 = collision_root(engine, e2);
    if (e2 < 0 || !cm_box_box(engine, e1, false, false, e2, true, false)) {
        return -1;
    }
    if (cm_in_collision(engine, cm, e1, p1, e2, p2)) {
        return e2;
    }
    for (int32_t child = engine->entities.data[e2].first_child; child >= 0;
         child = engine->entities.data[child].next_sibling) {
        int32_t hit = cm_with_hierarchy(engine, cm, e1, p1, child, p2);
        if (hit >= 0) {
            return hit;
        }
    }
    return -1;
}

/* IsHierarchyInCollisionWithHierarchy: the parts of each hierarchy that collide */
static bool cm_hierarchies(nmo_engine_t *engine, collision_manager_t *cm, int32_t e1, int32_t p1, int32_t e2,
                           int32_t p2, int32_t *sub1, int32_t *sub2)
{
    e1 = collision_root(engine, e1);
    e2 = e1 >= 0 ? collision_root(engine, e2) : -1;
    if (e2 < 0 || !cm_box_box(engine, e1, true, false, e2, true, false)) {
        return false;
    }
    int32_t hit = cm_with_hierarchy(engine, cm, e1, p1, e2, p2);
    if (hit >= 0) {
        *sub1 = e1;
        *sub2 = hit;
        return true;
    }
    for (int32_t child = engine->entities.data[e1].first_child; child >= 0;
         child = engine->entities.data[child].next_sibling) {
        if (cm_hierarchies(engine, cm, child, p1, e2, p2, sub1, sub2)) {
            return true;
        }
    }
    return false;
}

/* GetNearestVertex: the vertex of the entity's mesh nearest a world position; -1 without one */
static int32_t nearest_vertex(nmo_engine_t *engine, int32_t index, eng_vector_t world)
{
    entity_faces_t f;
    if (!entity_faces(engine, engine->entities.data[index].id, &f) || f.vertices == f.sprite || f.vertex_count == 0) {
        return -1;
    }
    eng_vector_t pos;
    eng_entity_inverse_transform(engine, index, &pos, &world, -1);
    float best = 10000000.0f;
    int32_t nearest = -1;
    for (int32_t i = 0; i < f.vertex_count; i++) {
        eng_vector_t d = vsub(face_vertex(&f, i), pos);
        float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (d2 < best) {
            best = d2;
            nearest = i;
        }
    }
    return nearest;
}

/*
 * FillImpactStructure, the DLL's: the vertices nearest the other side's
 * center, the faces the face test met, the touching vertex projected on the
 * touched face for the point, that face's normal. As in the DLL, the Object
 * Part is only given when the Touched Sub Obstacle is not asked for.
 */
static void cm_fill_impact(nmo_engine_t *engine, const collision_manager_t *cm, uint32_t flags, impact_t *imp,
                           int32_t ent, int32_t sub, int32_t ob, int32_t subob, const eng_matrix_t *replacement)
{
    imp->entity = engine->entities.data[ent].id;
    imp->obstacle = engine->entities.data[ob].id;
    if (flags & SUBOBSTACLETOUCHED) {
        imp->sub_obstacle = subob >= 0 ? engine->entities.data[subob].id : 0;
    } else if (flags & OWNERENTITY) {
        imp->owner = sub >= 0 ? engine->entities.data[sub].id : 0;
    }
    int32_t obstacle = subob >= 0 ? subob : ob, entity = sub >= 0 ? sub : ent;
    eng_vector_t center;
    if (flags & TOUCHEDVERTEX) {
        eng_entity_bary_center(engine, entity, &center);
        imp->touched_vertex = nearest_vertex(engine, obstacle, center);
    }
    if (flags & (TOUCHINGVERTEX | IMPACTPOINT)) {
        eng_entity_bary_center(engine, obstacle, &center);
        imp->touching_vertex = nearest_vertex(engine, entity, center);
    }
    if (flags & (TOUCHEDFACE | IMPACTPOINT | IMPACTNORMAL)) {
        imp->touched_face = cm->touched_face;
    }
    if (flags & TOUCHINGFACE) {
        imp->touching_face = cm->touching_face;
    }
    entity_faces_t of, ef;
    bool has_of = entity_faces(engine, engine->entities.data[obstacle].id, &of) && of.vertices != of.sprite;
    bool has_ef = entity_faces(engine, engine->entities.data[entity].id, &ef) && ef.vertices != ef.sprite;
    if ((flags & IMPACTPOINT) && imp->touched_face >= 0 && imp->touching_vertex >= 0 && has_of && has_ef &&
        imp->touched_face < of.face_count) {
        eng_vector_t world, pos, vertex = face_vertex(&ef, imp->touching_vertex);
        eng_entity_transform(engine, entity, &world, &vertex, -1);
        eng_entity_inverse_transform(engine, obstacle, &pos, &world, -1);
        eng_vector_t n = face_normal(&of, imp->touched_face);
        eng_vector_normalize(&n);
        eng_vector_t a = face_vertex(&of, of.indices[imp->touched_face * 3]);
        float d = n.z * (pos.z - a.z) + n.y * (pos.y - a.y) + (pos.x - a.x) * n.x;
        eng_vector_t local = v3(pos.x - n.x * d, pos.y - n.y * d, pos.z - n.z * d);
        eng_entity_transform(engine, obstacle, &imp->point, &local, -1);
    }
    if ((flags & IMPACTNORMAL) && imp->touched_face >= 0 && has_of && imp->touched_face < of.face_count) {
        eng_vector_t n = face_normal(&of, imp->touched_face);
        eng_entity_transform_vector(engine, obstacle, &imp->normal, &n, -1, false);
        eng_vector_normalize(&imp->normal);
    }
    if (flags & IMPACTWORLDMATRIX) {
        imp->matrix = *replacement;
    }
}

/* The test of the entity against an obstacle, by their hierarchical flags */
static bool cm_test(nmo_engine_t *engine, collision_manager_t *cm, int32_t ent, int32_t enttype, int32_t ob,
                    int32_t colltype, int32_t *sub, int32_t *subob)
{
    bool ent_hierarchy = (engine->entities.data[ent].entity_flags & CK_3DENTITY_HIERARCHICALOBSTACLE) != 0;
    bool ob_hierarchy = (engine->entities.data[ob].entity_flags & CK_3DENTITY_HIERARCHICALOBSTACLE) != 0;
    if (ent_hierarchy && ob_hierarchy) {
        return cm_hierarchies(engine, cm, ent, enttype, ob, colltype, sub, subob);
    }
    if (ent_hierarchy) {
        *sub = cm_with_hierarchy(engine, cm, ob, colltype, ent, enttype); /* on a character's root body part */
        return *sub >= 0;
    }
    if (ob_hierarchy) {
        *subob = cm_with_hierarchy(engine, cm, ent, enttype, ob, colltype);
        return *subob >= 0;
    }
    return cm_in_collision(engine, cm, ent, enttype, ob, colltype);
}

static void set_world(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m)
{
    eng_entity_set_world_matrix(engine, index, m, false);
}

static void interpolate(float step, const eng_matrix_t *a, const eng_matrix_t *c, eng_matrix_t *out)
{
    vx_interpolate_matrix(step, a->m, c->m, out->m);
}

/* A character's active animation set back one frame, as when it touched an obstacle */
static void rewind_character(nmo_engine_t *engine, nmo_object_id_t character)
{
    if (nmo_engine_object_class(engine, character) != NMO_CID_CHARACTER || !eng_character_automatic(engine, character)) {
        return;
    }
    nmo_object_id_t animation = eng_character_active_animation(engine, character);
    float step = 0.0f;
    if (animation == 0 || !eng_animation_current_step(engine, animation, &step)) {
        return;
    }
    const nmo_animation_state_t *state = eng_animation_playback_state(engine, animation);
    if (state != NULL && (state->flags & CKANIMATION_LINKTOFRAMERATE)) {
        float frame = 0.0f;
        eng_animation_next_frame(engine, animation, -engine->delta, &frame);
        eng_animation_set_frame(engine, animation, frame);
        eng_keyed_animation_center(engine, animation, frame);
        eng_animation_set_current_step(engine, animation, step);
    } else {
        eng_animation_set_step(engine, animation, step - 1.0f);
        eng_keyed_animation_center(engine, animation, step);
    }
}

/*
 * DetectCollision: the entity (an obstacle, or any entity against them) is
 * tested against the obstacles its bounds meet, moved from where it was
 * last frame in Detection Tests + 1 steps (moving obstacles too); at the
 * first one touched, the safe place is looked for in Safe Position Tests
 * halvings, and the impact filled. The entity ends where it was.
 */
static bool cm_detect(nmo_engine_t *engine, int32_t ent, int32_t precision, int32_t replacing, int32_t detecting,
                      uint32_t flags, impact_t *imp)
{
    collision_manager_t *cm = cm_state(engine);
    if (cm == NULL) {
        return false;
    }
    cm_sync(engine, cm);
    detecting = detecting < 0 ? 0 : detecting;
    replacing = replacing < 0 ? 0 : replacing;
    cm_update_end_movings(engine, cm);
    nmo_object_id_t id = engine->entities.data[ent].id;
    eng_matrix_t start, end = engine->entities.data[ent].world;
    int32_t enttype = CKCOLLISION_BOX, enthierarchy = 0;
    nmo_object_id_t *candidates = NULL;
    size_t count = 0;
    if (read_obstacle(engine, id, &enttype, &enthierarchy)) {
        const saved_moving_t *saved = cm_saved(cm, id);
        start = saved != NULL ? saved->world : end;
        const obstacle_t *self = NULL;
        for (size_t i = 0; i < cm->obstacles.count && self == NULL; i++) {
            self = cm->obstacles.data[i].id == id ? &cm->obstacles.data[i] : NULL;
        }
        if (self != NULL) {
            count = cm_overlapping(engine, cm, id, self->min, self->max, &candidates);
        }
    } else {
        enttype = CKCOLLISION_BOX;
        eng_vector_t min, max;
        bounding_box(engine, ent, &min, &max);
        if (engine->entities.data[ent].entity_flags & CK_3DENTITY_UPDATELASTFRAME) {
            start = *eng_entity_last_frame_matrix(engine, ent);
            eng_vector_t lmin, lmax, omin, omax;
            local_bounding_box(engine, ent, &lmin, &lmax);
            box_transform_from(&omin, &omax, lmin, lmax, &start);
            min = v3(fminf(min.x, omin.x), fminf(min.y, omin.y), fminf(min.z, omin.z));
            max = v3(fmaxf(max.x, omax.x), fmaxf(max.y, omax.y), fmaxf(max.z, omax.z));
        } else {
            start = end;
        }
        count = cm_overlapping(engine, cm, 0, min, max, &candidates);
    }
    if (precision != 0) {
        enttype = precision;
    }
    if (count == 0) {
        free(candidates);
        return false;
    }
    count = cm_filter(engine, cm, ent, candidates, count);
    cm->touched_face = cm->touching_face = -1;
    int32_t sub = -1, moving = moving_attribute(engine);
    for (size_t i = 0; i < count; i++) {
        int32_t ob = eng_entity_index(engine, candidates[i]);
        if (ob < 0) {
            continue;
        }
        int32_t subob = -1, colltype = CKCOLLISION_BOX, obhierarchy = 0;
        bool obmoving = has_attribute(engine, candidates[i], moving);
        eng_matrix_t ob_start, ob_end = engine->entities.data[ob].world, current = start, ob_current = ob_end;
        if (obmoving) {
            const saved_moving_t *saved = cm_saved(cm, candidates[i]);
            ob_start = saved != NULL ? saved->world : ob_end;
        }
        float step = 1.0f / (float)(detecting + 1), slerp = 0.0f;
        eng_matrix_t safe = start;
        bool collision = false;
        for (int32_t k = detecting + 1; k > 0; k--) {
            slerp += step;
            read_obstacle(engine, candidates[i], &colltype, &obhierarchy);
            if (precision != 0) {
                colltype = precision;
            }
            interpolate(slerp, &start, &end, &current);
            set_world(engine, ent, &current);
            if (obmoving) {
                interpolate(slerp, &ob_start, &ob_end, &ob_current);
                set_world(engine, ob, &ob_current);
            }
            collision = cm_test(engine, cm, ent, enttype, ob, colltype, &sub, &subob);
            if (collision) {
                break;
            }
            safe = current;
        }
        if (!collision) {
            if (obmoving) {
                set_world(engine, ob, &ob_end);
            }
            continue;
        }
        rewind_character(engine, id);
        float back = 1.0f, back_step = 0.5f;
        eng_matrix_t replacement = safe, temp, ob_temp;
        for (int32_t k = replacing; k > 0; k--) {
            back = collision ? back - back_step : back + back_step;
            interpolate(back, &start, &current, &temp);
            set_world(engine, ent, &temp);
            if (obmoving) {
                interpolate(back, &ob_start, &ob_current, &ob_temp);
                set_world(engine, ob, &ob_temp);
            }
            collision = cm_test(engine, cm, ent, enttype, ob, colltype, &sub, &subob);
            back_step *= 0.5f;
            if (!collision) {
                replacement = temp;
            }
        }
        if (collision) {
            replacement = safe;
        }
        if (imp != NULL) {
            cm_fill_impact(engine, cm, flags, imp, ent, sub, ob, subob, &replacement);
        }
        set_world(engine, ent, &end);
        if (obmoving) {
            set_world(engine, ob, &ob_end);
        }
        free(candidates);
        return true;
    }
    set_world(engine, ent, &end);
    free(candidates);
    return false;
}

/*
 * Declare Obstacles: every 3D entity of the level whose name holds Key
 * Substring becomes an obstacle, fixed or moving, of the Geometry's precision
 * and hierarchy.
 */
static int bb_declare_obstacles(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const char *key = input_string(b, 0);
    bool moving = get_in_int(b, 1, 0) != 0;
    int32_t precision = CKCOLLISION_BOX, hierarchy = 0;
    eng_param_t *geometry = input_source(b, 2, true);
    if (geometry != NULL && nmo_guid_equals(geometry->type, CKPGUID_OBSTACLE)) {
        eng_struct_field(engine, geometry->id, geometry->data, geometry->size, 0, &precision);
        eng_struct_field(engine, geometry->id, geometry->data, geometry->size, 1, &hierarchy);
    }
    for (size_t i = 0; key != NULL && i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        const char *name = entity->destroyed ? NULL : nmo_engine_object_name(engine, entity->id);
        if (name != NULL && strstr(name, key) != NULL) {
            cm_add_obstacle(engine, entity->id, moving, precision, hierarchy);
        }
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Add Obstacle (obsolete): the target becomes a fixed obstacle of its bounding box */
static int bb_add_obstacle(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    cm_add_obstacle(b->engine, b->engine->entities.data[entity].id, false, CKCOLLISION_BOX, 0);
    return NMO_ENGINE_BR_OK;
}

/* Collision Detection: the target against the obstacles; the Impact Data asked for when it touches one */
static int bb_collision_detection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t precision = get_in_int(b, 0, CKCOLLISION_NONE);
    int32_t detecting = get_in_int(b, 1, 0), replacing = get_in_int(b, 2, 0);
    uint32_t flags = (uint32_t)get_local_int(b, 0, 0);
    impact_t imp;
    memset(&imp, 0, sizeof(imp));
    if (!cm_detect(b->engine, entity, precision, replacing, detecting, flags, &imp)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (flags & OWNERENTITY) set_out(b, 0, &imp.owner, sizeof(imp.owner));
    if (flags & OBSTACLETOUCHED) set_out(b, 1, &imp.obstacle, sizeof(imp.obstacle));
    if (flags & SUBOBSTACLETOUCHED) set_out(b, 2, &imp.sub_obstacle, sizeof(imp.sub_obstacle));
    if (flags & TOUCHEDFACE) set_out_int(b, 3, imp.touched_face);
    if (flags & TOUCHINGFACE) set_out_int(b, 4, imp.touching_face);
    if (flags & TOUCHEDVERTEX) set_out_int(b, 5, imp.touched_vertex);
    if (flags & TOUCHINGVERTEX) set_out_int(b, 6, imp.touching_vertex);
    if (flags & IMPACTPOINT) set_out(b, 7, &imp.point, sizeof(imp.point));
    if (flags & IMPACTNORMAL) set_out(b, 8, &imp.normal, sizeof(imp.normal));
    if (flags & IMPACTWORLDMATRIX) set_out(b, 9, imp.matrix.m, sizeof(imp.matrix.m));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* SetAttribute(Moving Obstacle) from a block: its callback takes the fixed one off */
static void cm_set_moving(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t moving = moving_attribute(engine), fixed = fixed_attribute(engine);
    if (moving < 0 || eng_attribute_find(engine, id, moving) != NULL) {
        return;
    }
    if (fixed >= 0) {
        nmo_engine_object_remove_attribute(engine, id, fixed);
    }
    nmo_engine_object_set_attribute(engine, id, moving);
    collision_manager_t *cm = cm_state(engine);
    if (cm != NULL && cm->launched) {
        cm_sync(engine, cm);
    }
}

/*
 * Prevent Collision: from On, the target is a moving obstacle (of its
 * attribute's default, faces and hierarchy) put back each frame to its safe
 * place when it touches an obstacle. The priority On sets is not changed.
 */
static int bb_prevent_collision(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        if (IN_ACTIVE(b, 1)) {
            IN_OFF(b, 1);
        } else if (IN_ACTIVE(b, 0)) {
            IN_OFF(b, 0);
        }
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        cm_set_moving(engine, engine->entities.data[entity].id);
    }
    impact_t imp;
    memset(&imp, 0, sizeof(imp));
    bool collision = cm_detect(engine, entity, get_in_int(b, 0, 0), get_in_int(b, 2, 0), get_in_int(b, 1, 0),
                               IMPACTWORLDMATRIX, &imp);
    if (collision) {
        set_world(engine, entity, &imp.matrix);
    }
    nmo_engine_block_activate_output(b, 0, collision);
    nmo_engine_block_activate_output(b, 1, !collision);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* CKCharacter::GetEstimatedVelocity: how far its active animation moves its root body part in `delta` */
static eng_vector_t estimated_velocity(nmo_engine_t *engine, nmo_object_id_t character, float delta)
{
    nmo_object_id_t active = eng_character_active_animation(engine, character);
    if (active == 0 || nmo_engine_object_class(engine, active) != NMO_CID_KEYEDANIMATION) {
        return v3(0.0f, 0.0f, 0.0f);
    }
    nmo_object_id_t root = eng_keyed_animation_part(engine, active, eng_character_root(engine, character));
    float frame = 0.0f, next = 0.0f;
    nmo_vector_t p1 = {0, 0, 0}, p2 = {0, 0, 0};
    if (root == 0 || !eng_animation_frame(engine, active, &frame) ||
        !eng_animation_next_frame(engine, active, delta, &next)) {
        return v3(0.0f, 0.0f, 0.0f);
    }
    eng_animation_position(engine, root, frame, &p1);
    eng_animation_position(engine, root, next, &p2);
    return v3(p2.x - p1.x, p2.y - p1.y, p2.z - p1.z);
}

/*
 * Character Prevent From Collision (obsolete): Prevent Collision for a
 * character, a hierarchical moving obstacle, put back only when it walks
 * into the obstacle (its animation's velocity against the side of the
 * obstacle's box it stands by); the body part and obstacles touched. As in
 * the DLL, the body part is not filled when the sub obstacle is asked for.
 */
static int bb_character_prevent_from_collision(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        IN_OFF(b, IN_ACTIVE(b, 1) ? 1 : 0);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t id = engine->entities.data[entity].id;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        cm_set_moving(engine, id);
        eng_attribute_t *attribute = eng_attribute_find(engine, id, moving_attribute(engine));
        if (attribute == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        if (attribute->has_value) {
            int32_t precision = CKCOLLISION_FACE, hierarchy = 1;
            read_obstacle(engine, id, &precision, &hierarchy);
            if (attribute->fields == NULL) {
                attribute->fields = (int32_t *)malloc(2 * sizeof(int32_t));
                attribute->field_count = attribute->fields != NULL ? 2 : 0;
            }
            if (attribute->fields != NULL) {
                attribute->fields[0] = precision;
                attribute->fields[1] = 1;
            }
        }
    }
    impact_t imp;
    memset(&imp, 0, sizeof(imp));
    uint32_t flags = IMPACTWORLDMATRIX | OWNERENTITY | OBSTACLETOUCHED | SUBOBSTACLETOUCHED | IMPACTNORMAL;
    if (!cm_detect(engine, entity, get_in_int(b, 0, 0), get_in_int(b, 2, 0), get_in_int(b, 1, 0), flags, &imp)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    /* "avoid hooking", an input the prototype does not have: on */
    eng_vector_t local = estimated_velocity(engine, id, nmo_engine_block_delta_time(b)), velocity;
    eng_entity_transform_vector(engine, entity, &velocity, &local, -1, false);
    int32_t ob = eng_entity_index(engine, imp.sub_obstacle != 0 ? imp.sub_obstacle : imp.obstacle);
    if (ob >= 0) {
        eng_vector_t pos = eng_entity_position(engine, entity, -1), center, lpos, min, max;
        eng_entity_bary_center(engine, ob, &center);
        pos.y = center.y;
        eng_entity_inverse_transform(engine, ob, &lpos, &pos, -1);
        local_bounding_box(engine, ob, &min, &max);
        const float v[3] = {lpos.x - (min.x + max.x) * 0.5f, lpos.y - (min.y + max.y) * 0.5f,
                            lpos.z - (min.z + max.z) * 0.5f};
        const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
        float n[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 3; i++) {
            if (fabsf(v[i]) > half[i]) {
                n[i] += v[i] > 0.0f ? 1.0f : -1.0f;
            }
        }
        eng_vector_t normal = v3(n[0], n[1], n[2]), world;
        eng_entity_transform_vector(engine, ob, &world, &normal, -1, false);
        eng_vector_normalize(&world);
        if (world.z * velocity.z + world.y * velocity.y + world.x * velocity.x <= 0.0f) {
            set_world(engine, entity, &imp.matrix);
        }
    }
    set_out(b, 0, &imp.owner, sizeof(imp.owner));
    set_out(b, 1, &imp.obstacle, sizeof(imp.obstacle));
    set_out(b, 2, &imp.sub_obstacle, sizeof(imp.sub_obstacle));
    OUT_ON(b, 0);
    nmo_engine_block_activate_output(b, 1, false);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* A bound on an axis of the pair finder: its object (in the list) and whether it opens it */
typedef struct sweep_bound {
    float value;
    int32_t object;
    bool opening;
} sweep_bound_t;

static int compare_bounds(const void *a, const void *b)
{
    const sweep_bound_t *x = (const sweep_bound_t *)a, *y = (const sweep_bound_t *)b;
    if (x->value != y->value) {
        return x->value < y->value ? -1 : 1;
    }
    /* the list's order for ties (the DLL keeps the order its sorts left) */
    int64_t kx = (int64_t)x->object * 2 + (x->opening ? 0 : 1), ky = (int64_t)y->object * 2 + (y->opening ? 0 : 1);
    return kx < ky ? -1 : kx > ky ? 1 : 0;
}

/*
 * The DLL's FindOverlapping for every pair: the bounds swept along X then
 * Y, each closing one marking the objects still open with it; along Z a
 * closing one gives a pair with each object open, marked on both axes, in
 * the order they opened.
 */
static size_t cm_pairs(nmo_engine_t *engine, collision_manager_t *cm, nmo_object_id_t **out)
{
    size_t n = cm->obstacles.count;
    *out = NULL;
    uint8_t *marks = (uint8_t *)calloc(n * n + 1, 1);
    sweep_bound_t *bounds = (sweep_bound_t *)malloc((2 * n + 1) * sizeof(sweep_bound_t));
    int32_t *open = (int32_t *)malloc((n + 1) * sizeof(int32_t));
    nmo_object_id_t *pairs = (nmo_object_id_t *)malloc((n * n + 1) * 2 * sizeof(nmo_object_id_t));
    size_t count = 0;
    for (int axis = 0; marks != NULL && bounds != NULL && open != NULL && pairs != NULL && axis < 3; axis++) {
        for (size_t i = 0; i < n; i++) {
            const obstacle_t *o = &cm->obstacles.data[i];
            bounds[2 * i] = (sweep_bound_t){vcomp(&o->min, axis), (int32_t)i, true};
            bounds[2 * i + 1] = (sweep_bound_t){vcomp(&o->max, axis), (int32_t)i, false};
        }
        qsort(bounds, 2 * n, sizeof(sweep_bound_t), compare_bounds);
        size_t opened = 0;
        for (size_t k = 0; k < 2 * n; k++) {
            int32_t self = bounds[k].object;
            if (bounds[k].opening) {
                open[opened++] = self;
                continue;
            }
            for (size_t j = 0; j < opened; j++) {
                if (open[j] == self) {
                    memmove(&open[j], &open[j + 1], (opened - j - 1) * sizeof(int32_t));
                    opened--;
                    break;
                }
            }
            for (size_t j = 0; j < opened; j++) {
                int32_t other = open[j];
                if (axis < 2) {
                    marks[(size_t)other * n + (size_t)self] |= (uint8_t)(1 << axis);
                    marks[(size_t)self * n + (size_t)other] |= (uint8_t)(1 << axis);
                } else if (marks[(size_t)other * n + (size_t)self] == 3 &&
                           eng_entity_index(engine, cm->obstacles.data[self].id) >= 0 &&
                           eng_entity_index(engine, cm->obstacles.data[other].id) >= 0) {
                    pairs[count * 2] = cm->obstacles.data[self].id;
                    pairs[count * 2 + 1] = cm->obstacles.data[other].id;
                    count++;
                }
            }
        }
    }
    free(marks);
    free(bounds);
    free(open);
    *out = pairs;
    return count;
}

/* TestPair: the two obstacles by their attributes (or both at the precision given, without hierarchy) */
static bool cm_test_pair(nmo_engine_t *engine, collision_manager_t *cm, int32_t precision, int32_t e1, int32_t e2,
                         uint32_t flags, impact_t *imp)
{
    int32_t p1 = precision, p2 = precision, h1 = 0, h2 = 0, sub1 = -1, sub2 = -1;
    if (precision == 0) {
        read_obstacle(engine, engine->entities.data[e1].id, &p1, &h1);
        read_obstacle(engine, engine->entities.data[e2].id, &p2, &h2);
    }
    bool collide;
    if (h1 && h2) {
        collide = cm_hierarchies(engine, cm, e1, p1, e2, p2, &sub1, &sub2);
    } else if (h1) {
        sub1 = cm_with_hierarchy(engine, cm, e2, p2, e1, p1);
        collide = sub1 >= 0;
    } else if (h2) {
        sub2 = cm_with_hierarchy(engine, cm, e1, p1, e2, p2);
        collide = sub2 >= 0;
    } else {
        collide = cm_in_collision(engine, cm, e1, p1, e2, p2);
    }
    eng_matrix_t identity;
    eng_matrix_identity(&identity);
    cm_fill_impact(engine, cm, flags, imp, e1, sub1, e2, sub2, &identity);
    return collide;
}

/*
 * The DLL's DetectCollision of every pair of obstacles: the impacts of the
 * pairs colliding, in the array; as in the DLL, each pair's two objects are
 * also written over the Object Part and Touched Obstacle of the entry of
 * its rank, colliding or not.
 */
static void cm_detect_pairs(nmo_engine_t *engine, collision_manager_t *cm, int32_t precision, uint32_t flags)
{
    cm_sync(engine, cm);
    cm->impact_count = 0;
    cm_update_end_movings(engine, cm);
    nmo_object_id_t *pairs = NULL;
    size_t count = cm_pairs(engine, cm, &pairs);
    impact_t *impacts = (impact_t *)realloc(cm->impacts, (count + 1) * sizeof(impact_t));
    if (pairs == NULL || impacts == NULL) {
        free(pairs);
        if (impacts != NULL) {
            cm->impacts = impacts;
        }
        return;
    }
    cm->impacts = impacts;
    memset(impacts, 0, (count + 1) * sizeof(impact_t));
    for (size_t i = 0; i < count; i++) {
        int32_t e1 = eng_entity_index(engine, pairs[i * 2]), e2 = eng_entity_index(engine, pairs[i * 2 + 1]);
        if (cm_test_pair(engine, cm, precision, e1, e2, flags, &impacts[cm->impact_count])) {
            cm->impact_count++;
        }
        impacts[i].owner = pairs[i * 2];
        impacts[i].obstacle = pairs[i * 2 + 1];
    }
    free(pairs);
}

/*
 * Multi Collision Detection: In detects the collisions of every pair of
 * obstacles; then each Loop gives the next one's objects and the Impact
 * Data asked for, Out after the last.
 */
static int bb_multi_collision_detection(blk_t *b)
{
    collision_manager_t *cm = cm_state(b->engine);
    int32_t index = get_out_int(b, 0);
    uint32_t flags = (uint32_t)get_local_int(b, 0, 0);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
        if (cm != NULL) {
            cm_detect_pairs(b->engine, cm, get_in_int(b, 0, 0), flags);
        }
    } else {
        IN_OFF(b, 1);
        index++;
    }
    if (cm == NULL || index < 0 || (size_t)index >= cm->impact_count) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    const impact_t *imp = &cm->impacts[index];
    set_out(b, 1, &imp->entity, sizeof(imp->entity));
    set_out(b, 2, &imp->obstacle, sizeof(imp->obstacle));
    if (flags & OWNERENTITY) set_out(b, 3, &imp->owner, sizeof(imp->owner));
    if (flags & SUBOBSTACLETOUCHED) set_out(b, 4, &imp->sub_obstacle, sizeof(imp->sub_obstacle));
    if (flags & TOUCHEDFACE) set_out_int(b, 5, imp->touched_face);
    if (flags & TOUCHINGFACE) set_out_int(b, 6, imp->touching_face);
    if (flags & TOUCHEDVERTEX) set_out_int(b, 7, imp->touched_vertex);
    if (flags & TOUCHINGVERTEX) set_out_int(b, 8, imp->touching_vertex);
    if (flags & IMPACTPOINT) set_out(b, 9, &imp->point, sizeof(imp->point));
    if (flags & IMPACTNORMAL) set_out(b, 10, &imp->normal, sizeof(imp->normal));
    set_out_int(b, 0, index);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Sliding, avoiding and homing
 * ============================================================================ */

/*
 * CollBallFace::DetectCollisionWithBallAndFace (the DLL's): the ball (in the
 * face's space) within its radius above the face; the contact on the face,
 * an edge or a corner. As in the DLL, the third corner is tried under the
 * second's flag.
 */
static bool ball_face(const entity_faces_t *f, int32_t face, eng_vector_t ball, float radius, eng_vector_t *contact)
{
    eng_vector_t norm = face_normal(f, face), posa = face_vertex(f, f->indices[face * 3]);
    eng_vector_t pos_posa = vsub(ball, posa);
    float y = norm.x * pos_posa.x + norm.y * pos_posa.y + norm.z * pos_posa.z;
    if (y >= radius || y <= 0.0f) {
        return false;
    }
    eng_vector_t posb = face_vertex(f, f->indices[face * 3 + 1]), posc = face_vertex(f, f->indices[face * 3 + 2]);
    eng_vector_t n_ab = vsub(posb, posa);
    float mab = eng_vector_magnitude(&n_ab);
    n_ab = v3(n_ab.x / mab, n_ab.y / mab, n_ab.z / mab);
    eng_vector_t e_ab = eng_vector_cross(&n_ab, &norm);
    float ab = pos_posa.x * e_ab.x + pos_posa.y * e_ab.y + pos_posa.z * e_ab.z;
    if (ab >= radius) {
        return false;
    }
    eng_vector_t n_ac = vsub(posc, posa);
    float mac = eng_vector_magnitude(&n_ac);
    n_ac = v3(n_ac.x / mac, n_ac.y / mac, n_ac.z / mac);
    eng_vector_t e_ac = eng_vector_cross(&norm, &n_ac);
    float ac = pos_posa.x * e_ac.x + pos_posa.y * e_ac.y + pos_posa.z * e_ac.z;
    if (ac >= radius) {
        return false;
    }
    eng_vector_t n_bc = vsub(posc, posb);
    float mbc = eng_vector_magnitude(&n_bc);
    n_bc = v3(n_bc.x / mbc, n_bc.y / mbc, n_bc.z / mbc);
    eng_vector_t e_bc = eng_vector_cross(&n_bc, &norm), pos_posb = vsub(ball, posb);
    float bc = pos_posb.x * e_bc.x + pos_posb.y * e_bc.y + pos_posb.z * e_bc.z;
    if (bc >= radius) {
        return false;
    }
    float radius2 = radius * radius, s;
    if (ab <= 0.0f && bc <= 0.0f && ac <= 0.0f) { /* over the face */
        s = pos_posa.x * n_ab.x + pos_posa.y * n_ab.y + pos_posa.z * n_ab.z;
        *contact = v3(posa.x + n_ab.x * s + ab * e_ab.x, posa.y + n_ab.y * s + ab * e_ab.y,
                      posa.z + n_ab.z * s + ab * e_ab.z);
        return true;
    }
    uint32_t corners = 0x111;
    if (bc > 0.0f) {
        s = pos_posb.x * n_bc.x + pos_posb.y * n_bc.y + pos_posb.z * n_bc.z;
        if (s < mbc) {
            if (s > 0.0f) {
                if (bc * bc + y * y >= radius2) {
                    return false;
                }
                *contact = v3(posb.x + s * n_bc.x, posb.y + s * n_bc.y, posb.z + s * n_bc.z);
                return true;
            }
            corners &= 0x110;
        }
    }
    if (ab > 0.0f) {
        s = pos_posa.x * n_ab.x + pos_posa.y * n_ab.y + pos_posa.z * n_ab.z;
        if (s < mab) {
            if (s > 0.0f) {
                if (ab * ab + y * y >= radius2) {
                    return false;
                }
                *contact = v3(posa.x + s * n_ab.x, posa.y + s * n_ab.y, posa.z + s * n_ab.z);
                return true;
            }
            corners &= 0x101;
        }
    }
    if (ac > 0.0f) {
        s = pos_posa.x * n_ac.x + pos_posa.y * n_ac.y + pos_posa.z * n_ac.z;
        if (s > 0.0f) {
            if (s < mac) {
                if (ac * ac + y * y >= radius2) {
                    return false;
                }
                *contact = v3(posa.x + s * n_ac.x, posa.y + s * n_ac.y, posa.z + s * n_ac.z);
                return true;
            }
            corners &= 0x011;
        }
    }
    if ((corners & 0x100) && eng_vector_magnitude(&pos_posa) < radius) {
        *contact = posa;
        return true;
    }
    if ((corners & 0x010) && eng_vector_magnitude(&pos_posb) < radius) {
        *contact = posb;
        return true;
    }
    eng_vector_t pos_posc = vsub(ball, posc);
    if ((corners & 0x010) && eng_vector_magnitude(&pos_posc) < radius) {
        *contact = posc;
        return true;
    }
    return false;
}

/* The Object Slider's state: where the ball was last frame (A_OBS) */
typedef struct slider_state {
    eng_vector_t old;
} slider_state_t;

/*
 * ThreadObjectOBS: the ball (the target) pushed off each face of the object
 * it is within its radius of, in the object's space (the radius not scaled);
 * the faces of a mesh over 64 vertices culled when wholly on a side of the
 * ball's box (along its longest axis only when it has many vertices a face).
 */
static void slide_on_object(nmo_engine_t *engine, nmo_object_id_t object, int32_t ball, eng_vector_t ball_pos,
                            float radius, int32_t *steps, nmo_object_id_t touched_group, bool *touched)
{
    int32_t index = eng_entity_index(engine, object);
    if (index < 0 || index == ball) {
        return;
    }
    eng_vector_t min, max;
    bounding_box(engine, index, &min, &max);
    if (!(min.x - radius <= ball_pos.x && max.x + radius >= ball_pos.x && min.y - radius <= ball_pos.y &&
          max.y + radius >= ball_pos.y && min.z - radius <= ball_pos.z && max.z + radius >= ball_pos.z)) {
        return;
    }
    entity_faces_t f;
    if (!entity_faces(engine, object, &f) || f.vertices == f.sprite) {
        return;
    }
    eng_vector_t local = eng_entity_position(engine, ball, index), contact;
    bool hit = false;
    uint32_t *flags = NULL;
    if (f.vertex_count > 64) {
        flags = (uint32_t *)malloc((size_t)f.vertex_count * sizeof(uint32_t));
        eng_vector_t bmin = v3(local.x - radius, local.y - radius, local.z - radius);
        eng_vector_t bmax = v3(local.x + radius, local.y + radius, local.z + radius);
        uint32_t mask = V_ALL;
        if (f.vertex_count > f.face_count * 2) {
            eng_vector_t lmin, lmax;
            eng_entity_local_box(engine, index, &lmin, &lmax);
            eng_vector_t d = vsub(lmax, lmin);
            int axis = 0;
            float most = d.x;
            if (most < d.y) {
                most = d.y;
                axis = 1;
            }
            if (most < d.z) {
                axis = 2;
            }
            mask = axis == 0 ? (V_LEFT | V_RIGHT) : axis == 1 ? (V_TOP | V_BOTTOM) : (V_FRONT | V_BACK);
        }
        for (int32_t v = 0; flags != NULL && v < f.vertex_count; v++) {
            flags[v] = box_flags(face_vertex(&f, v), bmin, bmax) & mask;
        }
    }
    for (int32_t face = 0; face < f.face_count; face++) {
        const uint16_t *k = &f.indices[face * 3];
        if (flags != NULL && (flags[k[0]] & flags[k[1]] & flags[k[2]])) {
            continue;
        }
        if (ball_face(&f, face, local, radius, &contact)) {
            eng_vector_t dif = vsub(local, contact);
            float scale = radius / eng_vector_magnitude(&dif) - 1.0f;
            dif = v3(dif.x * scale, dif.y * scale, dif.z * scale);
            eng_entity_translate(engine, ball, &dif, index, false);
            local = v3(local.x + dif.x, local.y + dif.y, local.z + dif.z);
            *steps = 0;
            hit = true;
        }
    }
    free(flags);
    if (hit) {
        if (touched_group != 0) {
            if (!*touched) {
                eng_group_clear(engine, touched_group);
            }
            eng_group_add(engine, touched_group, object);
        }
        *touched = true;
    }
}

static bool in_group(const nmo_engine_t *engine, nmo_object_id_t group, nmo_object_id_t object)
{
    int32_t count = nmo_engine_group_count(engine, group);
    for (int32_t i = 0; i < count; i++) {
        if (nmo_engine_group_object(engine, group, i) == object) {
            return true;
        }
    }
    return false;
}

/* ParseHierarchyChildOBS: the children (last first) of an entity whose hierarchy box is near the ball,
   those in the group slid on */
static void slide_on_children(nmo_engine_t *engine, int32_t entity, int32_t ball, eng_vector_t ball_pos, float radius,
                              int32_t *steps, nmo_object_id_t touched_group, bool *touched, nmo_object_id_t group)
{
    eng_vector_t min, max;
    if (!eng_entity_hierarchical_box(engine, entity, &min, &max)) {
        min = max = eng_entity_position(engine, entity, -1);
    }
    if (!(min.x - radius <= ball_pos.x && max.x + radius >= ball_pos.x && min.y - radius <= ball_pos.y &&
          max.y + radius >= ball_pos.y && min.z - radius <= ball_pos.z && max.z + radius >= ball_pos.z)) {
        return;
    }
    for (int32_t child = engine->entities.data[entity].last_child; child >= 0;
         child = engine->entities.data[child].prev_sibling) {
        nmo_object_id_t id = engine->entities.data[child].id;
        if (in_group(engine, group, id)) {
            slide_on_object(engine, id, ball, ball_pos, radius, steps, touched_group, touched);
        }
        if (engine->entities.data[child].first_child >= 0) {
            slide_on_children(engine, child, ball, ball_pos, radius, steps, touched_group, touched, group);
        }
    }
}

/*
 * Object Slider: the target, a ball of Radius, kept off the faces of the
 * Group's objects (with Place Optimization, of those under the places);
 * with an Accuracy, tested at steps from where it was last frame. The
 * Reaction is how far it was pushed.
 */
static int bb_object_slider(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ball = target_entity(b);
    if (ball < 0) {
        if (IN_ACTIVE(b, 0)) {
            IN_OFF(b, 0);
        }
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    slider_state_t *state = nmo_engine_block_state(b, sizeof(*state));
    if (state == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    eng_vector_t init = eng_entity_position(engine, ball, -1);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        state->old = init;
    }
    float radius = get_in_float(b, 0, 1.0f);
    bool places = get_local_int(b, 3, 0) != 0;
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 1);
    if (group == 0) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t touched_group = 0;
    nmo_engine_block_get_local_value(b, 2, &touched_group, sizeof(touched_group));
    int32_t accuracy = get_local_int(b, 0, 0);
    if (accuracy < 0) {
        accuracy = 0;
    }
    int32_t steps = accuracy;
    eng_vector_t ball_pos = init, step = v3(0.0f, 0.0f, 0.0f);
    if (accuracy != 0) {
        eng_vector_t d = vsub(init, state->old);
        steps = (int32_t)(sqrt((double)(d.z * d.z + d.y * d.y + d.x * d.x)) / (radius - 0.1));
        if (steps > accuracy) {
            steps = accuracy;
        }
        float inv = (float)(1.0 / ((double)steps + 1.0));
        step = v3(inv * d.x, d.y * inv, d.z * inv);
        ball_pos = state->old;
    }
    bool touched = false;
    do {
        if (accuracy != 0) {
            ball_pos = v3(step.x + ball_pos.x, ball_pos.y + step.y, ball_pos.z + step.z);
            eng_entity_set_position(engine, ball, &ball_pos, -1, false);
        }
        if (places) {
            for (size_t i = 0; i < engine->entities.count; i++) {
                if (!engine->entities.data[i].destroyed &&
                    nmo_engine_object_class(engine, engine->entities.data[i].id) == NMO_CID_PLACE) {
                    slide_on_children(engine, (int32_t)i, ball, ball_pos, radius, &steps, touched_group, &touched,
                                      group);
                }
            }
        } else {
            int32_t count = nmo_engine_group_count(engine, group);
            for (int32_t i = 0; i < count; i++) {
                slide_on_object(engine, nmo_engine_group_object(engine, group, i), ball, ball_pos, radius, &steps,
                                touched_group, &touched);
            }
        }
    } while (steps-- != 0);
    eng_vector_t reaction = vsub(eng_entity_position(engine, ball, -1), ball_pos);
    set_out(b, 0, &reaction, sizeof(reaction));
    if (accuracy != 0) {
        state->old = eng_entity_position(engine, ball, -1);
    }
    OUT_ON(b, touched ? 0 : 1);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* GetObstacleCount and GetObstacle: the fixed obstacles then the moving ones, in their attributes' order */
static size_t obstacles_listed(nmo_engine_t *engine, nmo_object_id_t **out)
{
    int32_t types[2] = {fixed_attribute(engine), moving_attribute(engine)};
    *out = (nmo_object_id_t *)malloc((engine->attributes.values.count + 1) * sizeof(nmo_object_id_t));
    size_t count = 0;
    for (int t = 0; *out != NULL && t < 2; t++) {
        for (size_t i = 0; types[t] >= 0 && i < engine->attributes.values.count; i++) {
            const eng_attribute_t *attribute = &engine->attributes.values.data[i];
            if (attribute->type == types[t] && eng_entity_index(engine, attribute->object) >= 0) {
                (*out)[count++] = attribute->object;
            }
        }
    }
    return count;
}

/* The push away from the obstacles near a point, within Factor times their radius */
static eng_vector_t obstacle_push(nmo_engine_t *engine, nmo_object_id_t obstacle, eng_vector_t center, float factor,
                                  bool *near)
{
    int32_t index = eng_entity_index(engine, obstacle);
    eng_vector_t oc;
    eng_entity_bary_center(engine, index, &oc);
    float reach = eng_entity_radius(engine, index) * factor;
    eng_vector_t d = vsub(oc, center);
    float dist = (float)sqrt((double)(d.z * d.z + d.y * d.y + d.x * d.x));
    *near = dist < reach;
    if (!*near) {
        return v3(0.0f, 0.0f, 0.0f);
    }
    float amplitude = (1.0f - dist / reach) * 10.0f / dist;
    return normalized(v3((center.x - oc.x) * amplitude, (center.y - oc.y) * amplitude, (center.z - oc.z) * amplitude));
}

/*
 * Avoid Obstacles (obsolete): the Direction turned away from the obstacles
 * the target is near (within Influence Factor times their radius), by
 * Inertia; with Keep On Floor, the target on its floor (the upper whenever
 * there is one) and the turn horizontal.
 */
static int bb_avoid_obstacles(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, IN_ACTIVE(b, 0) ? 0 : 1);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t dir = get_in_vector(b, 0, 0.0f, 0.0f, 1.0f), local = dir;
    int32_t ref = input_entity(b, 1);
    if (ref >= 0) {
        eng_entity_transform_vector(engine, ref, &dir, &local, -1, false);
    }
    eng_vector_normalize(&dir);
    float inertia = get_in_float(b, 2, 0.5f), factor = get_in_float(b, 3, 2.0f);
    bool keep = get_in_int(b, 4, 1) != 0;
    if (keep) {
        eng_vector_t min, max;
        bounding_box(engine, entity, &min, &max);
        eng_vector_t pos = v3((max.x + min.x) * 0.5f, min.y, (max.z + min.z) * 0.5f);
        eng_floor_point_t fp;
        if (eng_floor_nearest(engine, &pos, &fp, 0) != CKFLOOR_NOFLOOR) {
            pos = eng_entity_position(engine, entity, -1);
            if (fp.up_floor != 0) {
                pos.y += fp.up_distance;
            } else if (fp.down_floor != 0) {
                pos.y += fp.down_distance;
            }
            eng_entity_set_position(engine, entity, &pos, -1, false);
        }
    }
    eng_vector_t center, deviation = v3(0.0f, 0.0f, 0.0f);
    eng_entity_bary_center(engine, entity, &center);
    nmo_object_id_t *obstacles = NULL;
    size_t count = obstacles_listed(engine, &obstacles);
    for (size_t i = 0; i < count; i++) {
        bool near;
        eng_vector_t push = obstacle_push(engine, obstacles[i], center, factor, &near);
        if (near) {
            deviation.x += push.x;
            if (!keep) {
                deviation.y += push.y;
            }
            deviation.z += push.z;
        }
    }
    free(obstacles);
    eng_vector_normalize(&deviation);
    float rest = 1.0f - inertia;
    dir = v3(dir.x * inertia + deviation.x * rest, dir.y * inertia + deviation.y * rest,
             dir.z * inertia + deviation.z * rest);
    if (ref >= 0) {
        local = dir;
        eng_entity_transform_vector(engine, ref, &dir, &local, -1, true);
    }
    eng_vector_normalize(&dir);
    set_out(b, 0, &dir, sizeof(dir));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Home on Entity: the target, a missile, turned toward the Destination and
 * away from the obstacles near it (Inertia keeping 90 to 100% of its
 * heading), then moved Initial Velocity along its old heading; Target
 * Reach within the destination's radius, Obstacle Reach within an
 * obstacle's.
 */
static int bb_home_on_entity(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
    }
    int32_t missile = target_entity(b), target = input_entity(b, 0);
    if (missile < 0 || target < 0) {
        nmo_engine_block_activate_output(b, 2, false);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float speed = get_in_float(b, 1, 0.0f);
    float inertia = 0.9f + get_in_float(b, 2, 0.5f) / 10.0f;
    float factor = get_in_float(b, 3, 1.5f);
    eng_vector_t dir, up, right;
    eng_entity_orientation(engine, missile, &dir, &up, &right, -1);
    eng_vector_t velocity = dir;
    eng_vector_normalize(&velocity);
    eng_vector_t center = eng_entity_position(engine, missile, -1), goal = eng_entity_position(engine, target, -1);
    eng_vector_t deviation = vsub(goal, center);
    eng_vector_normalize(&deviation);
    nmo_object_id_t *obstacles = NULL;
    size_t count = obstacles_listed(engine, &obstacles);
    for (size_t i = 0; i < count; i++) {
        bool near;
        eng_vector_t push = obstacle_push(engine, obstacles[i], center, factor, &near);
        if (near) {
            deviation = v3(push.x + deviation.x, push.y + deviation.y, push.z + deviation.z);
            eng_vector_normalize(&deviation);
        }
    }
    float rest = 1.0f - inertia;
    deviation = v3(velocity.x * inertia + deviation.x * rest, velocity.y * inertia + deviation.y * rest,
                   velocity.z * inertia + deviation.z * rest);
    eng_vector_normalize(&deviation);
    up = eng_vector_cross(&deviation, &dir);
    eng_vector_normalize(&up);
    dir = deviation;
    eng_vector_normalize(&dir);
    right = eng_vector_cross(&up, &dir);
    eng_vector_t scale = eng_entity_scale(engine, missile, true);
    eng_entity_set_orientation(engine, missile, &dir, &up, &right, -1, false);
    eng_entity_set_scale(engine, missile, &scale, false, true);
    eng_vector_t next = v3(center.x + velocity.x * speed, center.y + velocity.y * speed, center.z + velocity.z * speed);
    eng_entity_set_position(engine, missile, &next, -1, false);
    eng_vector_t to_goal = vsub(next, goal);
    if (eng_vector_magnitude(&to_goal) < eng_entity_radius(engine, target)) {
        nmo_object_id_t id = engine->entities.data[target].id;
        set_out(b, 0, &id, sizeof(id));
        OUT_ON(b, 0);
        free(obstacles);
        return NMO_ENGINE_BR_OK;
    }
    for (size_t i = 0; i < count; i++) {
        int32_t index = eng_entity_index(engine, obstacles[i]);
        eng_vector_t oc;
        eng_entity_bary_center(engine, index, &oc);
        eng_vector_t d = vsub(next, oc);
        if (eng_vector_magnitude(&d) < eng_entity_radius(engine, index)) {
            set_out(b, 0, &obstacles[i], sizeof(obstacles[i]));
            OUT_ON(b, 1);
            free(obstacles);
            return NMO_ENGINE_BR_OK;
        }
    }
    free(obstacles);
    OUT_ON(b, 2);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Nodal paths (N3DGraph)
 * ============================================================================ */

#define N3DGRAPH_IDENTIFIER 2000666u

/* The nodal path of a group (its Nodal Path attribute's graph); NULL when it has none */
static nodal_graph_t *graph_of(nmo_engine_t *engine, nmo_object_id_t group)
{
    collision_manager_t *cm = cm_state(engine);
    for (size_t i = 0; cm != NULL && group != 0 && i < cm->graphs.count; i++) {
        if (cm->graphs.data[i].group == group) {
            return &cm->graphs.data[i];
        }
    }
    return NULL;
}

/* SeekPosition: the state of a node; -1 when it is none */
static int32_t graph_seek(const nodal_graph_t *graph, nmo_object_id_t node)
{
    for (int32_t i = 0; i < graph->count; i++) {
        if (graph->states[i].node == node) {
            return i;
        }
    }
    return -1;
}

static nodal_edge_t *graph_edge(nodal_graph_t *graph, nmo_object_id_t s, nmo_object_id_t e)
{
    int32_t si = graph_seek(graph, s), ei = graph_seek(graph, e);
    if (si < 0 || ei < 0) {
        return NULL;
    }
    for (int32_t k = 0; k < graph->states[si].edge_count; k++) {
        if (graph->states[si].edges[k].successor == ei) {
            return &graph->states[si].edges[k];
        }
    }
    return NULL;
}

/* DeleteState: the node out, its links and the links to it too */
static void graph_delete_state(nodal_graph_t *graph, int32_t ni)
{
    free(graph->states[ni].edges);
    memmove(&graph->states[ni], &graph->states[ni + 1], (size_t)(graph->count - ni - 1) * sizeof(nodal_state_t));
    graph->count--;
    for (int32_t i = 0; i < graph->count; i++) {
        nodal_state_t *state = &graph->states[i];
        for (int32_t k = 0; k < state->edge_count;) {
            int32_t successor = state->edges[k].successor;
            if (successor == ni) {
                memmove(&state->edges[k], &state->edges[k + 1], (size_t)(state->edge_count - k - 1) * sizeof(nodal_edge_t));
                state->edge_count--;
            } else {
                state->edges[k].successor = successor > ni ? successor - 1 : successor;
                k++;
            }
        }
    }
}

/* UpdateDistance: the nodes' positions (a node gone leaves the graph), then the links' lengths */
static void graph_update_distance(nmo_engine_t *engine, nodal_graph_t *graph)
{
    for (int32_t i = 0; i < graph->count;) {
        int32_t index = eng_entity_index(engine, graph->states[i].node);
        if (index < 0 && eng_find_object(engine, graph->states[i].node) == NULL) {
            graph_delete_state(graph, i); /* GetSafeEntity */
            continue;
        }
        graph->states[i].position = index >= 0 ? eng_entity_position(engine, index, -1) : v3(0.0f, 0.0f, 0.0f);
        i++;
    }
    for (int32_t i = 0; i < graph->count; i++) {
        for (int32_t k = 0; k < graph->states[i].edge_count; k++) {
            eng_vector_t d = vsub(graph->states[i].position, graph->states[graph->states[i].edges[k].successor].position);
            graph->states[i].edges[k].distance = eng_vector_magnitude(&d);
        }
    }
}

/*
 * CreateFromChunk: the graph the Create Nodal Path block saved in its state
 * chunk local (the nodes, their occupiers in version 2, each link's
 * activity, difficulty and successor), without links to themselves (Clean),
 * the lengths from the nodes' positions.
 */
static void graph_load(nmo_engine_t *engine, nodal_graph_t *graph, nmo_object_id_t chunk_param)
{
    nmo_object_t *object = chunk_param != 0 ? eng_find_object(engine, chunk_param) : NULL;
    const nmo_parameter_state_t *value =
        object != NULL ? (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_PARAMETER)
                       : NULL;
    if (value == NULL || value->mode != CKPARAM_MODE_SUBCHUNK || value->subchunk == NULL) {
        return;
    }
    const eng_module_t *module = eng_module_of(engine, chunk_param, NULL);
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
    nmo_chunk_t *chunk = arena != NULL ? nmo_chunk_clone(value->subchunk, arena) : NULL;
    int32_t version = 0, count = 0;
    if (chunk != NULL && nmo_chunk_start_read(chunk) == NMO_OK &&
        nmo_chunk_seek_identifier(chunk, N3DGRAPH_IDENTIFIER) == NMO_OK && nmo_chunk_read_int(chunk, &version) == NMO_OK &&
        (version == 1 || version == 2) && nmo_chunk_read_int(chunk, &count) == NMO_OK && count >= 0) {
        graph->states = (nodal_state_t *)calloc((size_t)count + 1, sizeof(nodal_state_t));
        for (int32_t i = 0; graph->states != NULL && i < count; i++) {
            nodal_state_t *state = &graph->states[graph->count++];
            nmo_object_id_t id = 0, occupier = 0;
            int32_t edges = 0;
            nmo_chunk_read_object_id(chunk, &id);
            if (version == 2) {
                nmo_chunk_read_object_id(chunk, &occupier);
            }
            state->node = id != 0 && module != NULL ? eng_module_id(module, id) : 0;
            state->occupier = occupier != 0 && module != NULL ? eng_module_id(module, occupier) : 0;
            nmo_chunk_read_int(chunk, &edges);
            state->edges = edges > 0 ? (nodal_edge_t *)calloc((size_t)edges, sizeof(nodal_edge_t)) : NULL;
            for (int32_t k = 0; state->edges != NULL && k < edges; k++) {
                nodal_edge_t *edge = &state->edges[state->edge_count++];
                nmo_chunk_read_int(chunk, &edge->active);
                nmo_chunk_read_float(chunk, &edge->difficulty);
                nmo_chunk_read_int(chunk, &edge->successor);
            }
        }
        for (int32_t i = 0; i < graph->count; i++) {
            nodal_state_t *state = &graph->states[i];
            for (int32_t k = 0; k < state->edge_count;) {
                int32_t successor = state->edges[k].successor;
                if (successor == i || successor < 0 || successor >= graph->count) { /* Clean */
                    memmove(&state->edges[k], &state->edges[k + 1],
                            (size_t)(state->edge_count - k - 1) * sizeof(nodal_edge_t));
                    state->edge_count--;
                } else {
                    k++;
                }
            }
        }
        graph_update_distance(engine, graph);
    }
    nmo_arena_destroy(arena);
}

/* ActivateEdge: the dynamic activity, or both */
static void graph_activate_edge(nodal_graph_t *graph, nmo_object_id_t s, nmo_object_id_t e, bool active, bool dynamic)
{
    nodal_edge_t *edge = graph_edge(graph, s, e);
    if (edge == NULL) {
        return;
    }
    int32_t bits = dynamic ? EDGE_DYNAMIC : (EDGE_BASIC | EDGE_DYNAMIC);
    edge->active = active ? (edge->active | bits) : (edge->active & ~bits);
}

/* ActivateState: the dynamic activity of the node's links and of the links to it */
static void graph_activate_state(nodal_graph_t *graph, nmo_object_id_t node, bool active)
{
    int32_t sp = graph_seek(graph, node);
    for (int32_t i = 0; sp >= 0 && i < graph->count; i++) {
        for (int32_t k = 0; k < graph->states[i].edge_count; k++) {
            nodal_edge_t *edge = &graph->states[i].edges[k];
            if (i == sp || edge->successor == sp) {
                edge->active = active ? (edge->active | EDGE_DYNAMIC) : (edge->active & ~EDGE_DYNAMIC);
            }
        }
    }
}

/* ResetActivity: no occupiers, the dynamic activity back to the basic one */
static void graph_reset_activity(nodal_graph_t *graph)
{
    for (int32_t i = 0; i < graph->count; i++) {
        graph->states[i].occupier = 0;
        for (int32_t k = 0; k < graph->states[i].edge_count; k++) {
            nodal_edge_t *edge = &graph->states[i].edges[k];
            edge->active = (edge->active & EDGE_BASIC) ? (edge->active | EDGE_DYNAMIC) : (edge->active & ~EDGE_DYNAMIC);
        }
    }
}

/* SetOccupier: false when another occupies the node (or it is none) */
static bool graph_set_occupier(nodal_graph_t *graph, nmo_object_id_t node, nmo_object_id_t occupier)
{
    int32_t i = graph_seek(graph, node);
    if (i < 0) {
        return false;
    }
    if (occupier != 0 && graph->states[i].occupier != 0 && graph->states[i].occupier != occupier) {
        return false;
    }
    graph->states[i].occupier = occupier;
    return true;
}

static nmo_object_id_t graph_occupier(const nodal_graph_t *graph, nmo_object_id_t node)
{
    int32_t i = graph_seek(graph, node);
    return i >= 0 ? graph->states[i].occupier : 0;
}

static bool graph_edge_active(nodal_graph_t *graph, nmo_object_id_t s, nmo_object_id_t e)
{
    const nodal_edge_t *edge = graph_edge(graph, s, e);
    return edge != NULL && (edge->active & EDGE_DYNAMIC) != 0;
}

static float graph_distance(const nodal_graph_t *graph, int32_t a, int32_t b)
{
    eng_vector_t d = vsub(graph->states[a].position, graph->states[b].position);
    return eng_vector_magnitude(&d);
}

/*
 * N3DGraph::FindPath: A* over the links active (basic or dynamic), not
 * through occupied nodes with `occupation`; a link costs its difficulty
 * times its length. The path's nodes, start first, in `path` (when given);
 * the node to go to next, 0 when there is no way.
 */
static nmo_object_id_t graph_find_path(nodal_graph_t *graph, nmo_object_id_t start, nmo_object_id_t goal,
                                       nmo_object_id_t **path, int32_t *path_count, bool basic, bool occupation)
{
    if (path != NULL) {
        *path = NULL;
        *path_count = 0;
    }
    int32_t activity = basic ? EDGE_BASIC : EDGE_DYNAMIC;
    int32_t s = graph_seek(graph, start), g = graph_seek(graph, goal);
    if (s < 0 || g < 0) {
        return 0;
    }
    int32_t *open = (int32_t *)malloc(((size_t)graph->count * 2 + 4) * sizeof(int32_t));
    int32_t opened = 0, capacity = graph->count * 2 + 4;
    if (open == NULL) {
        return 0;
    }
    nodal_state_t *states = graph->states;
    states[s].g = 0.0f;
    states[s].h = graph_distance(graph, s, g);
    states[s].f = states[s].g + states[s].h;
    states[s].parent = -1;
#define PUSH_OPEN(n)                                                                    \
    do {                                                                                \
        states[n].flag |= 1u;                                                           \
        int32_t at_ = 0;                                                                \
        while (at_ < opened && !(states[n].f < states[open[at_]].f)) at_++;             \
        if (opened == capacity) {                                                       \
            capacity *= 2;                                                              \
            int32_t *grown_ = (int32_t *)realloc(open, (size_t)capacity * sizeof(int32_t)); \
            if (grown_ == NULL) break;                                                  \
            open = grown_;                                                              \
        }                                                                               \
        memmove(&open[at_ + 1], &open[at_], (size_t)(opened - at_) * sizeof(int32_t));  \
        open[at_] = (n);                                                                \
        opened++;                                                                       \
    } while (0)
    PUSH_OPEN(s);
    nmo_object_id_t next = 0;
    bool found = false;
    while (opened > 0) {
        int32_t n = open[0];
        memmove(&open[0], &open[1], (size_t)(opened - 1) * sizeof(int32_t));
        opened--;
        states[n].flag &= ~1u;
        if (n == g) {
            found = true;
            int32_t count = 0;
            for (int32_t k = n; states[k].parent >= 0 && k != states[k].parent; k = states[k].parent) {
                count++;
            }
            if (path != NULL) {
                *path = (nmo_object_id_t *)malloc(((size_t)count + 1) * sizeof(nmo_object_id_t));
                int32_t k = n, at = count;
                while (*path != NULL && states[k].parent >= 0 && k != states[k].parent) {
                    (*path)[at--] = states[k].node;
                    k = states[k].parent;
                }
                if (*path != NULL) {
                    (*path)[0] = states[k].node;
                    *path_count = count + 1;
                    next = count > 0 ? (*path)[1] : 0;
                }
            } else {
                int32_t k = n;
                while (states[k].parent >= 0 && k != states[k].parent) {
                    next = states[k].node;
                    k = states[k].parent;
                }
            }
            break;
        }
        for (int32_t e = 0; e < states[n].edge_count; e++) {
            const nodal_edge_t *edge = &states[n].edges[e];
            if (!(edge->active & activity)) {
                continue;
            }
            int32_t m = edge->successor;
            if (occupation && states[m].occupier != 0) {
                continue;
            }
            float newg = states[n].g + edge->difficulty * edge->distance;
            if (states[m].flag != 0 && states[m].g <= newg) {
                continue;
            }
            states[m].parent = n;
            states[m].g = newg;
            states[m].h = graph_distance(graph, m, g);
            states[m].f = states[m].g + states[m].h;
            states[m].flag &= ~2u;
            if (!(states[m].flag & 1u)) {
                PUSH_OPEN(m);
            }
        }
        states[n].flag |= 2u;
    }
#undef PUSH_OPEN
    (void)found;
    for (int32_t i = 0; i < graph->count; i++) {
        states[i].flag = 0;
    }
    free(open);
    return next;
}

/* The group's nodal path the block's input names; NULL (an exception in the DLL) when it has none */
static nodal_graph_t *input_graph(blk_t *b, int pos)
{
    return graph_of(b->engine, nmo_engine_block_get_input_object(b, pos));
}

/*
 * Create Nodal Path: its group's graph, loaded from the block's state chunk
 * (LOAD), its activity reset with the behavior. The debug display is not
 * drawn.
 */
static int cb_create_nodal_path(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    collision_manager_t *cm = cm_state(engine);
    if (cm == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t group = nmo_engine_block_owner(b);
    nodal_graph_t *graph = graph_of(engine, group);
    if (message == NMO_ENGINE_MSG_LOAD) {
        if (graph == NULL) {
            if (!ENG_VEC_PUSH(cm->graphs, graph)) {
                return NMO_ENGINE_BR_OK;
            }
            graph->group = group;
        } else {
            nodal_graph_free(graph);
        }
        eng_param_t *chunk = block_local(b, 2);
        graph_load(engine, graph, chunk != NULL ? chunk->id : 0);
    } else if (message == NMO_ENGINE_MSG_RESET && graph != NULL) {
        graph_reset_activity(graph);
    }
    return NMO_ENGINE_BR_OK;
}

/* Activate Link: the link (and with Two Way its way back) on or off */
static int bb_activate_link(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "Given Group isn't a Network" */
    }
    nmo_object_id_t s = nmo_engine_block_get_input_object(b, 1), e = nmo_engine_block_get_input_object(b, 2);
    bool active = get_in_int(b, 3, 0) != 0;
    graph_activate_edge(graph, s, e, active, true);
    if (get_in_int(b, 4, 0) != 0) {
        graph_activate_edge(graph, e, s, active, true);
    }
    return NMO_ENGINE_BR_OK;
}

/* Activate Node: the links from and to the node on or off */
static int bb_activate_node(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    graph_activate_state(graph, nmo_engine_block_get_input_object(b, 1), get_in_int(b, 2, 0) != 0);
    return NMO_ENGINE_BR_OK;
}

/* The path into the block's group (cleared, then the nodes start first); Path Found when it has any */
static int path_into(blk_t *b, nodal_graph_t *graph, nmo_object_id_t s, nmo_object_id_t e, nmo_object_id_t group)
{
    nmo_object_id_t *path = NULL;
    int32_t count = 0;
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_group_clear(b->engine, group);
    graph_find_path(graph, s, e, &path, &count, false, false);
    for (int32_t i = 0; i < count; i++) {
        eng_group_add(b->engine, group, path[i]);
    }
    free(path);
    OUT_ON(b, nmo_engine_group_count(b->engine, group) > 0 ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static nmo_object_id_t output_object(blk_t *b, int pos)
{
    nmo_object_id_t id = 0;
    nmo_engine_block_get_output_value(b, pos, &id, sizeof(id));
    return id;
}

/* Find Path: the path from Start Object to Goal Object in the Object List */
static int bb_find_path(blk_t *b)
{
    IN_OFF(b, 0);
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t s = nmo_engine_block_get_input_object(b, 1), e = nmo_engine_block_get_input_object(b, 2);
    if (graph_seek(graph, s) < 0 || graph_seek(graph, e) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return path_into(b, graph, s, e, output_object(b, 0));
}

/* The Find Path blocks' output group (Find Curved Nodal Path's local), made at LOAD */
static nmo_object_id_t made_group(blk_t *b)
{
    return eng_create_object(b->engine, NMO_CID_GROUP, "FindPathGroup");
}

static int cb_find_path(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD && nmo_engine_block_owner(b) != 0) {
        nmo_object_id_t group = made_group(b);
        set_out(b, 0, &group, sizeof(group));
    }
    return NMO_ENGINE_BR_OK;
}

/* Entity Find Nodal Path: from the node of the group nearest the target to Goal Object */
static int bb_entity_find_nodal_path(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    nodal_graph_t *graph = graph_of(engine, group);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t object = target_entity(b);
    if (object < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    eng_vector_t pos = eng_entity_position(engine, object, -1);
    nmo_object_id_t s = 0;
    float least = FLT_MAX;
    int32_t count = nmo_engine_group_count(engine, group);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_group_object(engine, group, i);
        int32_t index = eng_entity_index(engine, id);
        if (index < 0) {
            continue;
        }
        eng_vector_t d = vsub(eng_entity_position(engine, index, -1), pos);
        float m = d.x * d.x + d.y * d.y + d.z * d.z;
        if (m < least) {
            least = m;
            s = id;
        }
    }
    nmo_object_id_t e = nmo_engine_block_get_input_object(b, 1);
    if (graph_seek(graph, s) < 0 || graph_seek(graph, e) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "Start/End point isn't in the Network" */
    }
    return path_into(b, graph, s, e, output_object(b, 0));
}

/* Find Curved Nodal Path: the path as the output curve's control points, of Fitting Coef */
static int bb_find_curved_nodal_path(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t s = nmo_engine_block_get_input_object(b, 1), e = nmo_engine_block_get_input_object(b, 2);
    if (graph_seek(graph, s) < 0 || graph_seek(graph, e) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t group = 0;
    nmo_engine_block_get_local_value(b, 0, &group, sizeof(group));
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_group_clear(engine, group);
    nmo_object_id_t *path = NULL;
    int32_t count = 0;
    graph_find_path(graph, s, e, &path, &count, false, false);
    for (int32_t i = 0; i < count; i++) {
        eng_group_add(engine, group, path[i]);
    }
    free(path);
    count = nmo_engine_group_count(engine, group);
    if (count == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t curve = output_object(b, 0);
    eng_curve_properties_t properties;
    if (curve == 0 || !eng_curve_get_properties(engine, curve, &properties)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    const eng_curve_points_t *points = eng_curve_points(engine, curve);
    eng_curve_points_t old = {NULL, 0};
    if (points != NULL) {
        eng_curve_clone_points(&old, points);
    }
    const eng_curve_points_t none = {NULL, 0};
    eng_curve_set_points(engine, curve, &none);
    for (size_t i = 0; i < old.count; i++) {
        nmo_engine_destroy_object(engine, old.ids[i]);
    }
    free(old.ids);
    for (int32_t i = 0; i < count; i++) {
        int32_t node = eng_entity_index(engine, nmo_engine_group_object(engine, group, i));
        nmo_object_id_t point = eng_create_related_object_with_options(engine, curve, NMO_CID_CURVEPOINT, "CurvePoint", false);
        int32_t index = eng_entity_index(engine, point);
        if (index < 0 || node < 0) {
            continue;
        }
        eng_vector_t pos = eng_entity_position(engine, node, -1);
        eng_entity_set_position(engine, index, &pos, -1, false);
        eng_curve_insert_point(engine, curve, 0, point);
    }
    eng_curve_get_properties(engine, curve, &properties);
    properties.fitting_coeff = get_in_float(b, 3, 0.0f);
    eng_curve_set_properties(engine, curve, &properties);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int cb_find_curved_nodal_path(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_object_id_t group = made_group(b);
        nmo_engine_block_set_local_value(b, 0, &group, sizeof(group));
        nmo_object_id_t curve = eng_create_object(b->engine, NMO_CID_CURVE, "FindCurvedPath");
        set_out(b, 0, &curve, sizeof(curve));
    }
    return NMO_ENGINE_BR_OK;
}

/* Update Nodal Path: the nodes' positions and the links' lengths taken again */
static int bb_update_nodal_path(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    graph_update_distance(b->engine, graph);
    return NMO_ENGINE_BR_OK;
}

/*
 * Character Go To Node: In picks the node nearest the character (its floor
 * reference or root, on the ground plane) with a way to the Goal Node; each
 * Loop In it walks there by Joy_Up (within the Limit Angle) and turns by
 * Joy_Left/Joy_Right messages (or rotates), and once within the Guard
 * Distance takes the next node. As in the DLL, the link walked is turned
 * off while walking and back on at the node, with or without Mark
 * Occupation.
 */
static int bb_character_go_to_node(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t character = target_entity(b);
    if (character < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nodal_graph_t *graph = input_graph(b, 0);
    if (graph == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t self = engine->entities.data[character].id;
    bool occupation = get_in_int(b, 5, 0) != 0;
    int32_t carac = eng_entity_index(engine, eng_character_floor_reference(engine, self));
    if (carac < 0) {
        carac = eng_entity_index(engine, eng_character_root(engine, self));
        if (carac < 0) {
            return NMO_ENGINE_BR_OWNERERROR;
        }
    }
    nmo_object_id_t carac_id = engine->entities.data[carac].id;
    eng_vector_t carpos = eng_entity_position(engine, carac, -1);
    carpos.y = 0.0f;
    nmo_object_id_t goal = nmo_engine_block_get_input_object(b, 1), nodetogo = 0, oldnode = 0;
    nmo_engine_block_get_local_value(b, 1, &nodetogo, sizeof(nodetogo));
    nmo_engine_block_get_local_value(b, 2, &oldnode, sizeof(oldnode));
    const nmo_object_id_t none = 0;
    if (IN_ACTIVE(b, 2)) { /* Stop */
        IN_OFF(b, 1);
        IN_OFF(b, 2);
        if (oldnode != 0 && nodetogo != 0) {
            graph_activate_edge(graph, oldnode, nodetogo, true, true);
            graph_activate_edge(graph, nodetogo, oldnode, true, true);
        }
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        if (oldnode != 0 && occupation) {
            graph_set_occupier(graph, oldnode, 0);
        }
        IN_OFF(b, 0);
        nmo_object_id_t nearest = 0;
        float least = 1e17f;
        for (int32_t i = 0; i < graph->count; i++) {
            nmo_object_id_t node = graph->states[i].node;
            int32_t index = eng_entity_index(engine, node);
            if (index < 0) {
                continue;
            }
            eng_vector_t pos = eng_entity_position(engine, index, -1);
            float d2 = (carpos.x - pos.x) * (carpos.x - pos.x) + (carpos.z - pos.z) * (carpos.z - pos.z);
            if (d2 < least && (goal == node || graph_find_path(graph, node, goal, NULL, NULL, true, false) != 0)) {
                least = d2;
                nearest = node;
            }
        }
        if (nearest == 0) {
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
        nmo_engine_block_set_local_value(b, 1, &nearest, sizeof(nearest));
        nmo_engine_block_set_local_value(b, 2, &none, sizeof(none));
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 1);
    float prox = get_in_float(b, 2, 1.0f);
    int32_t old_index = eng_entity_index(engine, oldnode);
    if (old_index >= 0 && occupation) {
        eng_vector_t d = vsub(eng_entity_position(engine, old_index, -1), carpos);
        if (sqrtf(d.x * d.x + d.z * d.z) > prox) {
            graph_set_occupier(graph, oldnode, 0);
        }
    }
    if (nodetogo == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (oldnode != 0 && occupation) {
        nmo_object_id_t occ = graph_occupier(graph, nodetogo);
        if (occ != 0 && occ != self && nodetogo != goal) {
            nodetogo = oldnode;
        }
    }
    int32_t to = eng_entity_index(engine, nodetogo);
    eng_vector_t nodepos = to >= 0 ? eng_entity_position(engine, to, -1) : v3(0.0f, 0.0f, 0.0f);
    eng_vector_t dist = v3(nodepos.x - carpos.x, 0.0f, nodepos.z - carpos.z);
    float distance = sqrtf(dist.x * dist.x + dist.z * dist.z);
    if (distance < prox) {
        if (oldnode != 0) {
            graph_activate_edge(graph, oldnode, nodetogo, true, true);
            graph_activate_edge(graph, nodetogo, oldnode, true, true);
        }
        if (nodetogo == goal) {
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_OK;
        }
        if (occupation && !graph_set_occupier(graph, nodetogo, self)) {
            OUT_ON(b, 2);
            return NMO_ENGINE_BR_OK;
        }
        oldnode = nodetogo;
        nmo_engine_block_set_local_value(b, 2, &oldnode, sizeof(oldnode));
        nodetogo = graph_find_path(graph, oldnode, goal, NULL, NULL, false, false);
        if (nodetogo == 0) {
            nodetogo = graph_find_path(graph, oldnode, goal, NULL, NULL, true, false);
            if (nodetogo == 0) {
                OUT_ON(b, 1);
                return NMO_ENGINE_BR_OK;
            }
            if (!graph_edge_active(graph, oldnode, nodetogo)) {
                nodetogo = oldnode;
            }
        }
        if (occupation) {
            nmo_object_id_t occ = graph_occupier(graph, nodetogo);
            if (occ != 0 && occ != self && nodetogo != goal) {
                nodetogo = graph_find_path(graph, oldnode, goal, NULL, NULL, true, true);
                if (nodetogo == 0 || !graph_edge_active(graph, oldnode, nodetogo)) {
                    nodetogo = oldnode;
                }
            }
        }
        graph_activate_edge(graph, oldnode, nodetogo, false, true);
        graph_activate_edge(graph, nodetogo, oldnode, false, true);
        to = eng_entity_index(engine, nodetogo);
        nodepos = to >= 0 ? eng_entity_position(engine, to, -1) : v3(0.0f, 0.0f, 0.0f);
        dist = v3(nodepos.x - carpos.x, 0.0f, nodepos.z - carpos.z);
        distance = sqrtf(dist.x * dist.x + dist.z * dist.z);
        nmo_engine_block_set_local_value(b, 1, &nodetogo, sizeof(nodetogo));
    }
    /* The character's direction and its side, by Character Direction */
    eng_vector_t dir = v3(0.0f, 0.0f, 0.0f), dirx = v3(0.0f, 0.0f, 0.0f);
    int32_t direction = get_in_int(b, 3, 1);
    switch (direction) {
    case 1: case 2: eng_entity_orientation(engine, character, &dirx, NULL, &dir, -1); break;
    case 3: case 4: eng_entity_orientation(engine, character, NULL, &dir, &dirx, -1); break;
    case 5: case 6: eng_entity_orientation(engine, character, &dir, NULL, &dirx, -1); break;
    default: break;
    }
    if (direction == 2 || direction == 4 || direction == 6) {
        dir = v3(-dir.x, -dir.y, -dir.z);
    }
    if (direction == 1 || direction == 4 || direction == 6) {
        dirx = v3(-dirx.x, -dirx.y, -dirx.z);
    }
    eng_vector_t up = eng_vector_cross(&dir, &dirx);
    float inv = 1.0f / distance;
    float ahead = dir.x * dist.x * inv + dir.z * dist.z * inv, side = dirx.x * dist.x * inv + dirx.z * dist.z * inv;
    float limit = get_in_float(b, 4, 1.22f);
    int32_t joy_up = eng_message_type_add(engine, "Joy_Up"), joy_left = eng_message_type_add(engine, "Joy_Left");
    int32_t joy_right = eng_message_type_add(engine, "Joy_Right");
    eng_message_type_add(engine, "Joy_Down");
    if (distance > prox && ahead > cosf(limit)) {
        nmo_engine_block_send_message(b, joy_up, carac_id, carac_id);
    }
    bool messages = get_local_int(b, 3, 1) != 0;
    float speed = get_local_float(b, 4, 1.0f);
    if (ahead < 0.999f) {
        if (messages) {
            nmo_engine_block_send_message(b, side >= 0.0f ? joy_right : joy_left, carac_id, carac_id);
        } else {
            eng_entity_rotate(engine, character, &up, -(speed * side), -1, false);
        }
    }
    OUT_ON(b, 2);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_collisions_blocks[] = {
    {NMO_GUID_INIT(0x64154401, 0x76cf37af), "Box Box Intersection", bb_box_box_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x198531f9, 0x60f66c57), "Sphere Sphere Intersection", bb_sphere_sphere_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x01ad010b, 0x01bd010b), "Test Bounding Box Y Overlapping", bb_test_bounding_box_y_overlapping,
     NULL, NULL},
    {NMO_GUID_INIT(0x4d6075e1, 0x2dda272e), "Frustum Object Intersection", bb_frustum_object_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x0f1c5480, 0x04a811cb), "Box Face Intersection", bb_box_face_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x11426a61, 0x1a815584), "Face Face Intersection", bb_face_face_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x8e43cd12, 0x98dc215d), "Declare Floors", bb_declare_floors, NULL, NULL},
    {NMO_GUID_INIT(0x3a271561, 0x1d8c4b45), "Floor Manager Setup", bb_floor_manager_setup, NULL, NULL},
    {NMO_GUID_INIT(0x6dad424e, 0x54b6003d), "Get Nearest Floors", bb_get_nearest_floors, NULL, NULL},
    {NMO_GUID_INIT(0x43617677, 0x7de15e12), "Object Keep On Floor", bb_object_keep_on_floor, NULL, NULL},
    {NMO_GUID_INIT(0xdbac2124, 0x6adcbfa4), "Character Keep On Floor", bb_character_keep_on_floor, NULL, NULL},
    {NMO_GUID_INIT(0x136044b0, 0x1c59b21a), "Floor Slider", bb_floor_slider, NULL, NULL},
    {NMO_GUID_INIT(0x3de853bd, 0x133d0d2e), "Object Keep On Floor V2", bb_object_keep_on_floor_v2, NULL, NULL},
    {NMO_GUID_INIT(0xdbde2424, 0x6addef12), "Enhanced Character Keep On Floor", bb_enhanced_character_keep_on_floor,
     NULL, NULL},
    {NMO_GUID_INIT(0x788061fb, 0x249f29a1), "Character Keep On Floor Limits", bb_character_keep_on_floor_limits,
     cb_character_keep_on_floor_limits, NULL},
    {NMO_GUID_INIT(0x21dc34e8, 0xd512cd89), "Declare Obstacles", bb_declare_obstacles, NULL, NULL},
    {NMO_GUID_INIT(0x321a42c4, 0x006133d5), "Add Obstacle", bb_add_obstacle, NULL, NULL},
    {NMO_GUID_INIT(0x56204f19, 0x682801e8), "Collision Detection", bb_collision_detection, NULL, NULL},
    {NMO_GUID_INIT(0x301971f0, 0x7919343f), "Prevent Collision", bb_prevent_collision, NULL, NULL},
    {NMO_GUID_INIT(0x56204f09, 0x611101e8), "Multi Collision Detection", bb_multi_collision_detection, NULL, NULL},
    {NMO_GUID_INIT(0x13603db0, 0x16bc321a), "Object Slider", bb_object_slider, NULL, NULL},
    {NMO_GUID_INIT(0x39460907, 0x30f329af), "Avoid Obstacles", bb_avoid_obstacles, NULL, NULL},
    {NMO_GUID_INIT(0x71234f96, 0x34321e45), "Home on Entity", bb_home_on_entity, NULL, NULL},
    {NMO_GUID_INIT(0x435b3d9c, 0x3a8e41f6), "Create Nodal Path", NULL, cb_create_nodal_path, NULL},
    {NMO_GUID_INIT(0x51df79d8, 0x294163a3), "Activate Link", bb_activate_link, NULL, NULL},
    {NMO_GUID_INIT(0x092a4e59, 0x4009769c), "Activate Node", bb_activate_node, NULL, NULL},
    {NMO_GUID_INIT(0x078c60de, 0x6c744614), "Find Path", bb_find_path, cb_find_path, NULL},
    {NMO_GUID_INIT(0x170a5187, 0x42f10a3a), "Entity Find Nodal Path", bb_entity_find_nodal_path, cb_find_path, NULL},
    {NMO_GUID_INIT(0x72542b8a, 0x6fe901c7), "Find Curved Nodal Path", bb_find_curved_nodal_path,
     cb_find_curved_nodal_path, NULL},
    {NMO_GUID_INIT(0x21647449, 0x51126c00), "Update Nodal Path", bb_update_nodal_path, NULL, NULL},
    {NMO_GUID_INIT(0x4c710f65, 0x32ed4ea6), "Character Go To Node", bb_character_go_to_node, NULL, NULL},
    {NMO_GUID_INIT(0x319971f0, 0x7919193f), "Character Prevent From Collision", bb_character_prevent_from_collision,
     NULL, NULL},
};

nmo_status_t eng_register_collisions(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &collision_manager_desc, NULL);
    return status != NMO_OK ? status : ENG_REGISTER_BLOCKS(engine, eng_collisions_blocks);
}
