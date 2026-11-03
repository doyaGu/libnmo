#include "test_framework.h"
#include "../../src/engine/engine_transform.h"
#include "../fixtures/engine_affine_samples.h"

#include <math.h>
#include <string.h>

TEST(engine_affine, missing_channels_match_reference_poses)
{
    for (size_t i = 0; i < sizeof(AffineSamples) / sizeof(AffineSamples[0]); i++) {
        const affine_sample_t *sample = &AffineSamples[i];
        eng_matrix_t matrix, rotation;
        memcpy(&matrix, sample->matrix, sizeof(matrix));
        if (sample->principal) {
            /* Convert the captured right-polar input to the engine's
             * stretch-before-rotation convention. */
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) matrix.m[r][c] = sample->matrix[c*4+r];
        }
        eng_matrix_t before = matrix;
        eng_vector_t scale;
        ASSERT_TRUE(eng_matrix_decompose(&matrix, sample->principal, &rotation, &scale));
        const float actual[] = {scale.x, scale.y, scale.z};
        for (int c = 0; c < 3; c++) {
            ASSERT_FLOAT_EQ(sample->scale[c] * sample->reflection, actual[c], 0.00005f * (1 + fabsf(sample->scale[c])));
        }
        /* Independent quaternion rotation through transformed basis vectors. */
        double x = sample->rotation[0], y = sample->rotation[1];
        double z = sample->rotation[2], w = sample->rotation[3];
        double norm = x*x + y*y + z*z + w*w;
        const double expected[3][3] = {
            {1 - 2*(y*y+z*z)/norm, 2*(x*y-w*z)/norm, 2*(x*z+w*y)/norm},
            {2*(x*y+w*z)/norm, 1 - 2*(x*x+z*z)/norm, 2*(y*z-w*x)/norm},
            {2*(x*z-w*y)/norm, 2*(y*z+w*x)/norm, 1 - 2*(x*x+y*y)/norm},
        };
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            ASSERT_FLOAT_EQ((float)(sample->principal ? expected[c][r] : expected[r][c]), rotation.m[r][c], 0.00005f);
        }
        ASSERT_EQ(0, memcmp(&matrix, &before, sizeof(matrix)));
        eng_vector_t only_scale;
        ASSERT_TRUE(eng_matrix_decompose(&matrix, sample->principal, NULL, &only_scale));
        ASSERT_EQ(0, memcmp(&scale, &only_scale, sizeof(scale)));
    }
}

TEST(engine_affine, singular_poses_have_finite_channels_and_invalid_inputs_preserve_outputs)
{
    for (int rank = 0; rank <= 2; rank++) {
        eng_matrix_t matrix, rotation;
        eng_matrix_identity(&matrix);
        for (int i = 0; i < 3; i++) matrix.m[i][i] = i < rank ? (float)(i + 2) : 0;
        eng_vector_t scale;
        ASSERT_TRUE(eng_matrix_decompose(&matrix, true, &rotation, &scale));
        ASSERT_FLOAT_EQ(rank >= 1 ? 2 : 0, scale.x, 0.0001f);
        ASSERT_FLOAT_EQ(rank >= 2 ? 3 : 0, scale.y, 0.0001f);
        ASSERT_FLOAT_EQ(0, scale.z, 0.0001f);
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) ASSERT_TRUE(isfinite(rotation.m[i][j]));
        ASSERT_TRUE(eng_matrix_decompose(&matrix, false, &rotation, &scale));
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) ASSERT_TRUE(isfinite(rotation.m[i][j]));
    }
    eng_matrix_t matrix, before, rotation;
    eng_matrix_identity(&matrix);
    matrix.m[0][1] = NAN;
    eng_matrix_identity(&before);
    before.m[3][0] = 123;
    rotation = before;
    eng_vector_t scale = {2, 3, 4};
    ASSERT_FALSE(eng_matrix_decompose(&matrix, true, &rotation, &scale));
    ASSERT_EQ(0, memcmp(&before, &rotation, sizeof(before)));
    ASSERT_FLOAT_EQ(2, scale.x, 0);
    ASSERT_FLOAT_EQ(3, scale.y, 0);
    ASSERT_FLOAT_EQ(4, scale.z, 0);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_affine, missing_channels_match_reference_poses);
REGISTER_TEST(engine_affine, singular_poses_have_finite_channels_and_invalid_inputs_preserve_outputs);
TEST_MAIN_END()
