/**
 * @file affine.c
 * @brief Recover the unanimated rotation and principal scales of a local pose.
 *
 * VxMath's polar/spectral decomposition and scale permutations, using the local
 * Ballanced reference. Only the principal values are needed: the animation
 * supplies the new scale axis itself. Transposing around the right polar
 * decomposition recovers stretch before rotation in the engine's row layout.
 * A reflection is kept in the signed principal scales and the rotation is proper.
 */
#include "../engine_transform.h"

#include <math.h>
#include <string.h>

static double affine_determinant(const double m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) +
           m[0][1] * (m[1][2] * m[2][0] - m[1][0] * m[2][2]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

static double affine_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int affine_max_column(const double m[3][3]) {
    double maximum = 0.0;
    int column = -1;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double magnitude = fabs(m[i][j]);
            if (magnitude > maximum) {
                maximum = magnitude;
                column = j;
            }
        }
    }
    return column;
}

static void affine_reflector(double v[3]) {
    const double length = hypot(hypot(v[0], v[1]), v[2]);
    if (length == 0.0) {
        return;
    }
    const double x = v[0] / length;
    const double y = v[1] / length;
    const double z = v[2] / length + (v[2] < 0.0 ? -1.0 : 1.0);
    const double scale = sqrt(2.0 / (x * x + y * y + z * z));
    v[0] = x * scale;
    v[1] = y * scale;
    v[2] = z * scale;
}

static void affine_reflect_columns(double m[3][3], const double u[3]) {
    const double vector[3] = {u[0], u[1], u[2]};
    for (int j = 0; j < 3; ++j) {
        const double column[3] = {m[0][j], m[1][j], m[2][j]};
        const double scale = affine_dot(column, vector);
        for (int i = 0; i < 3; ++i) {
            m[i][j] -= scale * vector[i];
        }
    }
}

static void affine_reflect_rows(double m[3][3], const double u[3]) {
    const double vector[3] = {u[0], u[1], u[2]};
    for (int i = 0; i < 3; ++i) {
        const double scale = affine_dot(m[i], vector);
        for (int j = 0; j < 3; ++j) {
            m[i][j] -= scale * vector[j];
        }
    }
}

static void affine_rank_one(double m[3][3], double q[3][3]) {
    double work[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) work[i][j] = m[i][j];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) q[i][j] = i == j ? 1.0 : 0.0;
    const int column = affine_max_column(work);
    if (column < 0) return;
    double u[3] = {work[0][column], work[1][column], work[2][column]};
    affine_reflector(u);
    affine_reflect_columns(work, u);
    double v[3] = {work[2][0], work[2][1], work[2][2]};
    affine_reflector(v);
    affine_reflect_rows(work, v);
    // Choose a proper completion on the unused axis so quaternion callers can represent Q.
    if (work[2][2] < 0.0) q[0][0] = q[2][2] = -1.0;
    affine_reflect_columns(q, u);
    affine_reflect_rows(q, v);
    if (m != q) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m[i][j] = work[i][j];
}

static void affine_rank_two(double m[3][3], const double adjoint_transpose[3][3], double q[3][3]) {
    const int column = affine_max_column(adjoint_transpose);
    if (column < 0) { affine_rank_one(m, q); return; }
    double work[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) work[i][j] = m[i][j];
    double u[3] = {adjoint_transpose[0][column], adjoint_transpose[1][column], adjoint_transpose[2][column]};
    affine_reflector(u);
    affine_reflect_columns(work, u);
    double v[3] = {work[0][1]*work[1][2] - work[0][2]*work[1][1],
                   work[0][2]*work[1][0] - work[0][0]*work[1][2],
                   work[0][0]*work[1][1] - work[0][1]*work[1][0]};
    affine_reflector(v);
    affine_reflect_rows(work, v);
    const double w = work[0][0], z = work[1][1], x = work[1][0], y = work[0][1];
    if (x*y >= w*z) {
        const double c = z - w, s = x + y, length = hypot(c, s);
        q[1][1] = c/length; q[0][0] = -q[1][1]; q[1][0] = q[0][1] = s/length;
    } else {
        const double c = w + z, s = x - y, length = hypot(c, s);
        q[0][0] = q[1][1] = c/length; q[1][0] = s/length; q[0][1] = -q[1][0];
    }
    q[0][2] = q[1][2] = q[2][0] = q[2][1] = 0.0;
    q[2][2] = x*y >= w*z ? -1.0 : 1.0;
    affine_reflect_columns(q, u);
    affine_reflect_rows(q, v);
    if (m != q) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m[i][j] = work[i][j];
}

