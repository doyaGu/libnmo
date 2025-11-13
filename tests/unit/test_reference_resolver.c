/**
 * @file test_reference_resolver.c
 * @brief Unit tests for reference resolution system (Phase 4)
 * 
 * Tests the complete reference object system including:
 * - Default strategy (name + class matching)
 * - Parameter strategy (name + class + type_guid matching)
 * - GUID strategy (exhaustive GUID search)
 * - Fuzzy strategy (case-insensitive name matching)
 * - Multi-level fallback resolution
 * - Statistics tracking
 */

#include "test_framework.h"
#include "session/nmo_reference_resolver.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <string.h>

/* Test fixture */
typedef struct {
    nmo_arena_t *arena;
    nmo_object_repository_t *repo;
    nmo_reference_resolver_t *resolver;
} test_fixture_t;

/* Setup: Create arena, repository, and resolver */
static test_fixture_t *setup(void) {
    test_fixture_t *fix = (test_fixture_t *) malloc(sizeof(test_fixture_t));
    if (fix == NULL) return NULL;
    
    fix->arena = nmo_arena_create(NULL, 8192);
    if (fix->arena == NULL) {
        free(fix);
        return NULL;
    }
    
    fix->repo = nmo_object_repository_create(fix->arena);
    if (fix->repo == NULL) {
        nmo_arena_destroy(fix->arena);
        free(fix);
        return NULL;
    }
    
    fix->resolver = nmo_reference_resolver_create(fix->repo, fix->arena);
    if (fix->resolver == NULL) {
        nmo_object_repository_destroy(fix->repo);
        nmo_arena_destroy(fix->arena);
        free(fix);
        return NULL;
    }
    
    return fix;
}

/* Teardown: Destroy resolver, repository, and arena */
static void teardown(test_fixture_t *fix) {
    if (fix == NULL) return;
    
    nmo_reference_resolver_destroy(fix->resolver);
    nmo_object_repository_destroy(fix->repo);
    nmo_arena_destroy(fix->arena);
    free(fix);
}

/* Helper: Create test object with given id, name, and class */
static nmo_object_t *create_test_object(test_fixture_t *fix, 
                                       nmo_object_id_t id, 
                                       const char *name, 
                                       nmo_class_id_t class_id) {
    nmo_object_t *obj = nmo_object_create(fix->arena, id, class_id);
    if (obj == NULL) return NULL;
    
    if (name != NULL) {
        nmo_object_set_name(obj, name, fix->arena);
    }
    
    nmo_object_repository_add(fix->repo, obj);
    return obj;
}

/**
 * Test 1: Default strategy - exact name and class match
 */
TEST(reference_resolver, default_strategy_exact_match) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create target object: ID=100, name="TestObject", class=1000 */
    nmo_object_t *target = create_test_object(fix, 100, "TestObject", 1000);
    ASSERT_NOT_NULL(target);
    
    /* Create reference: name="TestObject", class=1000 */
    nmo_object_ref_t ref = {0};
    ref.name = "TestObject";
    ref.class_id = 1000;
    ref.id = 50;  /* Different ID */
    
    /* Resolve reference using default strategy */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NOT_NULL(resolved);
    ASSERT_EQ(resolved->id, 100);
    
    /* Check statistics */
    nmo_reference_stats_t stats = {0};
    nmo_reference_resolver_get_stats(fix->resolver, &stats);
    ASSERT_EQ(stats.resolved_count, 1);
    ASSERT_EQ(stats.unresolved_count, 0);
    
    teardown(fix);
}

/**
 * Test 2: Default strategy - no match (wrong class)
 */
TEST(reference_resolver, default_strategy_no_match) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create target object: ID=100, name="TestObject", class=1000 */
    create_test_object(fix, 100, "TestObject", 1000);
    
    /* Create reference: name="TestObject", class=2000 (different class) */
    nmo_object_ref_t ref = {0};
    ref.name = "TestObject";
    ref.class_id = 2000;  /* Wrong class */
    ref.id = 50;
    
    /* Resolve reference - should fail with default strategy */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NULL(resolved);
    
    /* Check statistics */
    nmo_reference_stats_t stats = {0};
    nmo_reference_resolver_get_stats(fix->resolver, &stats);
    ASSERT_EQ(stats.resolved_count, 0);
    ASSERT_EQ(stats.unresolved_count, 1);
    
    teardown(fix);
}

