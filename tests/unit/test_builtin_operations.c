/**
 * @file test_builtin_operations.c
 * @brief Unit tests for builtin operations (Phase 6.1.5)
 *
 * Tests builtin operations across 6 categories:
 * - Arithmetic: 16 operations (8 INT + 8 FLOAT)
 * - Logic: 4 operations (BOOL)
 * - Comparison: 16 operations (8 INT + 8 FLOAT)
 * - Bitwise: 7 operations (INT)
 * - Trigonometry: 6 operations (FLOAT)
 * - Vector: 16 operations (Vector2/3/4)
 */

#include "test_framework.h"
#include "type/nmo_builtin_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_math.h"
#include "core/nmo_arena.h"
#include <math.h>
#include <stdalign.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *type_registry = NULL;
static nmo_operation_registry_t *operation_registry = NULL;

static void setup_registries(void) {
    arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NE(NULL, arena);

    type_registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, type_registry);

    operation_registry = nmo_operation_registry_create(arena);
    ASSERT_NE(NULL, operation_registry);

    /* Register builtin types and operations */
    nmo_status_t result = nmo_register_builtin_types(type_registry);
    ASSERT_EQ(NMO_OK, result);

    result = nmo_register_builtin_operations(operation_registry, type_registry);
    ASSERT_EQ(NMO_OK, result);
}

static void teardown_registries(void) {
    nmo_operation_registry_destroy(operation_registry);
    nmo_type_registry_destroy(type_registry);
    nmo_arena_destroy(arena);
    operation_registry = NULL;
    type_registry = NULL;
    arena = NULL;
}

/* Helper to get type descriptor from registry */
static const nmo_type_descriptor_t* get_type(nmo_guid_t guid) {
    return nmo_type_registry_find_by_guid(type_registry, guid);
}

/* ============================================================================
 * Arithmetic Operations Tests
 * ============================================================================ */

TEST(builtin_operations, arithmetic_add_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_ADD;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);
    ASSERT_NE(NULL, cell->desc.function);

    int32_t a = 5, b = 3, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(8, result);

    teardown_registries();
}

TEST(builtin_operations, arithmetic_subtract_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_SUBTRACT;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        float_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 10.5f, b = 3.2f, result = 0.0f;
    res = cell->desc.function(&a, float_type, &b, float_type, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(7.3f, result, 0.01f);

    teardown_registries();
}

TEST(builtin_operations, arithmetic_multiply_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_MULTIPLY;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 7, b = 6, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(42, result);

    teardown_registries();
}

TEST(builtin_operations, arithmetic_divide_int_by_zero) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_DIVIDE;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 10, b = 0, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_NE(NMO_OK, res); /* Should fail */

    teardown_registries();
}