static bool affine_polar(const eng_matrix_t *matrix, float orthogonal[3][3], float symmetric[3][3], float *reflection)
{
    // Work on the transposed linear part; neither output may overwrite the input yet.
    double input[3][3], iterate[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            input[row][column] = matrix->m[row][column];
            iterate[column][row] = input[row][column];
        }
    }
    const double original_determinant = affine_determinant(input);
    bool first_iteration = true;
    bool converged = false;
    for (int iteration = 0; iteration < 128; iteration++) {
        if (!isfinite(original_determinant)) {
            return false;
        }

        double maximum = 0.0;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const double magnitude = fabs(iterate[row][column]);
                if (magnitude > maximum) {
                    maximum = magnitude;
                }
            }
        }
        // Uniform binary scaling conditions the Newton step without rounding its inputs.
        int exponent = 0;
        frexp(maximum, &exponent);
        double scaled[3][3], cofactors[3][3];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                scaled[row][column] = ldexp(iterate[row][column], -exponent);
            }
        }
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const int r0 = (row + 1) % 3, r1 = (row + 2) % 3;
                const int c0 = (column + 1) % 3, c1 = (column + 2) % 3;
                cofactors[row][column] = scaled[r0][c0] * scaled[r1][c1] - scaled[r0][c1] * scaled[r1][c0];
            }
        }
        const double scaled_determinant = first_iteration
            ? ldexp(original_determinant, -3 * exponent)
            : affine_dot(scaled[0], cofactors[0]);
        if (original_determinant == 0.0) {
            affine_rank_two(scaled, cofactors, iterate);
            converged = true;
            break;
        }

        double one_norm = 0.0, infinity_norm = 0.0;
        double cofactor_one_norm = 0.0, cofactor_infinity_norm = 0.0;
        for (int i = 0; i < 3; ++i) {
            double column_sum = 0.0, row_sum = 0.0;
            double cofactor_column_sum = 0.0, cofactor_row_sum = 0.0;
            for (int j = 0; j < 3; ++j) {
                column_sum += fabs(scaled[j][i]);
                row_sum += fabs(scaled[i][j]);
                cofactor_column_sum += fabs(cofactors[j][i]);
                cofactor_row_sum += fabs(cofactors[i][j]);
            }
            if (column_sum > one_norm) {
                one_norm = column_sum;
            }
            if (row_sum > infinity_norm) {
                infinity_norm = row_sum;
            }
            if (cofactor_column_sum > cofactor_one_norm) {
                cofactor_one_norm = cofactor_column_sum;
            }
            if (cofactor_row_sum > cofactor_infinity_norm) {
                cofactor_infinity_norm = cofactor_row_sum;
            }
        }
        const double gamma = sqrt(sqrt(cofactor_infinity_norm / infinity_norm)
                                * sqrt(cofactor_one_norm / one_norm) / fabs(scaled_determinant));
        double difference = 0.0, norm = 0.0;
        for (int column = 0; column < 3; ++column) {
            double column_difference = 0.0, column_norm = 0.0;
            for (int row = 0; row < 3; ++row) {
                const double next = 0.5 * (gamma * scaled[row][column]
                                         + cofactors[row][column] / (gamma * scaled_determinant));
                column_difference += fabs(iterate[row][column] - next);
                column_norm += fabs(next);
                iterate[row][column] = next;
            }
            if (column_difference > difference) {
                difference = column_difference;
            }
            if (column_norm > norm) {
                norm = column_norm;
            }
        }
        first_iteration = false;
        if (!isfinite(norm)) return false;
        if (difference <= norm * 1e-6) {
            converged = true;
            break;
        }
    }

    if (!converged) return false;
    double stretch[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const double input_column[3] = {input[0][column], input[1][column], input[2][column]};
            stretch[row][column] = affine_dot(iterate[row], input_column);
        }
    }
    float sign = original_determinant < 0 ? -1.0f : 1.0f;
    *reflection = sign;
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            orthogonal[row][column] = sign * (float)iterate[column][row];
            symmetric[row][column] = (float)(0.5 * (stretch[row][column] + stretch[column][row]));
        }
    }
    return true;
}

