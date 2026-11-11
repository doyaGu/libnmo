/**
 * @file operations_values.c
 * @brief The Parameter Operations plugin's functions on values that the
 *        operation registry has no function for: the mixed Float and Integer
 *        ones (CKFloatAddFloatInt, CKIntMaxIntFloat, CKBoolInfFloatInt...),
 *        Integer bit operations, Bool Not, the vector, 2D vector, matrix,
 *        quaternion, Euler angle, color, box and rectangle ones, the generic
 *        equality tests and the object array set operations, as
 *        ParameterOperations.dll computes them.
 *
 * An operand without a source reads zeros (the plugin's static defaults),
 * and a division by zero divides by one after saying so on the console
 * (HandleDivByZeroFloat, HandleDivByZeroInt). operations_query.c looks
 * these up after its own table (eng_operation_builtin_values()).
 */

#include "../engine_internal.h"

#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VO_ADDITION NMO_GUID_INIT(0x33cc6b49, 0x3589282b)
#define VO_SUBTRACTION NMO_GUID_INIT(0x67641171, 0x6499077a)
#define VO_MULTIPLICATION NMO_GUID_INIT(0x38996b85, 0x334e35c2)
#define VO_DIVISION NMO_GUID_INIT(0x748c3502, 0x0fea0cf1)
#define VO_MAX NMO_GUID_INIT(0x7c2e01a8, 0x6e702206)
#define VO_MIN NMO_GUID_INIT(0x6ea214cb, 0x43be6b56)
#define VO_ARC_TANGENT_2 NMO_GUID_INIT(0x30547441, 0x611f682c)
#define VO_AND NMO_GUID_INIT(0x4a450980, 0x6b230426)
#define VO_OR NMO_GUID_INIT(0x26d81cc1, 0x340273b0)
#define VO_XOR NMO_GUID_INIT(0x014405eb, 0x2e087eec)
#define VO_OPPOSITE NMO_GUID_INIT(0x599c2cb6, 0x3e7d4de8)
#define VO_EQUAL NMO_GUID_INIT(0x6c62476b, 0x7a922973)
#define VO_NOT_EQUAL NMO_GUID_INIT(0xcf50abbb, 0x0b50a0c8)
#define VO_INFERIOR NMO_GUID_INIT(0x74a36ef4, 0x61262b24)
#define VO_INFERIOR_OR_EQUAL NMO_GUID_INIT(0x287f6201, 0x7fc624ca)
#define VO_SUPERIOR NMO_GUID_INIT(0x05b92e50, 0x5fc91d82)
#define VO_SUPERIOR_OR_EQUAL NMO_GUID_INIT(0x16581103, 0x0a1977bb)
#define VO_GET_DISTANCE NMO_GUID_INIT(0x265c7d00, 0x5c8f1702)
#define VO_GET_X NMO_GUID_INIT(0x12926657, 0x6228322e)
#define VO_GET_Y NMO_GUID_INIT(0x389f72bd, 0x7aef3482)
#define VO_GET_Z NMO_GUID_INIT(0x093d5158, 0x53ef47a0)
#define VO_GET_POSITION NMO_GUID_INIT(0x4bc87aea, 0x6b5b643e)
#define VO_GET_SCALE NMO_GUID_INIT(0x25991e7e, 0x32cc55e7)
#define VO_GET_COUNT NMO_GUID_INIT(0x44df6f61, 0x5f8f55c8)
#define VO_GET_LENGTH NMO_GUID_INIT(0x556a69af, 0x076e3f09)
#define VO_GET_CURVE_POSITION NMO_GUID_INIT(0x61323b67, 0x3a282f1c)
#define VO_CONVERT NMO_GUID_INIT(0x428009b4, 0x1caa5c78)
#define VO_NONE {0, 0}

/* ---- Operands and results ---- */

/* The operand's first `count` dwords, zeros past its data or without it */
static void read_dwords(const eng_param_t *param, void *out, size_t count)
{
    memset(out, 0, count * 4u);
    if (param != NULL && param->data != NULL) {
        memcpy(out, param->data, param->size < count * 4u ? param->size : count * 4u);
    }
}

static float read_float(const eng_param_t *param)
{
    float value;
    read_dwords(param, &value, 1);
    return value;
}

static int32_t read_int(const eng_param_t *param)
{
    int32_t value;
    read_dwords(param, &value, 1);
    return value;
}

static nmo_status_t write_dword(nmo_engine_t *engine, eng_param_t *out, int32_t value)
{
    return eng_param_write(engine, out, &value, sizeof(value));
}

static nmo_status_t write_float(nmo_engine_t *engine, eng_param_t *out, float value)
{
    return eng_param_write(engine, out, &value, sizeof(value));
}