/**
 * Test 3: Parameter strategy - type_guid matching
 */
TEST(reference_resolver, parameter_strategy_guid_match) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create two objects with same name but different type GUIDs */
    nmo_object_t *obj1 = create_test_object(fix, 100, "Parameter", 1000);
    nmo_object_t *obj2 = create_test_object(fix, 101, "Parameter", 1000);
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    nmo_guid_t guid1 = {0x12345678, 0x9ABCDEF0};
    nmo_guid_t guid2 = {0xFEDCBA98, 0x76543210};
    
    nmo_object_set_type_guid(obj1, guid1);
    nmo_object_set_type_guid(obj2, guid2);
    
    /* Register parameter strategy for class 1000 */
    nmo_reference_resolver_register_strategy(
        fix->resolver,
        1000,  /* class_id */
        nmo_resolve_strategy_parameter,
        NULL   /* context */
    );
    
    /* Create reference with type_guid matching obj2 */
    nmo_object_ref_t ref = {0};
    ref.name = "Parameter";
    ref.class_id = 1000;
    ref.type_guid = guid2;
    ref.id = 50;
    
    /* Resolve - should match obj2 due to type_guid */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NOT_NULL(resolved);
    ASSERT_EQ(resolved->id, 101);  /* Should be obj2, not obj1 */
    
    teardown(fix);
}

/**
 * Test 4: GUID strategy - search by GUID
 */
TEST(reference_resolver, guid_strategy_match) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create objects with different names but specific GUID */
    nmo_object_t *obj1 = create_test_object(fix, 100, "Object1", 1000);
    nmo_object_t *obj2 = create_test_object(fix, 101, "Object2", 1000);
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    nmo_guid_t target_guid = {0xAABBCCDD, 0xEEFF0011};
    nmo_object_set_type_guid(obj2, target_guid);
    
    /* Register GUID strategy for class 1000 */
    nmo_reference_resolver_register_strategy(
        fix->resolver,
        1000,  /* class_id */
        nmo_resolve_strategy_guid,
        NULL   /* context */
    );
    
    /* Create reference with GUID only */
    nmo_object_ref_t ref = {0};
    ref.name = "NonExistentName";
    ref.class_id = 1000;
    ref.type_guid = target_guid;
    ref.id = 50;
    
    /* Resolve - should find obj2 by GUID even with wrong name */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NOT_NULL(resolved);
    ASSERT_EQ(resolved->id, 101);
    
    teardown(fix);
}

/**
 * Test 5: Fuzzy strategy - case insensitive matching
 */
TEST(reference_resolver, fuzzy_strategy_case_insensitive) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create object with specific case */
    create_test_object(fix, 100, "TestObject", 1000);
    
    /* Register fuzzy strategy for class 1000 */
    nmo_reference_resolver_register_strategy(
        fix->resolver,
        1000,  /* class_id */
        nmo_resolve_strategy_fuzzy,
        NULL   /* context */
    );
    
    /* Create reference with different case */
    nmo_object_ref_t ref = {0};
    ref.name = "testobject";  /* Lowercase */
    ref.class_id = 1000;
    ref.id = 50;
    
    /* Resolve - should match via fuzzy strategy */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NOT_NULL(resolved);
    ASSERT_EQ(resolved->id, 100);
    
    teardown(fix);
}

/**
 * Test 6: Multi-level fallback - default fails, fuzzy succeeds
 */
