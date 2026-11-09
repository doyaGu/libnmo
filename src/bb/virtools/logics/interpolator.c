/**
 * @file interpolator.c
 * @brief Ports of the Logics/Interpolator building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "type/nmo_type_guids.h"

#include <math.h>
#include <string.h>

/* ---- Logics/Interpolator ---- */

/* hsv_rgb.cpp */
#define HSV_UNDEFINED -1.0f

static void rgb_to_hsv(float r, float g, float bl, float *h, float *s, float *v)
{
    float fmax = r > g ? r : g;
    fmax = fmax > bl ? fmax : bl;
    float fmin = r < g ? r : g;
    fmin = fmin < bl ? fmin : bl;
    *v = fmax;
    *s = fmax != 0.0f ? (fmax - fmin) / fmax : 0.0f;
    if (*s == 0.0f) {
        *h = HSV_UNDEFINED;
        return;
    }
    float delta = fmax - fmin;
    if (r == fmax) {
        *h = (g - bl) / delta;
    } else if (g == fmax) {
        *h = 2 + (bl - r) / delta;
    } else {
        *h = 4 + (r - g) / delta;
    }
    *h *= 60.0f;
    if (*h < 0.0f) {
        *h += 360.0f;
    }
}

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *bl)
{
    if (s == 0) {
        if (h == HSV_UNDEFINED) {
            *r = *g = *bl = v;
        }
        return;
    }
    if (h == 360.0f) {
        h = 0.0f;
    }
    h /= 60.0f;
    int i = (int)floorf(h);
    float f = h - (float)i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0: *r = v; *g = t; *bl = p; break;
    case 1: *r = q; *g = v; *bl = p; break;
    case 2: *r = p; *g = v; *bl = t; break;
    case 3: *r = p; *g = q; *bl = v; break;
    case 4: *r = t; *g = p; *bl = v; break;
    case 5: *r = v; *g = p; *bl = q; break;
    default: break;
    }
}

