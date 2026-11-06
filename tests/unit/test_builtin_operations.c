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
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include <string.h>
#include <stdint.h>
#include "core/nmo_array.h"
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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

TEST(builtin_operations, comparison_equal_array) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_EQUAL;
    const nmo_type_descriptor_t *array_type = get_type(CKPGUID_ARRAY);
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
    ASSERT_NE(NULL, array_type);
    ASSERT_NE(NULL, bool_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t res = nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        array_type,
        array_type,
        type_registry,
        &cell
    );

    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);
    ASSERT_NE(NULL, cell->desc.function);

    nmo_array_t a;
    nmo_array_t b;
    ASSERT_EQ(NMO_OK, nmo_array_init(&a, 1, 0, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_init(&b, 1, 0, NULL));

    const uint8_t bytes1[] = { 1, 2, 3, 4 };
    const uint8_t bytes2[] = { 1, 2, 3, 4 };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&a, bytes1, sizeof(bytes1)));
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&b, bytes2, sizeof(bytes2)));

    bool eq = false;
    res = cell->desc.function(&a, array_type, &b, array_type, &eq, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(eq);

    const uint8_t extra = 5;
    ASSERT_EQ(NMO_OK, nmo_array_append(&b, &extra));

    eq = true;
    res = cell->desc.function(&a, array_type, &b, array_type, &eq, bool_type, NULL);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(!eq);

    nmo_array_dispose(&a);
    nmo_array_dispose(&b);
    teardown_registries();
}

TEST(builtin_operations, arithmetic_power_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_POWER;
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
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
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
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

TEST(builtin_operations, comparison_string_uses_pointer_representation) {
    setup_registries();

    const nmo_type_descriptor_t *string_type = get_type(CKPGUID_STRING);
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
    ASSERT_NE(NULL, string_type);
    ASSERT_NE(NULL, bool_type);
    ASSERT_EQ(sizeof(char *), string_type->size);

    const char first_storage[] = "alpha";
    const char equal_storage[] = "alpha";
    const char later_storage[] = "beta";
    const char *first = first_storage;
    const char *equal = equal_storage;
    const char *later = later_storage;
    bool result = false;
    const nmo_operation_tree_cell_t *cell = NULL;

    nmo_guid_t op_guid = NMO_OP_GUID_EQUAL;
    ASSERT_EQ(NMO_OK, nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        string_type,
        string_type,
        type_registry,
        &cell));
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(NMO_OK, cell->desc.function(
        &first, string_type, &equal, string_type,
        &result, bool_type, NULL));
    ASSERT_TRUE(result);

    op_guid = NMO_OP_GUID_LESS;
    cell = NULL;
    ASSERT_EQ(NMO_OK, nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        string_type,
        string_type,
        type_registry,
        &cell));
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(NMO_OK, cell->desc.function(
        &first, string_type, &later, string_type,
        &result, bool_type, NULL));
    ASSERT_TRUE(result);

    teardown_registries();
}

TEST(builtin_operations, comparison_less_float) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_LESS;
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
    const nmo_type_descriptor_t *bool_type = get_type(CKPGUID_BOOL);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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

    const nmo_guid_t op_guid = NMO_OP_GUID_ADD; /* Vector add is a type variant of Addition */
    const nmo_type_descriptor_t *vec3_type = get_type(CKPGUID_VECTOR);
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

