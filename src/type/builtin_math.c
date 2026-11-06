/**
 * @file builtin_math.c
 * @brief The math operations of the Parameter Operations plugin.
 *
 * Each function does what the plugin's function of the same name does
 * (ParameterOperations.dll; the reference is the Ballanced source of the
 * plugin and VxMath): Square Root, Inverse, degree and radian conversion,
 * Normalize, Get Magnitude, Get Angle, Reflect, Symmetry, Spheric To
 * Cartesian, From Rotation, the component getters and setters of
 * quaternions, colors, vectors, 2D vectors, Euler angles and matrices,
 * Rainbow Color, and the rectangle setters. The result may alias an operand,
 * so every function reads its operands before it writes.
 *
 * The plugin registers Float functions only; Angle, Time and Percentage
 * reach them through their base type, as CKParameterManager looks a function
 * up by the parents of the types.
 */

#include "type/nmo_operations.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "core/nmo_math.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MATH_OP(name) \
    static nmo_status_t name(const void *p1_data, const nmo_type_descriptor_t *p1_type, \
                             const void *p2_data, const nmo_type_descriptor_t *p2_type, \
                             void *result_data, const nmo_type_descriptor_t *result_type, \
                             void *user_data)
#define MATH_UNUSED() \
    (void)p1_type; \
    (void)p2_type; \
    (void)result_type; \
    (void)user_data

static float read_float(const void *data)
{
    float value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static int32_t read_int(const void *data)
{
    int32_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static void write_float(void *data, float value)
{
    memcpy(data, &value, sizeof(value));
}

/* VxVector::Normalize: a zero vector stays zero; the length is wide */
static nmo_vector_t vector_normalized(nmo_vector_t v)
{
    const double square = ((double)v.x * v.x + (double)v.y * v.y) + (double)v.z * v.z;
    if (square > 0.0) {
        const double length = sqrt(square);
        v.x = (float)(v.x / length);
        v.y = (float)(v.y / length);
        v.z = (float)(v.z / length);
    }
    return v;
}

/* ---- Float ---- */

/* CKFloatSqrtFloat */
MATH_OP(op_sqrt_float)
{
    MATH_UNUSED();
    (void)p2_data;
    write_float(result_data, sqrtf(read_float(p1_data)));
    NMO_RETURN_OK();
}

/* CKFloatInverseFloat: 1 / x, infinite for 0 as the plugin's is */
MATH_OP(op_inverse_float)
{
    MATH_UNUSED();
    (void)p2_data;
    write_float(result_data, 1.0f / read_float(p1_data));
    NMO_RETURN_OK();
}

/* CKFloatInverseInt */
MATH_OP(op_inverse_int_to_float)
{
    MATH_UNUSED();
    (void)p2_data;
    write_float(result_data, 1.0f / (float)read_int(p1_data));
    NMO_RETURN_OK();
}

/* CKIntInverseFloat: truncated; a quotient no int holds gives INT_MIN as x86 does */
MATH_OP(op_inverse_float_to_int)
{
    MATH_UNUSED();
    (void)p2_data;
    const float quotient = 1.0f / read_float(p1_data);
    int32_t value = INT32_MIN;
    if (isfinite(quotient) && quotient > -2147483649.0f && quotient < 2147483648.0f) {
        value = (int32_t)quotient;
    }
    memcpy(result_data, &value, sizeof(value));
    NMO_RETURN_OK();
}

/* CKFloatDegreToRadianFloat: the plugin's constant */
MATH_OP(op_deg_to_rad)
{
    MATH_UNUSED();
    (void)p2_data;
    write_float(result_data, read_float(p1_data) * 0.017453292f);
    NMO_RETURN_OK();
}

/* CKFloatRadianToDegreFloat: the plugin's constant */
MATH_OP(op_rad_to_deg)
{
    MATH_UNUSED();
    (void)p2_data;
    write_float(result_data, read_float(p1_data) * 57.295776f);
    NMO_RETURN_OK();
}

/* ---- Vectors ---- */

/* CKVectorInverseVector */
MATH_OP(op_inverse_vector)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_vector_t v;
    memcpy(&v, p1_data, sizeof(v));
    v.x = 1.0f / v.x;
    v.y = 1.0f / v.y;
    v.z = 1.0f / v.z;
    memcpy(result_data, &v, sizeof(v));
    NMO_RETURN_OK();
}

/* CK2dVectorInverse2dVector */
MATH_OP(op_inverse_vector2)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_vector2_t v;
    memcpy(&v, p1_data, sizeof(v));
    v.x = 1.0f / v.x;
    v.y = 1.0f / v.y;
    memcpy(result_data, &v, sizeof(v));
    NMO_RETURN_OK();
}