static nmo_status_t write_floats(nmo_engine_t *engine, eng_param_t *out, const float *values,
                                 size_t count)
{
    return eng_param_write(engine, out, values, count * sizeof(float));
}

/* (int)(x) as the plugin's (__int64) cast of a float or double: toward zero */
static int32_t to_int(double value)
{
    return (int32_t)(int64_t)value;
}

/* HandleDivByZeroFloat/Int: a zero divisor becomes one, said on the console */
static void divide_by_zero(nmo_engine_t *engine, float *divisor)
{
    if (*divisor == 0.0f) {
        *divisor = 1.0f;
        eng_console(engine, "ParamerOperation 'Divide' : Divide by zero! in []\n", false);
    }
}

static void divide_by_zero_int(nmo_engine_t *engine, int32_t *divisor)
{
    if (*divisor == 0) {
        *divisor = 1;
        eng_console(engine, "ParamerOperation 'Divide' : Divide by zero! in []\n", false);
    }
}

static void read_matrix(const eng_param_t *param, eng_matrix_t *m)
{
    read_dwords(param, m->m, 16);
}

static nmo_status_t write_matrix(nmo_engine_t *engine, eng_param_t *out, const eng_matrix_t *m)
{
    return write_floats(engine, out, &m->m[0][0], 16);
}

/* ---- Float and Integer ---- */

static nmo_status_t op_atan2(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_float(e, o, (float)atan2(read_float(a), read_float(b)));
}

static nmo_status_t op_f_add_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_float(e, o, (float)((double)read_int(b) + read_float(a)));
}

static nmo_status_t op_f_sub_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_float(e, o, (float)(read_float(a) - (double)read_int(b)));
}

static nmo_status_t op_f_sub_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_float(e, o, (float)((double)read_int(a) - read_float(b)));
}

static nmo_status_t op_f_mul_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_float(e, o, (float)((double)read_int(b) * read_float(a)));
}

static nmo_status_t op_f_div_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    int32_t divisor = read_int(b);
    divide_by_zero_int(e, &divisor);
    return write_float(e, o, (float)(read_float(a) / (double)divisor));
}

static nmo_status_t op_f_div_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float divisor = read_float(b);
    divide_by_zero(e, &divisor);
    return write_float(e, o, (float)((double)read_int(a) / divisor));
}

/* CKFloatMaxFloatInt: the float when the integer is below it, else the integer */
static nmo_status_t op_f_max_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    const float f = read_float(a);
    const int32_t i = read_int(b);
    return write_float(e, o, (double)i < f ? f : (float)i);
}

/* CKFloatMinFloatInt: the float when the integer is above it, else the integer */
static nmo_status_t op_f_min_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    const float f = read_float(a);
    const int32_t i = read_int(b);
    return write_float(e, o, (double)i > f ? f : (float)i);
}

static nmo_status_t op_i_add_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, read_int(a) + to_int(read_float(b)));
}

static nmo_status_t op_i_sub_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, to_int((double)read_int(a) - read_float(b)));
}

static nmo_status_t op_i_sub_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, to_int(read_float(a) - (double)read_int(b)));
}

static nmo_status_t op_i_mul_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, to_int((double)read_int(a) * read_float(b)));
}

static nmo_status_t op_i_div_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float divisor = read_float(b);
    divide_by_zero(e, &divisor);
    return write_dword(e, o, to_int((double)read_int(a) / divisor));
}

static nmo_status_t op_i_div_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    int32_t divisor = read_int(b);
    divide_by_zero_int(e, &divisor);
    return write_dword(e, o, to_int(read_float(a) / (double)divisor));
}

/* CKIntMaxIntFloat: the float (truncated) when the integer is at most it, else the integer */
static nmo_status_t op_i_max_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    const int32_t i = read_int(a);
    const float f = read_float(b);
    return write_dword(e, o, (double)i <= f ? to_int(f) : i);
}

/* CKIntMinIntFloat: the float (truncated) when the integer is at least it, else the integer */
static nmo_status_t op_i_min_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    const int32_t i = read_int(a);
    const float f = read_float(b);
    return write_dword(e, o, (double)i >= f ? to_int(f) : i);
}

/* CKIntAndIntInt (0 without a second operand), CKIntOrIntInt, CKIntXorIntInt (the first) */
static nmo_status_t op_i_and(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, b != NULL && b->data != NULL ? read_int(a) & read_int(b) : 0);
}

static nmo_status_t op_i_or(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, read_int(a) | read_int(b));
}

static nmo_status_t op_i_xor(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, read_int(a) ^ read_int(b));
}

/* CKBoolNotBool: true without an operand */
static nmo_status_t op_b_not(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    return write_dword(e, o, a != NULL && a->data != NULL ? read_int(a) == 0 : 1);
}

/* The Float and Integer comparisons, each side as the plugin orders it */
static nmo_status_t op_b_inf_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(b) > read_float(a));
}

