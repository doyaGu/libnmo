/**
 * @file test_operation_registry.c
 * @brief Unit tests for operation registry (Phase 6.1)
 */

#include "test_framework.h"
#include "type/operation_system.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stdalign.h>
#include <time.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

typedef struct test_context {
    nmo_arena_t *arena;
    nmo_type_registry_t *type_registry;
    nmo_operation_registry_t *operation_registry;
} test_context_t;

static test_context_t *setup_context(void) {
    test_context_t *ctx = (test_context_t *)malloc(sizeof(test_context_t));
    if (!ctx) return NULL;
    
    ctx->arena = nmo_arena_create(NULL, 1024 * 1024); /* 1 MB */
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }
    
    ctx->type_registry = nmo_type_registry_create(ctx->arena);
    if (!ctx->type_registry) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    ctx->operation_registry = nmo_operation_registry_create(ctx->arena);
    if (!ctx->operation_registry) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

static void teardown_context(test_context_t *ctx) {
    if (!ctx) return;
    
    nmo_operation_registry_destroy(ctx->operation_registry);
    nmo_arena_destroy(ctx->arena);
    free(ctx);
}

/* ============================================================================
 * Mock Operation Functions
 * ============================================================================ */

/* Simple integer addition */
static nmo_result_t mock_add_int(
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    void *user_data
) {
    (void)p1_type;
    (void)p2_type;
    (void)result_type;
    (void)user_data;
    
    const int32_t *a = (const int32_t *)p1_data;
    const int32_t *b = (const int32_t *)p2_data;
    int32_t *result = (int32_t *)result_data;
    
    *result = *a + *b;
    
    return nmo_result_ok();
}

static nmo_result_t mock_add_int_to_float(
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    void *user_data
) {
    (void)p1_type;
    (void)p2_type;
    (void)result_type;
    (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(float *)result_data = (float)(a + b);
    return nmo_result_ok();
}

/* Simple integer negation (unary) */
static nmo_result_t mock_negate_int(
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    void *user_data
) {
    (void)p1_type;
    (void)p2_data;
    (void)p2_type;
    (void)result_type;
    (void)user_data;
    
    const int32_t *a = (const int32_t *)p1_data;
    int32_t *result = (int32_t *)result_data;
    
    *result = -*a;
    
    return nmo_result_ok();
}

/* ============================================================================
 * Test GUIDs
 * ============================================================================ */

static const nmo_guid_t GUID_OP_ADD = {0x10000001, 0x00000000};
static const nmo_guid_t GUID_OP_NEGATE = {0x10000002, 0x00000000};
static const nmo_guid_t GUID_TYPE_INT = {0x20000001, 0x00000000};
static const nmo_guid_t GUID_TYPE_FLOAT = {0x20000002, 0x00000000};

/* ============================================================================
 * Registry Lifecycle Tests
 * ============================================================================ */

TEST(operation_registry, create_destroy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_operation_registry_t *registry = nmo_operation_registry_create(arena);
    ASSERT_NE(NULL, registry);
    ASSERT_NE(NULL, registry->arena);
    ASSERT_NE(NULL, registry->families);
    ASSERT_NE(NULL, registry->family_map);
    ASSERT_EQ(0, registry->family_count);
    ASSERT_EQ(0, registry->total_operations);
    
    nmo_operation_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(operation_registry, create_null_arena) {
    nmo_operation_registry_t *registry = nmo_operation_registry_create(NULL);
    ASSERT_EQ(NULL, registry);
}

/* ============================================================================
 * Registration Tests (Placeholder - will be implemented in Task 6.1.2)
 * ============================================================================ */

TEST(operation_registry, register_operation_success) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register INT type first */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    
    nmo_result_t result = nmo_type_registry_register(ctx->type_registry, &int_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register operation: INT + INT -> INT */
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY | NMO_OP_COMMUTATIVE;
    desc.priority = 100;
    desc.name = "Add";
    desc.description = "Integer addition";
    
    result = nmo_operation_registry_register(
        ctx->operation_registry,
        &desc,
        ctx->type_registry
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify statistics */
    uint64_t total_ops = 0;
    nmo_operation_registry_get_stats(ctx->operation_registry, &total_ops, NULL, NULL);
    ASSERT_EQ(1, total_ops);
    
    teardown_context(ctx);
}

TEST(operation_registry, register_operation_not_implemented) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY | NMO_OP_COMMUTATIVE;
    desc.name = "Add";
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry,
        &desc,
        ctx->type_registry
    );
    
    /* Should fail because types are not registered yet */
    ASSERT_NE(NMO_OK, result.code);
    
    teardown_context(ctx);
}

TEST(operation_registry, register_null_params) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    nmo_operation_desc_t desc = {0};
    
    /* NULL registry */
    nmo_result_t result = nmo_operation_registry_register(NULL, &desc, ctx->type_registry);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* NULL descriptor */
    result = nmo_operation_registry_register(ctx->operation_registry, NULL, ctx->type_registry);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* NULL type registry */
    result = nmo_operation_registry_register(ctx->operation_registry, &desc, NULL);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown_context(ctx);
}