/* CKVectorNormalizeVector */
MATH_OP(op_normalize_vector)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_vector_t v;
    memcpy(&v, p1_data, sizeof(v));
    v = vector_normalized(v);
    memcpy(result_data, &v, sizeof(v));
    NMO_RETURN_OK();
}

/* CKFloatGetMagnitudeVector */
MATH_OP(op_magnitude_vector)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_vector_t v;
    memcpy(&v, p1_data, sizeof(v));
    write_float(result_data, sqrtf(v.x * v.x + v.y * v.y + v.z * v.z));
    NMO_RETURN_OK();
}

/* CKFloatGetMagnitude2dVector */
MATH_OP(op_magnitude_vector2)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_vector2_t v;
    memcpy(&v, p1_data, sizeof(v));
    write_float(result_data, sqrtf(v.x * v.x + v.y * v.y));
    NMO_RETURN_OK();
}

/* CKFloatGetAngleVector: the angle between the directions, the cosine clamped */
MATH_OP(op_angle_vector)
{
    MATH_UNUSED();
    nmo_vector_t a, b;
    memcpy(&a, p1_data, sizeof(a));
    memcpy(&b, p2_data, sizeof(b));
    const float la = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (la > 0.0f) {
        const float inverse = 1.0f / la;
        a.x *= inverse;
        a.y *= inverse;
        a.z *= inverse;
    }
    const float lb = sqrtf(b.x * b.x + b.y * b.y + b.z * b.z);
    if (lb > 0.0f) {
        const float inverse = 1.0f / lb;
        b.x *= inverse;
        b.y *= inverse;
        b.z *= inverse;
    }
    float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < -1.0f) {
        dot = -1.0f;
    } else if (dot > 1.0f) {
        dot = 1.0f;
    }
    write_float(result_data, acosf(dot));
    NMO_RETURN_OK();
}

/* CKFloatGetAngle2dVector2dVector: no clamp, as the plugin's */
MATH_OP(op_angle_vector2)
{
    MATH_UNUSED();
    nmo_vector2_t a, b;
    memcpy(&a, p1_data, sizeof(a));
    memcpy(&b, p2_data, sizeof(b));
    const float la = sqrtf(a.x * a.x + a.y * a.y);
    if (la > 0.0f) {
        const float inverse = 1.0f / la;
        a.x *= inverse;
        a.y *= inverse;
    }
    const float lb = sqrtf(b.x * b.x + b.y * b.y);
    if (lb > 0.0f) {
        const float inverse = 1.0f / lb;
        b.x *= inverse;
        b.y *= inverse;
    }
    write_float(result_data, acosf(a.x * b.x + a.y * b.y));
    NMO_RETURN_OK();
}

/* CKVectorReflectVectorVector: the incident vector reflected on the normal */
MATH_OP(op_reflect_vector)
{
    MATH_UNUSED();
    nmo_vector_t incident, normal;
    memcpy(&incident, p1_data, sizeof(incident));
    memcpy(&normal, p2_data, sizeof(normal));
    const nmo_vector_t negated = {-incident.x, -incident.y, -incident.z};
    normal = vector_normalized(normal);
    const float dot = normal.x * negated.x + normal.y * negated.y + normal.z * negated.z;
    const float twice = dot + dot;
    const nmo_vector_t result = {normal.x * twice - negated.x, normal.y * twice - negated.y,
                                 normal.z * twice - negated.z};
    memcpy(result_data, &result, sizeof(result));
    NMO_RETURN_OK();
}

/* CKVectorSymmetryVectorVector: the point mirrored through the center */
MATH_OP(op_symmetry_vector)
{
    MATH_UNUSED();
    nmo_vector_t point, center;
    memcpy(&point, p1_data, sizeof(point));
    memcpy(&center, p2_data, sizeof(center));
    const nmo_vector_t result = {2.0f * center.x - point.x, 2.0f * center.y - point.y,
                                 2.0f * center.z - point.z};
    memcpy(result_data, &result, sizeof(result));
    NMO_RETURN_OK();
}

