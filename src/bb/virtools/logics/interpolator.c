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
static int bb_interpolator_quaternion(blk_t *b)
{
    float a[4] = {0, 0, 0, 1}, c[4] = {0, 0, 0, 1}, r[4];
    double t = get_in_float(b, 2, 0.0f);
    nmo_engine_block_get_input_value(b, 0, a, sizeof(a));
    nmo_engine_block_get_input_value(b, 1, c, sizeof(c));
    double dot = (((double)a[2] * c[2] + (double)a[1] * c[1]) + (double)a[0] * c[0]) +
                 (double)a[3] * c[3];
    double sign = dot < 0.0 ? -1.0 : 1.0;
    double cosine = dot < 0.0 ? -dot : dot;
    if (t == 0.0) {
        memcpy(r, a, sizeof(r));
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

/*
 * The function goes by the output's type, as the load callback picks it. A
 * matrix (Vx3DInterpolateMatrix) has no port: the block keeps no function.
 */
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
    } else if (!nmo_guid_equals(out, CKPGUID_MATRIX)) {
        return NMO_ENGINE_BR_OK; /* another type: the function stays InterpolatorFloat */
    }
    nmo_engine_block_set_function(b, fn);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_interpolator_blocks[] = {
    {NMO_GUID(0x35503950, 0x2dde7a65), "Interpolator", bb_interpolator_float, cb_interpolator,
     NULL},
};

nmo_status_t eng_register_logics_interpolator(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_interpolator_blocks);
}