static nmo_status_t op_b_infeq_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, !((double)read_int(b) < read_float(a)));
}

static nmo_status_t op_b_sup_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(b) < read_float(a));
}

static nmo_status_t op_b_supeq_fi(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(b) <= read_float(a));
}

static nmo_status_t op_b_inf_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(a) < read_float(b));
}

static nmo_status_t op_b_infeq_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(a) <= read_float(b));
}

static nmo_status_t op_b_sup_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(a) > read_float(b));
}

static nmo_status_t op_b_supeq_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, !((double)read_int(a) < read_float(b)));
}

static nmo_status_t op_b_eq_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(a) == read_float(b));
}

static nmo_status_t op_b_neq_if(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return write_dword(e, o, (double)read_int(a) != read_float(b));
}

/* CKGenericEqual1..4Dword, CKBoolEqualMatrixMatrix and their Not Equal: the bytes compared */
static nmo_status_t compare_dwords(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                   eng_param_t *o, size_t count, bool equal)
{
    uint32_t x[16], y[16];
    read_dwords(a, x, count);
    read_dwords(b, y, count);
    const bool same = memcmp(x, y, count * 4u) == 0;
    return write_dword(e, o, equal ? same : !same);
}

#define EQUALITY(name, count)                                                                 \
    static nmo_status_t op_eq_##name(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,         \
                                     eng_param_t *o)                                          \
    {                                                                                         \
        return compare_dwords(e, a, b, o, count, true);                                       \
    }                                                                                         \
    static nmo_status_t op_neq_##name(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,        \
                                      eng_param_t *o)                                         \
    {                                                                                         \
        return compare_dwords(e, a, b, o, count, false);                                      \
    }
EQUALITY(1, 1)
EQUALITY(2, 2)
EQUALITY(3, 3)
EQUALITY(4, 4)
EQUALITY(16, 16)

/* ---- Vectors and 2D vectors ---- */

static nmo_status_t op_v_opposite(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float v[3];
    read_dwords(a, v, 3);
    const float r[3] = {-v[0], -v[1], -v[2]};
    return write_floats(e, o, r, 3);
}

static nmo_status_t op_2_opposite(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float v[2];
    read_dwords(a, v, 2);
    const float r[2] = {-v[0], -v[1]};
    return write_floats(e, o, r, 2);
}

/* Max takes the second component when the first is at most it, Min when the first is at least it */
static nmo_status_t components_max_min(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                       eng_param_t *o, size_t count, bool max)
{
    float x[3], y[3], r[3];
    read_dwords(a, x, count);
    read_dwords(b, y, count);
    for (size_t i = 0; i < count; i++) {
        r[i] = (max ? x[i] <= y[i] : x[i] >= y[i]) ? y[i] : x[i];
    }
    return write_floats(e, o, r, count);
}

static nmo_status_t op_v_max(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_max_min(e, a, b, o, 3, true);
}

static nmo_status_t op_v_min(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_max_min(e, a, b, o, 3, false);
}

static nmo_status_t op_2_max(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_max_min(e, a, b, o, 2, true);
}

static nmo_status_t op_2_min(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_max_min(e, a, b, o, 2, false);
}

/* CKVectorDivideVectorFloat, CK2dVectorDivide2dVectorFloat: times the divisor's inverse */
static nmo_status_t components_divide(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                      eng_param_t *o, size_t count)
{
    float divisor = read_float(b);
    divide_by_zero(e, &divisor);
    float v[3], r[3];
    read_dwords(a, v, count);
    const float inverse = 1.0f / divisor;
    for (size_t i = 0; i < count; i++) {
        r[i] = inverse * v[i];
    }
    return write_floats(e, o, r, count);
}

static nmo_status_t op_v_div_f(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_divide(e, a, b, o, 3);
}

static nmo_status_t op_2_div_f(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return components_divide(e, a, b, o, 2);
}

static nmo_status_t op_2_mul_f(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float v[2];
    read_dwords(a, v, 2);
    const float f = read_float(b);
    const float r[2] = {f * v[0], f * v[1]};
    return write_floats(e, o, r, 2);
}

/* CKVectorMultiplyVectorMatrix: Vx3DMultiplyMatrixVector */
static nmo_status_t op_v_mul_m(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    eng_matrix_t m;
    read_matrix(b, &m);
    eng_vector_t v, r;
    read_dwords(a, &v, 3);
    eng_vector_transform(&r, &m, &v);
    return write_floats(e, o, &r.x, 3);
}

static nmo_status_t op_distance_v(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float x[3], y[3];
    read_dwords(a, x, 3);
    read_dwords(b, y, 3);
    const float dx = x[0] - y[0], dy = x[1] - y[1], dz = x[2] - y[2];
    return write_float(e, o, (float)sqrt(dx * dx + dy * dy + dz * dz));
}

