/**
 * @file test_dsl_expr.c
 * @brief Parity tests for the DSL expression engine (Type layer)
 *
 * Mirrors every test in test_query.c using the new nmo_dsl API.
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

static void assert_dsl_ok(nmo_status_t st, const char *expr) {
    if (st == NMO_OK) return;
    char msg[256];
    (void)nmo_last_error_message_copy(msg, sizeof(msg));
    fprintf(stderr, "dsl failed (%d): %s\n  expr: %s\n", (int)st, msg, expr ? expr : "(null)");
    ASSERT_EQ(NMO_OK, st);
}

static void eval_ok(const nmo_dsl_eval_context_t *ctx, const char *expr, nmo_dsl_value_t *out) {
    ASSERT_NE(NULL, ctx);
    ASSERT_NE(NULL, expr);
    ASSERT_NE(NULL, out);
    *out = (nmo_dsl_value_t){0};
    assert_dsl_ok(nmo_dsl_eval_one(ctx->registry, ctx, expr, out), expr);
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
 * Fixture types (identical to test_query.c)
 * ============================================================================ */

typedef struct {
    int32_t v;
} query_elem_t;

typedef struct {
    int32_t x;
    float y;

    int32_t *arr;
    uint32_t arr_count;

    bool *bools;
    uint32_t bools_count;

    query_elem_t *elems;
    uint32_t elems_count;
} query_root_t;

typedef struct {
    int32_t arr_storage[4];
    bool bools_storage[3];
    query_elem_t elems_storage[3];
    query_root_t root;
} query_fixture_t;

static void fixture_init(query_fixture_t *fx) {
    ASSERT_NE(NULL, fx);
    fx->arr_storage[0] = 1;
    fx->arr_storage[1] = 2;
    fx->arr_storage[2] = 3;
    fx->arr_storage[3] = 4;
    fx->bools_storage[0] = false;
    fx->bools_storage[1] = true;
    fx->bools_storage[2] = true;
    fx->elems_storage[0] = (query_elem_t){1};
    fx->elems_storage[1] = (query_elem_t){2};
    fx->elems_storage[2] = (query_elem_t){3};
    fx->root = (query_root_t){
        .x = 3,
        .y = 2.5f,
        .arr = fx->arr_storage,
        .arr_count = 4,
        .bools = fx->bools_storage,
        .bools_count = 3,
        .elems = fx->elems_storage,
        .elems_count = 3,
    };
}

#define QUERY_ELEM_GUID NMO_GUID(0x51555259u, 0x00000001u)
#define QUERY_ROOT_GUID NMO_GUID(0x51555259u, 0x00000002u)

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static uint64_t guess_array_count_cb(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    void *user)
{
    (void)owner_type;
    (void)user;
    if (!owner_instance || !field || !field->name) return 0;
    const query_root_t *r = (const query_root_t *)owner_instance;

    if (strcmp(field->name, "arr") == 0) return (uint64_t)r->arr_count;
    if (strcmp(field->name, "bools") == 0) return (uint64_t)r->bools_count;
    if (strcmp(field->name, "elems") == 0) return (uint64_t)r->elems_count;
    return 0;
}

static const char *resolve_object_name_cb(uint32_t id, void *user) {
    (void)user;
    if (id == 42u) return "Foo";
    return NULL;
}

