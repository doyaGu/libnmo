/**
 * @file test_dsl_schema.c
 * @brief Tests for DSL schema mode (Phase C) -- type declarations that
 *        register enums, flags, structs, and aliases into the type registry.
 */

#include "test_framework.h"

#include "dsl/nmo_dsl.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_guids.h"

#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include <stdalign.h>
#include <string.h>
#include <stdio.h>

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

static nmo_guid_t test_guid_generator(const char *type_name, void *user);

/* Compile schema source and apply to registry, returning status */
static nmo_status_t apply_schema(
    nmo_type_registry_t *reg,
    const char *source)
{
    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCHEMA };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(reg, NULL, source, &opts, &prog);
    if (st != NMO_OK) return st;
    st = nmo_dsl_apply_schema(reg, prog);
    nmo_dsl_program_destroy(prog);
    return st;
}

/* Compile schema source and apply with options, returning status */
static nmo_status_t apply_schema_with_options(
    nmo_type_registry_t *reg,
    const char *source,
    const nmo_dsl_schema_options_t *options)
{
    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCHEMA };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(reg, NULL, source, &opts, &prog);
    if (st != NMO_OK) return st;
    st = nmo_dsl_apply_schema_ex(reg, prog, options);
    nmo_dsl_program_destroy(prog);
    return st;
}

/* Compile module source and run schema+script */
static nmo_status_t run_module(
    nmo_type_registry_t *reg,
    const char *source)
{
    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_MODULE };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(reg, NULL, source, &opts, &prog);
    if (st != NMO_OK) return st;

    nmo_dsl_eval_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.registry = reg;

    nmo_dsl_value_t out = {0};
    st = nmo_dsl_run_module(reg, prog, &ctx, NULL, &out);
    nmo_dsl_value_destroy(&out);
    nmo_dsl_program_destroy(prog);
    return st;
}

/* ============================================================================
 * Per-test registry
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static void setup(void) {
    arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));
}

static void teardown(void) {
    if (registry) { nmo_type_registry_destroy(registry); registry = NULL; }
    if (arena)    { nmo_arena_destroy(arena);            arena    = NULL; }
}

/* ============================================================================
 * Enum declaration tests
 * ============================================================================ */

TEST(dsl_schema, enum_basic) {
    setup();

    const char *src =
        "schema { enum Color : int { Red = 0, Green = 1, Blue = 2 } }";
    assert_ok(apply_schema(registry, src), "enum Color");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Color");
    ASSERT_NE(NULL, t);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, t->category);

    teardown();
}

TEST(dsl_schema, enum_multi) {
    setup();

    const char *src =
        "schema {\n"
        "enum A : int { X = 0 }\n"
        "enum B : int { Y = 10, Z = 20 }\n"
        "}";
    assert_ok(apply_schema(registry, src), "enum multi");

    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "A"));
    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "B"));

    teardown();
}

TEST(dsl_schema, enum_auto_values) {
    setup();

    const char *src =
        "schema { enum AutoEnum : int { A, B, C = 7, D, E } }";
    assert_ok(apply_schema(registry, src), "enum auto values");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "AutoEnum");
    ASSERT_NE(NULL, t);

    const nmo_enum_descriptor_t *a = nmo_type_get_enum_value_by_name(registry, t, "A");
    const nmo_enum_descriptor_t *b = nmo_type_get_enum_value_by_name(registry, t, "B");
    const nmo_enum_descriptor_t *c = nmo_type_get_enum_value_by_name(registry, t, "C");
    const nmo_enum_descriptor_t *d = nmo_type_get_enum_value_by_name(registry, t, "D");
    const nmo_enum_descriptor_t *e = nmo_type_get_enum_value_by_name(registry, t, "E");

    ASSERT_NE(NULL, a);
    ASSERT_NE(NULL, b);
    ASSERT_NE(NULL, c);
    ASSERT_NE(NULL, d);
    ASSERT_NE(NULL, e);

    ASSERT_EQ(0, a->value);
    ASSERT_EQ(1, b->value);
    ASSERT_EQ(7, c->value);
    ASSERT_EQ(8, d->value);
    ASSERT_EQ(9, e->value);

    teardown();
}

/* ============================================================================
 * Flags declaration tests
 * ============================================================================ */

TEST(dsl_schema, flags_basic) {
    setup();

    const char *src =
        "schema { flags Perms : uint { Read = 0x01, Write = 0x02, Exec = 0x04 } }";
    assert_ok(apply_schema(registry, src), "flags Perms");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Perms");
    ASSERT_NE(NULL, t);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, t->category);

    teardown();
}