/* CK2dVectorSymmetry2dVector2dVector */
MATH_OP(op_symmetry_vector2)
{
    MATH_UNUSED();
    nmo_vector2_t point, center;
    memcpy(&point, p1_data, sizeof(point));
    memcpy(&center, p2_data, sizeof(center));
    const nmo_vector2_t result = {2.0f * center.x - point.x, 2.0f * center.y - point.y};
    memcpy(result_data, &result, sizeof(result));
    NMO_RETURN_OK();
}

/*
 * CKVectorSphericToCartFloatFloat: the unit vector of elevation p1 and
 * azimuth p2, as the plugin computes it (ParameterOperations.dll 0x24B49280):
 * x = cos(elevation) cos(azimuth), y = sin(elevation), z = cos(elevation) sin(azimuth).
 */
MATH_OP(op_spheric_to_cartesian)
{
    MATH_UNUSED();
    const float elevation = read_float(p1_data), azimuth = read_float(p2_data);
    const float cos_elevation = cosf(elevation);
    const nmo_vector_t result = {cosf(azimuth) * cos_elevation, sinf(elevation),
                                 cos_elevation * sinf(azimuth)};
    memcpy(result_data, &result, sizeof(result));
    NMO_RETURN_OK();
}

/* Vx3DMatrixFromRotation: the rotation of `angle` around `axis`, normalized wide */
static void matrix_from_rotation(nmo_matrix_t *out, nmo_vector_t axis, float angle)
{
    const double length =
        sqrt(((double)axis.x * axis.x + (double)axis.y * axis.y) + (double)axis.z * axis.z);
    const double inverse = 1.0 / length;
    const float x = (float)(axis.x * inverse), y = (float)(axis.y * inverse),
                z = (float)(axis.z * inverse);
    const float s = (float)sin((double)angle), c = (float)cos((double)angle);
    const float t = 1.0f - c;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float xs = x * s, ys = y * s, zs = z * s;
    const float xyt = xy * t, xzt = xz * t, yzt = yz * t;
    out->m[0][0] = (1.0f - xx) * c + xx;
    out->m[0][1] = xyt - zs;
    out->m[0][2] = xzt + ys;
    out->m[0][3] = 0.0f;
    out->m[1][0] = xyt + zs;
    out->m[1][1] = (1.0f - yy) * c + yy;
    out->m[1][2] = yzt - xs;
    out->m[1][3] = 0.0f;
    out->m[2][0] = xzt - ys;
    out->m[2][1] = yzt + xs;
    out->m[2][2] = (1.0f - zz) * c + zz;
    out->m[2][3] = 0.0f;
    out->m[3][0] = 0.0f;
    out->m[3][1] = 0.0f;
    out->m[3][2] = 0.0f;
    out->m[3][3] = 1.0f;
}