static void setup(void) {
    arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    static const nmo_type_field_t elem_fields[] = {
        NMO_FIELD(query_elem_t, v, NMO_GUID_FIELD_INT32),
    };

    nmo_type_descriptor_t elem_desc = {
        .guid = QUERY_ELEM_GUID,
        .name = "QueryElem",
        .size = sizeof(query_elem_t),
        .alignment = (uint32_t)alignof(query_elem_t),
        .class_id = 0,
        .base_type = NMO_GUID_NULL,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = elem_fields,
        .field_count = NMO_FIELD_COUNT(elem_fields),
        .vtable = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &elem_desc));

    static const nmo_type_field_t root_fields[] = {
        NMO_FIELD(query_root_t, x, NMO_GUID_FIELD_INT32),
        NMO_FIELD(query_root_t, y, NMO_GUID_FIELD_FLOAT),
        NMO_FIELD_ARRAY(query_root_t, arr, NMO_GUID_FIELD_INT32),
        NMO_FIELD(query_root_t, arr_count, NMO_GUID_FIELD_UINT32),

        NMO_FIELD_ARRAY(query_root_t, bools, NMO_GUID_FIELD_BOOL),
        NMO_FIELD(query_root_t, bools_count, NMO_GUID_FIELD_UINT32),

        NMO_FIELD_ARRAY(query_root_t, elems, QUERY_ELEM_GUID),
        NMO_FIELD(query_root_t, elems_count, NMO_GUID_FIELD_UINT32),
    };

    nmo_type_descriptor_t root_desc = {
        .guid = QUERY_ROOT_GUID,
        .name = "QueryRoot",
        .size = sizeof(query_root_t),
        .alignment = (uint32_t)alignof(query_root_t),
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

static void make_ctx(const query_root_t *root, nmo_dsl_eval_context_t *out_ctx) {
    ASSERT_NE(NULL, root);
    ASSERT_NE(NULL, out_ctx);

    const nmo_type_descriptor_t *root_type = nmo_type_registry_find_by_guid(registry, QUERY_ROOT_GUID);
    ASSERT_NE(NULL, root_type);

    memset(out_ctx, 0, sizeof(*out_ctx));
    out_ctx->registry = registry;
    out_ctx->root_type = root_type;
    out_ctx->root_instance = (void *)root;
    out_ctx->current_type = NULL;
    out_ctx->current_instance = NULL;
    out_ctx->guess_array_count = guess_array_count_cb;
    out_ctx->guess_array_count_user = NULL;
    out_ctx->resolve_object_name = resolve_object_name_cb;
    out_ctx->resolve_object_name_user = NULL;
}

/* ============================================================================
 * Parity Tests (mirroring test_query.c)
 * ============================================================================ */

TEST(dsl, field_access) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "x", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, v.kind);
    ASSERT_EQ(3, *(const int32_t *)v.as.byref.ptr);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, arithmetic_add) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "x + 1", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 3.9 && v.as.r < 4.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, index_literal) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "arr[2]", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, v.kind);
    ASSERT_EQ(3, *(const int32_t *)v.as.byref.ptr);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, index_expression) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "arr[1 + 1]", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, v.kind);
    ASSERT_EQ(3, *(const int32_t *)v.as.byref.ptr);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, index_oob_fails) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "arr[999]");
    teardown();
}

TEST(dsl, index_neg_fails) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "arr[-1]");
    teardown();
}