TEST(dsl_schema, flags_auto_values) {
    setup();

    const char *src =
        "schema { flags AutoFlags : uint { Read, Write, Exec = 0x10, Admin } }";
    assert_ok(apply_schema(registry, src), "flags auto values");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "AutoFlags");
    ASSERT_NE(NULL, t);

    const nmo_flags_descriptor_t *read = nmo_type_get_flags_bit_by_name(registry, t, "Read");
    const nmo_flags_descriptor_t *write = nmo_type_get_flags_bit_by_name(registry, t, "Write");
    const nmo_flags_descriptor_t *exec = nmo_type_get_flags_bit_by_name(registry, t, "Exec");
    const nmo_flags_descriptor_t *admin = nmo_type_get_flags_bit_by_name(registry, t, "Admin");

    ASSERT_NE(NULL, read);
    ASSERT_NE(NULL, write);
    ASSERT_NE(NULL, exec);
    ASSERT_NE(NULL, admin);

    ASSERT_EQ(0x01, read->mask);
    ASSERT_EQ(0x02, write->mask);
    ASSERT_EQ(0x10, exec->mask);
    ASSERT_EQ(0x20, admin->mask);

    teardown();
}

TEST(dsl_schema, module_schema_only) {
    setup();

    const char *src =
        "schema {\n"
        "enum Mode : int { A = 0, B = 1 }\n"
        "flags Mask : uint { X = 0x01, Y = 0x02 }\n"
        "}";

    assert_ok(run_module(registry, src), "module schema only");

    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "Mode"));
    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "Mask"));

    teardown();
}

/* ============================================================================
 * Struct declaration tests
 * ============================================================================ */

TEST(dsl_schema, struct_basic) {
    setup();

    const char *src =
        "schema { struct Vec3 { float x; float y; float z; } }";
    assert_ok(apply_schema(registry, src), "struct Vec3");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Vec3");
    ASSERT_NE(NULL, t);
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, t->category);
    ASSERT_EQ(3u, t->field_count);

    teardown();
}

TEST(dsl_schema, struct_packed) {
    setup();

    const char *src =
        "schema { packed struct PackedPair { int a; int b; } }";
    assert_ok(apply_schema(registry, src), "packed struct");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "PackedPair");
    ASSERT_NE(NULL, t);
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, t->category);

    teardown();
}

TEST(dsl_schema, struct_inheritance) {
    setup();

    const char *src =
        "schema {\n"
        "struct Base { int x; }\n"
        "struct Child : Base { float y; }\n"
        "}";
    assert_ok(apply_schema(registry, src), "struct inheritance");

    const nmo_type_descriptor_t *base = nmo_type_registry_find_by_name(registry, "Base");
    const nmo_type_descriptor_t *child = nmo_type_registry_find_by_name(registry, "Child");
    ASSERT_NE(NULL, base);
    ASSERT_NE(NULL, child);
    ASSERT_TRUE(nmo_guid_equals(child->base_type, base->guid));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, child->id, base->id));

    teardown();
}

TEST(dsl_schema, struct_repeated_field_pointer_backed) {
    setup();

    const char *src =
        "schema { struct Bag { int nums[]; } }";
    assert_ok(apply_schema(registry, src), "repeated field pointer-backed");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Bag");
    ASSERT_NE(NULL, t);
    ASSERT_EQ(1u, t->field_count);

    const nmo_type_field_t *f = nmo_type_get_field_by_name(t, "nums");
    ASSERT_NE(NULL, f);
    ASSERT_TRUE((f->flags & NMO_FIELD_REPEATED) != 0);
    ASSERT_TRUE(nmo_guid_equals(f->type_guid, CKPGUID_POINTER));
    ASSERT_EQ((uint32_t)sizeof(void *), f->size);

    const nmo_struct_descriptor_t *sf = nmo_type_get_struct_field_by_name(registry, t, "nums");
    ASSERT_NE(NULL, sf);
    ASSERT_TRUE(nmo_guid_equals(sf->type_guid, CKPGUID_POINTER));
    ASSERT_TRUE(nmo_guid_equals(sf->pointee_guid, CKPGUID_INT));
    ASSERT_EQ(1u, sf->pointer_depth);
    ASSERT_EQ(0u, sf->array_count);

    teardown();
}