/* VxQuaternion::FromMatrix of a unit rotation matrix, with wide intermediates */
static nmo_quaternion_t quaternion_from_matrix(const nmo_matrix_t *mat)
{
    nmo_quaternion_t q;
    const float(*m)[4] = mat->m;
    const double trace = (double)m[1][1] + m[2][2] + m[0][0];
    if (trace >= 0.0) {
        const double root = sqrt(trace + 1.0);
        const double s = 0.5 / root;
        q.w = (float)(root * 0.5);
        q.x = (float)(((double)m[2][1] - m[1][2]) * s);
        q.y = (float)(((double)m[0][2] - m[2][0]) * s);
        q.z = (float)(((double)m[1][0] - m[0][1]) * s);
        return q;
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
    const double other = (double)m[k][k] + m[j][j];
    const double root = sqrt((m[i][i] - other) + 1.0);
    const double s = 0.5 / root;
    float *components[4] = {&q.x, &q.y, &q.z, &q.w};
    *components[i] = (float)(root * 0.5);
    *components[j] = (float)(((double)m[j][i] + m[i][j]) * s);
    *components[k] = (float)(((double)m[k][i] + m[i][k]) * s);
    *components[3] = (float)(((double)m[k][j] - m[j][k]) * s);
    return q;
}

/* CKQuaternionFromRotation: VxQuaternion::FromRotation builds the matrix first */
MATH_OP(op_quaternion_from_rotation)
{
    MATH_UNUSED();
    nmo_vector_t axis;
    memcpy(&axis, p1_data, sizeof(axis));
    nmo_matrix_t mat;
    matrix_from_rotation(&mat, axis, read_float(p2_data));
    const nmo_quaternion_t q = quaternion_from_matrix(&mat);
    memcpy(result_data, &q, sizeof(q));
    NMO_RETURN_OK();
}

/* CKMatrixFromRotation */
MATH_OP(op_matrix_from_rotation)
{
    MATH_UNUSED();
    nmo_vector_t axis;
    memcpy(&axis, p1_data, sizeof(axis));
    nmo_matrix_t mat;
    matrix_from_rotation(&mat, axis, read_float(p2_data));
    memcpy(result_data, &mat, sizeof(mat));
    NMO_RETURN_OK();
}

/* Vx3DInverseMatrix: the inverse of an affine matrix; a singular one leaves the result */
MATH_OP(op_inverse_matrix)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_matrix_t mat;
    memcpy(&mat, p1_data, sizeof(mat));
    const float(*m)[4] = mat.m;
    const double a00 = m[0][0], a01 = m[0][1], a02 = m[0][2];
    const double a10 = m[1][0], a11 = m[1][1], a12 = m[1][2];
    const double a20 = m[2][0], a21 = m[2][1], a22 = m[2][2];
    const double determinant = a00 * (a11 * a22 - a12 * a21) + a01 * (a12 * a20 - a10 * a22) +
                               a02 * (a10 * a21 - a11 * a20);
    if (determinant == 0.0) {
        NMO_RETURN_OK();
    }
    double linear[3][3];
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            const int i = (column + 1) % 3, j = (column + 2) % 3;
            const int k = (row + 1) % 3, l = (row + 2) % 3;
            linear[row][column] =
                ((double)m[i][k] * m[j][l] - (double)m[i][l] * m[j][k]) / determinant;
        }
    }
    nmo_matrix_t out;
    memset(&out, 0, sizeof(out));
    out.m[3][3] = 1.0f;
    for (int column = 0; column < 3; column++) {
        for (int row = 0; row < 3; row++) {
            out.m[row][column] = (float)linear[row][column];
        }
        const double translation = m[3][0] * linear[0][column] + m[3][1] * linear[1][column] +
                                   m[3][2] * linear[2][column];
        out.m[3][column] = (float)-translation;
    }
    memcpy(result_data, &out, sizeof(out));
    NMO_RETURN_OK();
}

/* ---- Components ---- */

/* CKFloatGetXQuaternion and the others: float `index` of any four-float value */
static nmo_status_t get_component(const void *p1_data, void *result_data, int index)
{
    write_float(result_data, read_float((const float *)p1_data + index));
    NMO_RETURN_OK();
}

MATH_OP(op_get_component_0)
{
    MATH_UNUSED();
    (void)p2_data;
    return get_component(p1_data, result_data, 0);
}

MATH_OP(op_get_component_1)
{
    MATH_UNUSED();
    (void)p2_data;
    return get_component(p1_data, result_data, 1);
}

MATH_OP(op_get_component_2)
{
    MATH_UNUSED();
    (void)p2_data;
    return get_component(p1_data, result_data, 2);
}

MATH_OP(op_get_component_3)
{
    MATH_UNUSED();
    (void)p2_data;
    return get_component(p1_data, result_data, 3);
}

/* CKVectorSetXVectorFloat and the others: `count` floats of p1 with float `index` replaced */
static nmo_status_t set_component(const void *p1_data, const void *p2_data, void *result_data,
                                  int count, int index, bool clamp)
{
    float values[4];
    memcpy(values, p1_data, (size_t)count * sizeof(float));
    values[index] = read_float(p2_data);
    if (clamp && values[index] > 1.0f) {
        values[index] = 1.0f;
    }
    memcpy(result_data, values, (size_t)count * sizeof(float));
    NMO_RETURN_OK();
}

MATH_OP(op_set_vector2_x)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 2, 0, false);
}