TEST(reference_resolver, multi_strategy_fallback) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create object */
    create_test_object(fix, 100, "TestObject", 1000);
    
    /* Register fuzzy as fallback for class 1000 */
    nmo_reference_resolver_register_strategy(
        fix->resolver,
        1000,  /* class_id */
        nmo_resolve_strategy_fuzzy,
        NULL   /* context */
    );
    
    /* Create reference with wrong case (default will fail, fuzzy will succeed) */
    nmo_object_ref_t ref = {0};
    ref.name = "TESTOBJECT";  /* Wrong case */
    ref.class_id = 1000;
    ref.id = 50;
    
    /* Resolve - should try default first (fail), then fuzzy (succeed) */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NOT_NULL(resolved);
    ASSERT_EQ(resolved->id, 100);
    
    /* Check statistics show fallback happened */
    nmo_reference_stats_t stats = {0};
    nmo_reference_resolver_get_stats(fix->resolver, &stats);
    ASSERT_EQ(stats.resolved_count, 1);
    /* Note: strategy_fallback_count may not be available in current API */
    
    teardown(fix);
}

/**
 * Test 7: Resolve all - batch resolution
 */
TEST(reference_resolver, resolve_all) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Create multiple target objects */
    create_test_object(fix, 100, "Object1", 1000);
    create_test_object(fix, 101, "Object2", 1000);
    create_test_object(fix, 102, "Object3", 1000);
    
    /* Create multiple references */
    nmo_object_ref_t refs[3];
    memset(refs, 0, sizeof(refs));
    
    refs[0].name = "Object1";
    refs[0].class_id = 1000;
    refs[0].id = 50;
    
    refs[1].name = "Object2";
    refs[1].class_id = 1000;
    refs[1].id = 51;
    
    refs[2].name = "NonExistent";
    refs[2].class_id = 1000;
    refs[2].id = 52;
    
    /* Add references to resolver */
    nmo_object_ref_t *registered_refs[3];
    for (int i = 0; i < 3; i++) {
        registered_refs[i] = nmo_reference_resolver_register_reference(fix->resolver, &refs[i]);
        ASSERT_NOT_NULL(registered_refs[i]);
    }
    
    /* Resolve all */
    int result = nmo_reference_resolver_resolve_all(fix->resolver);
    ASSERT_EQ(result, NMO_OK);
    
    /* Check results */
    ASSERT_NOT_NULL(registered_refs[0]->resolved_object);
    ASSERT_EQ(registered_refs[0]->resolved_object->id, 100);
    
    ASSERT_NOT_NULL(registered_refs[1]->resolved_object);
    ASSERT_EQ(registered_refs[1]->resolved_object->id, 101);
    
    ASSERT_NULL(registered_refs[2]->resolved_object);  /* NonExistent not found */
    
    /* Check statistics */
    nmo_reference_stats_t stats = {0};
    nmo_reference_resolver_get_stats(fix->resolver, &stats);
    ASSERT_EQ(stats.total_count, 3);
    ASSERT_EQ(stats.resolved_count, 2);
    ASSERT_EQ(stats.unresolved_count, 1);
    
    teardown(fix);
}

/**
 * Test 8: Edge case - NULL and empty references
 */
TEST(reference_resolver, edge_cases) {
    test_fixture_t *fix = setup();
    ASSERT_NOT_NULL(fix);
    
    /* Test NULL reference */
    nmo_object_t *resolved = nmo_reference_resolver_resolve(fix->resolver, NULL);
    ASSERT_NULL(resolved);
    
    /* Test reference with NULL name */
    nmo_object_ref_t ref = {0};
    ref.name = NULL;
    ref.class_id = 1000;
    resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NULL(resolved);
    
    /* Test reference with empty name */
    ref.name = "";
    resolved = nmo_reference_resolver_resolve(fix->resolver, &ref);
    ASSERT_NULL(resolved);
    
    teardown(fix);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(reference_resolver, default_strategy_exact_match);
    REGISTER_TEST(reference_resolver, default_strategy_no_match);
    REGISTER_TEST(reference_resolver, parameter_strategy_guid_match);
    REGISTER_TEST(reference_resolver, guid_strategy_match);
    REGISTER_TEST(reference_resolver, fuzzy_strategy_case_insensitive);
    REGISTER_TEST(reference_resolver, multi_strategy_fallback);
    REGISTER_TEST(reference_resolver, resolve_all);
    REGISTER_TEST(reference_resolver, edge_cases);
TEST_MAIN_END()