/* A and B as `count` floats, and the value; out = A + (B - A) * value per float. */
static int interpolate_floats(blk_t *b, size_t count)
{
    float a[4] = {0}, c[4] = {0}, r[4];
    float value = get_in_float(b, 2, 0.0f);
    nmo_engine_block_get_input_value(b, 0, a, count * sizeof(float));
    nmo_engine_block_get_input_value(b, 1, c, count * sizeof(float));
    for (size_t i = 0; i < count; i++) {
        r[i] = a[i] + (c[i] - a[i]) * value;
    }
    nmo_engine_block_set_output_value(b, 0, r, count * sizeof(float));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_interpolator_float(blk_t *b) { return interpolate_floats(b, 1); }

static int bb_interpolator_2d(blk_t *b) { return interpolate_floats(b, 2); }

static int bb_interpolator_vector(blk_t *b) { return interpolate_floats(b, 3); }

/* InterpolatorColor and InterpolatorRectangle (VxRect::Interpolate) are per float too */
static int bb_interpolator_four(blk_t *b) { return interpolate_floats(b, 4); }

static int bb_interpolator_int(blk_t *b)
{
    int32_t a = get_in_int(b, 0, 0), c = get_in_int(b, 1, 0);
    float value = get_in_float(b, 2, 0.0f);
    set_out_int(b, 0, (int32_t)(a + (c - a) * value));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Slerp (VxQuaternionDetail::Interpolate) */
static void quaternion_slerp(double t, const float a[4], const float c[4], float r[4])
{
    double dot = (((double)a[2] * c[2] + (double)a[1] * c[1]) + (double)a[0] * c[0]) +
                 (double)a[3] * c[3];
    double sign = dot < 0.0 ? -1.0 : 1.0;
    double cosine = dot < 0.0 ? -dot : dot;
    if (t == 0.0) {
        memcpy(r, a, 4 * sizeof(float));
    } else if (t == 1.0) {
        for (int i = 0; i < 4; i++) {
            r[i] = (float)(sign * c[i]);
        }
    } else if (!(cosine < 0.99)) {
        for (int i = 0; i < 4; i++) {
            r[i] = (float)(a[i] + t * (sign * c[i] - a[i]));
        }
    } else {
        double angle = acos(cosine), phase = t * angle, inverse_sine = 1.0 / sin(angle);
        for (int i = 0; i < 4; i++) {
            double tangent = (sign * c[i] - cosine * a[i]) * inverse_sine;
            r[i] = (float)(a[i] * cos(phase) + tangent * sin(phase));
        }
    }
}

static int bb_interpolator_quaternion(blk_t *b)
{
    float a[4] = {0, 0, 0, 1}, c[4] = {0, 0, 0, 1}, r[4];
    double t = get_in_float(b, 2, 0.0f);
    nmo_engine_block_get_input_value(b, 0, a, sizeof(a));
    nmo_engine_block_get_input_value(b, 1, c, sizeof(c));
    quaternion_slerp(t, a, c, r);
    nmo_engine_block_set_output_value(b, 0, r, sizeof(r));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ColorHSVInterpolator, with the original's green-for-blue slip on black colors */
static int bb_interpolator_color_hsv(blk_t *b)
{
    float c1[4] = {0}, c2[4] = {0}, res[4] = {0};
    nmo_engine_block_get_input_value(b, 0, c1, sizeof(c1));
    nmo_engine_block_get_input_value(b, 1, c2, sizeof(c2));
    float mix = get_in_float(b, 2, 0.0f);
    if (!(c1[0] || c1[1] || c1[2]) || !(c2[0] || c2[1] || c2[2])) {
        res[0] = (1.0f - mix) * c1[0] + mix * c2[0];
        res[1] = (1.0f - mix) * c1[1] + mix * c2[1];
        res[2] = (1.0f - mix) * c1[1] + mix * c2[2];
    } else {
        float h1, s1, v1, h2, s2, v2;
        const float pi = 3.14159265358979323846f;
        rgb_to_hsv(c1[0], c1[1], c1[2], &h1, &s1, &v1);
        rgb_to_hsv(c2[0], c2[1], c2[2], &h2, &s2, &v2);
        if (h1 > 180.0f) {
            h1 -= 360.0f;
        }
        if (h2 > 180.0f) {
            h2 -= 360.0f;
        }
        float x1 = cosf(h1 * pi / 180.0f) * s1, y1 = sinf(h1 * pi / 180.0f) * s1;
        float x2 = cosf(h2 * pi / 180.0f) * s2, y2 = sinf(h2 * pi / 180.0f) * s2;
        float x = x1 + (x2 - x1) * mix, y = y1 + (y2 - y1) * mix;
        s1 = sqrtf(x * x + y * y);
        h1 = asinf(y / s1) * 180.0f / pi;
        if (x < 0.0f) {
            h1 = y > 0.0f ? 180 - h1 : -180.0f - h1;
        }
        v1 = (1.0f - mix) * v1 + mix * v2;
        if (h1 < 0.0f) {
            h1 += 360.0f;
        }
        hsv_to_rgb(h1, s1, v1, &res[0], &res[1], &res[2]);
    }
    res[3] = (1.0f - mix) * c1[3] + mix * c2[3];
    nmo_engine_block_set_output_value(b, 0, res, sizeof(res));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Vx3DNormalizeRotationRows: rows 0 and 1 to unit length (wide), row 2 their cross product */
static void normalize_rotation_rows(const float m[4][4], float out[3][3])
{
    for (int r = 0; r < 2; r++) {
        const double inverse = 1.0 / sqrt(((double)m[r][0] * m[r][0] + (double)m[r][1] * m[r][1]) +
                                          (double)m[r][2] * m[r][2]);
        for (int c = 0; c < 3; c++) {
            out[r][c] = (float)(m[r][c] * inverse);
        }
    }
    out[2][0] = (float)((double)out[1][2] * out[0][1] - (double)out[0][2] * out[1][1]);
    out[2][1] = (float)((double)out[0][2] * out[1][0] - (double)out[0][0] * out[1][2]);
    out[2][2] = (float)((double)out[0][0] * out[1][1] - (double)out[1][0] * out[0][1]);
}

/* VxQuaternion::FromMatrix of a unit rotation, with wide intermediates */
static void quaternion_from_rows(const float m[3][3], float q[4])
{
    const double trace = (double)m[1][1] + m[2][2] + m[0][0];
    if (trace >= 0.0) {
        const double root = sqrt(trace + 1.0), s = 0.5 / root;
        q[3] = (float)(root * 0.5);
        q[0] = (float)(((double)m[2][1] - m[1][2]) * s);
        q[1] = (float)(((double)m[0][2] - m[2][0]) * s);
        q[2] = (float)(((double)m[1][0] - m[0][1]) * s);
        return;
    }
    int i = 0;
    if (m[1][1] > m[0][0]) {
        i = 1;
    }
    if (m[2][2] > m[i][i]) {
        i = 2;
    }
    static const int next[3] = {1, 2, 0};
    const int j = next[i], k = next[j];
    const double root = sqrt((m[i][i] - ((double)m[k][k] + m[j][j])) + 1.0), s = 0.5 / root;
    q[i] = (float)(root * 0.5);
    q[j] = (float)(((double)m[j][i] + m[i][j]) * s);
    q[k] = (float)(((double)m[k][i] + m[i][k]) * s);
    q[3] = (float)(((double)m[k][j] - m[j][k]) * s);
}

/* VxQuaternion::FromMatrix(Mat, MatIsUnit, TRUE) */
static void quaternion_from_matrix(const float m[4][4], bool unit, float q[4])
{
    float rows[3][3];
    if (unit) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                rows[r][c] = m[r][c];
            }
        }
    } else {
        normalize_rotation_rows(m, rows);
    }
    quaternion_from_rows((const float (*)[3])rows, q);
}

/* VxQuaternion::ToMatrix: the rotation, no translation */
static void quaternion_to_matrix(const float q[4], float m[4][4])
{
    eng_matrix_t matrix;
    const nmo_quaternion_t quaternion = {q[0], q[1], q[2], q[3]};
    eng_matrix_from_quaternion_unchecked(&matrix, &quaternion);
    memcpy(m, matrix.m, sizeof(matrix.m));
}

/*
 * Vx3DInterpolateMatrix (Vx3DInterpolateMatrixNoScale, which interpolates the
 * scale too): each matrix as a rotation, a position and a scale by rows, the
 * rotation slerped and the others linear, the ends kept as they are.
 */
void vx_interpolate_matrix(float step, const float a[4][4], const float c[4][4], float r[4][4])
{
    if (step == 0.0f) {
        memcpy(r, a, 16 * sizeof(float));
        return;
    }
    if (step == 1.0f) {
        memcpy(r, c, 16 * sizeof(float));
        return;
    }
    const float(*ends[2])[4] = {a, c};
    float q[2][4], position[2][3], scale[2][3];
    for (int e = 0; e < 2; e++) {
        float rows[3][3];
        normalize_rotation_rows(ends[e], rows);
        quaternion_from_rows((const float (*)[3])rows, q[e]);
        for (int i = 0; i < 3; i++) {
            position[e][i] = ends[e][3][i];
            scale[e][i] = (float)(((double)rows[i][0] * ends[e][i][0] +
                                   (double)rows[i][1] * ends[e][i][1]) +
                                  (double)rows[i][2] * ends[e][i][2]);
        }
    }
    float rotation[4];
    quaternion_slerp(step, q[0], q[1], rotation);
    float p[3], sc[3];
    for (int i = 0; i < 3; i++) {
        p[i] = (float)((double)position[0][i] + ((double)position[1][i] - position[0][i]) * step);
        sc[i] = (float)((double)scale[0][i] + ((double)scale[1][i] - scale[0][i]) * step);
    }
    const double length = sqrt((((double)rotation[0] * rotation[0] + (double)rotation[1] * rotation[1]) +
                                (double)rotation[2] * rotation[2]) +
                               (double)rotation[3] * rotation[3]);
    if (length == 0.0) {
        rotation[0] = rotation[1] = rotation[2] = 0.0f;
        rotation[3] = 1.0f;
    } else {
        for (int i = 0; i < 4; i++) {
            rotation[i] = (float)(rotation[i] / length);
        }
    }
    const double x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    const double xx = x * x, yy = y * y, zz = z * z, ww = w * w, xy = y * x, wz = w * z;
    const float xz = (float)(z * x), wx = (float)(w * x), yz = (float)(z * y), wy = (float)(w * y);
    memset(r, 0, 16 * sizeof(float));
    r[3][3] = 1.0f;
    r[0][0] = (float)(ww + xx - yy - zz);
    r[0][1] = (float)(2 * xy - 2 * wz);
    r[0][2] = 2 * wy + 2 * xz;
    r[1][0] = (float)(2 * wz + 2 * xy);
    r[1][1] = (float)(ww - xx + yy - zz);
    r[1][2] = 2 * yz - 2 * wx;
    r[2][0] = 2 * xz - 2 * wy;
    r[2][1] = 2 * yz + 2 * wx;
    r[2][2] = (float)(ww - xx - yy + zz);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] *= sc[i];
        }
        r[3][i] = p[i];
    }
}

