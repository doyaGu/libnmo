/**
 * @file test_macro_integration.c
 * @brief Integration tests for declarative schema registration
 * 
 * Tests realistic scenarios with CK* class-like structures:
 * - Multi-level inheritance
 * - Complex nested types
 * - Full registration workflow
 */

#include "test_framework.h"
#include "schema/nmo_schema_macros.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <string.h>

/* ============================================================================
 * Mock CK* Class Hierarchy
 * ============================================================================ */

/* CKObject (base) */
typedef struct mock_ckobject {
    uint32_t id;
    uint32_t flags;
    const char *name;
} mock_ckobject_t;

/* CKBeObject : CKObject */
typedef struct mock_ckbeobject {
    mock_ckobject_t object;  /* Inheritance */
    uint32_t visibility_flags;
    uint32_t render_flags;
} mock_ckbeobject_t;

/* Mock3dEntity : CKBeObject */
typedef struct mock_3d_entity {
    mock_ckbeobject_t beobject;  /* Inheritance */
    float position[3];
    float rotation[4];
    float scale[3];
    uint32_t mesh_id;
} mock_3d_entity_t;

/* ============================================================================
 * Schema Declarations
 * ============================================================================ */

NMO_DECLARE_SCHEMA(MockCKObject, mock_ckobject_t) {
    SCHEMA_FIELD_EX(id, u32, mock_ckobject_t, NMO_ANNOTATION_ID_FIELD),
    SCHEMA_FIELD(flags, u32, mock_ckobject_t),
    SCHEMA_FIELD(name, string, mock_ckobject_t)
};

NMO_DECLARE_SCHEMA(MockCKBeObject, mock_ckbeobject_t) {
    SCHEMA_FIELD(object, MockCKObject, mock_ckbeobject_t),
    SCHEMA_FIELD(visibility_flags, u32, mock_ckbeobject_t),
    SCHEMA_FIELD(render_flags, u32, mock_ckbeobject_t)
};

NMO_DECLARE_SCHEMA(Mock3dEntity, mock_3d_entity_t) {
    SCHEMA_FIELD(beobject, MockCKBeObject, mock_3d_entity_t),
    SCHEMA_FIELD_EX(position, f32, mock_3d_entity_t, NMO_ANNOTATION_POSITION),
    SCHEMA_FIELD_EX(rotation, f32, mock_3d_entity_t, NMO_ANNOTATION_ROTATION),
    SCHEMA_FIELD_EX(scale, f32, mock_3d_entity_t, NMO_ANNOTATION_SCALE),
    SCHEMA_FIELD_EX(mesh_id, u32, mock_3d_entity_t, NMO_ANNOTATION_ID_FIELD)
};

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

typedef struct test_context {
    nmo_arena_t *arena;
    nmo_schema_registry_t *registry;
} test_context_t;

static test_context_t *setup_integration_context(void) {
    test_context_t *ctx = malloc(sizeof(test_context_t));
    if (!ctx) return NULL;
    
    ctx->arena = nmo_arena_create(NULL, 131072);
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }
    
    ctx->registry = nmo_schema_registry_create(ctx->arena);
    if (!ctx->registry) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    /* Register scalar types */
    nmo_result_t result = nmo_register_scalar_types(ctx->registry, ctx->arena);
    if (result.code != NMO_OK) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

static void teardown_integration_context(test_context_t *ctx) {
    if (ctx) {
        if (ctx->arena) {
            nmo_arena_destroy(ctx->arena);
        }
        free(ctx);
    }
}

/* ============================================================================
 * Test Cases: Class Hierarchy Registration
 * ============================================================================ */

TEST(macro_integration, register_class_hierarchy) {
    test_context_t *ctx = setup_integration_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register in dependency order */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKObject, mock_ckobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKBeObject, mock_ckbeobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, Mock3dEntity, mock_3d_entity_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify all types registered */
    const nmo_schema_type_t *obj_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "MockCKObject");
    ASSERT_NE(NULL, obj_type);
    
    const nmo_schema_type_t *beobj_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "MockCKBeObject");
    ASSERT_NE(NULL, beobj_type);
    
    const nmo_schema_type_t *entity_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "Mock3dEntity");
    ASSERT_NE(NULL, entity_type);
    
    teardown_integration_context(ctx);
}

/* ============================================================================
 * Test Cases: Nested Type Resolution
 * ============================================================================ */

