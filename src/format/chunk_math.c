/**
 * @file chunk_math.c
 * @brief Implementation of chunk math type operations
 * 
 * This file implements mathematical type serialization for chunks:
 * - Vector2, Vector3, Vector4
 * - Quaternion
 * - Matrix
 * - Color
 * 
 * Note: Object ID arrays and primitive arrays are implemented in chunk_api.c
 */

#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include <string.h>

// =============================================================================
// Math Type Helpers
// =============================================================================

nmo_result_t nmo_chunk_read_vector2(nmo_chunk_t *chunk, nmo_vector2_t *out_vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_vec, "Invalid arguments to nmo_chunk_read_vector2");

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector2(nmo_chunk_t *chunk, const nmo_vector2_t *vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, vec, "Invalid arguments to nmo_chunk_write_vector2");

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->y);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_vector3(nmo_chunk_t *chunk, nmo_vector_t *out_vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_vec, "Invalid arguments to nmo_chunk_read_vector3");

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->z);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector3(nmo_chunk_t *chunk, const nmo_vector_t *vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, vec, "Invalid arguments to nmo_chunk_write_vector3");

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->z);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_vector4(nmo_chunk_t *chunk, nmo_vector4_t *out_vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_vec, "Invalid arguments to nmo_chunk_read_vector4");

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->z);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_vec->w);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector4(nmo_chunk_t *chunk, const nmo_vector4_t *vec) {
    NMO_CHUNK_CHECK_ARGS(chunk, vec, "Invalid arguments to nmo_chunk_write_vector4");

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->z);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, vec->w);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_quaternion(nmo_chunk_t *chunk, nmo_quaternion_t *out_quat) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_quat, "Invalid arguments to nmo_chunk_read_quaternion");

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_quat->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_quat->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_quat->z);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_quat->w);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_quaternion(nmo_chunk_t *chunk, const nmo_quaternion_t *quat) {
    NMO_CHUNK_CHECK_ARGS(chunk, quat, "Invalid arguments to nmo_chunk_write_quaternion");

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, quat->x);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, quat->y);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, quat->z);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, quat->w);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_matrix(nmo_chunk_t *chunk, nmo_matrix_t *out_mat) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_mat, "Invalid arguments to nmo_chunk_read_matrix");

    nmo_result_t result;

    // Read 4x4 matrix in row-major order
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result = nmo_chunk_read_float(chunk, &out_mat->m[row][col]);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_matrix(nmo_chunk_t *chunk, const nmo_matrix_t *mat) {
    NMO_CHUNK_CHECK_ARGS(chunk, mat, "Invalid arguments to nmo_chunk_write_matrix");

    nmo_result_t result;

    // Write 4x4 matrix in row-major order
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result = nmo_chunk_write_float(chunk, mat->m[row][col]);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_color(nmo_chunk_t *chunk, nmo_color_t *out_color) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_color, "Invalid arguments to nmo_chunk_read_color");

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_color->r);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_color->g);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_color->b);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_read_float(chunk, &out_color->a);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_color(nmo_chunk_t *chunk, const nmo_color_t *color) {
    NMO_CHUNK_CHECK_ARGS(chunk, color, "Invalid arguments to nmo_chunk_write_color");

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, color->r);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, color->g);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, color->b);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_float(chunk, color->a);
    NMO_RETURN_IF_ERROR(result);

    return nmo_result_ok();
}