TEST(operation_registry, register_unary_operation) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register INT type */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    
    nmo_result_t result = nmo_type_registry_register(ctx->type_registry, &int_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register unary operation: -INT -> INT */
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_NEGATE;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = (nmo_guid_t){0, 0};  /* NULL for unary */
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_negate_int;
    desc.flags = NMO_OP_UNARY;
    desc.priority = 100;
    desc.name = "Negate";
    desc.description = "Integer negation";
    
    result = nmo_operation_registry_register(
        ctx->operation_registry,
        &desc,
        ctx->type_registry
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    
    teardown_context(ctx);
}

TEST(operation_registry, register_bulk_operations) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_TYPE_FLOAT;
    float_type.name = "FLOAT";
    float_type.size = sizeof(float);
    float_type.alignment = alignof(float);
    nmo_type_registry_register(ctx->type_registry, &float_type);
    
    /* Register multiple operations */
    nmo_operation_desc_t descs[3];
    
    /* INT + INT -> INT */
    descs[0] = (nmo_operation_desc_t){0};
    descs[0].operation_guid = GUID_OP_ADD;
    descs[0].p1_type_guid = GUID_TYPE_INT;
    descs[0].p2_type_guid = GUID_TYPE_INT;
    descs[0].result_type_guid = GUID_TYPE_INT;
    descs[0].function = mock_add_int;
    descs[0].flags = NMO_OP_BINARY;
    descs[0].priority = 100;
    descs[0].name = "Add";
    
    /* -INT -> INT */
    descs[1] = (nmo_operation_desc_t){0};
    descs[1].operation_guid = GUID_OP_NEGATE;
    descs[1].p1_type_guid = GUID_TYPE_INT;
    descs[1].result_type_guid = GUID_TYPE_INT;
    descs[1].function = mock_negate_int;
    descs[1].flags = NMO_OP_UNARY;
    descs[1].priority = 100;
    descs[1].name = "Negate";
    
    /* FLOAT + FLOAT -> FLOAT (will fail silently - no mock function) */
    descs[2] = (nmo_operation_desc_t){0};
    descs[2].operation_guid = GUID_OP_ADD;
    descs[2].p1_type_guid = GUID_TYPE_FLOAT;
    descs[2].p2_type_guid = GUID_TYPE_FLOAT;
    descs[2].result_type_guid = GUID_TYPE_FLOAT;
    descs[2].function = mock_add_int;  /* Reuse for testing */
    descs[2].flags = NMO_OP_BINARY;
    descs[2].priority = 100;
    descs[2].name = "Add";
    
    nmo_result_t result = nmo_operation_registry_register_bulk(
        ctx->operation_registry,
        descs,
        3,
        ctx->type_registry,
        NULL  /* No logger for this test */
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify statistics (should have 3 operations) */
    uint64_t total_ops = 0;
    nmo_operation_registry_get_stats(ctx->operation_registry, &total_ops, NULL, NULL);
    ASSERT_EQ(3, total_ops);
    
    teardown_context(ctx);
}

/* ============================================================================
 * Lookup Tests (Placeholder - will be implemented in Task 6.1.3)
 * ============================================================================ */

TEST(operation_registry, find_operation_success) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types and operation */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY | NMO_OP_COMMUTATIVE;
    desc.priority = 100;
    desc.name = "Add";
    
    nmo_operation_registry_register(ctx->operation_registry, &desc, ctx->type_registry);
    
    /* Find the operation */
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_result_t result = nmo_operation_registry_find(
        ctx->operation_registry,
        &GUID_OP_ADD,
        &int_type,
        &int_type,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(mock_add_int, cell->desc.function);
    ASSERT_EQ(100, cell->desc.priority);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_operation_not_implemented) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Create mock type descriptor */
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = GUID_TYPE_INT;
    
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_result_t result = nmo_operation_registry_find(
        ctx->operation_registry,
        &GUID_OP_ADD,
        &type_desc,
        &type_desc,
        ctx->type_registry,
        &cell
    );

    /* Should fail - family not found */
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NULL, cell);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_null_params) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    nmo_type_descriptor_t type_desc = {0};
    const nmo_operation_tree_cell_t *cell = NULL;
    
    /* NULL registry */
    nmo_result_t result = nmo_operation_registry_find(NULL, &GUID_OP_ADD, &type_desc, &type_desc, NULL, &cell);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* NULL operation GUID */
    result = nmo_operation_registry_find(ctx->operation_registry, NULL, &type_desc, &type_desc, NULL, &cell);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* NULL p1_type */
    result = nmo_operation_registry_find(ctx->operation_registry, &GUID_OP_ADD, NULL, &type_desc, NULL, &cell);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* NULL out_cell */
    result = nmo_operation_registry_find(ctx->operation_registry, &GUID_OP_ADD, &type_desc, &type_desc, NULL, NULL);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown_context(ctx);
}