static nmo_status_t op_distance_2(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float x[2], y[2];
    read_dwords(a, x, 2);
    read_dwords(b, y, 2);
    const float dx = x[0] - y[0], dy = x[1] - y[1];
    return write_float(e, o, (float)sqrt(dx * dx + dy * dy));
}

/* CK2dVectorGetPosRect: the rectangle's left and top */
static nmo_status_t op_2_pos_rect(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float r[2];
    read_dwords(a, r, 2);
    return write_floats(e, o, r, 2);
}

/* ---- Matrices, quaternions and Euler angles ---- */

/* CKVectorGetXMatrix ... CKVectorGetPosMatrix: a row; Get Z reads the second operand (none) */
static nmo_status_t matrix_row(nmo_engine_t *e, eng_param_t *p, eng_param_t *o, int row)
{
    eng_matrix_t m;
    read_matrix(p, &m);
    return write_floats(e, o, m.m[row], 3);
}

static nmo_status_t op_m_x(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    return matrix_row(e, a, o, 0);
}

static nmo_status_t op_m_y(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    return matrix_row(e, a, o, 1);
}

static nmo_status_t op_m_z(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)a;
    return matrix_row(e, b, o, 2);
}

static nmo_status_t op_m_pos(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    return matrix_row(e, a, o, 3);
}

static nmo_status_t op_m_scale(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    eng_matrix_t m;
    read_matrix(a, &m);
    float r[3];
    for (int i = 0; i < 3; i++) {
        const float *row = m.m[i];
        r[i] = (float)sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    }
    return write_floats(e, o, r, 3);
}

/* CKVectorGetScaleBox: Max minus Min */
static nmo_status_t op_box_scale(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float box[6];
    read_dwords(a, box, 6);
    const float r[3] = {box[0] - box[3], box[1] - box[4], box[2] - box[5]};
    return write_floats(e, o, r, 3);
}

static nmo_status_t matrix_elements(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                    eng_param_t *o, bool add)
{
    float x[16], y[16];
    read_dwords(a, x, 16);
    read_dwords(b, y, 16);
    for (int i = 0; i < 16; i++) {
        x[i] = add ? y[i] + x[i] : x[i] - y[i];
    }
    return write_floats(e, o, x, 16);
}

static nmo_status_t op_m_add(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return matrix_elements(e, a, b, o, true);
}

static nmo_status_t op_m_sub(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return matrix_elements(e, a, b, o, false);
}

/* CKMatrixMultiplyMatrixMatrix, and CKMatrixDivideMatrixMatrix, which inverts the second
   into a temporary and then multiplies by the second itself */
static nmo_status_t op_m_mul(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    eng_matrix_t x, y, r;
    read_matrix(a, &x);
    read_matrix(b, &y);
    eng_matrix_multiply(&r, &x, &y);
    return write_matrix(e, o, &r);
}

static nmo_status_t op_m_mul_f(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float m[16];
    read_dwords(a, m, 16);
    const float f = read_float(b);
    for (int i = 0; i < 16; i++) {
        m[i] = f * m[i];
    }
    return write_floats(e, o, m, 16);
}

/* CKVectorSetMatrix: the rows the two vectors and their cross product, the rest zero */
static nmo_status_t op_m_from_vv(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float m[16] = {0}, x[3], y[3];
    read_dwords(a, x, 3);
    read_dwords(b, y, 3);
    memcpy(m, x, sizeof(x));
    memcpy(m + 4, y, sizeof(y));
    m[8] = y[2] * x[1] - x[2] * y[1];
    m[9] = x[2] * y[0] - y[2] * x[0];
    m[10] = x[0] * y[1] - x[1] * y[0];
    return write_floats(e, o, m, 16);
}

static nmo_status_t op_m_from_q(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    nmo_quaternion_t q;
    read_dwords(a, &q, 4);
    eng_matrix_t m;
    eng_matrix_from_quaternion_unchecked(&m, &q);
    return write_matrix(e, o, &m);
}

static nmo_status_t op_m_from_e(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float angles[3];
    read_dwords(a, angles, 3);
    eng_matrix_t m;
    eng_matrix_from_euler(&m, angles[0], angles[1], angles[2]);
    return write_matrix(e, o, &m);
}

/* CKQuaternionSetMatrix: FromMatrix(non-unit), the identity when it fails */
static nmo_status_t op_q_from_m(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    eng_matrix_t m;
    read_matrix(a, &m);
    nmo_quaternion_t q = {0.0f, 0.0f, 0.0f, 1.0f};
    eng_quaternion_from_matrix(&m, &q);
    return write_floats(e, o, &q.x, 4);
}