TEST(builtin_operations, vector_scalar_multiplication_resolves_exact_types_and_supports_in_place_results) {
    setup_registries();
    const nmo_guid_t guid = NMO_OP_GUID_MULTIPLY;
    const nmo_type_descriptor_t *vector_type = get_type(CKPGUID_VECTOR), *float_type = get_type(CKPGUID_FLOAT);
    ASSERT_NOT_NULL(vector_type); ASSERT_NOT_NULL(float_type);
    const nmo_operation_tree_cell_t *cell = NULL;
    ASSERT_EQ(NMO_OK,nmo_operation_registry_find_typed(operation_registry,&guid,vector_type,float_type,vector_type,type_registry,&cell));
    ASSERT_NOT_NULL(cell); ASSERT_NOT_NULL(cell->desc.function);
    const nmo_vector_t input = {-2,3,.5f}, expected[] = {{-4,6,1},{.5f,-.75f,-.125f},{0,0,0}};
    const float factors[] = {2,-.25f,0};
    for (size_t i = 0; i < 3; i++) {
        nmo_vector_t output = {17,18,19};
        ASSERT_EQ(NMO_OK,cell->desc.function(&input,vector_type,factors+i,float_type,&output,vector_type,NULL));
        ASSERT_TRUE(output.x == expected[i].x && output.y == expected[i].y && output.z == expected[i].z);
    }
    nmo_vector_t aliased = {2,-4,8}; float scale = -.5f;
    ASSERT_EQ(NMO_OK,cell->desc.function(&aliased,vector_type,&scale,float_type,&aliased,vector_type,NULL));
    ASSERT_TRUE(aliased.x == -1 && aliased.y == 2 && aliased.z == -4);
    teardown_registries();
}

TEST(builtin_operations, vector_dot_vector2) {
    setup_registries();

    const nmo_guid_t op_guid = NMO_OP_GUID_VECTOR_DOT;
    const nmo_type_descriptor_t *vec2_type = get_type(CKPGUID_2DVECTOR);
    const nmo_type_descriptor_t *float_type = get_type(CKPGUID_FLOAT);
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
    const nmo_type_descriptor_t *vec3_type = get_type(CKPGUID_VECTOR);
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

    /*
     * This is a sanity check that builtin operation registration remains rich.
     * The exact count is expected to evolve as we add more builtin operations.
     */
    ASSERT_TRUE(total_ops >= 200);

    teardown_registries();
}

TEST(builtin_operations, finalize_keeps_lookup_stable) {
    setup_registries();

    ASSERT_EQ(NMO_OK, nmo_operation_registry_finalize(operation_registry, type_registry));

    const nmo_guid_t op_guid = NMO_OP_GUID_ADD;
    const nmo_type_descriptor_t *int_type = get_type(CKPGUID_INT);
    ASSERT_NE(NULL, int_type);

    const nmo_operation_tree_cell_t *cell = NULL;
    ASSERT_EQ(NMO_OK, nmo_operation_registry_find(
        operation_registry,
        &op_guid,
        int_type,
        int_type,
        type_registry,
        &cell));
    ASSERT_NE(NULL, cell);

    teardown_registries();
}




/* ============================================================================
 * Math Operations Tests (the Parameter Operations plugin's)
 * ============================================================================ */

/* The function registered for exactly these types, or NULL */
static nmo_operation_fn math_function(nmo_guid_t op, nmo_guid_t p1, nmo_guid_t p2, nmo_guid_t result) {
    const nmo_type_descriptor_t *t1 = get_type(p1);
    const nmo_type_descriptor_t *t2 = nmo_guid_is_null(p2) ? NULL : get_type(p2);
    const nmo_type_descriptor_t *tr = get_type(result);
    const nmo_operation_tree_cell_t *cell = NULL;
    if (t1 == NULL || tr == NULL ||
        nmo_operation_registry_find_typed(operation_registry, &op, t1, t2, tr, type_registry, &cell) != NMO_OK ||
        cell == NULL) {
        return NULL;
    }
    return cell->desc.function;
}