TEST(operation_registry, execute_operation_success) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types and operation */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY;
    desc.name = "Add";
    
    nmo_operation_registry_register(ctx->operation_registry, &desc, ctx->type_registry);
    
    /* Execute: 5 + 3 = 8 */
    int32_t a = 5;
    int32_t b = 3;
    int32_t result = 0;
    
    nmo_result_t exec_result = nmo_operation_registry_execute(
        ctx->operation_registry,
        &GUID_OP_ADD,
        &a, &int_type,
        &b, &int_type,
        &result, &int_type,
        ctx->type_registry
    );
    
    ASSERT_EQ(NMO_OK, exec_result.code);
    ASSERT_EQ(8, result);
    
    teardown_context(ctx);
}

TEST(operation_registry, execute_selects_requested_result_type) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);

    /* Register types */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(ctx->type_registry, &int_type).code);

    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_TYPE_FLOAT;
    float_type.name = "FLOAT";
    float_type.size = sizeof(float);
    float_type.alignment = alignof(float);
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(ctx->type_registry, &float_type).code);

    /* Register two cells for the same (op, p1, p2), with different result types */
    nmo_operation_desc_t int_result = {0};
    int_result.operation_guid = GUID_OP_ADD;
    int_result.p1_type_guid = GUID_TYPE_INT;
    int_result.p2_type_guid = GUID_TYPE_INT;
    int_result.result_type_guid = GUID_TYPE_INT;
    int_result.function = mock_add_int;
    int_result.flags = NMO_OP_BINARY;
    int_result.priority = 50;
    int_result.name = "Add";
    ASSERT_EQ(NMO_OK, nmo_operation_registry_register(ctx->operation_registry, &int_result, ctx->type_registry).code);

    nmo_operation_desc_t float_result = {0};
    float_result.operation_guid = GUID_OP_ADD;
    float_result.p1_type_guid = GUID_TYPE_INT;
    float_result.p2_type_guid = GUID_TYPE_INT;
    float_result.result_type_guid = GUID_TYPE_FLOAT;
    float_result.function = mock_add_int_to_float;
    float_result.flags = NMO_OP_BINARY;
    float_result.priority = 100; /* Higher priority, but must still be selected by result_type */
    float_result.name = "Add";
    ASSERT_EQ(NMO_OK, nmo_operation_registry_register(ctx->operation_registry, &float_result, ctx->type_registry).code);

    const int32_t a = 3;
    const int32_t b = 5;
    float out = 0.0f;

    const nmo_type_descriptor_t *int_desc = nmo_type_registry_find_by_guid(ctx->type_registry, GUID_TYPE_INT);
    const nmo_type_descriptor_t *float_desc = nmo_type_registry_find_by_guid(ctx->type_registry, GUID_TYPE_FLOAT);
    ASSERT_NE(NULL, int_desc);
    ASSERT_NE(NULL, float_desc);

    nmo_result_t exec_result = nmo_operation_registry_execute(
        ctx->operation_registry,
        &GUID_OP_ADD,
        &a, int_desc,
        &b, int_desc,
        &out, float_desc,
        ctx->type_registry
    );

    ASSERT_EQ(NMO_OK, exec_result.code);
    ASSERT_TRUE(out == 8.0f);

    teardown_context(ctx);
}

