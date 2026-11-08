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

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
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

/* CKCollisionManager::FaceFaceIntersection: a face (or line) of one entity across one of the other */
#define FACE_EPSILON 0.001f
static bool face_face_intersection(nmo_engine_t *engine, int32_t first, int32_t second)
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
    bool hit = face_face_intersection(b->engine, first, second);
    nmo_engine_block_activate_output(b, 0, hit);
    nmo_engine_block_activate_output(b, 1, !hit);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Floors
 * ============================================================================ */

/* CK3dEntity::GetBoundingBox: a character's is its hierarchy's */
static void bounding_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    if (nmo_engine_object_class(engine, engine->entities.data[index].id) == NMO_CID_CHARACTER) {
        eng_entity_hierarchical_box(engine, index, min, max);
    } else {
        eng_entity_world_box(engine, index, min, max);
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

/* CK3dEntity::GetBoundingBox(TRUE): a character's is its hierarchy's */
static void local_bounding_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    if (nmo_engine_object_class(engine, engine->entities.data[index].id) == NMO_CID_CHARACTER) {
        local_hierarchical_box(engine, index, min, max);
    } else {
        eng_entity_local_box(engine, index, min, max);
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
};

nmo_status_t eng_register_collisions(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_collisions_blocks);
}