/* CKQuaternionSetEuler: VxQuaternion::FromEulerAngles, through a unit matrix */
static nmo_status_t op_q_from_e(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    float angles[3];
    read_dwords(a, angles, 3);
    eng_matrix_t m;
    eng_matrix_from_euler(&m, angles[0], angles[1], angles[2]);
    nmo_quaternion_t q;
    eng_quaternion_from_unit_matrix(&m, &q);
    return write_floats(e, o, &q.x, 4);
}

static nmo_status_t op_e_from_m(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    eng_matrix_t m;
    read_matrix(a, &m);
    float angles[3] = {0.0f, 0.0f, 0.0f};
    eng_matrix_to_euler(&m, &angles[0], &angles[1], &angles[2]);
    return write_floats(e, o, angles, 3);
}

/* CKEulerSetQuaternion: VxQuaternion::ToEulerAngles, through its matrix */
static nmo_status_t op_e_from_q(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    (void)b;
    nmo_quaternion_t q;
    read_dwords(a, &q, 4);
    eng_matrix_t m;
    eng_matrix_from_quaternion_unchecked(&m, &q);
    float angles[3] = {0.0f, 0.0f, 0.0f};
    eng_matrix_to_euler(&m, &angles[0], &angles[1], &angles[2]);
    return write_floats(e, o, angles, 3);
}

/* Vx3DQuaternionMultiply */
static nmo_status_t op_q_mul(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    nmo_quaternion_t l, r;
    read_dwords(a, &l, 4);
    read_dwords(b, &r, 4);
    const double x = l.x, y = l.y, z = l.z, w = l.w;
    const float q[4] = {
        (float)(x * r.w + w * r.x + y * r.z - z * r.y),
        (float)(y * r.w + w * r.y + z * r.x - x * r.z),
        (float)(z * r.w + w * r.z + x * r.y - y * r.x),
        (float)(w * r.w - x * r.x - y * r.y - z * r.z),
    };
    return write_floats(e, o, q, 4);
}

/* Vx3DQuaternionDivide: conjugate(Q) * P in VxMath's accumulation order */
static nmo_status_t op_q_div(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    nmo_quaternion_t p, q;
    read_dwords(a, &p, 4);
    read_dwords(b, &q, 4);
    const double x = p.x, y = p.y, z = p.z, w = p.w;
    const float r[4] = {
        (float)(q.w * x - q.x * w - z * q.y + y * q.z),
        (float)(y * q.w - w * q.y - x * q.z + z * q.x),
        (float)(z * q.w - w * q.z - y * q.x + x * q.y),
        (float)(z * q.z + y * q.y + x * q.x + q.w * w),
    };
    return write_floats(e, o, r, 4);
}

/* ---- Colors and boxes ---- */

/* CKColorAddColorColor: each channel capped at 1 */
static nmo_status_t op_c_add(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float x[4], y[4];
    read_dwords(a, x, 4);
    read_dwords(b, y, 4);
    for (int i = 0; i < 4; i++) {
        x[i] = x[i] + y[i];
        if (x[i] > 1.0f) {
            x[i] = 1.0f;
        }
    }
    return write_floats(e, o, x, 4);
}

/* CKColorSubtractColorColor: each channel floored at 0 */
static nmo_status_t op_c_sub(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float x[4], y[4];
    read_dwords(a, x, 4);
    read_dwords(b, y, 4);
    for (int i = 0; i < 4; i++) {
        x[i] = x[i] - y[i];
        if (x[i] < 0.0f) {
            x[i] = 0.0f;
        }
    }
    return write_floats(e, o, x, 4);
}

/* CKColorMultiplyFloatColor: each channel capped at 1 */
static nmo_status_t op_c_mul_f(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    const float f = read_float(a);
    float c[4];
    read_dwords(b, c, 4);
    for (int i = 0; i < 4; i++) {
        c[i] = c[i] * f;
        if (c[i] > 1.0f) {
            c[i] = 1.0f;
        }
    }
    return write_floats(e, o, c, 4);
}

/* CKBoxAddBoxBox: the boxes' union; CKBoxSubtractBoxBox: their intersection (Max, then Min) */
static nmo_status_t op_box_add(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float r[6], y[6];
    read_dwords(a, r, 6);
    read_dwords(b, y, 6);
    for (int i = 0; i < 3; i++) {
        r[i] = y[i] <= r[i] ? r[i] : y[i];
        r[i + 3] = y[i + 3] >= r[i + 3] ? r[i + 3] : y[i + 3];
    }
    return write_floats(e, o, r, 6);
}

static nmo_status_t op_box_sub(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    float r[6], y[6];
    read_dwords(a, r, 6);
    read_dwords(b, y, 6);
    for (int i = 0; i < 3; i++) {
        r[i] = y[i] >= r[i] ? r[i] : y[i];
        r[i + 3] = y[i + 3] <= r[i + 3] ? r[i + 3] : y[i + 3];
    }
    return write_floats(e, o, r, 6);
}

