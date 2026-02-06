/**
 * @file test_dsl_script.c
 * @brief Tests for DSL script mode (Phase B) -- multi-statement execution
 *        with mutable field assignment.
 */

#include "test_framework.h"

#include "dsl/nmo_dsl.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "type/nmo_builtin_operations.h"
#include "type/nmo_builtin_type_guids.h"

#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include <stdalign.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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

/* Convenience: compile + exec a script, return last value */
static nmo_status_t run_script(
    const nmo_type_registry_t *registry,
    const nmo_dsl_eval_context_t *ctx,
    const char *source,
    nmo_dsl_value_t *out_last)
{
    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, source, &opts, &prog);
    if (st != NMO_OK) return st;
    st = nmo_dsl_exec(prog, ctx, out_last);
    nmo_dsl_program_destroy(prog);
    return st;
}

/* ============================================================================
 * Fixture types (reuse from test_dsl_expr)
 * ============================================================================ */

typedef struct {
    int32_t *arr;
    uint32_t arr_count;
} script_child_t;

typedef struct {
    int32_t x;
    int64_t big;
    float y;
    const char *name;
    int32_t inline_arr[3];

    int32_t *arr;
    uint32_t arr_count;

    script_child_t child;
} script_root_t;

typedef struct {
    int32_t arr_storage[4];
    script_root_t root;
} script_fixture_t;

static void fixture_init(script_fixture_t *fx) {
    ASSERT_NE(NULL, fx);
    fx->arr_storage[0] = 10;
    fx->arr_storage[1] = 20;
    fx->arr_storage[2] = 30;
    fx->arr_storage[3] = 40;
    fx->root = (script_root_t){
        .x = 5,
        .big = 0x1111111100000000LL,
        .y = 1.5f,
        .name = "root",
        .inline_arr = {7, 8, 9},
        .arr = fx->arr_storage,
        .arr_count = 4,
        .child = {
            .arr = fx->arr_storage,
            .arr_count = 4,
        },
    };
}

#define SCRIPT_CHILD_GUID NMO_GUID(0x53435250u, 0x00000010u)
#define SCRIPT_ROOT_GUID  NMO_GUID(0x53435250u, 0x00000001u)

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static uint64_t guess_array_count_cb(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    void *user)
{
    (void)user;
    if (!owner_instance || !field || !field->name) return 0;
    if (strcmp(field->name, "arr") != 0) return 0;

    if (owner_type && nmo_guid_equals(owner_type->guid, SCRIPT_CHILD_GUID)) {
        const script_child_t *c = (const script_child_t *)owner_instance;
        return (uint64_t)c->arr_count;
    }

    const script_root_t *r = (const script_root_t *)owner_instance;
    return (uint64_t)r->arr_count;
}

static void setup(void) {
    arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    static const nmo_type_field_t child_fields[] = {
        NMO_FIELD_ARRAY(script_child_t, arr, NMO_GUID_FIELD_INT32),
        NMO_FIELD(script_child_t, arr_count, NMO_GUID_FIELD_UINT32),
    };

    nmo_type_descriptor_t child_desc = {
        .guid = SCRIPT_CHILD_GUID,
        .name = "ScriptChild",
        .size = sizeof(script_child_t),
        .alignment = (uint32_t)alignof(script_child_t),
        .class_id = 0,
        .base_type = NMO_GUID_NULL,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE,
        .id = 0,
        .description = NULL,
        .fields = child_fields,
        .field_count = NMO_FIELD_COUNT(child_fields),
        .vtable = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &child_desc));

    static const nmo_type_field_t root_fields[] = {
        NMO_FIELD(script_root_t, x, NMO_GUID_FIELD_INT32),
        NMO_FIELD(script_root_t, big, NMO_GUID_FIELD_INT64),
        NMO_FIELD(script_root_t, y, NMO_GUID_FIELD_FLOAT),
        NMO_FIELD(script_root_t, name, NMO_GUID_FIELD_STRING),
        NMO_FIELD_FULL(script_root_t, inline_arr, NMO_GUID_FIELD_INT32, NMO_FIELD_REPEATED, NMO_SEMANTIC_NONE),
        NMO_FIELD_ARRAY(script_root_t, arr, NMO_GUID_FIELD_INT32),
        NMO_FIELD(script_root_t, arr_count, NMO_GUID_FIELD_UINT32),
        NMO_FIELD(script_root_t, child, SCRIPT_CHILD_GUID),
    };

    nmo_type_descriptor_t root_desc = {
        .guid = SCRIPT_ROOT_GUID,
        .name = "ScriptRoot",
        .size = sizeof(script_root_t),
        .alignment = (uint32_t)alignof(script_root_t),
        .class_id = 0,
        .base_type = NMO_GUID_NULL,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE,
        .id = 0,
        .description = NULL,
        .fields = root_fields,
        .field_count = NMO_FIELD_COUNT(root_fields),
        .vtable = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &root_desc));
}