MATH_OP(op_set_vector2_y)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 2, 1, false);
}

MATH_OP(op_set_vector_x)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 3, 0, false);
}

MATH_OP(op_set_vector_y)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 3, 1, false);
}

MATH_OP(op_set_vector_z)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 3, 2, false);
}

/* CKColorSetRedColorFloat2 and the others: the quaternion's component, unclamped */
MATH_OP(op_set_quaternion_x)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 0, false);
}

MATH_OP(op_set_quaternion_y)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 1, false);
}

MATH_OP(op_set_quaternion_z)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 2, false);
}

MATH_OP(op_set_quaternion_w)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 3, false);
}

/* CKColorSetRedColorFloat and the others: the color's component, capped at 1 */
MATH_OP(op_set_color_r)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 0, true);
}

MATH_OP(op_set_color_g)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 1, true);
}

MATH_OP(op_set_color_b)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 2, true);
}

MATH_OP(op_set_color_a)
{
    MATH_UNUSED();
    return set_component(p1_data, p2_data, result_data, 4, 3, true);
}

/* CKVectorSetXMatrix and the others: the matrix (p2) with row `row` set to the vector (p1) */
static nmo_status_t set_matrix_row(const void *p1_data, const void *p2_data, void *result_data,
                                   int row)
{
    nmo_vector_t v;
    nmo_matrix_t mat;
    memcpy(&v, p1_data, sizeof(v));
    memcpy(&mat, p2_data, sizeof(mat));
    mat.m[row][0] = v.x;
    mat.m[row][1] = v.y;
    mat.m[row][2] = v.z;
    memcpy(result_data, &mat, sizeof(mat));
    NMO_RETURN_OK();
}

MATH_OP(op_set_matrix_x)
{
    MATH_UNUSED();
    return set_matrix_row(p1_data, p2_data, result_data, 0);
}

MATH_OP(op_set_matrix_y)
{
    MATH_UNUSED();
    return set_matrix_row(p1_data, p2_data, result_data, 1);
}

MATH_OP(op_set_matrix_z)
{
    MATH_UNUSED();
    return set_matrix_row(p1_data, p2_data, result_data, 2);
}

/* CKVectorSetPosMatrix */
MATH_OP(op_set_matrix_position)
{
    MATH_UNUSED();
    return set_matrix_row(p1_data, p2_data, result_data, 3);
}

/* ---- Colors ---- */

/* CKColorInverseColor: every component, alpha too */
MATH_OP(op_inverse_color)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_color_t c;
    memcpy(&c, p1_data, sizeof(c));
    c.r = 1.0f - c.r;
    c.g = 1.0f - c.g;
    c.b = 1.0f - c.b;
    c.a = 1.0f - c.a;
    memcpy(result_data, &c, sizeof(c));
    NMO_RETURN_OK();
}

/* CKColorRainbowFloatFloat: the plugin's six-sector wheel of hue p1, scaled by p2 */
MATH_OP(op_rainbow_color)
{
    MATH_UNUSED();
    const float hue = read_float(p1_data), intensity = read_float(p2_data);
    nmo_color_t c = {0.0f, 0.0f, 0.0f, 1.0f};
    if (hue >= 0.0f && hue < 0.15f) {
        c.r = 1.0f;
        c.g = hue * 6.6666665f;
    } else if (hue >= 0.15f && hue < 0.30f) {
        c.r = 1.0f - (hue - 0.15f) * 6.6666665f;
        c.g = 1.0f;
    } else if (hue >= 0.30f && hue < 0.5f) {
        c.g = 1.0f;
        c.b = (hue - 0.30f) * 5.0f;
    } else if (hue >= 0.5f && hue < 0.65f) {
        c.g = 1.0f - (hue - 0.5f) * 6.6666665f;
        c.b = 1.0f;
    } else if (hue >= 0.65f && hue < 0.85f) {
        c.r = (hue - 0.65f) * 5.0f;
        c.b = 1.0f;
    } else {
        const float h = hue > 1.0f ? 1.0f : hue;
        c.r = 1.0f;
        c.b = 1.0f - (h - 0.85f) * 6.6666665f;
    }
    c.r *= intensity;
    c.g *= intensity;
    c.b *= intensity;
    c.r = c.r > 1.0f ? 1.0f : c.r;
    c.g = c.g > 1.0f ? 1.0f : c.g;
    c.b = c.b > 1.0f ? 1.0f : c.b;
    memcpy(result_data, &c, sizeof(c));
    NMO_RETURN_OK();
}