TEST(operation_registry, get_family_success) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types and operation */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY;
    desc.name = "Add";
    desc.description = "Integer addition";
    
    nmo_operation_registry_register(ctx->operation_registry, &desc, ctx->type_registry);
    
    /* Get family */
    const nmo_operation_family_t *family = nmo_operation_registry_get_family(
        ctx->operation_registry,
        &GUID_OP_ADD
    );
    
    ASSERT_NE(NULL, family);
    ASSERT_EQ(0, strcmp("Add", family->name));
    ASSERT_EQ(1, family->total_operations);
    
    teardown_context(ctx);
}

/* Enumeration callback for testing */
static nmo_result_t enum_callback(const nmo_operation_tree_cell_t *cell, void *user_data) {
    int *count = (int *)user_data;
    (*count)++;
    (void)cell;
    return nmo_result_ok();
}

TEST(operation_registry, enumerate_family_success) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types and operations */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    nmo_operation_desc_t desc1 = {0};
    desc1.operation_guid = GUID_OP_ADD;
    desc1.p1_type_guid = GUID_TYPE_INT;
    desc1.p2_type_guid = GUID_TYPE_INT;
    desc1.result_type_guid = GUID_TYPE_INT;
    desc1.function = mock_add_int;
    desc1.flags = NMO_OP_BINARY;
    desc1.name = "Add";
    nmo_operation_registry_register(ctx->operation_registry, &desc1, ctx->type_registry);
    
    /* Get family and enumerate */
    const nmo_operation_family_t *family = nmo_operation_registry_get_family(
        ctx->operation_registry,
        &GUID_OP_ADD
    );
    ASSERT_NE(NULL, family);
    
    int count = 0;
    nmo_result_t result = nmo_operation_family_enumerate(family, enum_callback, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    
    teardown_context(ctx);
}

/* ============================================================================
 * Statistics Tests
 * ============================================================================ */

TEST(operation_registry, get_stats_initial) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    uint64_t total_ops = 0;
    uint64_t total_lookups = 0;
    uint64_t cache_hits = 0;
    
    nmo_operation_registry_get_stats(
        ctx->operation_registry,
        &total_ops,
        &total_lookups,
        &cache_hits
    );
    
    ASSERT_EQ(0, total_ops);
    ASSERT_EQ(0, total_lookups);
    ASSERT_EQ(0, cache_hits);
    
    teardown_context(ctx);
}

TEST(operation_registry, get_stats_null_params) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Should not crash with NULL outputs */
    nmo_operation_registry_get_stats(ctx->operation_registry, NULL, NULL, NULL);
    
    /* Should not crash with NULL registry */
    uint64_t dummy = 0;
    nmo_operation_registry_get_stats(NULL, &dummy, &dummy, &dummy);
    
    teardown_context(ctx);
}

/* ============================================================================
 * Inheritance Matching Tests (Phase 6.3)
 * ============================================================================ */