static void teardown(void) {
    if (registry) {
        nmo_type_registry_destroy(registry);
        registry = NULL;
    }
    if (arena) {
        nmo_arena_destroy(arena);
        arena = NULL;
    }
}

static void make_ctx(script_root_t *root, nmo_dsl_eval_context_t *out_ctx) {
    ASSERT_NE(NULL, root);
    ASSERT_NE(NULL, out_ctx);

    const nmo_type_descriptor_t *root_type =
        nmo_type_registry_find_by_guid(registry, SCRIPT_ROOT_GUID);
    ASSERT_NE(NULL, root_type);

    memset(out_ctx, 0, sizeof(*out_ctx));
    out_ctx->registry = registry;
    out_ctx->root_type = root_type;
    out_ctx->root_instance = root;
    out_ctx->guess_array_count = guess_array_count_cb;
    out_ctx->guess_array_count_user = NULL;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(dsl_script, single_assignment) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "x = 42", NULL), "x = 42");
    ASSERT_EQ(42, fx.root.x);

    teardown();
}

TEST(dsl_script, chain_assignments) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "x = 10; y = 3.14", NULL), "chain");
    ASSERT_EQ(10, fx.root.x);

    float diff = fx.root.y - 3.14f;
    ASSERT_TRUE(diff > -0.01f && diff < 0.01f);

    teardown();
}

TEST(dsl_script, expr_stmt_returns_last) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t last = {0};
    assert_ok(run_script(registry, &ctx, "x; y", &last), "x; y");

    ASSERT_EQ(NMO_DSL_VALUE_BYREF, last.kind);
    nmo_dsl_value_destroy(&last);

    teardown();
}

TEST(dsl_script, computed_rhs) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "x = 3 + 4", NULL), "x = 3+4");
    ASSERT_EQ(7, fx.root.x);

    teardown();
}

TEST(dsl_script, assign_coercion_real_to_int) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "x = 2.5", NULL), "x = 2.5");
    ASSERT_EQ(2, fx.root.x);

    teardown();
}

TEST(dsl_script, empty_script) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "", &opts, &prog);
    assert_ok(st, "empty compile");
    st = nmo_dsl_exec(prog, &ctx, NULL);
    assert_ok(st, "empty exec");
    nmo_dsl_program_destroy(prog);

    teardown();
}

TEST(dsl_script, mode_mismatch) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "x", &opts, &prog);
    assert_ok(st, "expr compile");

    st = nmo_dsl_exec(prog, &ctx, NULL);
    ASSERT_NE(NMO_OK, st);

    nmo_dsl_program_destroy(prog);
    teardown();
}

TEST(dsl_script, assign_does_not_produce_value) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t last = {0};
    assert_ok(run_script(registry, &ctx, "x = 99", &last), "assign no val");
    ASSERT_EQ(NMO_DSL_VALUE_NULL, last.kind);
    ASSERT_EQ(99, fx.root.x);
    nmo_dsl_value_destroy(&last);

    teardown();
}