/* ---- Rectangles ---- */

/* CKRectSetLeftRectFloat and the others: the rectangle with one side moved */
static nmo_status_t set_rect_side(const void *p1_data, const void *p2_data, void *result_data,
                                  int index)
{
    float sides[4];
    memcpy(sides, p1_data, sizeof(sides));
    sides[index] = read_float(p2_data);
    memcpy(result_data, sides, sizeof(sides));
    NMO_RETURN_OK();
}

MATH_OP(op_set_rect_left)
{
    MATH_UNUSED();
    return set_rect_side(p1_data, p2_data, result_data, 0);
}

MATH_OP(op_set_rect_top)
{
    MATH_UNUSED();
    return set_rect_side(p1_data, p2_data, result_data, 1);
}

MATH_OP(op_set_rect_right)
{
    MATH_UNUSED();
    return set_rect_side(p1_data, p2_data, result_data, 2);
}

MATH_OP(op_set_rect_bottom)
{
    MATH_UNUSED();
    return set_rect_side(p1_data, p2_data, result_data, 3);
}

/* CKRectSetWidthRectFloat: the right side follows the left */
MATH_OP(op_set_rect_width)
{
    MATH_UNUSED();
    nmo_rect_t r;
    memcpy(&r, p1_data, sizeof(r));
    r.right = r.left + read_float(p2_data);
    memcpy(result_data, &r, sizeof(r));
    NMO_RETURN_OK();
}

/* CKRectSetHeightRectFloat: the bottom follows the top */
MATH_OP(op_set_rect_height)
{
    MATH_UNUSED();
    nmo_rect_t r;
    memcpy(&r, p1_data, sizeof(r));
    r.bottom = r.top + read_float(p2_data);
    memcpy(result_data, &r, sizeof(r));
    NMO_RETURN_OK();
}

/* CKRectSetCenterRect2dVector: the rectangle moved so that its center is p2 */
MATH_OP(op_set_rect_center)
{
    MATH_UNUSED();
    nmo_rect_t r;
    nmo_vector2_t center;
    memcpy(&r, p1_data, sizeof(r));
    memcpy(&center, p2_data, sizeof(center));
    const float half_width = (r.right - r.left) * 0.5f;
    const float half_height = (r.bottom - r.top) * 0.5f;
    r.left = center.x - half_width;
    r.top = center.y - half_height;
    r.right = center.x + half_width;
    r.bottom = center.y + half_height;
    memcpy(result_data, &r, sizeof(r));
    NMO_RETURN_OK();
}

/* CK2dVectorGetBRRect */
MATH_OP(op_get_rect_bottom_right)
{
    MATH_UNUSED();
    (void)p2_data;
    nmo_rect_t r;
    memcpy(&r, p1_data, sizeof(r));
    const nmo_vector2_t corner = {r.right, r.bottom};
    memcpy(result_data, &corner, sizeof(corner));
    NMO_RETURN_OK();
}

/* ---- Registration ---- */

#define UNARY(OP, P1, RES, FN, NAME) \
    { \
        .operation_guid = OP, \
        .p1_type_guid = P1, \
        .p2_type_guid = {0, 0}, \
        .result_type_guid = RES, \
        .function = FN, \
        .user_data = NULL, \
        .flags = NMO_OP_UNARY, \
        .priority = 100, \
        .name = NAME, \
        .description = NAME, \
    }
#define BINARY(OP, P1, P2, RES, FN, NAME) \
    { \
        .operation_guid = OP, \
        .p1_type_guid = P1, \
        .p2_type_guid = P2, \
        .result_type_guid = RES, \
        .function = FN, \
        .user_data = NULL, \
        .flags = NMO_OP_BINARY, \
        .priority = 100, \
        .name = NAME, \
        .description = NAME, \
    }

