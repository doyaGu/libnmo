/**
 * @file test_chunk_helpers.c
 * @brief Unit tests for chunk operations (Phase 2)
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include <math.h>
#include <float.h>

// =============================================================================
// Test Fixtures
// =============================================================================

static nmo_arena_t *arena = NULL;
static nmo_chunk_t *chunk = NULL;

static void setup_chunk_helpers(void) {
    arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);

    chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
}

static void teardown_chunk_helpers(void) {
    if (arena) {
        nmo_arena_destroy(arena);
        arena = NULL;
    }
    chunk = NULL;
}

// =============================================================================
// Helper Functions
// =============================================================================

static bool float_equals(float a, float b, float epsilon) {
    if (isnan(a) && isnan(b)) return true;
    if (isinf(a) && isinf(b)) return (a > 0) == (b > 0);
    return fabs(a - b) < epsilon;
}

static bool vector2_equals(const nmo_vector2_t *a, const nmo_vector2_t *b, float epsilon) {
    return float_equals(a->x, b->x, epsilon) && 
           float_equals(a->y, b->y, epsilon);
}

static bool vector3_equals(const nmo_vector_t *a, const nmo_vector_t *b, float epsilon) {
    return float_equals(a->x, b->x, epsilon) && 
           float_equals(a->y, b->y, epsilon) && 
           float_equals(a->z, b->z, epsilon);
}

static bool vector4_equals(const nmo_vector4_t *a, const nmo_vector4_t *b, float epsilon) {
    return float_equals(a->x, b->x, epsilon) && 
           float_equals(a->y, b->y, epsilon) && 
           float_equals(a->z, b->z, epsilon) && 
           float_equals(a->w, b->w, epsilon);
}

static bool quaternion_equals(const nmo_quaternion_t *a, const nmo_quaternion_t *b, float epsilon) {
    return float_equals(a->x, b->x, epsilon) && 
           float_equals(a->y, b->y, epsilon) && 
           float_equals(a->z, b->z, epsilon) && 
           float_equals(a->w, b->w, epsilon);
}

static bool matrix_equals(const nmo_matrix_t *a, const nmo_matrix_t *b, float epsilon) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            if (!float_equals(a->m[row][col], b->m[row][col], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

static bool color_equals(const nmo_color_t *a, const nmo_color_t *b, float epsilon) {
    return float_equals(a->r, b->r, epsilon) && 
           float_equals(a->g, b->g, epsilon) && 
           float_equals(a->b, b->b, epsilon) && 
           float_equals(a->a, b->a, epsilon);
}

// =============================================================================
// Object ID Array Tests
// =============================================================================

TEST(chunk_helpers, object_id_array_empty) {
    nmo_result_t result;

    // Write empty array
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_object_id_array(chunk, NULL, 0);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_object_id_t *ids = NULL;
    size_t count = 0;
    result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(0, count);
    ASSERT_EQ(NULL, ids);
}

TEST(chunk_helpers, object_id_array_single) {
    nmo_result_t result;
    nmo_object_id_t original_ids[] = {42};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_object_id_array(chunk, original_ids, 1);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_object_id_t *ids = NULL;
    size_t count = 0;
    result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_NE(NULL, ids);
    ASSERT_EQ(42, ids[0]);
}

TEST(chunk_helpers, object_id_array_multiple) {
    nmo_result_t result;
    nmo_object_id_t original_ids[] = {1, 2, 100, 0, 999};
    const size_t original_count = sizeof(original_ids) / sizeof(original_ids[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_object_id_array(chunk, original_ids, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_object_id_t *ids = NULL;
    size_t count = 0;
    result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, ids);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_EQ(original_ids[i], ids[i]);
    }
}

// =============================================================================
// Primitive Array Tests
// =============================================================================

TEST(chunk_helpers, int_array_roundtrip) {
    nmo_result_t result;
    int32_t original_array[] = {-100, 0, 42, 999, INT32_MIN, INT32_MAX};
    const size_t original_count = sizeof(original_array) / sizeof(original_array[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_int_array(chunk, original_array, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    int32_t *array = NULL;
    size_t count = 0;
    result = nmo_chunk_read_int_array(chunk, &array, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, array);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_EQ(original_array[i], array[i]);
    }
}

TEST(chunk_helpers, float_array_roundtrip) {
    nmo_result_t result;
    float original_array[] = {-1.5f, 0.0f, 3.14f, 999.999f, -FLT_MAX, FLT_MAX};
    const size_t original_count = sizeof(original_array) / sizeof(original_array[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_float_array(chunk, original_array, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    float *array = NULL;
    size_t count = 0;
    result = nmo_chunk_read_float_array(chunk, &array, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, array);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_TRUE(float_equals(original_array[i], array[i], 0.0001f));
    }
}

TEST(chunk_helpers, dword_array_roundtrip) {
    nmo_result_t result;
    uint32_t original_array[] = {0, 1, 255, 65535, 0xDEADBEEF, UINT32_MAX};
    const size_t original_count = sizeof(original_array) / sizeof(original_array[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_dword_array(chunk, original_array, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    uint32_t *array = NULL;
    size_t count = 0;
    result = nmo_chunk_read_dword_array(chunk, &array, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, array);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_EQ(original_array[i], array[i]);
    }
}

TEST(chunk_helpers, byte_array_roundtrip) {
    nmo_result_t result;
    uint8_t original_array[] = {0, 1, 127, 128, 255};
    const size_t original_count = sizeof(original_array) / sizeof(original_array[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_byte_array(chunk, original_array, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    uint8_t *array = NULL;
    size_t count = 0;
    result = nmo_chunk_read_byte_array(chunk, &array, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, array);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_EQ(original_array[i], array[i]);
    }
}

TEST(chunk_helpers, string_array_roundtrip) {
    nmo_result_t result;
    const char *original_strings[] = {"hello", "world", "", "test string"};
    const size_t original_count = sizeof(original_strings) / sizeof(original_strings[0]);

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_string_array(chunk, original_strings, original_count);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    char **strings = NULL;
    size_t count = 0;
    result = nmo_chunk_read_string_array(chunk, &strings, &count, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(original_count, count);
    ASSERT_NE(NULL, strings);

    for (size_t i = 0; i < original_count; i++) {
        ASSERT_STR_EQ(original_strings[i], strings[i]);
    }
}

// =============================================================================
// Vector Tests
// =============================================================================

TEST(chunk_helpers, vector2_roundtrip) {
    nmo_result_t result;
    nmo_vector2_t original = {1.5f, -2.7f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector2(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector2_t vec;
    result = nmo_chunk_read_vector2(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector2_equals(&original, &vec, 0.0001f));
}

TEST(chunk_helpers, vector2_zero) {
    nmo_result_t result;
    nmo_vector2_t zero = {0.0f, 0.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector2(chunk, &zero);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector2_t vec;
    result = nmo_chunk_read_vector2(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector2_equals(&zero, &vec, 0.0001f));
}

TEST(chunk_helpers, vector3_roundtrip) {
    nmo_result_t result;
    nmo_vector_t original = {1.5f, -2.7f, 3.14f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector3(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector_t vec;
    result = nmo_chunk_read_vector3(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector3_equals(&original, &vec, 0.0001f));
}

TEST(chunk_helpers, vector3_zero) {
    nmo_result_t result;
    nmo_vector_t zero = {0.0f, 0.0f, 0.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector3(chunk, &zero);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector_t vec;
    result = nmo_chunk_read_vector3(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector3_equals(&zero, &vec, 0.0001f));
}

TEST(chunk_helpers, vector3_infinity) {
    nmo_result_t result;
    nmo_vector_t inf = {INFINITY, -INFINITY, 0.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector3(chunk, &inf);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector_t vec;
    result = nmo_chunk_read_vector3(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector3_equals(&inf, &vec, 0.0001f));
}

TEST(chunk_helpers, vector4_roundtrip) {
    nmo_result_t result;
    nmo_vector4_t original = {1.0f, 2.0f, 3.0f, 4.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_vector4(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_vector4_t vec;
    result = nmo_chunk_read_vector4(chunk, &vec);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(vector4_equals(&original, &vec, 0.0001f));
}

// =============================================================================
// Quaternion Tests
// =============================================================================

TEST(chunk_helpers, quaternion_identity) {
    nmo_result_t result;
    nmo_quaternion_t identity = {0.0f, 0.0f, 0.0f, 1.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_quaternion(chunk, &identity);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_quaternion_t quat;
    result = nmo_chunk_read_quaternion(chunk, &quat);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(quaternion_equals(&identity, &quat, 0.0001f));
}

TEST(chunk_helpers, quaternion_roundtrip) {
    nmo_result_t result;
    nmo_quaternion_t original = {0.5f, 0.5f, 0.5f, 0.5f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_quaternion(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_quaternion_t quat;
    result = nmo_chunk_read_quaternion(chunk, &quat);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(quaternion_equals(&original, &quat, 0.0001f));
}

// =============================================================================
// Matrix Tests
// =============================================================================

TEST(chunk_helpers, matrix_identity) {
    nmo_result_t result;
    nmo_matrix_t identity = {
        {{1, 0, 0, 0},
         {0, 1, 0, 0},
         {0, 0, 1, 0},
         {0, 0, 0, 1}}
    };

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_matrix(chunk, &identity);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_matrix_t mat;
    result = nmo_chunk_read_matrix(chunk, &mat);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(matrix_equals(&identity, &mat, 0.0001f));
}

TEST(chunk_helpers, matrix_zero) {
    nmo_result_t result;
    nmo_matrix_t zero = {{{0}}};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_matrix(chunk, &zero);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_matrix_t mat;
    result = nmo_chunk_read_matrix(chunk, &mat);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(matrix_equals(&zero, &mat, 0.0001f));
}

TEST(chunk_helpers, matrix_arbitrary) {
    nmo_result_t result;
    nmo_matrix_t original = {
        {{1.1f, 2.2f, 3.3f, 4.4f},
         {5.5f, 6.6f, 7.7f, 8.8f},
         {9.9f, 10.1f, 11.2f, 12.3f},
         {13.4f, 14.5f, 15.6f, 16.7f}}
    };

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_matrix(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_matrix_t mat;
    result = nmo_chunk_read_matrix(chunk, &mat);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(matrix_equals(&original, &mat, 0.0001f));
}

// =============================================================================
// Color Tests
// =============================================================================

TEST(chunk_helpers, color_white) {
    nmo_result_t result;
    nmo_color_t white = {1.0f, 1.0f, 1.0f, 1.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_color(chunk, &white);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_color_t color;
    result = nmo_chunk_read_color(chunk, &color);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(color_equals(&white, &color, 0.0001f));
}

TEST(chunk_helpers, color_black) {
    nmo_result_t result;
    nmo_color_t black = {0.0f, 0.0f, 0.0f, 1.0f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_color(chunk, &black);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_color_t color;
    result = nmo_chunk_read_color(chunk, &color);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(color_equals(&black, &color, 0.0001f));
}

TEST(chunk_helpers, color_arbitrary) {
    nmo_result_t result;
    nmo_color_t original = {0.2f, 0.4f, 0.6f, 0.8f};

    // Write
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_chunk_write_color(chunk, &original);
    ASSERT_EQ(NMO_OK, result.code);

    // Read back
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_color_t color;
    result = nmo_chunk_read_color(chunk, &color);
    ASSERT_EQ(NMO_OK, result.code);

    ASSERT_TRUE(color_equals(&original, &color, 0.0001f));
}

// =============================================================================
// Test Registration
// =============================================================================

TEST_MAIN_BEGIN()
    // Object ID arrays
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, object_id_array_empty, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, object_id_array_single, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, object_id_array_multiple, setup_chunk_helpers, teardown_chunk_helpers);

    // Primitive arrays
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, int_array_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, float_array_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, dword_array_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, byte_array_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, string_array_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);

    // Vector tests
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector2_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector2_zero, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector3_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector3_zero, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector3_infinity, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, vector4_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);

    // Quaternion tests
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, quaternion_identity, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, quaternion_roundtrip, setup_chunk_helpers, teardown_chunk_helpers);

    // Matrix tests
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, matrix_identity, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, matrix_zero, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, matrix_arbitrary, setup_chunk_helpers, teardown_chunk_helpers);

    // Color tests
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, color_white, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, color_black, setup_chunk_helpers, teardown_chunk_helpers);
    REGISTER_TEST_WITH_FIXTURE(chunk_helpers, color_arbitrary, setup_chunk_helpers, teardown_chunk_helpers);
TEST_MAIN_END()
