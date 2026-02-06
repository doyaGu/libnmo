/**
 * @file test_dsl_operations.c
 * @brief Tests for DSL operation invocation (Phase E) — the op() builtin
 *        and Phase D type builtins (type(), type_name(), to_string(), etc.).
 */

#include "test_framework.h"

#include "dsl/nmo_dsl.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_builtin_operations.h"
#include "type/nmo_builtin_type_guids.h"

#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include <stdalign.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

static void assert_ok(nmo_status_t st, const char *label) {
    if (st == NMO_OK) return;
    char msg[256];
    (void)nmo_last_error_message_copy(msg, sizeof(msg));
    fprintf(stderr, "FAIL (%d): %s\n  label: %s\n", (int)st, msg, label ? label : "(null)");
    ASSERT_EQ(NMO_OK, st);
}

static void eval_ok(const nmo_dsl_eval_context_t *ctx, const char *expr, nmo_dsl_value_t *out) {
    ASSERT_NE(NULL, ctx);
    ASSERT_NE(NULL, expr);
    ASSERT_NE(NULL, out);
    *out = (nmo_dsl_value_t){0};
    assert_ok(nmo_dsl_eval_one(ctx->registry, ctx, expr, out), expr);
}

static void eval_fail(const nmo_dsl_eval_context_t *ctx, const char *expr) {
    ASSERT_NE(NULL, ctx);
    ASSERT_NE(NULL, expr);
    nmo_dsl_value_t v = {0};
    nmo_status_t st = nmo_dsl_eval_one(ctx->registry, ctx, expr, &v);
    ASSERT_NE(NMO_OK, st);
    nmo_dsl_value_destroy(&v);
}

/* ============================================================================
 * Fixture
 * ============================================================================ */

typedef struct {
    int32_t x;
    float y;
} ops_root_t;

#define OPS_ROOT_GUID NMO_GUID(0x4F505254u, 0x00000001u)

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;
static nmo_operation_registry_t *ops = NULL;

static void setup(void) {
    arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    ops = nmo_operation_registry_create(arena);
    ASSERT_NE(NULL, ops);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_operations(ops, registry));

    static const nmo_type_field_t root_fields[] = {
        NMO_FIELD(ops_root_t, x, NMO_GUID_FIELD_INT32),
        NMO_FIELD(ops_root_t, y, NMO_GUID_FIELD_FLOAT),
    };

    nmo_type_descriptor_t root_desc = {
        .guid = OPS_ROOT_GUID,
        .name = "OpsRoot",
        .size = sizeof(ops_root_t),
        .alignment = (uint32_t)alignof(ops_root_t),
        .class_id = 0,
        .base_type = NMO_GUID_NULL,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = root_fields,
        .field_count = NMO_FIELD_COUNT(root_fields),
        .vtable = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &root_desc));
}

static void teardown(void) {
    if (ops)      { nmo_operation_registry_destroy(ops); ops = NULL; }
    if (registry) { nmo_type_registry_destroy(registry); registry = NULL; }
    if (arena)    { nmo_arena_destroy(arena);            arena = NULL; }
}

static void make_ctx(const ops_root_t *root, nmo_dsl_eval_context_t *out_ctx) {
    const nmo_type_descriptor_t *root_type =
        nmo_type_registry_find_by_guid(registry, OPS_ROOT_GUID);
    ASSERT_NE(NULL, root_type);

    memset(out_ctx, 0, sizeof(*out_ctx));
    out_ctx->registry = registry;
    out_ctx->ops = ops;
    out_ctx->root_type = root_type;
    out_ctx->root_instance = (void *)root;
}

/* ============================================================================
 * Phase D: type builtins
 * ============================================================================ */

TEST(dsl_ops, type_of_field) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    /* type(x) should return the GUID of int32 */
    nmo_dsl_value_t v;
    eval_ok(&ctx, "type(x)", &v);
    /* Should return a GUID-like value or string — just ensure no error */
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, type_name_of_field) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "type_name(x)", &v);
    /* Should be a string like "int" or "Int32" */
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_NE(NULL, v.as.s);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, to_string_int) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "to_string(x)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_NE(NULL, v.as.s);
    /* The string representation of 42 */
    ASSERT_TRUE(strstr(v.as.s, "42") != NULL);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, from_string_uint32_returns_uint) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "from_string(\"UINT32\", \"42\")", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(42u, v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, from_string_uint64_returns_uint) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "from_string(\"UINT64\", \"18446744073709551615\")", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(UINT64_MAX, v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, from_string_string_returns_string) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "from_string(\"STRING\", \"hello\")", &v);
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_STR_EQ("hello", v.as.s);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, from_string_uint8_overflow_fails) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    eval_fail(&ctx, "from_string(\"UINT8\", \"300\")");

    teardown();
}

/* ============================================================================
 * Phase E: op() builtin
 * ============================================================================ */

TEST(dsl_ops, op_unknown_fails) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    /* Calling op with a non-existent operation should fail */
    eval_fail(&ctx, "op(\"NonExistentOp\", x)");

    teardown();
}

TEST(dsl_ops, op_add_int_success) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "op(\"Add\", 2, 3)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_INT, v.kind);
    ASSERT_EQ(5, v.as.i);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, op_bool_logic_success) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "op(\"And\", true, false)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_EQ(false, v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "op(\"Not\", true)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_EQ(false, v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl_ops, type_builtins_no_args_fail) {
    setup();
    ops_root_t root = { .x = 42, .y = 1.5f };
    nmo_dsl_eval_context_t ctx;
    make_ctx(&root, &ctx);

    /* type() with no args should fail */
    eval_fail(&ctx, "type()");
    eval_fail(&ctx, "type_name()");
    eval_fail(&ctx, "to_string()");

    teardown();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(dsl_ops, type_of_field);
    REGISTER_TEST(dsl_ops, type_name_of_field);
    REGISTER_TEST(dsl_ops, to_string_int);
    REGISTER_TEST(dsl_ops, from_string_uint32_returns_uint);
    REGISTER_TEST(dsl_ops, from_string_uint64_returns_uint);
    REGISTER_TEST(dsl_ops, from_string_string_returns_string);
    REGISTER_TEST(dsl_ops, from_string_uint8_overflow_fails);
    REGISTER_TEST(dsl_ops, op_unknown_fails);
    REGISTER_TEST(dsl_ops, op_add_int_success);
    REGISTER_TEST(dsl_ops, op_bool_logic_success);
    REGISTER_TEST(dsl_ops, type_builtins_no_args_fail);
TEST_MAIN_END()