nmo_status_t nmo_register_math_operations(nmo_operation_registry_t *operation_registry,
                                          const nmo_type_registry_t *type_registry)
{
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL operation_registry or type_registry");
    }

    /* The type triples the plugin registers (ParamOp_RegisterOperationFunction) */
    nmo_operation_desc_t operations[] = {
        UNARY(NMO_OP_GUID_SQRT, CKPGUID_FLOAT, CKPGUID_FLOAT, op_sqrt_float, "Square Root"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, CKPGUID_FLOAT, op_inverse_float, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_INT, CKPGUID_FLOAT, op_inverse_int_to_float, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, CKPGUID_INT, op_inverse_float_to_int, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_VECTOR, CKPGUID_VECTOR, op_inverse_vector, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, op_inverse_vector2, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_MATRIX, CKPGUID_MATRIX, op_inverse_matrix, "Inverse"),
        UNARY(NMO_OP_GUID_INVERSE, CKPGUID_COLOR, CKPGUID_COLOR, op_inverse_color, "Inverse"),
        UNARY(NMO_OP_GUID_DEG_TO_RAD, CKPGUID_FLOAT, CKPGUID_FLOAT, op_deg_to_rad, "Degre To Radian"),
        UNARY(NMO_OP_GUID_RAD_TO_DEG, CKPGUID_FLOAT, CKPGUID_FLOAT, op_rad_to_deg, "Radian To Degre"),

        UNARY(NMO_OP_GUID_NORMALIZE, CKPGUID_VECTOR, CKPGUID_VECTOR, op_normalize_vector, "Normalize"),
        UNARY(NMO_OP_GUID_GET_MAGNITUDE, CKPGUID_VECTOR, CKPGUID_FLOAT, op_magnitude_vector, "Get Magnitude"),
        UNARY(NMO_OP_GUID_GET_MAGNITUDE, CKPGUID_2DVECTOR, CKPGUID_FLOAT, op_magnitude_vector2, "Get Magnitude"),
        BINARY(NMO_OP_GUID_GET_ANGLE, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_FLOAT, op_angle_vector, "Get Angle"),
        BINARY(NMO_OP_GUID_GET_ANGLE, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, CKPGUID_FLOAT, op_angle_vector2, "Get Angle"),
        BINARY(NMO_OP_GUID_REFLECT, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, op_reflect_vector, "Reflect"),
        BINARY(NMO_OP_GUID_SYMMETRY, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, op_symmetry_vector, "Symmetry"),
        BINARY(NMO_OP_GUID_SYMMETRY, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, op_symmetry_vector2, "Symmetry"),
        BINARY(NMO_OP_GUID_SPHERIC_TO_CART, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_VECTOR, op_spheric_to_cartesian, "Spheric To Cartesian"),
        BINARY(NMO_OP_GUID_FROM_ROTATION, CKPGUID_VECTOR, CKPGUID_ANGLE, CKPGUID_QUATERNION, op_quaternion_from_rotation, "From Rotation"),
        BINARY(NMO_OP_GUID_FROM_ROTATION, CKPGUID_VECTOR, CKPGUID_ANGLE, CKPGUID_MATRIX, op_matrix_from_rotation, "From Rotation"),

        UNARY(NMO_OP_GUID_GET_X, CKPGUID_QUATERNION, CKPGUID_FLOAT, op_get_component_0, "Get X"),
        UNARY(NMO_OP_GUID_GET_Y, CKPGUID_QUATERNION, CKPGUID_FLOAT, op_get_component_1, "Get Y"),
        UNARY(NMO_OP_GUID_GET_Z, CKPGUID_QUATERNION, CKPGUID_FLOAT, op_get_component_2, "Get Z"),
        UNARY(NMO_OP_GUID_GET_W, CKPGUID_QUATERNION, CKPGUID_FLOAT, op_get_component_3, "Get W"),
        UNARY(NMO_OP_GUID_GET_RED, CKPGUID_COLOR, CKPGUID_FLOAT, op_get_component_0, "Get Red"),
        UNARY(NMO_OP_GUID_GET_GREEN, CKPGUID_COLOR, CKPGUID_FLOAT, op_get_component_1, "Get Green"),
        UNARY(NMO_OP_GUID_GET_BLUE, CKPGUID_COLOR, CKPGUID_FLOAT, op_get_component_2, "Get Blue"),
        UNARY(NMO_OP_GUID_GET_ALPHA, CKPGUID_COLOR, CKPGUID_FLOAT, op_get_component_3, "Get Alpha"),
        BINARY(NMO_OP_GUID_SET_X, CKPGUID_VECTOR, CKPGUID_FLOAT, CKPGUID_VECTOR, op_set_vector_x, "Set X"),
        BINARY(NMO_OP_GUID_SET_Y, CKPGUID_VECTOR, CKPGUID_FLOAT, CKPGUID_VECTOR, op_set_vector_y, "Set Y"),
        BINARY(NMO_OP_GUID_SET_Z, CKPGUID_VECTOR, CKPGUID_FLOAT, CKPGUID_VECTOR, op_set_vector_z, "Set Z"),
        BINARY(NMO_OP_GUID_SET_X, CKPGUID_EULERANGLES, CKPGUID_FLOAT, CKPGUID_EULERANGLES, op_set_vector_x, "Set X"),
        BINARY(NMO_OP_GUID_SET_Y, CKPGUID_EULERANGLES, CKPGUID_FLOAT, CKPGUID_EULERANGLES, op_set_vector_y, "Set Y"),
        BINARY(NMO_OP_GUID_SET_Z, CKPGUID_EULERANGLES, CKPGUID_FLOAT, CKPGUID_EULERANGLES, op_set_vector_z, "Set Z"),
        BINARY(NMO_OP_GUID_SET_X, CKPGUID_2DVECTOR, CKPGUID_FLOAT, CKPGUID_2DVECTOR, op_set_vector2_x, "Set X"),
        BINARY(NMO_OP_GUID_SET_Y, CKPGUID_2DVECTOR, CKPGUID_FLOAT, CKPGUID_2DVECTOR, op_set_vector2_y, "Set Y"),
        BINARY(NMO_OP_GUID_SET_X, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, op_set_quaternion_x, "Set X"),
        BINARY(NMO_OP_GUID_SET_Y, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, op_set_quaternion_y, "Set Y"),
        BINARY(NMO_OP_GUID_SET_Z, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, op_set_quaternion_z, "Set Z"),
        BINARY(NMO_OP_GUID_SET_W, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, op_set_quaternion_w, "Set W"),
        BINARY(NMO_OP_GUID_SET_X, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, op_set_matrix_x, "Set X"),
        BINARY(NMO_OP_GUID_SET_Y, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, op_set_matrix_y, "Set Y"),
        BINARY(NMO_OP_GUID_SET_Z, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, op_set_matrix_z, "Set Z"),
        BINARY(NMO_OP_GUID_SET_POSITION, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, op_set_matrix_position, "Set Position"),

        BINARY(NMO_OP_GUID_SET_RED, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, op_set_color_r, "Set Red"),
        BINARY(NMO_OP_GUID_SET_GREEN, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, op_set_color_g, "Set Green"),
        BINARY(NMO_OP_GUID_SET_BLUE, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, op_set_color_b, "Set Blue"),
        BINARY(NMO_OP_GUID_SET_ALPHA, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, op_set_color_a, "Set Alpha"),
        BINARY(NMO_OP_GUID_RAINBOW_COLOR, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, op_rainbow_color, "Rainbow Color"),

        BINARY(NMO_OP_GUID_SET_LEFT, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_left, "Set Left"),
        BINARY(NMO_OP_GUID_SET_TOP, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_top, "Set Top"),
        BINARY(NMO_OP_GUID_SET_RIGHT, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_right, "Set Right"),
        BINARY(NMO_OP_GUID_SET_BOTTOM, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_bottom, "Set Bottom"),
        BINARY(NMO_OP_GUID_SET_WIDTH, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_width, "Set Width"),
        BINARY(NMO_OP_GUID_SET_HEIGHT, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, op_set_rect_height, "Set Height"),
        BINARY(NMO_OP_GUID_SET_CENTER, CKPGUID_RECT, CKPGUID_2DVECTOR, CKPGUID_RECT, op_set_rect_center, "Set Geometric Center"),
        UNARY(NMO_OP_GUID_GET_BOTTOM_RIGHT, CKPGUID_RECT, CKPGUID_2DVECTOR, op_get_rect_bottom_right, "Get Bottom-Right Corner"),
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, (uint32_t)count,
                                                type_registry, NULL);
}