static void affine_spectral(const float symmetric[3][3], float eigenvectors[3][3], float values[3])
{
    // 0x2429BC30 uses cyclic off-diagonals [yz,zx,xy] and sweeps 2,1,0.
    // Snapshot before writing U, including when both matrices are the same.
    // Columns of U are eigenvectors; a repeated eigenspace has no unique basis.
    double diagonal[3] = {symmetric[0][0], symmetric[1][1], symmetric[2][2]};
    double off[3] = {symmetric[1][2], symmetric[2][0], symmetric[0][1]};
    double basis[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    for (int sweep = 0; sweep < 20; ++sweep) {
        if (fabs(off[2]) + fabs(off[1]) + fabs(off[0]) == 0.0) break;
        for (int k = 2; k >= 0; --k) {
            const int p = (k+1)%3, q = (p+1)%3;
            const double magnitude = fabs(off[k]);
            if (!(magnitude > 0.0)) continue;
            const double difference = diagonal[q] - diagonal[p];
            double tangent;
            if (magnitude*100.0 + fabs(difference) == fabs(difference)) tangent = off[k] / difference;
            else {
                const double theta = 0.5*difference / off[k];
                tangent = 1.0 / (hypot(theta, 1.0) + fabs(theta));
                if (theta < 0.0) tangent = -tangent;
            }
            const double cosine = 1.0 / hypot(tangent, 1.0);
            const double sine = cosine*tangent;
            const double tau = sine / (cosine+1.0);
            const double shift = tangent*off[k];
            off[k] = 0.0;
            diagonal[p] -= shift;
            diagonal[q] += shift;
            const double old_q = off[q];
            off[q] = old_q - (tau*old_q + off[p])*sine;
            off[p] = (old_q - tau*off[p])*sine + off[p];
            for (int row = 2; row >= 0; --row) {
                const double a = basis[row][p], b = basis[row][q];
                basis[row][p] = a - (a*tau+b)*sine;
                basis[row][q] = (a-b*tau)*sine+b;
            }
        }
    }
    for (int row = 0; row < 3; row++) {
        values[row] = (float)diagonal[row];
        for (int column = 0; column < 3; column++) eigenvectors[row][column] = (float)basis[row][column];
    }
}

/* Vx3DQuaternionFromMatrix: unit rotation rows to x,y,z,w. */
static void affine_quaternion(const float matrix[3][3], float q[4])
{
    double trace = (double)matrix[1][1] + matrix[2][2] + matrix[0][0];
    if (trace >= 0) {
        double root = sqrt(trace + 1), factor = 0.5 / root;
        q[3] = (float)(root * 0.5);
        q[0] = (float)(((double)matrix[2][1] - matrix[1][2]) * factor);
        q[1] = (float)(((double)matrix[0][2] - matrix[2][0]) * factor);
        q[2] = (float)(((double)matrix[1][0] - matrix[0][1]) * factor);
    } else {
        int i = matrix[1][1] > matrix[0][0] ? 1 : 0;
        if (matrix[2][2] > matrix[i][i]) i = 2;
        int j = (i + 1) % 3, k = (j + 1) % 3;
        double root = sqrt(((double)matrix[i][i] - ((double)matrix[k][k] + matrix[j][j])) + 1);
        double factor = 0.5 / root;
        q[i] = (float)(root * 0.5);
        q[j] = (float)(((double)matrix[j][i] + matrix[i][j]) * factor);
        q[k] = (float)(((double)matrix[k][i] + matrix[i][k]) * factor);
        q[3] = (float)(((double)matrix[k][j] - matrix[j][k]) * factor);
    }
}

static void affine_rotation(const float q[4], eng_matrix_t *rotation)
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double factor = 2.0 / (x*x + y*y + z*z + w*w);
    eng_matrix_identity(rotation);
    rotation->m[0][0] = (float)(1 - factor * (y*y + z*z));
    rotation->m[0][1] = (float)(factor * (x*y - w*z));
    rotation->m[0][2] = (float)(factor * (x*z + w*y));
    rotation->m[1][0] = (float)(factor * (x*y + w*z));
    rotation->m[1][1] = (float)(1 - factor * (x*x + z*z));
    rotation->m[1][2] = (float)(factor * (y*z - w*x));
    rotation->m[2][0] = (float)(factor * (x*z - w*y));
    rotation->m[2][1] = (float)(factor * (y*z + w*x));
    rotation->m[2][2] = (float)(1 - factor * (x*x + y*y));
}