/* ---- 2D curves ---- */

/* CKFloatGetLength2dCurve, CKFloatGetY2dCurveFloat, CK2dVectorGetCurvePosFloat2dCurve: nothing
   without a curve */
static nmo_status_t op_curve2d_length(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                      eng_param_t *o)
{
    (void)b;
    float length = 0.0f;
    if (a == NULL || !eng_curve2d_length(a->data, a->size, &length)) {
        return NMO_OK;
    }
    return write_float(e, o, length);
}

static nmo_status_t op_curve2d_y(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    if (a == NULL || a->data == NULL) {
        return NMO_OK;
    }
    return write_float(e, o, nmo_engine_curve_y(a->data, a->size, read_float(b)));
}

static nmo_status_t op_curve2d_pos(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                   eng_param_t *o)
{
    float pos[2];
    if (b == NULL || !eng_curve2d_pos(b->data, b->size, read_float(a), pos)) {
        return NMO_OK;
    }
    return write_floats(e, o, pos, 2);
}

/* ---- Object arrays ---- */

typedef struct id_list {
    uint32_t *ids;
    size_t count;
} id_list_t;

static id_list_t read_ids(const eng_param_t *param)
{
    id_list_t list = {NULL, 0};
    if (param != NULL && param->data != NULL && param->size >= 4u) {
        list.ids = (uint32_t *)(void *)param->data;
        list.count = param->size / 4u;
    }
    return list;
}

static bool has_id(id_list_t list, uint32_t id)
{
    for (size_t i = 0; i < list.count; i++) {
        uint32_t value;
        memcpy(&value, &list.ids[i], sizeof(value));
        if (value == id) {
            return true;
        }
    }
    return false;
}

static uint32_t id_at(id_list_t list, size_t i)
{
    uint32_t value;
    memcpy(&value, &list.ids[i], sizeof(value));
    return value;
}

/* CKIntGetCountObjectArray */
static nmo_status_t op_array_count(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                   eng_param_t *o)
{
    (void)b;
    return write_dword(e, o, (int32_t)read_ids(a).count);
}

/* CKBoolEqualObjectArrayObjectArray: as many IDs, each of the first in the second */
static nmo_status_t op_array_equal(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                   eng_param_t *o)
{
    const id_list_t x = read_ids(a), y = read_ids(b);
    bool equal = x.count == y.count;
    for (size_t i = 0; equal && i < x.count; i++) {
        equal = has_id(y, id_at(x, i));
    }
    return write_dword(e, o, equal);
}

/*
 * The array set operations: Or, the second's IDs and then those of the first
 * it lacks (CKObjectArrayAddObjectArrayObjectArray); Subtraction, the first's
 * IDs the second lacks; And, the first's IDs the second has.
 */