/* Run the function for the types on the operands; the result buffer is `out` */
static nmo_status_t math_run(nmo_guid_t op, nmo_guid_t p1, nmo_guid_t p2, nmo_guid_t result,
                             const void *a, const void *b, void *out) {
    nmo_operation_fn fn = math_function(op, p1, p2, result);
    if (fn == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_type_descriptor_t *t2 = nmo_guid_is_null(p2) ? NULL : get_type(p2);
    return fn(a, get_type(p1), b, t2, out, get_type(result), NULL);
}

TEST(builtin_operations, math_float_functions) {
    setup_registries();
    float in = 9.0f, out = 0.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SQRT, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_FLOAT, &in, NULL, &out));
    ASSERT_FLOAT_EQ(3.0f, out, 0.0f);
    in = 4.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_FLOAT, &in, NULL, &out));
    ASSERT_FLOAT_EQ(0.25f, out, 0.0f);
    /* Inverse of 0 is infinite, as the plugin's, not an error */
    in = 0.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_FLOAT, &in, NULL, &out));
    ASSERT_TRUE(isinf(out));
    int32_t whole = 8, whole_out = 0;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_INT, NMO_GUID(0, 0), CKPGUID_FLOAT, &whole, NULL, &out));
    ASSERT_FLOAT_EQ(0.125f, out, 0.0f);
    in = 0.25f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_INT, &in, NULL, &whole_out));
    ASSERT_EQ(4, whole_out);
    in = 0.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_INT, &in, NULL, &whole_out));
    ASSERT_EQ(INT32_MIN, whole_out);
    in = 180.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_DEG_TO_RAD, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_FLOAT, &in, NULL, &out));
    ASSERT_FLOAT_EQ(180.0f * 0.017453292f, out, 0.0f);
    in = (float)M_PI;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_RAD_TO_DEG, CKPGUID_FLOAT, NMO_GUID(0, 0), CKPGUID_FLOAT, &in, NULL, &out));
    ASSERT_FLOAT_EQ((float)M_PI * 57.295776f, out, 0.0f);
    /* The plugin registers Float only; Angle reaches it through its base type in the engine */
    ASSERT_TRUE(math_function(NMO_OP_GUID_SQRT, CKPGUID_ANGLE, NMO_GUID(0, 0), CKPGUID_ANGLE) == NULL);
    teardown_registries();
}