/* ============================================================================
 * Alias declaration tests
 * ============================================================================ */

TEST(dsl_schema, alias_basic) {
    setup();

    const char *src = "schema { alias MyInt = INT }";
    assert_ok(apply_schema(registry, src), "alias MyInt");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "MyInt");
    ASSERT_NE(NULL, t);

    teardown();
}

TEST(dsl_schema, alias_enum_preserves_metadata) {
    setup();

    const char *src =
        "schema {\n"
        "enum Color : int { Red = 1, Blue = 2 }\n"
        "alias ColorAlias = Color\n"
        "}";
    assert_ok(apply_schema(registry, src), "alias enum metadata");

    const nmo_type_descriptor_t *alias = nmo_type_registry_find_by_name(registry, "ColorAlias");
    ASSERT_NE(NULL, alias);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, alias->category);
    ASSERT_NE(NMO_SPECIALIZED_INDEX_INVALID, alias->specialized_index);

    const nmo_enum_descriptor_t *red = nmo_type_get_enum_value_by_name(registry, alias, "Red");
    const nmo_enum_descriptor_t *blue = nmo_type_get_enum_value_by_name(registry, alias, "Blue");
    ASSERT_NE(NULL, red);
    ASSERT_NE(NULL, blue);
    ASSERT_EQ(1, red->value);
    ASSERT_EQ(2, blue->value);

    teardown();
}

TEST(dsl_schema, alias_struct_preserves_metadata) {
    setup();

    const char *src =
        "schema {\n"
        "struct Pair { int x; float y; }\n"
        "alias PairAlias = Pair\n"
        "}";
    assert_ok(apply_schema(registry, src), "alias struct metadata");

    const nmo_type_descriptor_t *alias = nmo_type_registry_find_by_name(registry, "PairAlias");
    ASSERT_NE(NULL, alias);
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, alias->category);
    ASSERT_NE(NMO_SPECIALIZED_INDEX_INVALID, alias->specialized_index);

    const nmo_struct_descriptor_t *x =
        nmo_type_get_struct_field_by_name(registry, alias, "x");
    const nmo_struct_descriptor_t *y =
        nmo_type_get_struct_field_by_name(registry, alias, "y");
    ASSERT_NE(NULL, x);
    ASSERT_NE(NULL, y);

    teardown();
}

TEST(dsl_schema, redeclare_rejected) {
    setup();

    const char *src =
        "schema {\n"
        "enum Again : int { A = 1 }\n"
        "enum Again : int { B = 2 }\n"
        "}";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, redeclare_allowed) {
    setup();

    const char *src =
        "schema {\n"
        "enum Again : int { A = 1 }\n"
        "enum Again : int { B = 2 }\n"
        "}";

    nmo_dsl_schema_options_t options = {
        .allow_redeclare = true,
        .allow_alias_existing = false,
        .generate_guid = NULL,
        .generate_guid_user = NULL,
    };
    assert_ok(apply_schema_with_options(registry, src, &options), "redeclare allowed");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Again");
    ASSERT_NE(NULL, t);
    ASSERT_NE(NULL, nmo_type_get_enum_value_by_name(registry, t, "A"));
    ASSERT_EQ(NULL, nmo_type_get_enum_value_by_name(registry, t, "B"));

    teardown();
}

TEST(dsl_schema, redeclare_allowed_skips_enum_underlying_validation) {
    setup();

    const char *src =
        "schema {\n"
        "enum Again : int { A = 1 }\n"
        "enum Again : float { B = 2 }\n"
        "}";

    nmo_dsl_schema_options_t options = {
        .allow_redeclare = true,
        .allow_alias_existing = false,
        .generate_guid = NULL,
        .generate_guid_user = NULL,
    };
    assert_ok(apply_schema_with_options(registry, src, &options),
              "redeclare skips enum underlying validation");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Again");
    ASSERT_NE(NULL, t);
    ASSERT_NE(NULL, nmo_type_get_enum_value_by_name(registry, t, "A"));
    ASSERT_EQ(NULL, nmo_type_get_enum_value_by_name(registry, t, "B"));

    teardown();
}