static nmo_status_t array_combine(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                  eng_param_t *o, int mode)
{
    const id_list_t x = read_ids(a), y = read_ids(b);
    uint32_t *ids = (uint32_t *)malloc((x.count + y.count + 1u) * sizeof(uint32_t));
    if (ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t count = 0;
    if (mode == 0) {
        for (size_t i = 0; i < y.count; i++) {
            ids[count++] = id_at(y, i);
        }
    }
    for (size_t i = 0; i < x.count; i++) {
        const uint32_t id = id_at(x, i);
        if ((mode == 2) == has_id(y, id)) {
            ids[count++] = id;
        }
    }
    nmo_status_t status = eng_param_write(e, o, ids, count * sizeof(uint32_t));
    free(ids);
    return status;
}

static nmo_status_t op_array_or(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return array_combine(e, a, b, o, 0);
}

static nmo_status_t op_array_sub(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return array_combine(e, a, b, o, 1);
}

static nmo_status_t op_array_and(nmo_engine_t *e, eng_param_t *a, eng_param_t *b, eng_param_t *o)
{
    return array_combine(e, a, b, o, 2);
}

/*
 * CKObjectArrayAddObjectArrayObject: the object joins the first operand's
 * own array when it is not in it (the source changes); whether it joined.
 * CKObjectArraySubtractObjectArrayObject: its first place there removed;
 * whether it was there. False for an object that is not there.
 */
static nmo_status_t op_array_add_object(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                        eng_param_t *o)
{
    const uint32_t id = (uint32_t)read_int(b);
    if (a == NULL || id == 0 || eng_find_object(e, id) == NULL) {
        return write_dword(e, o, 0);
    }
    const id_list_t list = read_ids(a);
    if (has_id(list, id)) {
        return write_dword(e, o, 0);
    }
    uint32_t *ids = (uint32_t *)malloc((list.count + 1u) * sizeof(uint32_t));
    if (ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (list.count > 0) {
        memcpy(ids, list.ids, list.count * sizeof(uint32_t));
    }
    ids[list.count] = id;
    nmo_status_t status = eng_param_write(e, a, ids, (list.count + 1u) * sizeof(uint32_t));
    free(ids);
    return status != NMO_OK ? status : write_dword(e, o, 1);
}

static nmo_status_t op_array_remove_object(nmo_engine_t *e, eng_param_t *a, eng_param_t *b,
                                           eng_param_t *o)
{
    const uint32_t id = (uint32_t)read_int(b);
    const id_list_t list = read_ids(a);
    if (a == NULL || id == 0 || eng_find_object(e, id) == NULL || !has_id(list, id)) {
        return write_dword(e, o, 0);
    }
    uint32_t *ids = (uint32_t *)malloc(list.count * sizeof(uint32_t));
    if (ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t count = 0;
    bool removed = false;
    for (size_t i = 0; i < list.count; i++) {
        const uint32_t value = id_at(list, i);
        if (!removed && value == id) {
            removed = true;
            continue;
        }
        ids[count++] = value;
    }
    nmo_status_t status = eng_param_write(e, a, ids, count * sizeof(uint32_t));
    free(ids);
    return status != NMO_OK ? status : write_dword(e, o, 1);
}

/* ---- The table ---- */

typedef struct value_op {
    nmo_guid_t operation;
    nmo_guid_t result, input1, input2;
    eng_builtin_op_fn fn;
} value_op_t;

#define F CKPGUID_FLOAT_INIT
#define I CKPGUID_INT_INIT
#define B CKPGUID_BOOL_INIT
#define V CKPGUID_VECTOR_INIT
#define V2 CKPGUID_2DVECTOR_INIT
#define M CKPGUID_MATRIX_INIT
#define Q CKPGUID_QUATERNION_INIT
#define E CKPGUID_EULERANGLES_INIT
#define C CKPGUID_COLOR_INIT
#define BX CKPGUID_BOX_INIT
#define R CKPGUID_RECT_INIT
#define OA CKPGUID_OBJECTARRAY_INIT
#define N VO_NONE

static const value_op_t value_ops[] = {
    {VO_ARC_TANGENT_2, F, F, F, op_atan2},
    {VO_ADDITION, F, F, I, op_f_add_fi},
    {VO_SUBTRACTION, F, F, I, op_f_sub_fi},
    {VO_SUBTRACTION, F, I, F, op_f_sub_if},
    {VO_MULTIPLICATION, F, F, I, op_f_mul_fi},
    {VO_DIVISION, F, F, I, op_f_div_fi},
    {VO_DIVISION, F, I, F, op_f_div_if},
    {VO_MAX, F, F, I, op_f_max_fi},
    {VO_MIN, F, F, I, op_f_min_fi},
    {VO_GET_DISTANCE, F, V, V, op_distance_v},
    {VO_GET_DISTANCE, F, V2, V2, op_distance_2},
    {VO_GET_LENGTH, F, CKPGUID_2DCURVE_INIT, N, op_curve2d_length},
    {VO_GET_Y, F, CKPGUID_2DCURVE_INIT, F, op_curve2d_y},
    {VO_AND, I, I, I, op_i_and},
    {VO_OR, I, I, I, op_i_or},
    {VO_XOR, I, I, I, op_i_xor},
    {VO_ADDITION, I, I, F, op_i_add_if},
    {VO_SUBTRACTION, I, I, F, op_i_sub_if},
    {VO_SUBTRACTION, I, F, I, op_i_sub_fi},
    {VO_MULTIPLICATION, I, I, F, op_i_mul_if},
    {VO_DIVISION, I, I, F, op_i_div_if},
    {VO_DIVISION, I, F, I, op_i_div_fi},
    {VO_MAX, I, I, F, op_i_max_if},
    {VO_MIN, I, I, F, op_i_min_if},
    {VO_GET_COUNT, I, OA, N, op_array_count},
    {VO_OPPOSITE, B, B, N, op_b_not},
    {VO_INFERIOR, B, F, I, op_b_inf_fi},
    {VO_SUPERIOR, B, F, I, op_b_sup_fi},
    {VO_INFERIOR_OR_EQUAL, B, F, I, op_b_infeq_fi},
    {VO_SUPERIOR_OR_EQUAL, B, F, I, op_b_supeq_fi},
    {VO_INFERIOR, B, I, F, op_b_inf_if},
    {VO_SUPERIOR, B, I, F, op_b_sup_if},
    {VO_INFERIOR_OR_EQUAL, B, I, F, op_b_infeq_if},
    {VO_SUPERIOR_OR_EQUAL, B, I, F, op_b_supeq_if},
    {VO_EQUAL, B, I, F, op_b_eq_if},
    {VO_NOT_EQUAL, B, I, F, op_b_neq_if},
    {VO_OPPOSITE, V, V, N, op_v_opposite},
    {VO_MAX, V, V, V, op_v_max},
    {VO_MIN, V, V, V, op_v_min},
    {VO_DIVISION, V, V, F, op_v_div_f},
    {VO_MULTIPLICATION, V, V, M, op_v_mul_m},
    {VO_DIVISION, Q, Q, Q, op_q_div},
    {VO_MULTIPLICATION, Q, Q, Q, op_q_mul},
    {VO_GET_X, V, M, N, op_m_x},
    {VO_GET_Y, V, M, N, op_m_y},
    {VO_GET_Z, V, M, N, op_m_z},
    {VO_GET_POSITION, V, M, N, op_m_pos},
    {VO_GET_SCALE, V, M, N, op_m_scale},
    {VO_GET_SCALE, V, BX, N, op_box_scale},
    {VO_CONVERT, Q, M, N, op_q_from_m},
    {VO_CONVERT, Q, E, N, op_q_from_e},
    {VO_CONVERT, E, M, N, op_e_from_m},
    {VO_CONVERT, E, Q, N, op_e_from_q},
    {VO_OPPOSITE, V2, V2, N, op_2_opposite},
    {VO_MAX, V2, V2, V2, op_2_max},
    {VO_MIN, V2, V2, V2, op_2_min},
    {VO_MULTIPLICATION, V2, V2, F, op_2_mul_f},
    {VO_DIVISION, V2, V2, F, op_2_div_f},
    {VO_GET_CURVE_POSITION, V2, F, CKPGUID_2DCURVE_INIT, op_curve2d_pos},
    {VO_GET_POSITION, V2, R, N, op_2_pos_rect},
    {VO_ADDITION, M, M, M, op_m_add},
    {VO_SUBTRACTION, M, M, M, op_m_sub},
    {VO_MULTIPLICATION, M, M, M, op_m_mul},
    {VO_DIVISION, M, M, M, op_m_mul},
    {VO_MULTIPLICATION, M, M, F, op_m_mul_f},
    {VO_CONVERT, M, V, V, op_m_from_vv},
    {VO_CONVERT, M, Q, N, op_m_from_q},
    {VO_CONVERT, M, E, N, op_m_from_e},
    {VO_ADDITION, C, C, C, op_c_add},
    {VO_SUBTRACTION, C, C, C, op_c_sub},
    {VO_MULTIPLICATION, C, F, C, op_c_mul_f},
    {VO_ADDITION, BX, BX, BX, op_box_add},
    {VO_SUBTRACTION, BX, BX, BX, op_box_sub},
    {VO_ADDITION, B, OA, CKPGUID_OBJECT_INIT, op_array_add_object},
    {VO_SUBTRACTION, B, OA, CKPGUID_OBJECT_INIT, op_array_remove_object},
    {VO_OR, OA, OA, OA, op_array_or},
    {VO_SUBTRACTION, OA, OA, OA, op_array_sub},
    {VO_AND, OA, OA, OA, op_array_and},
    {VO_EQUAL, B, OA, OA, op_array_equal},
    {VO_EQUAL, B, CKPGUID_OBJECT_INIT, CKPGUID_OBJECT_INIT, op_eq_1},
    {VO_NOT_EQUAL, B, CKPGUID_OBJECT_INIT, CKPGUID_OBJECT_INIT, op_neq_1},
    {VO_EQUAL, B, V2, V2, op_eq_2},
    {VO_NOT_EQUAL, B, V2, V2, op_neq_2},
    {VO_EQUAL, B, V, V, op_eq_3},
    {VO_NOT_EQUAL, B, V, V, op_neq_3},
    {VO_EQUAL, B, E, E, op_eq_3},
    {VO_NOT_EQUAL, B, E, E, op_neq_3},
    {VO_EQUAL, B, Q, Q, op_eq_4},
    {VO_NOT_EQUAL, B, Q, Q, op_neq_4},
    {VO_EQUAL, B, R, R, op_eq_4},
    {VO_NOT_EQUAL, B, R, R, op_neq_4},
    {VO_EQUAL, B, C, C, op_eq_4},
    {VO_NOT_EQUAL, B, C, C, op_neq_4},
    {VO_EQUAL, B, M, M, op_eq_16},
    {VO_NOT_EQUAL, B, M, M, op_neq_16},
};

eng_builtin_op_fn eng_operation_builtin_values(nmo_guid_t operation, nmo_guid_t result,
                                               nmo_guid_t input1, nmo_guid_t input2)
{
    for (size_t i = 0; i < sizeof(value_ops) / sizeof(value_ops[0]); i++) {
        const value_op_t *op = &value_ops[i];
        if (nmo_guid_equals(op->operation, operation) && nmo_guid_equals(op->result, result) &&
            nmo_guid_equals(op->input1, input1) && nmo_guid_equals(op->input2, input2)) {
            return op->fn;
        }
    }
    return NULL;
}