TEST(builtin_operations, math_vector_functions) {
    setup_registries();
    nmo_vector_t v = {3.0f, 0.0f, 4.0f}, out = {0, 0, 0};
    float f = 0.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_NORMALIZE, CKPGUID_VECTOR, NMO_GUID(0, 0), CKPGUID_VECTOR, &v, NULL, &out));
    ASSERT_FLOAT_EQ(0.6f, out.x, 1e-6f); ASSERT_FLOAT_EQ(0.0f, out.y, 0.0f); ASSERT_FLOAT_EQ(0.8f, out.z, 1e-6f);
    /* In place, and a zero vector stays zero */
    nmo_vector_t zero = {0, 0, 0};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_NORMALIZE, CKPGUID_VECTOR, NMO_GUID(0, 0), CKPGUID_VECTOR, &zero, NULL, &zero));
    ASSERT_FLOAT_EQ(0.0f, zero.x, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_MAGNITUDE, CKPGUID_VECTOR, NMO_GUID(0, 0), CKPGUID_FLOAT, &v, NULL, &f));
    ASSERT_FLOAT_EQ(5.0f, f, 0.0f);
    nmo_vector2_t v2 = {6.0f, 8.0f}, out2 = {0, 0};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_MAGNITUDE, CKPGUID_2DVECTOR, NMO_GUID(0, 0), CKPGUID_FLOAT, &v2, NULL, &f));
    ASSERT_FLOAT_EQ(10.0f, f, 0.0f);
    /* Get Angle: directions, so the lengths do not matter; the cosine is clamped */
    nmo_vector_t x = {2.0f, 0.0f, 0.0f}, y = {0.0f, 5.0f, 0.0f}, minus_x = {-3.0f, 0.0f, 0.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_ANGLE, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_FLOAT, &x, &y, &f));
    ASSERT_FLOAT_EQ((float)(M_PI / 2.0), f, 1e-6f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_ANGLE, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_FLOAT, &x, &minus_x, &f));
    ASSERT_FLOAT_EQ((float)M_PI, f, 1e-6f);
    nmo_vector2_t a2 = {1.0f, 0.0f}, b2 = {1.0f, 1.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_ANGLE, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, CKPGUID_FLOAT, &a2, &b2, &f));
    ASSERT_FLOAT_EQ((float)(M_PI / 4.0), f, 1e-6f);
    /* Reflect: a ray falling at 45 degrees on the floor leaves at 45 degrees; the normal need not be unit */
    nmo_vector_t incident = {1.0f, -1.0f, 0.0f}, floor_normal = {0.0f, 2.0f, 0.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_REFLECT, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, &incident, &floor_normal, &out));
    ASSERT_FLOAT_EQ(1.0f, out.x, 1e-6f); ASSERT_FLOAT_EQ(1.0f, out.y, 1e-6f); ASSERT_FLOAT_EQ(0.0f, out.z, 0.0f);
    /* Symmetry: the point mirrored through the center */
    nmo_vector_t point = {1.0f, 2.0f, 3.0f}, center = {0.0f, 0.0f, 1.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SYMMETRY, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, &point, &center, &out));
    ASSERT_FLOAT_EQ(-1.0f, out.x, 0.0f); ASSERT_FLOAT_EQ(-2.0f, out.y, 0.0f); ASSERT_FLOAT_EQ(-1.0f, out.z, 0.0f);
    nmo_vector2_t c2 = {1.0f, 1.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SYMMETRY, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, CKPGUID_2DVECTOR, &v2, &c2, &out2));
    ASSERT_FLOAT_EQ(-4.0f, out2.x, 0.0f); ASSERT_FLOAT_EQ(-6.0f, out2.y, 0.0f);
    /* Inverse: each component */
    nmo_vector_t quarters = {2.0f, 4.0f, 8.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_VECTOR, NMO_GUID(0, 0), CKPGUID_VECTOR, &quarters, NULL, &out));
    ASSERT_FLOAT_EQ(0.5f, out.x, 0.0f); ASSERT_FLOAT_EQ(0.25f, out.y, 0.0f); ASSERT_FLOAT_EQ(0.125f, out.z, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_2DVECTOR, NMO_GUID(0, 0), CKPGUID_2DVECTOR, &v2, NULL, &out2));
    ASSERT_FLOAT_EQ(1.0f / 6.0f, out2.x, 1e-7f); ASSERT_FLOAT_EQ(0.125f, out2.y, 0.0f);
    /* Spheric To Cartesian: elevation first, azimuth second, as the plugin computes it */
    const float elevation = 0.5f, azimuth = 1.25f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SPHERIC_TO_CART, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_VECTOR, &elevation, &azimuth, &out));
    ASSERT_FLOAT_EQ(cosf(azimuth) * cosf(elevation), out.x, 1e-6f);
    ASSERT_FLOAT_EQ(sinf(elevation), out.y, 1e-6f);
    ASSERT_FLOAT_EQ(cosf(elevation) * sinf(azimuth), out.z, 1e-6f);
    teardown_registries();
}

