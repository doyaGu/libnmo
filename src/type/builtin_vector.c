/**
 * @file builtin_vector.c
 * @brief Builtin vector operations implementation
 *
 * Implements vector operations: VectorAdd, VectorSub, VectorMul, VectorDiv,
 * VectorDot, VectorCross (Vector3 only for cross).
 */

#include "type/nmo_builtin_operations.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_error.h"
#include "core/nmo_math.h"
#include <stdint.h>

/* ============================================================================
 * Vector2 Operations
 * ============================================================================ */

static nmo_status_t op_vector2_add(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector2_t *a = (const nmo_vector2_t *)p1_data;
    const nmo_vector2_t *b = (const nmo_vector2_t *)p2_data;
    nmo_vector2_t *out = (nmo_vector2_t *)result_data;
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector2_sub(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector2_t *a = (const nmo_vector2_t *)p1_data;
    const nmo_vector2_t *b = (const nmo_vector2_t *)p2_data;
    nmo_vector2_t *out = (nmo_vector2_t *)result_data;
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector2_mul(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector2_t *a = (const nmo_vector2_t *)p1_data;
    const nmo_vector2_t *b = (const nmo_vector2_t *)p2_data;
    nmo_vector2_t *out = (nmo_vector2_t *)result_data;
    out->x = a->x * b->x;
    out->y = a->y * b->y;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector2_div(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector2_t *a = (const nmo_vector2_t *)p1_data;
    const nmo_vector2_t *b = (const nmo_vector2_t *)p2_data;
    if (b->x == 0.0f || b->y == 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Vector2 division by zero");
    }
    nmo_vector2_t *out = (nmo_vector2_t *)result_data;
    out->x = a->x / b->x;
    out->y = a->y / b->y;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector2_dot(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector2_t *a = (const nmo_vector2_t *)p1_data;
    const nmo_vector2_t *b = (const nmo_vector2_t *)p2_data;
    *(float *)result_data = a->x * b->x + a->y * b->y;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vector3 Operations
 * ============================================================================ */

static nmo_status_t op_vector3_add(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    nmo_vector_t *out = (nmo_vector_t *)result_data;
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector3_sub(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    nmo_vector_t *out = (nmo_vector_t *)result_data;
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    out->z = a->z - b->z;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector3_mul(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    nmo_vector_t *out = (nmo_vector_t *)result_data;
    out->x = a->x * b->x;
    out->y = a->y * b->y;
    out->z = a->z * b->z;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector3_div(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    if (b->x == 0.0f || b->y == 0.0f || b->z == 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Vector3 division by zero");
    }
    nmo_vector_t *out = (nmo_vector_t *)result_data;
    out->x = a->x / b->x;
    out->y = a->y / b->y;
    out->z = a->z / b->z;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector3_dot(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    *(float *)result_data = a->x * b->x + a->y * b->y + a->z * b->z;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector3_cross(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector_t *a = (const nmo_vector_t *)p1_data;
    const nmo_vector_t *b = (const nmo_vector_t *)p2_data;
    nmo_vector_t *out = (nmo_vector_t *)result_data;
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * b->x;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vector4 Operations
 * ============================================================================ */

static nmo_status_t op_vector4_add(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector4_t *a = (const nmo_vector4_t *)p1_data;
    const nmo_vector4_t *b = (const nmo_vector4_t *)p2_data;
    nmo_vector4_t *out = (nmo_vector4_t *)result_data;
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
    out->w = a->w + b->w;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector4_sub(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector4_t *a = (const nmo_vector4_t *)p1_data;
    const nmo_vector4_t *b = (const nmo_vector4_t *)p2_data;
    nmo_vector4_t *out = (nmo_vector4_t *)result_data;
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    out->z = a->z - b->z;
    out->w = a->w - b->w;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector4_mul(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector4_t *a = (const nmo_vector4_t *)p1_data;
    const nmo_vector4_t *b = (const nmo_vector4_t *)p2_data;
    nmo_vector4_t *out = (nmo_vector4_t *)result_data;
    out->x = a->x * b->x;
    out->y = a->y * b->y;
    out->z = a->z * b->z;
    out->w = a->w * b->w;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector4_div(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector4_t *a = (const nmo_vector4_t *)p1_data;
    const nmo_vector4_t *b = (const nmo_vector4_t *)p2_data;
    if (b->x == 0.0f || b->y == 0.0f || b->z == 0.0f || b->w == 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Vector4 division by zero");
    }
    nmo_vector4_t *out = (nmo_vector4_t *)result_data;
    out->x = a->x / b->x;
    out->y = a->y / b->y;
    out->z = a->z / b->z;
    out->w = a->w / b->w;
    NMO_RETURN_OK();
}

static nmo_status_t op_vector4_dot(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data)
{
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_vector4_t *a = (const nmo_vector4_t *)p1_data;
    const nmo_vector4_t *b = (const nmo_vector4_t *)p2_data;
    *(float *)result_data = a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Registration
 * ============================================================================ */

nmo_status_t nmo_register_vector_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry)
{
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        /* Vector2 */
        {NMO_OP_GUID_VECTOR_ADD, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2,
         op_vector2_add, NULL, NMO_OP_BINARY, 100, "VectorAdd", "Vector2 addition"},
        {NMO_OP_GUID_VECTOR_SUB, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2,
         op_vector2_sub, NULL, NMO_OP_BINARY, 100, "VectorSub", "Vector2 subtraction"},
        {NMO_OP_GUID_VECTOR_MUL, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2,
         op_vector2_mul, NULL, NMO_OP_BINARY, 100, "VectorMul", "Vector2 component-wise multiply"},
        {NMO_OP_GUID_VECTOR_DIV, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2,
         op_vector2_div, NULL, NMO_OP_BINARY, 100, "VectorDiv", "Vector2 component-wise divide"},
        {NMO_OP_GUID_VECTOR_DOT, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_VECTOR2, NMO_TYPE_GUID_FLOAT,
         op_vector2_dot, NULL, NMO_OP_BINARY, 100, "VectorDot", "Vector2 dot product"},

        /* Vector3 */
        {NMO_OP_GUID_VECTOR_ADD, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3,
         op_vector3_add, NULL, NMO_OP_BINARY, 100, "VectorAdd", "Vector3 addition"},
        {NMO_OP_GUID_VECTOR_SUB, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3,
         op_vector3_sub, NULL, NMO_OP_BINARY, 100, "VectorSub", "Vector3 subtraction"},
        {NMO_OP_GUID_VECTOR_MUL, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3,
         op_vector3_mul, NULL, NMO_OP_BINARY, 100, "VectorMul", "Vector3 component-wise multiply"},
        {NMO_OP_GUID_VECTOR_DIV, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3,
         op_vector3_div, NULL, NMO_OP_BINARY, 100, "VectorDiv", "Vector3 component-wise divide"},
        {NMO_OP_GUID_VECTOR_DOT, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_FLOAT,
         op_vector3_dot, NULL, NMO_OP_BINARY, 100, "VectorDot", "Vector3 dot product"},
        {NMO_OP_GUID_VECTOR_CROSS, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3, NMO_TYPE_GUID_VECTOR3,
         op_vector3_cross, NULL, NMO_OP_BINARY, 100, "VectorCross", "Vector3 cross product"},

        /* Vector4 */
        {NMO_OP_GUID_VECTOR_ADD, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4,
         op_vector4_add, NULL, NMO_OP_BINARY, 100, "VectorAdd", "Vector4 addition"},
        {NMO_OP_GUID_VECTOR_SUB, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4,
         op_vector4_sub, NULL, NMO_OP_BINARY, 100, "VectorSub", "Vector4 subtraction"},
        {NMO_OP_GUID_VECTOR_MUL, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4,
         op_vector4_mul, NULL, NMO_OP_BINARY, 100, "VectorMul", "Vector4 component-wise multiply"},
        {NMO_OP_GUID_VECTOR_DIV, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4,
         op_vector4_div, NULL, NMO_OP_BINARY, 100, "VectorDiv", "Vector4 component-wise divide"},
        {NMO_OP_GUID_VECTOR_DOT, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_VECTOR4, NMO_TYPE_GUID_FLOAT,
         op_vector4_dot, NULL, NMO_OP_BINARY, 100, "VectorDot", "Vector4 dot product"}
    };

    return nmo_operation_registry_register_bulk(
        operation_registry,
        operations,
        (uint32_t)(sizeof(operations) / sizeof(operations[0])),
        type_registry,
        NULL
    );
}