TEST(dsl_schema, redeclare_allowed_skips_flags_underlying_validation) {
    setup();

    const char *src =
        "schema {\n"
        "flags Mask : uint { X = 0x01 }\n"
        "flags Mask : int { Y = 0x02 }\n"
        "}";

    nmo_dsl_schema_options_t options = {
        .allow_redeclare = true,
        .allow_alias_existing = false,
        .generate_guid = NULL,
        .generate_guid_user = NULL,
    };
    assert_ok(apply_schema_with_options(registry, src, &options),
              "redeclare skips flags underlying validation");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Mask");
    ASSERT_NE(NULL, t);
    ASSERT_NE(NULL, nmo_type_get_flags_bit_by_name(registry, t, "X"));
    ASSERT_EQ(NULL, nmo_type_get_flags_bit_by_name(registry, t, "Y"));

    teardown();
}

TEST(dsl_schema, alias_existing_allowed) {
    setup();

    const char *src =
        "schema { alias INT = INT }";

    nmo_dsl_schema_options_t options = {
        .allow_redeclare = false,
        .allow_alias_existing = true,
        .generate_guid = NULL,
        .generate_guid_user = NULL,
    };
    assert_ok(apply_schema_with_options(registry, src, &options), "alias existing allowed");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "INT");
    ASSERT_NE(NULL, t);

    teardown();
}

TEST(dsl_schema, alias_existing_rejected) {
    setup();

    const char *src =
        "schema { alias INT = UINT }";

    nmo_dsl_schema_options_t options = {
        .allow_redeclare = false,
        .allow_alias_existing = true,
        .generate_guid = NULL,
        .generate_guid_user = NULL,
    };
    nmo_status_t st = apply_schema_with_options(registry, src, &options);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

static nmo_guid_t test_guid_generator(const char *type_name, void *user) {
    (void)type_name;
    nmo_guid_t *guid = (nmo_guid_t *)user;
    return *guid;
}

TEST(dsl_schema, custom_guid_provider) {
    setup();

    const char *src =
        "schema { struct Guided { int x; } }";

    nmo_guid_t expected = {0x47444944u, 0x00000042u};
    nmo_dsl_schema_options_t options = {
        .allow_redeclare = false,
        .allow_alias_existing = false,
        .generate_guid = test_guid_generator,
        .generate_guid_user = &expected,
    };

    assert_ok(apply_schema_with_options(registry, src, &options), "custom guid");

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_name(registry, "Guided");
    ASSERT_NE(NULL, t);
    ASSERT_TRUE(nmo_guid_equals(t->guid, expected));

    teardown();
}

/* ============================================================================
 * Mode mismatch tests
 * ============================================================================ */

TEST(dsl_schema, apply_rejects_expr_mode) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "42", &opts, &prog);
    assert_ok(st, "compile expr");

    st = nmo_dsl_apply_schema(registry, prog);
    ASSERT_NE(NMO_OK, st);

    nmo_dsl_program_destroy(prog);
    teardown();
}

TEST(dsl_schema, apply_rejects_script_mode) {
    setup();

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_SCRIPT };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, "x = 1", &opts, &prog);
    if (st == NMO_OK) {
        st = nmo_dsl_apply_schema(registry, prog);
        ASSERT_NE(NMO_OK, st);
        nmo_dsl_program_destroy(prog);
    }

    teardown();
}

/* ============================================================================
 * Mixed declarations
 * ============================================================================ */

TEST(dsl_schema, mixed_declarations) {
    setup();

    const char *src =
        "schema {\n"
        "enum Direction : int { Up = 0, Down = 1, Left = 2, Right = 3 }\n"
        "flags Attributes : uint { Visible = 0x01, Active = 0x02 }\n"
        "struct Entity { int id; float health; }\n"
        "}";
    assert_ok(apply_schema(registry, src), "mixed decls");

    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "Direction"));
    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "Attributes"));
    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "Entity"));

    teardown();
}