TEST(builtin_operations, math_from_rotation_and_matrix_inverse) {
    setup_registries();
    /* A quarter turn around a scaled y axis: the axis is normalized */
    nmo_vector_t axis = {0.0f, 3.0f, 0.0f};
    const float angle = (float)(M_PI / 2.0);
    nmo_matrix_t m;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_FROM_ROTATION, CKPGUID_VECTOR, CKPGUID_ANGLE, CKPGUID_MATRIX, &axis, &angle, &m));
    /* Vx3DMatrixFromRotation's rows: the x row turns to +z, the z row to -x */
    ASSERT_FLOAT_EQ(0.0f, m.m[0][0], 1e-6f); ASSERT_FLOAT_EQ(1.0f, m.m[0][2], 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, m.m[1][1], 1e-6f);
    ASSERT_FLOAT_EQ(-1.0f, m.m[2][0], 1e-6f); ASSERT_FLOAT_EQ(0.0f, m.m[2][2], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, m.m[3][0], 0.0f); ASSERT_FLOAT_EQ(1.0f, m.m[3][3], 0.0f);
    nmo_quaternion_t q;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_FROM_ROTATION, CKPGUID_VECTOR, CKPGUID_ANGLE, CKPGUID_QUATERNION, &axis, &angle, &q));
    const float half = sqrtf(0.5f);
    ASSERT_FLOAT_EQ(0.0f, q.x, 1e-6f); ASSERT_FLOAT_EQ(half, q.y, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, q.z, 1e-6f); ASSERT_FLOAT_EQ(half, q.w, 1e-6f);
    /* A turn past 180 degrees takes the other branch of VxQuaternion::FromMatrix */
    const float wide = 3.0f;
    nmo_vector_t x_axis = {1.0f, 0.0f, 0.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_FROM_ROTATION, CKPGUID_VECTOR, CKPGUID_ANGLE, CKPGUID_QUATERNION, &x_axis, &wide, &q));
    ASSERT_FLOAT_EQ(sinf(1.5f), q.x, 1e-6f); ASSERT_FLOAT_EQ(0.0f, q.y, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, q.z, 1e-6f); ASSERT_FLOAT_EQ(cosf(1.5f), q.w, 1e-6f);
    /* Inverse of a rotation with a translation undoes both */
    m.m[3][0] = 10.0f; m.m[3][2] = -4.0f;
    nmo_matrix_t inverse;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_MATRIX, NMO_GUID(0, 0), CKPGUID_MATRIX, &m, NULL, &inverse));
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += m.m[row][k] * inverse.m[k][column];
            }
            ASSERT_FLOAT_EQ(row == column ? 1.0f : 0.0f, sum, 1e-5f);
        }
    }
    /* A singular matrix leaves the result as it was */
    nmo_matrix_t flat; memset(&flat, 0, sizeof(flat));
    nmo_matrix_t kept = inverse;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_MATRIX, NMO_GUID(0, 0), CKPGUID_MATRIX, &flat, NULL, &inverse));
    ASSERT_EQ(0, memcmp(&kept, &inverse, sizeof(kept)));
    teardown_registries();
}