TEST(dsl, builtin_len_seq) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(4u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_len_string) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(\"abc\")", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(3u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_size_count) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "size(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(4u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "count(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(4u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_empty_first_last_avg) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "empty(arr[? @ > 100])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_EQ(true, v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "first(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, v.kind);
    ASSERT_EQ(1, *(const int32_t *)v.as.byref.ptr);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "last(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BYREF, v.kind);
    ASSERT_EQ(4, *(const int32_t *)v.as.byref.ptr);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "avg(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(fabs(v.as.r - 2.5) < 0.001);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_sum) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "sum(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 9.9 && v.as.r < 10.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_min_max) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "min(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 0.9 && v.as.r < 1.1);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "max(arr)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 3.9 && v.as.r < 4.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_any_all) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    fx.bools_storage[0] = false;
    fx.bools_storage[1] = true;
    fx.bools_storage[2] = true;

    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "any(bools)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "all(bools)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_FALSE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_any_all_empty) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    fx.root.bools_count = 0;
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "any(bools)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_FALSE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "all(bools)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, wildcard) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(arr[*])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(4u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "sum(arr[*])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 9.9 && v.as.r < 10.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, slice_prefix_suffix) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "sum(arr[1:])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 8.9 && v.as.r < 9.1);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "sum(arr[:2])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 2.9 && v.as.r < 3.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, slice_range) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(arr[0:2])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(2u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, slice_inverted) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(arr[3:1])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(0u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, filter_basic) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(elems[? v > 1])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(2u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, filter_at_symbol) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "sum(elems[? @.v > 1].v)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 4.9 && v.as.r < 5.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, member_mapping) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "sum(elems.v)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 5.9 && v.as.r < 6.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, precedence) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "1 + 2 * 3", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 6.9 && v.as.r < 7.1);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "(1 + 2) * 3", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 8.9 && v.as.r < 9.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, unary_ops) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "-x", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r < -2.9 && v.as.r > -3.1);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "+1", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 0.9 && v.as.r < 1.1);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "!!true", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, comparisons) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "x > 2", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "x == 3", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "x != 3", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_FALSE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, logic_ops) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "!false && true", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "true || false && false", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, logic_short_circuit_and) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "false && type()", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_FALSE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, logic_short_circuit_or) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "true || type()", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, modulo) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "10 % 3", &v);
    ASSERT_EQ(NMO_DSL_VALUE_INT, v.kind);
    ASSERT_EQ(1, (int32_t)v.as.i);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "10 % 0", &v);
    ASSERT_EQ(NMO_DSL_VALUE_INT, v.kind);
    ASSERT_EQ(0, (int32_t)v.as.i);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, div_by_zero) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "1 / 0", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r != v.as.r); /* NaN */
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, string_escapes) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "\"a\\n\\\"b\"", &v);
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_STR_EQ("a\n\"b", v.as.s);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, builtin_name) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "name(42)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_STR_EQ("Foo", v.as.s);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "name(x)", &v);
    ASSERT_EQ(NMO_DSL_VALUE_STRING, v.kind);
    ASSERT_STR_EQ("", v.as.s);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, literals) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "null", &v);
    ASSERT_EQ(NMO_DSL_VALUE_NULL, v.kind);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "true", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_TRUE(v.as.b);
    nmo_dsl_value_destroy(&v);

    eval_ok(&ctx, "false", &v);
    ASSERT_EQ(NMO_DSL_VALUE_BOOL, v.kind);
    ASSERT_FALSE(v.as.b);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, whitespace) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "  sum ( arr ) ", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 9.9 && v.as.r < 10.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, filter_prim_empty) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(arr[? true])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(0u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, filter_root_fallback) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    fx.root.x = 0;
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "len(elems[? x > 1])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(0u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    fx.root.x = 3;
    make_ctx(&fx.root, &ctx);
    eval_ok(&ctx, "len(elems[? x > 1])", &v);
    ASSERT_EQ(NMO_DSL_VALUE_UINT, v.kind);
    ASSERT_EQ(3u, (uint32_t)v.as.u);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, filter_predicate_error_propagates) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "len(elems[? @.nope > 0])");
    teardown();
}

/* ============================================================================
 * Error Tests (mirroring test_query.c)
 * ============================================================================ */

TEST(dsl, error_unknown_identifier) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "does_not_exist");
    teardown();
}

TEST(dsl, error_unknown_function) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "nope(1)");
    teardown();
}

TEST(dsl, error_too_many_args) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "len(1,2,3,4,5)");
    teardown();
}

TEST(dsl, error_at_outside_filter) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "@");
    teardown();
}

TEST(dsl, error_unknown_field) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "elems.nope[0]");
    teardown();
}

TEST(dsl, error_unterminated_string) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "\"unterminated");
    teardown();
}

TEST(dsl, error_bad_escape) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "\"\\q\"");
    teardown();
}

TEST(dsl, error_parse_mismatch_paren) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "(1 + 2");
    teardown();
}

TEST(dsl, error_parse_empty) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    eval_fail(&ctx, "");
    teardown();
}

/* ============================================================================
 * Additional DSL-specific Tests
 * ============================================================================ */

TEST(dsl, compile_destroy_lifecycle) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "1 + 2", &opts, &prog);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NE(NULL, prog);
    nmo_dsl_program_destroy(prog);

    teardown();
}

TEST(dsl, program_reuse) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "x + 1", &opts, &prog);
    ASSERT_EQ(NMO_OK, st);

    /* Eval with x=3 */
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);
    nmo_dsl_value_t v;
    st = nmo_dsl_eval_expr(prog, &ctx, &v);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 3.9 && v.as.r < 4.1);
    nmo_dsl_value_destroy(&v);

    /* Eval with x=10 */
    fx.root.x = 10;
    make_ctx(&fx.root, &ctx);
    st = nmo_dsl_eval_expr(prog, &ctx, &v);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 10.9 && v.as.r < 11.1);
    nmo_dsl_value_destroy(&v);

    nmo_dsl_program_destroy(prog);
    teardown();
}