bool eng_quaternion_from_matrix(const eng_matrix_t *matrix, nmo_quaternion_t *out)
{
    if (matrix == NULL || out == NULL) return false;
    float rows[3][3], q[4];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) if (!isfinite(matrix->m[r][c])) return false;
        double length = hypot(hypot(matrix->m[r][0],matrix->m[r][1]),matrix->m[r][2]);
        if (length == 0) return false;
        for (int c = 0; c < 3; c++) rows[r][c] = (float)(matrix->m[r][c]/length);
    }
    for (int c = 0; c < 3; c++) {
        int a = (c+1)%3, b = (c+2)%3;
        rows[2][c] = (float)((double)rows[0][a]*rows[1][b]-(double)rows[0][b]*rows[1][a]);
    }
    affine_quaternion(rows,q);
    for (int i = 0; i < 4; i++) if (!isfinite(q[i])) return false;
    *out = (nmo_quaternion_t){q[0],q[1],q[2],q[3]};
    return true;
}

bool eng_matrix_from_quaternion(eng_matrix_t *matrix, const nmo_quaternion_t *quaternion)
{
    if (matrix == NULL || quaternion == NULL) return false;
    const float q[] = {quaternion->x,quaternion->y,quaternion->z,quaternion->w};
    double norm = 0;
    for (int i = 0; i < 4; i++) {
        if (!isfinite(q[i])) return false;
        norm += (double)q[i]*q[i];
    }
    if (norm == 0) return false;
    affine_rotation(q,matrix);
    return true;
}

nmo_quaternion_t eng_quaternion_slerp(float step, nmo_quaternion_t a, nmo_quaternion_t b)
{
    double t = step;
    double dot = (((double)a.z*b.z+(double)a.y*b.y)+(double)a.x*b.x)+(double)a.w*b.w;
    double sign = dot < 0 ? -1 : 1, cosine = dot < 0 ? -dot : dot;
    double left = 1-t, right = sign*t;
    if (cosine < .99) {
        double angle = acos(cosine), inverse = 1/sin(angle);
        left = sin((1-t)*angle)*inverse; right = sign*sin(t*angle)*inverse;
    }
    return (nmo_quaternion_t){(float)(a.x*left+b.x*right),(float)(a.y*left+b.y*right),
        (float)(a.z*left+b.z*right),(float)(a.w*left+b.w*right)};
}

/* Vx3DQuaternionSnuggle's scale permutation. Its returned quaternion is not
 * needed when a scale-axis controller supplies the replacement orientation. */