/* InterpolatorMatrix */
static int bb_interpolator_matrix(blk_t *b)
{
    float a[4][4] = {{0}}, c[4][4] = {{0}}, r[4][4];
    nmo_engine_block_get_input_value(b, 0, a, sizeof(a));
    nmo_engine_block_get_input_value(b, 1, c, sizeof(c));
    vx_interpolate_matrix(get_in_float(b, 2, 0.0f), (const float (*)[4])a, (const float (*)[4])c, r);
    nmo_engine_block_set_output_value(b, 0, r, sizeof(r));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Interpolator Object Orientation: the slerp of A's and B's world rotations as
 * a matrix, its rows scaled by Scale unless the setting (local 0) says the
 * objects are unit scaled, in which case their matrices are taken as rotations
 * as they are. The original reads a missing entity; here that is a parameter
 * error.
 */
static int bb_interpolator_orientation(blk_t *b)
{
    bool unit = get_local_int(b, 0, 0) != 0;
    int32_t ea = input_entity(b, 0), ec = input_entity(b, 1);
    if (ea < 0 || ec < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    float qa[4], qc[4], q[4], m[4][4];
    quaternion_from_matrix((const float (*)[4])b->engine->entities.data[ea].world.m, unit, qa);
    quaternion_from_matrix((const float (*)[4])b->engine->entities.data[ec].world.m, unit, qc);
    quaternion_slerp(get_in_float(b, 2, 0.0f), qa, qc, q);
    quaternion_to_matrix(q, m);
    if (!unit) {
        eng_vector_t scale = get_in_vector(b, 3, 1.0f, 1.0f, 1.0f);
        if (scale.x != 1.0f || scale.y != 1.0f || scale.z != 1.0f) {
            const float s[3] = {scale.x, scale.y, scale.z};
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    m[i][j] *= s[i];
                }
            }
        }
    }
    nmo_engine_block_set_output_value(b, 0, m, sizeof(m));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The function goes by the output's type, as the load callback picks it. */
static int cb_interpolator(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    nmo_engine_block_fn fn = NULL;
    if (type_derives(b, out, CKPGUID_FLOAT)) {
        fn = bb_interpolator_float;
    } else if (nmo_guid_equals(out, CKPGUID_VECTOR) || nmo_guid_equals(out, CKPGUID_EULERANGLES)) {
        fn = bb_interpolator_vector;
    } else if (nmo_guid_equals(out, CKPGUID_2DVECTOR)) {
        fn = bb_interpolator_2d;
    } else if (nmo_guid_equals(out, CKPGUID_INT)) {
        fn = bb_interpolator_int;
    } else if (nmo_guid_equals(out, CKPGUID_COLOR)) {
        fn = get_local_int(b, 0, 0) ? bb_interpolator_color_hsv : bb_interpolator_four;
    } else if (nmo_guid_equals(out, CKPGUID_QUATERNION)) {
        fn = bb_interpolator_quaternion;
    } else if (nmo_guid_equals(out, CKPGUID_RECT)) {
        fn = bb_interpolator_four;
    } else if (nmo_guid_equals(out, CKPGUID_MATRIX)) {
        fn = bb_interpolator_matrix;
    } else {
        return NMO_ENGINE_BR_OK; /* another type: the function stays InterpolatorFloat */
    }
    nmo_engine_block_set_function(b, fn);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_interpolator_blocks[] = {
    {NMO_GUID_INIT(0x35503950, 0x2dde7a65), "Interpolator", bb_interpolator_float, cb_interpolator,
     NULL},
    {NMO_GUID_INIT(0x17171700, 0x17171700), "Interpolator Float", bb_interpolator_float, NULL, NULL},
    {NMO_GUID_INIT(0x17171701, 0x17171701), "Interpolator Int", bb_interpolator_int, NULL, NULL},
    {NMO_GUID_INIT(0x17171702, 0x17171702), "Interpolator Vector", bb_interpolator_vector, NULL,
     NULL},
    {NMO_GUID_INIT(0x17171703, 0x17171703), "Interpolator Color", bb_interpolator_four, NULL, NULL},
    {NMO_GUID_INIT(0x03184d55, 0x7d7a0509), "Interpolator Matrix", bb_interpolator_matrix, NULL,
     NULL},
    {NMO_GUID_INIT(0x17171704, 0x17171704), "Interpolator Object Orientation",
     bb_interpolator_orientation, NULL, NULL},
    {NMO_GUID_INIT(0x778400bb, 0x1a480d35), "Interpolator HSV Color", bb_interpolator_color_hsv,
     NULL, NULL},
};

nmo_status_t eng_register_logics_interpolator(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_interpolator_blocks);
}
