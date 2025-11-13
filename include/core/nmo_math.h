#ifndef NMO_MATH_H
#define NMO_MATH_H

/**
 * @file nmo_math.h
 * @brief VxMath-compatible mathematical types
 *
 * Provides basic mathematical types used in Virtools files:
 * - 2D/3D/4D vectors
 * - 4x4 matrices
 * - Quaternions
 * - Colors (RGBA float)
 *
 * These types match the memory layout of VxMath types from the
 * Virtools SDK for binary compatibility.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 2D vector
 */
typedef struct nmo_vector2 {
    float x, y;
} nmo_vector2_t;

/**
 * @brief 3D vector
 */
typedef struct nmo_vector {
    float x, y, z;
} nmo_vector_t;

/**
 * @brief 4D vector
 */
typedef struct nmo_vector4 {
    float x, y, z, w;
} nmo_vector4_t;

/**
 * @brief 4x4 matrix (row-major order)
 *
 * Memory layout:
 * m[0][0] m[0][1] m[0][2] m[0][3]
 * m[1][0] m[1][1] m[1][2] m[1][3]
 * m[2][0] m[2][1] m[2][2] m[2][3]
 * m[3][0] m[3][1] m[3][2] m[3][3]
 */
typedef struct nmo_matrix {
    float m[4][4];
} nmo_matrix_t;

/**
 * @brief Quaternion (x, y, z, w)
 */
typedef struct nmo_quaternion {
    float x, y, z, w;
} nmo_quaternion_t;

/**
 * @brief RGBA color (0.0-1.0 range)
 */
typedef struct nmo_color {
    float r, g, b, a;
} nmo_color_t;

/**
 * @brief 2x2 matrix
 */
typedef struct nmo_matrix2 {
    float m[2][2];
} nmo_matrix2_t;

/**
 * @brief 3x3 matrix
 */
typedef struct nmo_matrix3 {
    float m[3][3];
} nmo_matrix3_t;

/**
 * @brief Bounding box (axis-aligned)
 */
typedef struct nmo_box {
    nmo_vector_t min;
    nmo_vector_t max;
} nmo_box_t;

/**
 * @brief 2D rectangle
 */
typedef struct nmo_rect {
    float left, top, right, bottom;
} nmo_rect_t;

#ifdef __cplusplus
}
#endif

#endif // NMO_MATH_H