TEST(dsl, stub_rejection) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "1", &opts, &prog);
    ASSERT_EQ(NMO_OK, st);

    nmo_dsl_value_t v = {0};
    st = nmo_dsl_exec(prog, NULL, &v);
    ASSERT_NE(NMO_OK, st);

    st = nmo_dsl_apply_schema(registry, prog);
    ASSERT_NE(NMO_OK, st);

    nmo_dsl_program_destroy(prog);
    teardown();
}

TEST(dsl, comment_handling) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "/* comment */ x + /* inline */ 1 // trailing", &v);
    ASSERT_EQ(NMO_DSL_VALUE_REAL, v.kind);
    ASSERT_TRUE(v.as.r > 3.9 && v.as.r < 4.1);
    nmo_dsl_value_destroy(&v);

    teardown();
}

TEST(dsl, hex_int_literal) {
    setup();
    query_fixture_t fx;
    fixture_init(&fx);
    nmo_dsl_eval_context_t ctx;
    make_ctx(&fx.root, &ctx);

    nmo_dsl_value_t v;
    eval_ok(&ctx, "0xFF", &v);
    ASSERT_EQ(NMO_DSL_VALUE_INT, v.kind);
    ASSERT_EQ(255, (int32_t)v.as.i);
    nmo_dsl_value_destroy(&v);

    teardown();
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Parity tests */
    REGISTER_TEST(dsl, field_access);
    REGISTER_TEST(dsl, arithmetic_add);
    REGISTER_TEST(dsl, index_literal);
    REGISTER_TEST(dsl, index_expression);
    REGISTER_TEST(dsl, index_oob_fails);
    REGISTER_TEST(dsl, index_neg_fails);
    REGISTER_TEST(dsl, builtin_len_seq);
    REGISTER_TEST(dsl, builtin_len_string);
    REGISTER_TEST(dsl, builtin_size_count);
    REGISTER_TEST(dsl, builtin_empty_first_last_avg);
    REGISTER_TEST(dsl, builtin_sum);
    REGISTER_TEST(dsl, builtin_min_max);
    REGISTER_TEST(dsl, builtin_any_all);
    REGISTER_TEST(dsl, builtin_any_all_empty);
    REGISTER_TEST(dsl, wildcard);
    REGISTER_TEST(dsl, slice_prefix_suffix);
    REGISTER_TEST(dsl, slice_range);
    REGISTER_TEST(dsl, slice_inverted);
    REGISTER_TEST(dsl, filter_basic);
    REGISTER_TEST(dsl, filter_at_symbol);
    REGISTER_TEST(dsl, member_mapping);
    REGISTER_TEST(dsl, precedence);
    REGISTER_TEST(dsl, unary_ops);
    REGISTER_TEST(dsl, comparisons);
    REGISTER_TEST(dsl, logic_ops);
    REGISTER_TEST(dsl, logic_short_circuit_and);
    REGISTER_TEST(dsl, logic_short_circuit_or);
    REGISTER_TEST(dsl, modulo);
    REGISTER_TEST(dsl, div_by_zero);
    REGISTER_TEST(dsl, string_escapes);
    REGISTER_TEST(dsl, builtin_name);
    REGISTER_TEST(dsl, literals);
    REGISTER_TEST(dsl, whitespace);
    REGISTER_TEST(dsl, filter_prim_empty);
    REGISTER_TEST(dsl, filter_root_fallback);
    REGISTER_TEST(dsl, filter_predicate_error_propagates);
    /* Error tests */
    REGISTER_TEST(dsl, error_unknown_identifier);
    REGISTER_TEST(dsl, error_unknown_function);
    REGISTER_TEST(dsl, error_too_many_args);
    REGISTER_TEST(dsl, error_at_outside_filter);
    REGISTER_TEST(dsl, error_unknown_field);
    REGISTER_TEST(dsl, error_unterminated_string);
    REGISTER_TEST(dsl, error_bad_escape);
    REGISTER_TEST(dsl, error_parse_mismatch_paren);
    REGISTER_TEST(dsl, error_parse_empty);
    /* Additional tests */
    REGISTER_TEST(dsl, compile_destroy_lifecycle);
    REGISTER_TEST(dsl, program_reuse);
    REGISTER_TEST(dsl, stub_rejection);
    REGISTER_TEST(dsl, comment_handling);
    REGISTER_TEST(dsl, hex_int_literal);
TEST_MAIN_END()