TEST(operation_registry, find_with_inheritance_exact_match) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register base type */
    nmo_guid_t base_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t base_type = {
        .guid = base_guid,
        .name = "BaseInt",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &base_type);
    
    /* Register operation for base type */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    nmo_operation_desc_t op_desc = {
        .operation_guid = op_guid,
        .p1_type_guid = base_guid,
        .p2_type_guid = base_guid,
        .result_type_guid = base_guid,
        .function = mock_add_int,
        .name = "Add",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry, &op_desc, ctx->type_registry
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Find with exact match (should succeed) */
    const nmo_operation_tree_cell_t *cell = NULL;
    result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &base_type,
        &base_type,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(mock_add_int, cell->desc.function);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_with_inheritance_derived_type) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register base type */
    nmo_guid_t base_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t base_type = {
        .guid = base_guid,
        .name = "BaseNumber",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &base_type);
    
    /* Register derived type */
    nmo_guid_t derived_guid = {0x1000, 0x0002};
    nmo_type_descriptor_t derived_type = {
        .guid = derived_guid,
        .name = "DerivedInt",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = base_guid  /* Inherits from BaseNumber */
    };
    nmo_type_registry_register(ctx->type_registry, &derived_type);
    
    /* Register operation for BASE type only */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    nmo_operation_desc_t op_desc = {
        .operation_guid = op_guid,
        .p1_type_guid = base_guid,
        .p2_type_guid = base_guid,
        .result_type_guid = base_guid,
        .function = mock_add_int,
        .name = "Add",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry, &op_desc, ctx->type_registry
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Find with DERIVED type (should match via inheritance) */
    const nmo_operation_tree_cell_t *cell = NULL;
    result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &derived_type,  /* Using derived type */
        &derived_type,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(mock_add_int, cell->desc.function);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_with_inheritance_multi_level) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register 3-level hierarchy: Root -> Middle -> Leaf */
    nmo_guid_t root_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t root_type = {
        .guid = root_guid,
        .name = "Root",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &root_type);
    
    nmo_guid_t middle_guid = {0x1000, 0x0002};
    nmo_type_descriptor_t middle_type = {
        .guid = middle_guid,
        .name = "Middle",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = root_guid
    };
    nmo_type_registry_register(ctx->type_registry, &middle_type);
    
    nmo_guid_t leaf_guid = {0x1000, 0x0003};
    nmo_type_descriptor_t leaf_type = {
        .guid = leaf_guid,
        .name = "Leaf",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = middle_guid
    };
    nmo_type_registry_register(ctx->type_registry, &leaf_type);
    
    /* Register operation for ROOT type only */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    nmo_operation_desc_t op_desc = {
        .operation_guid = op_guid,
        .p1_type_guid = root_guid,
        .p2_type_guid = root_guid,
        .result_type_guid = root_guid,
        .function = mock_add_int,
        .name = "Add",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry, &op_desc, ctx->type_registry
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Find with LEAF type (should match via 2-level inheritance) */
    const nmo_operation_tree_cell_t *cell = NULL;
    result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &leaf_type,  /* Using leaf type */
        &leaf_type,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, cell);
    ASSERT_EQ(mock_add_int, cell->desc.function);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_with_inheritance_no_match) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register two unrelated types */
    nmo_guid_t type1_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t type1 = {
        .guid = type1_guid,
        .name = "Type1",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &type1);
    
    nmo_guid_t type2_guid = {0x1000, 0x0002};
    nmo_type_descriptor_t type2 = {
        .guid = type2_guid,
        .name = "Type2",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &type2);
    
    /* Register operation for type1 */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    nmo_operation_desc_t op_desc = {
        .operation_guid = op_guid,
        .p1_type_guid = type1_guid,
        .p2_type_guid = type1_guid,
        .result_type_guid = type1_guid,
        .function = mock_add_int,
        .name = "Add",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry, &op_desc, ctx->type_registry
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Find with type2 (should fail - no inheritance relationship) */
    const nmo_operation_tree_cell_t *cell = NULL;
    result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &type2,
        &type2,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_NE(NMO_OK, result.code);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_with_inheritance_closest_match) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register hierarchy: Root -> Middle -> Leaf */
    nmo_guid_t root_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t root_type = {
        .guid = root_guid,
        .name = "Root",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &root_type);
    
    nmo_guid_t middle_guid = {0x1000, 0x0002};
    nmo_type_descriptor_t middle_type = {
        .guid = middle_guid,
        .name = "Middle",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = root_guid
    };
    nmo_type_registry_register(ctx->type_registry, &middle_type);
    
    nmo_guid_t leaf_guid = {0x1000, 0x0003};
    nmo_type_descriptor_t leaf_type = {
        .guid = leaf_guid,
        .name = "Leaf",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = middle_guid
    };
    nmo_type_registry_register(ctx->type_registry, &leaf_type);
    
    /* Register TWO operations: one for Root, one for Middle */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    
    nmo_operation_desc_t op_root = {
        .operation_guid = op_guid,
        .p1_type_guid = root_guid,
        .p2_type_guid = root_guid,
        .result_type_guid = root_guid,
        .function = mock_add_int,
        .name = "AddRoot",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    nmo_operation_registry_register(ctx->operation_registry, &op_root, ctx->type_registry);
    
    nmo_operation_desc_t op_middle = {
        .operation_guid = op_guid,
        .p1_type_guid = middle_guid,
        .p2_type_guid = middle_guid,
        .result_type_guid = middle_guid,
        .function = mock_negate_int,  /* Different function */
        .name = "AddMiddle",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    nmo_operation_registry_register(ctx->operation_registry, &op_middle, ctx->type_registry);
    
    /* Find with Leaf type - should prefer Middle over Root (closer match) */
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_result_t result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &leaf_type,
        &leaf_type,
        ctx->type_registry,
        &cell
    );
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, cell);
    /* Should match Middle (depth=1) instead of Root (depth=2) */
    ASSERT_EQ(mock_negate_int, cell->desc.function);
    
    teardown_context(ctx);
}