TEST(macro_integration, nested_type_resolution) {
    test_context_t *ctx = setup_integration_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register dependencies */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKObject, mock_ckobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKBeObject, mock_ckbeobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify nested field type resolution */
    const nmo_schema_type_t *beobj_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "MockCKBeObject");
    ASSERT_NE(NULL, beobj_type);
    ASSERT_EQ(3U, beobj_type->field_count);
    
    /* Check first field references base class */
    const nmo_schema_field_t *object_field = &beobj_type->fields[0];
    ASSERT_STR_EQ("object", object_field->name);
    ASSERT_NE(NULL, object_field->type);
    ASSERT_STR_EQ("MockCKObject", object_field->type->name);
    
    /* Verify base class type has correct structure */
    ASSERT_EQ(3U, object_field->type->field_count);
    ASSERT_STR_EQ("id", object_field->type->fields[0].name);
    
    teardown_integration_context(ctx);
}

/* ============================================================================
 * Test Cases: Annotation Propagation
 * ============================================================================ */

TEST(macro_integration, annotation_propagation) {
    test_context_t *ctx = setup_integration_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register dependencies */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKObject, mock_ckobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKBeObject, mock_ckbeobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, Mock3dEntity, mock_3d_entity_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify annotations on entity fields */
    const nmo_schema_type_t *entity_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "Mock3dEntity");
    ASSERT_NE(NULL, entity_type);
    ASSERT_EQ(5U, entity_type->field_count);
    
    /* Check position annotation */
    const nmo_schema_field_t *pos_field = &entity_type->fields[1];
    ASSERT_STR_EQ("position", pos_field->name);
    ASSERT_EQ(NMO_ANNOTATION_POSITION, pos_field->annotations);
    
    /* Check ID field annotation */
    const nmo_schema_field_t *mesh_field = &entity_type->fields[4];
    ASSERT_STR_EQ("mesh_id", mesh_field->name);
    ASSERT_EQ(NMO_ANNOTATION_ID_FIELD, mesh_field->annotations);
    
    teardown_integration_context(ctx);
}

/* ============================================================================
 * Test Cases: Complete Workflow
 * ============================================================================ */

TEST(macro_integration, complete_registration_workflow) {
    test_context_t *ctx = setup_integration_context();
    ASSERT_NE(NULL, ctx);
    
    /* Simulate complete type registration (like builtin_types.c) */
    nmo_result_t result;
    
    /* Step 1: Register base class */
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKObject, mock_ckobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Step 2: Register derived class */
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKBeObject, mock_ckbeobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Step 3: Register leaf class */
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, Mock3dEntity, mock_3d_entity_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify complete type information */
    const nmo_schema_type_t *entity_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "Mock3dEntity");
    ASSERT_NE(NULL, entity_type);
    
    /* Check size and alignment */
    ASSERT_EQ(sizeof(mock_3d_entity_t), entity_type->size);
    ASSERT_EQ(_Alignof(mock_3d_entity_t), entity_type->align);
    
    /* Check field count */
    ASSERT_EQ(5U, entity_type->field_count);
    
    /* Verify nested base class accessible */
    const nmo_schema_field_t *beobj_field = &entity_type->fields[0];
    ASSERT_STR_EQ("beobject", beobj_field->name);
    ASSERT_NE(NULL, beobj_field->type);
    
    const nmo_schema_type_t *beobj_type = beobj_field->type;
    ASSERT_EQ(3U, beobj_type->field_count);
    
    const nmo_schema_field_t *obj_field = &beobj_type->fields[0];
    ASSERT_STR_EQ("object", obj_field->name);
    ASSERT_NE(NULL, obj_field->type);
    
    teardown_integration_context(ctx);
}

/* ============================================================================
 * Test Cases: Memory Management
 * ============================================================================ */

TEST(macro_integration, memory_allocation_tracking) {
    test_context_t *ctx = setup_integration_context();
    ASSERT_NE(NULL, ctx);
    
    size_t initial_used = nmo_arena_bytes_used(ctx->arena);
    
    /* Register multiple types */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKObject, mock_ckobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, MockCKBeObject, mock_ckbeobject_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    size_t final_used = nmo_arena_bytes_used(ctx->arena);
    
    /* Verify memory was allocated */
    ASSERT_GT(final_used, initial_used);
    
    /* Verify no leaks (arena cleanup handles everything) */
    teardown_integration_context(ctx);
    /* If no leaks, this completes cleanly */
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(macro_integration, register_class_hierarchy);
    REGISTER_TEST(macro_integration, nested_type_resolution);
    REGISTER_TEST(macro_integration, annotation_propagation);
    REGISTER_TEST(macro_integration, complete_registration_workflow);
    REGISTER_TEST(macro_integration, memory_allocation_tracking);
TEST_MAIN_END()
