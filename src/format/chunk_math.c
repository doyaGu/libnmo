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
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_vector2"));
    }

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector2(nmo_chunk_t *chunk, const nmo_vector2_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_vector2"));
    }

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->y);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_vector3(nmo_chunk_t *chunk, nmo_vector_t *out_vec) {
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_vector3"));
    }

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->z);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector3(nmo_chunk_t *chunk, const nmo_vector_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_vector3"));
    }

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->z);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_vector4(nmo_chunk_t *chunk, nmo_vector4_t *out_vec) {
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_vector4"));
    }

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->z);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_vec->w);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_vector4(nmo_chunk_t *chunk, const nmo_vector4_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_vector4"));
    }

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->z);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, vec->w);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_quaternion(nmo_chunk_t *chunk, nmo_quaternion_t *out_quat) {
    if (!chunk || !out_quat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_quaternion"));
    }

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_quat->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_quat->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_quat->z);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_quat->w);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_quaternion(nmo_chunk_t *chunk, const nmo_quaternion_t *quat) {
    if (!chunk || !quat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_quaternion"));
    }

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, quat->x);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, quat->y);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, quat->z);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, quat->w);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_matrix(nmo_chunk_t *chunk, nmo_matrix_t *out_mat) {
    if (!chunk || !out_mat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_matrix"));
    }

    nmo_result_t result;

    // Read 4x4 matrix in row-major order
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result = nmo_chunk_read_float(chunk, &out_mat->m[row][col]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_matrix(nmo_chunk_t *chunk, const nmo_matrix_t *mat) {
    if (!chunk || !mat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_matrix"));
    }

    nmo_result_t result;

    // Write 4x4 matrix in row-major order
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result = nmo_chunk_write_float(chunk, mat->m[row][col]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_color(nmo_chunk_t *chunk, nmo_color_t *out_color) {
    if (!chunk || !out_color) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_read_color"));
    }

    nmo_result_t result;

    result = nmo_chunk_read_float(chunk, &out_color->r);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_color->g);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_color->b);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_float(chunk, &out_color->a);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_color(nmo_chunk_t *chunk, const nmo_color_t *color) {
    if (!chunk || !color) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to nmo_chunk_write_color"));
    }

    nmo_result_t result;

    result = nmo_chunk_write_float(chunk, color->r);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, color->g);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, color->b);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, color->a);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}