TEST(dsl_script, read_after_write) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t last = {0};
    assert_ok(run_script(registry, &ctx, "x = 77; x", &last), "set-then-read");
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, last.kind);
    const int32_t *p = (const int32_t *)last.as.byref.ptr;
    ASSERT_NE(NULL, p);
    ASSERT_EQ(77, *p);
    nmo_dsl_value_destroy(&last);

    teardown();
}

TEST(dsl_script, byref_to_wider_int_field_is_numeric) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "big = x", NULL), "big = x");
    ASSERT_EQ((int64_t)5, fx.root.big);

    teardown();
}

TEST(dsl_script, non_numeric_assignment_rejected) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_status_t st = run_script(registry, &ctx, "name = 1", NULL);
    ASSERT_NE(NMO_OK, st);
    ASSERT_STR_EQ("root", fx.root.name);

    teardown();
}

TEST(dsl_script, member_array_index_assignment) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "child.arr[2] = 123", NULL), "child.arr[2] = 123");
    ASSERT_EQ(123, fx.arr_storage[2]);

    teardown();
}

TEST(dsl_script, inline_repeated_index_read) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t last = {0};
    assert_ok(run_script(registry, &ctx, "inline_arr[1]", &last), "inline_arr[1]");
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, last.kind);
    ASSERT_NE(NULL, last.as.byref.ptr);
    ASSERT_EQ(8, *(const int32_t *)last.as.byref.ptr);
    nmo_dsl_value_destroy(&last);

    teardown();
}

TEST(dsl_script, inline_repeated_index_assignment) {
    setup();
    script_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    assert_ok(run_script(registry, &ctx, "inline_arr[2] = 99", NULL), "inline_arr[2] = 99");
    ASSERT_EQ(99, fx.root.inline_arr[2]);

    teardown();
}

TEST(dsl_script, trailing_junk_rejected) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "x = 1 y = 2", &opts, &prog);
    ASSERT_NE(NMO_OK, st);
    ASSERT_EQ(NULL, prog);

    teardown();
}

TEST(dsl_script, invalid_index_lvalue_rejected) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "len(arr)[0] = 1", &opts, &prog);
    ASSERT_NE(NMO_OK, st);
    ASSERT_EQ(NULL, prog);

    teardown();
}

TEST(dsl_script, nested_index_lvalue_rejected) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "arr[0][0] = 1", &opts, &prog);
    ASSERT_NE(NMO_OK, st);
    ASSERT_EQ(NULL, prog);

    teardown();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(dsl_script, single_assignment);
    REGISTER_TEST(dsl_script, chain_assignments);
    REGISTER_TEST(dsl_script, expr_stmt_returns_last);
    REGISTER_TEST(dsl_script, computed_rhs);
    REGISTER_TEST(dsl_script, assign_coercion_real_to_int);
    REGISTER_TEST(dsl_script, empty_script);
    REGISTER_TEST(dsl_script, mode_mismatch);
    REGISTER_TEST(dsl_script, assign_does_not_produce_value);
    REGISTER_TEST(dsl_script, read_after_write);
    REGISTER_TEST(dsl_script, byref_to_wider_int_field_is_numeric);
    REGISTER_TEST(dsl_script, non_numeric_assignment_rejected);
    REGISTER_TEST(dsl_script, member_array_index_assignment);
    REGISTER_TEST(dsl_script, inline_repeated_index_read);
    REGISTER_TEST(dsl_script, inline_repeated_index_assignment);
    REGISTER_TEST(dsl_script, trailing_junk_rejected);
    REGISTER_TEST(dsl_script, invalid_index_lvalue_rejected);
    REGISTER_TEST(dsl_script, nested_index_lvalue_rejected);
TEST_MAIN_END()