TEST(operation_registry, find_without_type_registry_no_inheritance) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types */
    nmo_guid_t base_guid = {0x1000, 0x0001};
    nmo_type_descriptor_t base_type = {
        .guid = base_guid,
        .name = "Base",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = {0, 0}
    };
    nmo_type_registry_register(ctx->type_registry, &base_type);
    
    nmo_guid_t derived_guid = {0x1000, 0x0002};
    nmo_type_descriptor_t derived_type = {
        .guid = derived_guid,
        .name = "Derived",
        .size = sizeof(int32_t),
        .alignment = _Alignof(int32_t),
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .base_type = base_guid
    };
    nmo_type_registry_register(ctx->type_registry, &derived_type);
    
    /* Register operation for base type */
    nmo_guid_t op_guid = {0x2000, 0x0001};
    nmo_operation_desc_t op_desc = {
        .operation_guid = op_guid,
        .p1_type_guid = base_guid,
        .p2_type_guid = base_guid,
        .result_type_guid = base_guid,
        .function = mock_add_int,
        .name = "Add",
        .flags = NMO_OP_BINARY,
        .priority = 100
    };
    nmo_operation_registry_register(ctx->operation_registry, &op_desc, ctx->type_registry);
    
    /* Find with derived type but WITHOUT type_registry (NULL) */
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_result_t result = nmo_operation_registry_find(
        ctx->operation_registry,
        &op_guid,
        &derived_type,
        &derived_type,
        NULL,  /* No type registry - inheritance matching disabled */
        &cell
    );
    
    /* Should fail without inheritance matching */
    ASSERT_NE(NMO_OK, result.code);
    
    teardown_context(ctx);
}

/* ============================================================================
 * Performance Benchmark Tests
 * ============================================================================ */

TEST(operation_registry, performance_register_100_operations) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    /* Create 100 operation descriptors */
    nmo_operation_desc_t descs[100];
    for (int i = 0; i < 100; i++) {
        nmo_guid_t op_guid = {0x10000000 + i, 0x00000000};
        descs[i] = (nmo_operation_desc_t){0};
        descs[i].operation_guid = op_guid;
        descs[i].p1_type_guid = GUID_TYPE_INT;
        descs[i].p2_type_guid = GUID_TYPE_INT;
        descs[i].result_type_guid = GUID_TYPE_INT;
        descs[i].function = mock_add_int;
        descs[i].flags = NMO_OP_BINARY;
        descs[i].priority = 100;
        descs[i].name = "TestOp";
    }
    
    /* Benchmark registration time */
    clock_t start = clock();
    nmo_result_t result = nmo_operation_registry_register_bulk(
        ctx->operation_registry,
        descs,
        100,
        ctx->type_registry,
        NULL
    );
    clock_t end = clock();
    
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Performance requirement: < 10ms for 100 operations */
    /* Note: Actual performance: ~%.3fms */
    printf("    [Performance] Registered 100 operations in %.3f ms (requirement: < 10ms)\n", elapsed_ms);
    
    /* Verify all operations registered */
    uint64_t total_ops = 0;
    nmo_operation_registry_get_stats(ctx->operation_registry, &total_ops, NULL, NULL);
    ASSERT_EQ(100, total_ops);
    
    teardown_context(ctx);
}