TEST(dsl_schema, schema_wrapper_required) {
    setup();

    nmo_status_t st = apply_schema(registry, "enum E : int { A = 0 }");
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, module_schema_wrapper_required) {
    setup();

    nmo_status_t st = run_module(registry, "enum E : int { A = 0 }");
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, module_requires_schema_even_for_script) {
    setup();

    nmo_status_t st = run_module(registry, "x = 1");
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, module_requires_schema_when_empty) {
    setup();

    nmo_status_t st = run_module(registry, "");
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, enum_missing_comma_rejected) {
    setup();

    const char *src = "schema { enum E : int { A = 0 B = 1 } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, flags_missing_comma_rejected) {
    setup();

    const char *src = "schema { flags F : uint { A = 0x01 B = 0x02 } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, struct_missing_semicolon_rejected) {
    setup();

    const char *src = "schema { struct S { int x int y; } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, enum_underlying_unknown_rejected) {
    setup();

    const char *src = "schema { enum E : unknown_t { A = 0 } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, enum_underlying_non_int32_rejected) {
    setup();

    const char *src = "schema { enum E : float { A = 0 } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, flags_underlying_non_uint32_rejected) {
    setup();

    const char *src = "schema { flags F : int { A = 0x01 } }";
    nmo_status_t st = apply_schema(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, module_trailing_junk_rejected) {
    setup();

    const char *src =
        "schema { enum E : int { A = 0 } }\n"
        "x = 1 y = 2";
    nmo_status_t st = run_module(registry, src);
    ASSERT_NE(NMO_OK, st);

    teardown();
}

TEST(dsl_schema, module_registry_mismatch_rejected) {
    setup();

    nmo_arena_t *other_arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, other_arena);
    nmo_type_registry_t *other_registry = nmo_type_registry_create(other_arena);
    ASSERT_NE(NULL, other_registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(other_registry));

    const char *src =
        "schema { enum E : int { A = 0 } }\n"
        "1";

    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_MODULE };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, src, &opts, &prog);
    assert_ok(st, "compile module");

    nmo_dsl_eval_context_t ctx = {0};
    ctx.registry = other_registry; /* intentionally mismatched */

    nmo_dsl_value_t out = {0};
    st = nmo_dsl_run_module(registry, prog, &ctx, NULL, &out);
    ASSERT_NE(NMO_OK, st);

    nmo_dsl_value_destroy(&out);
    nmo_dsl_program_destroy(prog);
    nmo_type_registry_destroy(other_registry);
    nmo_arena_destroy(other_arena);

    teardown();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(dsl_schema, enum_basic);
    REGISTER_TEST(dsl_schema, enum_multi);
    REGISTER_TEST(dsl_schema, enum_auto_values);
    REGISTER_TEST(dsl_schema, flags_basic);
    REGISTER_TEST(dsl_schema, flags_auto_values);
    REGISTER_TEST(dsl_schema, module_schema_only);
    REGISTER_TEST(dsl_schema, struct_basic);
    REGISTER_TEST(dsl_schema, struct_packed);
    REGISTER_TEST(dsl_schema, struct_inheritance);
    REGISTER_TEST(dsl_schema, struct_repeated_field_pointer_backed);
    REGISTER_TEST(dsl_schema, alias_basic);
    REGISTER_TEST(dsl_schema, alias_enum_preserves_metadata);
    REGISTER_TEST(dsl_schema, alias_struct_preserves_metadata);
    REGISTER_TEST(dsl_schema, redeclare_rejected);
    REGISTER_TEST(dsl_schema, redeclare_allowed);
    REGISTER_TEST(dsl_schema, redeclare_allowed_skips_enum_underlying_validation);
    REGISTER_TEST(dsl_schema, redeclare_allowed_skips_flags_underlying_validation);
    REGISTER_TEST(dsl_schema, alias_existing_allowed);
    REGISTER_TEST(dsl_schema, alias_existing_rejected);
    REGISTER_TEST(dsl_schema, custom_guid_provider);
    REGISTER_TEST(dsl_schema, apply_rejects_expr_mode);
    REGISTER_TEST(dsl_schema, apply_rejects_script_mode);
    REGISTER_TEST(dsl_schema, mixed_declarations);
    REGISTER_TEST(dsl_schema, schema_wrapper_required);
    REGISTER_TEST(dsl_schema, module_schema_wrapper_required);
    REGISTER_TEST(dsl_schema, module_requires_schema_even_for_script);
    REGISTER_TEST(dsl_schema, module_requires_schema_when_empty);
    REGISTER_TEST(dsl_schema, enum_missing_comma_rejected);
    REGISTER_TEST(dsl_schema, flags_missing_comma_rejected);
    REGISTER_TEST(dsl_schema, struct_missing_semicolon_rejected);
    REGISTER_TEST(dsl_schema, enum_underlying_unknown_rejected);
    REGISTER_TEST(dsl_schema, enum_underlying_non_int32_rejected);
    REGISTER_TEST(dsl_schema, flags_underlying_non_uint32_rejected);
    REGISTER_TEST(dsl_schema, module_trailing_junk_rejected);
    REGISTER_TEST(dsl_schema, module_registry_mismatch_rejected);
TEST_MAIN_END()