TEST(builtin_operations, arithmetic_negate_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_NEGATE;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 5.5f, result = 0.0f;
    res = cell->desc.function(&a, float_type, NULL, NULL, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(-5.5f, result, 0.01f);

    teardown_registries();
}

TEST(builtin_operations, arithmetic_abs_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_ABS;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = -42, result = 0;
    res = cell->desc.function(&a, int_type, NULL, NULL, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(42, result);

    teardown_registries();
}

TEST(builtin_operations, arithmetic_power_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_POWER;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        float_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 2.0f, b = 8.0f, result = 0.0f;
    res = cell->desc.function(&a, float_type, &b, float_type, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(256.0f, result, 0.01f);

    teardown_registries();
}

/* ============================================================================
 * Logic Operations Tests
 * ============================================================================ */

TEST(builtin_operations, logic_and_bool) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_AND;
    const nmo_type_descriptor_t *bool_type = get_type(NMO_TYPE_GUID_BOOL);
    ASSERT_NE(NULL, bool_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        bool_type,
        bool_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    bool a = true, b = false, result = false;
    res = cell->desc.function(&a, bool_type, &b, bool_type, &result, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(false, result);

    teardown_registries();
}

TEST(builtin_operations, logic_not_bool) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_NOT;
    const nmo_type_descriptor_t *bool_type = get_type(NMO_TYPE_GUID_BOOL);
    ASSERT_NE(NULL, bool_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        bool_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    bool a = true, result = false;
    res = cell->desc.function(&a, bool_type, NULL, NULL, &result, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(false, result);

    teardown_registries();
}

/* ============================================================================
 * Comparison Operations Tests
 * ============================================================================ */

TEST(builtin_operations, comparison_equal_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_EQUAL;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    const nmo_type_descriptor_t *bool_type = get_type(NMO_TYPE_GUID_BOOL);
    ASSERT_NE(NULL, int_type);
    ASSERT_NE(NULL, bool_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 5, b = 5;
    bool result = false;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(true, result);

    teardown_registries();
}

TEST(builtin_operations, comparison_less_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_LESS;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    const nmo_type_descriptor_t *bool_type = get_type(NMO_TYPE_GUID_BOOL);
    ASSERT_NE(NULL, float_type);
    ASSERT_NE(NULL, bool_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        float_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 3.5f, b = 7.5f;
    bool result = false;
    res = cell->desc.function(&a, float_type, &b, float_type, &result, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(true, result);

    teardown_registries();
}

TEST(builtin_operations, comparison_min_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_MIN;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 10, b = 5, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(5, result);

    teardown_registries();
}

TEST(builtin_operations, comparison_max_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_MAX;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        float_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 3.5f, b = 7.5f, result = 0.0f;
    res = cell->desc.function(&a, float_type, &b, float_type, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(7.5f, result, 0.01f);

    teardown_registries();
}

/* ============================================================================
 * Bitwise Operations Tests
 * ============================================================================ */

TEST(builtin_operations, bitwise_and_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_BIT_AND;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 0xC, b = 0xA, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(0x8, result);

    teardown_registries();
}

TEST(builtin_operations, bitwise_shift_left_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_SHIFT_LEFT;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 5, b = 2, result = 0;
    res = cell->desc.function(&a, int_type, &b, int_type, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(20, result);

    teardown_registries();
}

TEST(builtin_operations, bitwise_not_int) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_BIT_NOT;
    const nmo_type_descriptor_t *int_type = get_type(NMO_TYPE_GUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    int32_t a = 0x0F, result = 0;
    res = cell->desc.function(&a, int_type, NULL, NULL, &result, int_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(~0x0F, result);

    teardown_registries();
}

/* ============================================================================
 * Trigonometry Operations Tests
 * ============================================================================ */

TEST(builtin_operations, trigonometry_sin_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_SIN;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = (float)(M_PI / 2.0), result = 0.0f;
    res = cell->desc.function(&a, float_type, NULL, NULL, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(1.0f, result, 0.001f);

    teardown_registries();
}

TEST(builtin_operations, trigonometry_cos_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_COS;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 0.0f, result = 0.0f;
    res = cell->desc.function(&a, float_type, NULL, NULL, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(1.0f, result, 0.001f);

    teardown_registries();
}

TEST(builtin_operations, trigonometry_asin_domain_error) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_ASIN;
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        float_type,
        NULL, /* Unary operation */
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    float a = 2.0f, result = 0.0f;  /* Out of domain */
    res = cell->desc.function(&a, float_type, NULL, NULL, &result, float_type, NULL);
    ASSERT_NE(NMO_OK, res); /* Should fail */

    teardown_registries();
}

/* ============================================================================
 * Vector Operations Tests
 * ============================================================================ */

TEST(builtin_operations, vector_add_vector3) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_VECTOR_ADD;
    const nmo_type_descriptor_t *vec3_type = get_type(NMO_TYPE_GUID_VECTOR3);
    ASSERT_NE(NULL, vec3_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        vec3_type,
        vec3_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    nmo_vector_t a = {1.0f, 2.0f, 3.0f};
    nmo_vector_t b = {4.0f, 5.0f, 6.0f};
    nmo_vector_t result = {0};
    res = cell->desc.function(&a, vec3_type, &b, vec3_type, &result, vec3_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(5.0f, result.x, 0.001f);
    ASSERT_FLOAT_EQ(7.0f, result.y, 0.001f);
    ASSERT_FLOAT_EQ(9.0f, result.z, 0.001f);

    teardown_registries();
}

TEST(builtin_operations, vector_dot_vector2) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_VECTOR_DOT;
    const nmo_type_descriptor_t *vec2_type = get_type(NMO_TYPE_GUID_VECTOR2);
    const nmo_type_descriptor_t *float_type = get_type(NMO_TYPE_GUID_FLOAT);
    ASSERT_NE(NULL, vec2_type);
    ASSERT_NE(NULL, float_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        vec2_type,
        vec2_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    nmo_vector2_t a = {2.0f, 3.0f};
    nmo_vector2_t b = {4.0f, 5.0f};
    float result = 0.0f;
    res = cell->desc.function(&a, vec2_type, &b, vec2_type, &result, float_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(23.0f, result, 0.001f);

    teardown_registries();
}

TEST(builtin_operations, vector_cross_vector3) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_VECTOR_CROSS;
    const nmo_type_descriptor_t *vec3_type = get_type(NMO_TYPE_GUID_VECTOR3);
    ASSERT_NE(NULL, vec3_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        vec3_type,
        vec3_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);

    nmo_vector_t a = {1.0f, 0.0f, 0.0f};
    nmo_vector_t b = {0.0f, 1.0f, 0.0f};
    nmo_vector_t result = {0};
    res = cell->desc.function(&a, vec3_type, &b, vec3_type, &result, vec3_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_FLOAT_EQ(0.0f, result.x, 0.001f);
    ASSERT_FLOAT_EQ(0.0f, result.y, 0.001f);
    ASSERT_FLOAT_EQ(1.0f, result.z, 0.001f);

    teardown_registries();
}

/* ============================================================================
 * Registration Statistics Tests
 * ============================================================================ */

TEST(builtin_operations, check_total_operations_count) {
    setup_registries();

    uint64_t total_ops = 0, total_lookups = 0, cache_hits = 0;
    nmo_operation_registry_get_stats(operation_registry, &total_ops, &total_lookups, &cache_hits);

    /* Expected: 66 total (core 50 + 16 vector ops) */
    ASSERT_EQ(66, (int)total_ops);

    teardown_registries();
}



/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(builtin_operations, arithmetic_add_int);
    REGISTER_TEST(builtin_operations, arithmetic_subtract_float);
    REGISTER_TEST(builtin_operations, arithmetic_multiply_int);
    REGISTER_TEST(builtin_operations, arithmetic_divide_int_by_zero);
    REGISTER_TEST(builtin_operations, arithmetic_negate_float);
    REGISTER_TEST(builtin_operations, arithmetic_abs_int);
    REGISTER_TEST(builtin_operations, arithmetic_power_float);

    REGISTER_TEST(builtin_operations, logic_and_bool);
    REGISTER_TEST(builtin_operations, logic_not_bool);

    REGISTER_TEST(builtin_operations, comparison_equal_int);
    REGISTER_TEST(builtin_operations, comparison_less_float);
    REGISTER_TEST(builtin_operations, comparison_min_int);
    REGISTER_TEST(builtin_operations, comparison_max_float);

    REGISTER_TEST(builtin_operations, bitwise_and_int);
    REGISTER_TEST(builtin_operations, bitwise_shift_left_int);
    REGISTER_TEST(builtin_operations, bitwise_not_int);

    REGISTER_TEST(builtin_operations, trigonometry_sin_float);
    REGISTER_TEST(builtin_operations, trigonometry_cos_float);
    REGISTER_TEST(builtin_operations, trigonometry_asin_domain_error);

    REGISTER_TEST(builtin_operations, vector_add_vector3);
    REGISTER_TEST(builtin_operations, vector_dot_vector2);
    REGISTER_TEST(builtin_operations, vector_cross_vector3);

    REGISTER_TEST(builtin_operations, check_total_operations_count);
TEST_MAIN_END()