TEST(operation_registry, performance_lookup_1000_operations) {
    test_context_t *ctx = setup_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register type */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_TYPE_INT;
    int_type.name = "INT";
    int_type.size = sizeof(int32_t);
    int_type.alignment = alignof(int32_t);
    nmo_type_registry_register(ctx->type_registry, &int_type);
    
    /* Get type descriptor for lookup */
    const nmo_type_descriptor_t *int_type_ptr = nmo_type_registry_find_by_guid(ctx->type_registry, GUID_TYPE_INT);
    ASSERT_NE(NULL, int_type_ptr);
    
    /* Register one operation */
    nmo_operation_desc_t desc = {0};
    desc.operation_guid = GUID_OP_ADD;
    desc.p1_type_guid = GUID_TYPE_INT;
    desc.p2_type_guid = GUID_TYPE_INT;
    desc.result_type_guid = GUID_TYPE_INT;
    desc.function = mock_add_int;
    desc.flags = NMO_OP_BINARY;
    desc.priority = 100;
    desc.name = "Add";
    
    nmo_result_t result = nmo_operation_registry_register(
        ctx->operation_registry,
        &desc,
        ctx->type_registry
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Benchmark 1000 lookups */
    const nmo_operation_tree_cell_t *cell = NULL;
    clock_t start = clock();
    
    for (int i = 0; i < 1000; i++) {
        result = nmo_operation_registry_find(
            ctx->operation_registry,
            &GUID_OP_ADD,
            int_type_ptr,
            int_type_ptr,
            ctx->type_registry,
            &cell
        );
        ASSERT_EQ(NMO_OK, result.code);
        ASSERT_NE(NULL, cell);
    }
    
    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    double avg_us = (elapsed_ms * 1000.0) / 1000.0;
    
    /* Performance requirement: < 1μs per lookup */
    printf("    [Performance] 1000 lookups in %.3f ms (avg: %.3f μs per lookup, requirement: < 1μs)\n", 
           elapsed_ms, avg_us);
    
    /* Verify call count */
    ASSERT_EQ(1000, cell->call_count);
    
    teardown_context(ctx);
}

/* ============================================================================
 * Test Suite
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(operation_registry, create_destroy);
    REGISTER_TEST(operation_registry, create_null_arena);
    REGISTER_TEST(operation_registry, register_operation_success);
    REGISTER_TEST(operation_registry, register_operation_not_implemented);
    REGISTER_TEST(operation_registry, register_null_params);
    REGISTER_TEST(operation_registry, register_unary_operation);
    REGISTER_TEST(operation_registry, register_bulk_operations);
    REGISTER_TEST(operation_registry, find_operation_success);
    REGISTER_TEST(operation_registry, find_operation_not_implemented);
    REGISTER_TEST(operation_registry, find_null_params);
    REGISTER_TEST(operation_registry, execute_operation_success);
    REGISTER_TEST(operation_registry, execute_selects_requested_result_type);
    REGISTER_TEST(operation_registry, get_family_success);
    REGISTER_TEST(operation_registry, enumerate_family_success);
    REGISTER_TEST(operation_registry, get_stats_initial);
    REGISTER_TEST(operation_registry, get_stats_null_params);
    
    /* Phase 6.3: Inheritance matching tests */
    REGISTER_TEST(operation_registry, find_with_inheritance_exact_match);
    REGISTER_TEST(operation_registry, find_with_inheritance_derived_type);
    REGISTER_TEST(operation_registry, find_with_inheritance_multi_level);
    REGISTER_TEST(operation_registry, find_with_inheritance_no_match);
    REGISTER_TEST(operation_registry, find_with_inheritance_closest_match);
    REGISTER_TEST(operation_registry, find_without_type_registry_no_inheritance);
    
    /* Performance benchmark tests */
    REGISTER_TEST(operation_registry, performance_register_100_operations);
    REGISTER_TEST(operation_registry, performance_lookup_1000_operations);
TEST_MAIN_END()