static void affine_permute_scale(float q[4], float scale[3])
{
    const float half_root = 0.70710677f;
    int distinct = -1;
    if (scale[0] == scale[1]) {
        if (scale[0] == scale[2]) return;
        distinct = 2;
    } else if (scale[0] == scale[2]) distinct = 1;
    else if (scale[1] == scale[2]) distinct = 0;
    if (distinct >= 0) {
        if (distinct != 2) {
            double x = q[0], y = q[1], z = q[2], w = q[3];
            float r[4] = {0, 0, 0, half_root};
            r[distinct == 0 ? 1 : 0] = half_root;
            q[0] = (float)(x*r[3] + w*r[0] + y*r[2] - z*r[1]);
            q[1] = (float)(y*r[3] + w*r[1] + z*r[0] - x*r[2]);
            q[2] = (float)(z*r[3] + w*r[2] + x*r[1] - y*r[0]);
            q[3] = (float)(w*r[3] - x*r[0] - y*r[1] - z*r[2]);
            float temp = scale[distinct]; scale[distinct] = scale[2]; scale[2] = temp;
        }
        double x = -q[0], y = -q[1], z = -q[2], w = q[3];
        double magnitude[3] = {fabs(z*z + w*w - 0.5), fabs(z*x - w*y), fabs(w*x + z*y)};
        int winner = magnitude[0] > magnitude[1] ? (magnitude[0] > magnitude[2] ? 0 : 2)
                                                : (magnitude[1] > magnitude[2] ? 1 : 2);
        float copy[3]; memcpy(copy, scale, sizeof(copy));
        if (winner == 1) { scale[0] = copy[2]; scale[1] = copy[0]; scale[2] = copy[1]; }
        if (winner == 2) { scale[0] = copy[1]; scale[1] = copy[2]; scale[2] = copy[0]; }
        return;
    }
    float magnitude[4];
    bool odd = false;
    for (int i = 0; i < 4; i++) { odd ^= q[i] < 0; magnitude[i] = fabsf(q[i]); }
    int low = magnitude[0] > magnitude[1] ? 0 : 1;
    int high = magnitude[2] > magnitude[3] ? 2 : 3;
    if (magnitude[low] > magnitude[high]) {
        int other = low ^ 1;
        if (magnitude[other] > magnitude[high]) { high = low; low = other; }
        else { int temp = low; low = high; high = temp; }
    } else if (magnitude[high ^ 1] > magnitude[low]) low = high ^ 1;
    double all = ((double)magnitude[3] + magnitude[2] + magnitude[1] + magnitude[0]) * 0.5;
    double two = ((double)magnitude[low] + magnitude[high]) * half_root;
    double one = magnitude[high];
    if (all > two && all > one) {
        float copy[3]; memcpy(copy, scale, sizeof(copy));
        scale[0] = copy[odd ? 1 : 2]; scale[1] = copy[odd ? 2 : 0]; scale[2] = copy[odd ? 0 : 1];
    } else if (!(all > two) && two > one) {
        if (low > high) { int temp = low; low = high; high = temp; }
        if (high == 3) { static const int next[3] = {1, 2, 0}; high = next[low]; low = 3 - high - low; }
        float temp = scale[low]; scale[low] = scale[high]; scale[high] = temp;
    }
}

bool eng_matrix_decompose(const eng_matrix_t *matrix, bool principal_axes,
                           eng_matrix_t *rotation, eng_vector_t *scale)
{
    float rows[3][3], values[3], q[4];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
        if (!isfinite(matrix->m[r][c])) return false;
    if (principal_axes) {
        eng_matrix_t transposed = *matrix;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) transposed.m[r][c] = matrix->m[c][r];
        float symmetric[3][3];
        float reflection;
        if (!affine_polar(&transposed, rows, symmetric, &reflection)) return false;
        if (scale != NULL) {
            float eigenvectors[3][3];
            affine_spectral(symmetric, eigenvectors, values);
            affine_quaternion(eigenvectors, q);
            affine_permute_scale(q, values);
            for (int i = 0; i < 3; i++) values[i] *= reflection;
        }
    } else {
        for (int r = 0; r < 2; r++) {
            double length = hypot(hypot(matrix->m[r][0], matrix->m[r][1]), matrix->m[r][2]);
            if (length == 0) return eng_matrix_decompose(matrix, true, rotation, scale);
            for (int c = 0; c < 3; c++) rows[r][c] = (float)(matrix->m[r][c] / length);
        }
        for (int c = 0; c < 3; c++) {
            int a = (c + 1) % 3, b = (c + 2) % 3;
            rows[2][c] = (float)((double)rows[0][a] * rows[1][b] - (double)rows[0][b] * rows[1][a]);
        }
        if (scale != NULL) for (int r = 0; r < 3; r++)
            values[r] = (float)((double)rows[r][0] * matrix->m[r][0] +
                                (double)rows[r][1] * matrix->m[r][1] +
                                (double)rows[r][2] * matrix->m[r][2]);
    }
    eng_matrix_t result;
    if (rotation != NULL) {
        affine_quaternion(rows, q);
        affine_rotation(q, &result);
        if (principal_axes) {
            for (int r = 0; r < 3; r++) for (int c = r + 1; c < 3; c++) {
                float temp = result.m[r][c]; result.m[r][c] = result.m[c][r]; result.m[c][r] = temp;
            }
        }
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
            if (!isfinite(result.m[r][c])) return false;
    }
    if (scale != NULL) for (int i = 0; i < 3; i++) if (!isfinite(values[i])) return false;
    if (rotation != NULL) *rotation = result;
    if (scale != NULL) *scale = (eng_vector_t){values[0], values[1], values[2]};
    return true;
}