TEST(builtin_operations, math_components_colors_and_rectangles) {
    setup_registries();
    nmo_quaternion_t q = {1.0f, 2.0f, 3.0f, 4.0f}, q_out;
    float f = 0.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_W, CKPGUID_QUATERNION, NMO_GUID(0, 0), CKPGUID_FLOAT, &q, NULL, &f));
    ASSERT_FLOAT_EQ(4.0f, f, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_Z, CKPGUID_QUATERNION, NMO_GUID(0, 0), CKPGUID_FLOAT, &q, NULL, &f));
    ASSERT_FLOAT_EQ(3.0f, f, 0.0f);
    const float nine = 9.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_W, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, &q, &nine, &q_out));
    ASSERT_FLOAT_EQ(1.0f, q_out.x, 0.0f); ASSERT_FLOAT_EQ(9.0f, q_out.w, 0.0f);
    /* The quaternion setters do not cap at 1, the color setters do */
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_X, CKPGUID_QUATERNION, CKPGUID_FLOAT, CKPGUID_QUATERNION, &q, &nine, &q_out));
    ASSERT_FLOAT_EQ(9.0f, q_out.x, 0.0f);
    nmo_color_t c = {0.25f, 0.5f, 0.75f, 1.0f}, c_out;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_GREEN, CKPGUID_COLOR, NMO_GUID(0, 0), CKPGUID_FLOAT, &c, NULL, &f));
    ASSERT_FLOAT_EQ(0.5f, f, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_ALPHA, CKPGUID_COLOR, NMO_GUID(0, 0), CKPGUID_FLOAT, &c, NULL, &f));
    ASSERT_FLOAT_EQ(1.0f, f, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_RED, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, &c, &nine, &c_out));
    ASSERT_FLOAT_EQ(1.0f, c_out.r, 0.0f); ASSERT_FLOAT_EQ(0.5f, c_out.g, 0.0f); ASSERT_FLOAT_EQ(1.0f, c_out.a, 0.0f);
    const float minus = -2.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_BLUE, CKPGUID_COLOR, CKPGUID_FLOAT, CKPGUID_COLOR, &c, &minus, &c_out));
    ASSERT_FLOAT_EQ(-2.0f, c_out.b, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_INVERSE, CKPGUID_COLOR, NMO_GUID(0, 0), CKPGUID_COLOR, &c, NULL, &c_out));
    ASSERT_FLOAT_EQ(0.75f, c_out.r, 0.0f); ASSERT_FLOAT_EQ(0.25f, c_out.b, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.a, 0.0f);
    /* Rainbow Color: pure red at 0, green at 0.3, blue at 0.65, scaled and capped by the intensity */
    float hue = 0.0f, intensity = 1.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_RAINBOW_COLOR, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, &hue, &intensity, &c_out));
    ASSERT_FLOAT_EQ(1.0f, c_out.r, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.g, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.b, 0.0f); ASSERT_FLOAT_EQ(1.0f, c_out.a, 0.0f);
    hue = 0.3f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_RAINBOW_COLOR, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, &hue, &intensity, &c_out));
    ASSERT_FLOAT_EQ(0.0f, c_out.r, 1e-6f); ASSERT_FLOAT_EQ(1.0f, c_out.g, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.b, 1e-6f);
    hue = 0.65f; intensity = 0.5f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_RAINBOW_COLOR, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, &hue, &intensity, &c_out));
    ASSERT_FLOAT_EQ(0.0f, c_out.r, 1e-6f); ASSERT_FLOAT_EQ(0.0f, c_out.g, 1e-6f); ASSERT_FLOAT_EQ(0.5f, c_out.b, 0.0f);
    hue = 2.0f; intensity = 3.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_RAINBOW_COLOR, CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, &hue, &intensity, &c_out));
    ASSERT_FLOAT_EQ(1.0f, c_out.r, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.g, 0.0f); ASSERT_FLOAT_EQ(0.0f, c_out.b, 1e-6f);
    /* Vector and Euler components, and matrix rows */
    nmo_vector_t v = {1.0f, 2.0f, 3.0f}, v_out;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_Y, CKPGUID_VECTOR, CKPGUID_FLOAT, CKPGUID_VECTOR, &v, &nine, &v_out));
    ASSERT_FLOAT_EQ(1.0f, v_out.x, 0.0f); ASSERT_FLOAT_EQ(9.0f, v_out.y, 0.0f); ASSERT_FLOAT_EQ(3.0f, v_out.z, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_Z, CKPGUID_EULERANGLES, CKPGUID_FLOAT, CKPGUID_EULERANGLES, &v, &nine, &v_out));
    ASSERT_FLOAT_EQ(9.0f, v_out.z, 0.0f);
    nmo_vector2_t v2 = {1.0f, 2.0f}, v2_out;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_X, CKPGUID_2DVECTOR, CKPGUID_FLOAT, CKPGUID_2DVECTOR, &v2, &nine, &v2_out));
    ASSERT_FLOAT_EQ(9.0f, v2_out.x, 0.0f); ASSERT_FLOAT_EQ(2.0f, v2_out.y, 0.0f);
    nmo_matrix_t m; memset(&m, 0, sizeof(m));
    for (int i = 0; i < 4; i++) m.m[i][i] = 1.0f;
    nmo_matrix_t m_out;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_POSITION, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, &v, &m, &m_out));
    ASSERT_FLOAT_EQ(1.0f, m_out.m[3][0], 0.0f); ASSERT_FLOAT_EQ(2.0f, m_out.m[3][1], 0.0f);
    ASSERT_FLOAT_EQ(3.0f, m_out.m[3][2], 0.0f); ASSERT_FLOAT_EQ(1.0f, m_out.m[3][3], 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_Z, CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_MATRIX, &v, &m, &m_out));
    ASSERT_FLOAT_EQ(1.0f, m_out.m[2][0], 0.0f); ASSERT_FLOAT_EQ(3.0f, m_out.m[2][2], 0.0f); ASSERT_FLOAT_EQ(0.0f, m_out.m[2][3], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, m_out.m[0][0], 0.0f);
    /* Rectangles: sides, width and height from the left and top, the center */
    nmo_rect_t r = {10.0f, 20.0f, 30.0f, 60.0f}, r_out;
    const float five = 5.0f;
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_LEFT, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, &r, &five, &r_out));
    ASSERT_FLOAT_EQ(5.0f, r_out.left, 0.0f); ASSERT_FLOAT_EQ(30.0f, r_out.right, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_BOTTOM, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, &r, &five, &r_out));
    ASSERT_FLOAT_EQ(20.0f, r_out.top, 0.0f); ASSERT_FLOAT_EQ(5.0f, r_out.bottom, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_WIDTH, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, &r, &five, &r_out));
    ASSERT_FLOAT_EQ(10.0f, r_out.left, 0.0f); ASSERT_FLOAT_EQ(15.0f, r_out.right, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_HEIGHT, CKPGUID_RECT, CKPGUID_FLOAT, CKPGUID_RECT, &r, &five, &r_out));
    ASSERT_FLOAT_EQ(20.0f, r_out.top, 0.0f); ASSERT_FLOAT_EQ(25.0f, r_out.bottom, 0.0f);
    nmo_vector2_t center = {0.0f, 0.0f};
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_SET_CENTER, CKPGUID_RECT, CKPGUID_2DVECTOR, CKPGUID_RECT, &r, &center, &r_out));
    ASSERT_FLOAT_EQ(-10.0f, r_out.left, 0.0f); ASSERT_FLOAT_EQ(-20.0f, r_out.top, 0.0f);
    ASSERT_FLOAT_EQ(10.0f, r_out.right, 0.0f); ASSERT_FLOAT_EQ(20.0f, r_out.bottom, 0.0f);
    ASSERT_EQ(NMO_OK, math_run(NMO_OP_GUID_GET_BOTTOM_RIGHT, CKPGUID_RECT, NMO_GUID(0, 0), CKPGUID_2DVECTOR, &r, NULL, &v2_out));
    ASSERT_FLOAT_EQ(30.0f, v2_out.x, 0.0f); ASSERT_FLOAT_EQ(60.0f, v2_out.y, 0.0f);
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
    REGISTER_TEST(builtin_operations, comparison_string_uses_pointer_representation);
    REGISTER_TEST(builtin_operations, comparison_equal_array);
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
    REGISTER_TEST(builtin_operations, vector_scalar_multiplication_resolves_exact_types_and_supports_in_place_results);
    REGISTER_TEST(builtin_operations, vector_dot_vector2);
    REGISTER_TEST(builtin_operations, vector_cross_vector3);

    REGISTER_TEST(builtin_operations, check_total_operations_count);
    REGISTER_TEST(builtin_operations, finalize_keeps_lookup_stable);
    REGISTER_TEST(builtin_operations, math_float_functions);
    REGISTER_TEST(builtin_operations, math_vector_functions);
    REGISTER_TEST(builtin_operations, math_from_rotation_and_matrix_inverse);
    REGISTER_TEST(builtin_operations, math_components_colors_and_rectangles);
TEST_MAIN_END()
